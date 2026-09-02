#pragma once
/**
 * @file
 * @brief Core ECS registry and iterator/view implementation (SoA-based storage).
 *
 * The Registry manages:
 *   - Allocation and ownership of entity ids (bitmap of live ids via IdSet<>).
 *   - Lazily created or pre-registered component sector arrays (SectorsArray).
 *   - Thread-aware access and pinning (if ThreadSafe template parameter is true).
 *   - Views (ArraysView) that iterate a "main" component and project other components.
 *
 * Storage model (SoA - Structure of Arrays):
 *   - Each component (or grouped component set) is stored in a SectorsArray.
 *   - Sector metadata (id, isAliveData) stored in parallel arrays for cache locality.
 *   - Component data stored contiguously in ChunksAllocator.
 *
 * Thread safety:
 *   - When ThreadSafe == true:
 *       * Public mutating APIs acquire internal shared / unique locks.
 *       * Pinning uses a pin counter to defer destruction until safe.
 *   - When ThreadSafe == false:
 *       * No internal synchronization (caller is responsible).
 *
 * @see ArraysView for iteration semantics.
 * @see PinnedComponent for safe temporary access in thread-safe builds.
 */

 // ecss - Entity Component System with Sectors
 // "Sectors" refers to the logic of storing components.
 // Multiple components of different types can be stored in one memory location, which I've named a "sector."

#include <algorithm>
#include <array>
#include <atomic>
#include <shared_mutex>
#include <tuple>
#include <vector>

#include <ecss/Access.h>
#include <ecss/AccessTracker.h>
#include <ecss/IdSet.h>
#include <ecss/Ranges.h>
#include <ecss/memory/Reflection.h>
#include <ecss/memory/SectorsArray.h>
#include <ecss/memory/Sector.h>

namespace ecss {

	/**
	 * @brief RAII wrapper that pins the sector holding component T and exposes a typed pointer.
	 *
	 * @tparam T Component type stored in the pinned sector.
	 *
	 * Pin semantics (thread-safe build):
	 *   - Pin increments a pin counter preventing concurrent structural erase of the sector.
	 *   - Releasing (explicitly via release() or implicitly in destructor) decrements the pin counter.
	 *
	 * @warning Never store the raw pointer `get()` beyond the lifetime of this wrapper.
	 * @note In non-thread-safe builds pinning still exists conceptually but can be a no-op.
	 *
	 * @thread_safety Thread-confined -- the handle, not what it
	 *                points at. One of these belongs to one thread: it is move-only, and its
	 *                accessors (get, operator->, operator*, operator bool, release) are plain
	 *                reads of members with no synchronization of their own. Do not share one
	 *                across threads; take a pin per thread instead.
	 *
	 *                What the handle buys is the opposite direction: while it lives, that
	 *                sector will not be moved, destroyed or reused by anyone, so the pointer
	 *                stays good. It also makes other threads wait -- destroyComponent and
	 *                destroyEntity for this entity block until it is released -- so hold it
	 *                for as short a time as the work allows, and never across a frame.
	 *
	 *                The pin protects the sector's *existence*, not its *value*: another
	 *                thread may still be writing this component. @see Registry::access()
	 */
	template<class T>
	struct PinnedComponent {
		PinnedComponent(const PinnedComponent& other) = delete;
		PinnedComponent(PinnedComponent&& other) noexcept = default;
		PinnedComponent& operator=(const PinnedComponent& other) = delete;
		PinnedComponent& operator=(PinnedComponent&& other) noexcept = default;
		PinnedComponent() = default;

		/**
		 * @brief Construct from a pinned sector and component pointer.
		 * @param pinnedSector Sector pin handle (ownership transferred).
		 * @param ptr Pointer to component T in that sector (may be nullptr).
		 */
		PinnedComponent(Memory::PinnedSector&& pinnedSector, T* ptr) : sec(std::move(pinnedSector)), ptr(ptr) {}

		/// @brief Destructor automatically releases the pin and nulls pointer.
		~PinnedComponent() { release(); }

		/// @return The raw component pointer or nullptr if invalid.
		T* get() const noexcept { return ptr; }

		/// @return Operator access forwarding to underlying component pointer.
		T* operator->() const noexcept { return ptr; }

		/// @return Dereferenced component reference (UB if ptr is null � guard with bool()).
		T& operator* () const noexcept { return *ptr; }

		/// @return True if a valid component pointer is held.
		explicit operator bool() const noexcept { return ptr != nullptr; }

		/// @brief Release the pin early. After this call get() returns nullptr.
		/// @thread_safety Thread-confined. Releasing is what
		///                unblocks a thread waiting to destroy this entity, so calling it as
		///                soon as the pointer is finished with is worth doing.
		void release() { sec.release(); ptr = nullptr; }

	private:
		Memory::PinnedSector sec;   ///< RAII handle for sector pin.
		T* ptr = nullptr;           ///< Pointer to pinned component (or nullptr).
	};

	template <bool ThreadSafe, typename Allocator, bool Ranged, typename T, typename ...ComponentTypes>
	class ArraysView;

	/**
	 * @brief Central ECS registry that owns component sector arrays, entities and iteration utilities.
	 *
	 * @tparam ThreadSafe If true, operations use internal locks / pin counters for safe concurrent access.
	 * @tparam Allocator  Allocator used by SectorsArray (defaults to chunked allocator).
	 *
	 * Responsibilities:
	 *   - Entity lifecycle (allocate / erase ids).
	 *   - Lazily create or explicitly register component arrays (can group types).
	 *   - Component add / overwrite / remove (single or batch).
	 *   - Bulk entity destruction with all their components.
	 *   - Iteration via ArraysView over one or more component types.
	 *
	 * Thread safety. Every public member carries an @thread_safety line. It answers one
	 * question -- may two threads call this on the same object at once -- with one of three
	 * answers, and adds "; blocks" when the call waits:
	 *
	 *   - "Internally synchronized." Call it from any number of threads at once.
	 *
	 *   - "Thread-confined." The object belongs to one thread and holds no lock, exactly as a
	 *     std::vector does. Do not add a mutex around it -- give each thread its own. Views,
	 *     pins, access guards and command buffers are all this.
	 *
	 *   - "Caller must ensure exclusive access." A *shared* object with no synchronization of
	 *     its own. Here you do have to supply the ordering: a lock, or a place in the frame
	 *     where nothing else is running. Setup switches and raw lock accessors live here.
	 *
	 *   The difference matters because the fix is opposite. The first says stop sharing it;
	 *   the second says guard it.
	 *
	 *   - "Not applicable (single-threaded build)." Only exists when ThreadSafe == false.
	 *
	 * "; blocks" is separate from all three, because waiting and being callable concurrently
	 * are different questions -- a destructor waits for in-flight readers and still must not
	 * race with new ones. It means one specific thing: the call waits for a pin, a hold or a
	 * view to be released, which is state a *reader* controls. That is the wait worth warning
	 * about, because a caller can be the reader it is waiting for. Ordinary contention for a
	 * mutex between writers is not marked -- almost every synchronized call has some, and
	 * flagging it would say nothing. Where it appears, what is waited for is named. Two widths:
	 *       * one sector -- waits until that entity's sector carries no pins. Only a pin on
	 *         that same entity delays it.
	 *       * the whole array -- waits until the array carries no pins *and* no open views.
	 *         Every ArraysView holds one for as long as it lives, so anything in this group
	 *         cannot finish while a view on that array is open. From the thread that opened
	 *         the view it is a deadlock; from another thread it finishes when the view closes,
	 *         and a thread iterating in a tight loop can hold it off indefinitely.
	 *     Nothing here is enforced at compile time. Debug builds assert on the same-thread
	 *     case; release builds simply block.
	 *
	 * All of the above is about the *shape* of an array. A component's *value* is a separate
	 * question the container does not answer: reading one while another thread writes it is
	 * the caller's to arrange. @see access(), setAccessTracking()
	 *
	 * Performance notes:
	 *   - Component insertion is O(1) amortized (sector-based).
	 *   - hasComponent is O(1) (sector lookup + bit test).
	 *   - destroyEntities (sequential version here) visits each array => O(A * log N) for sorting per array prep.
	 *
	 * @warning Entity ids are reused; do not cache them beyond system boundaries without validation.
	 * @note Use reserve<Components...>() to pre-allocate sector capacity and reduce reallocations.
	 */
	template<bool ThreadSafe = true, typename Allocator = Memory::ChunksAllocator<8192>>
	class Registry final {
		template <bool TS, typename Alloc, bool Ranged, typename T, typename ...ComponentTypes>
		friend class ArraysView;

		template <bool TS, typename Alloc>
		friend class Registry;

	public:
		/**
		 * @brief Get a stable numeric type id for component T.
		 * @tparam T Component type.
		 * @thread_safety Internally synchronized. The id is assigned once, by whichever thread
		 *                asks first, and never changes afterwards. Ids come from one process-wide
		 *                counter, so two registries agree on the number for a type.
		 */
		template<typename T>
		FORCE_INLINE static ECSType componentTypeId() noexcept { return Memory::DenseTypeIdGenerator::getTypeId<T>(); }

	public:
		Registry(const Registry& other) noexcept = delete;
		Registry& operator=(const Registry& other) noexcept = delete;
		Registry(Registry&& other) noexcept = delete;
		Registry& operator=(Registry&& other) noexcept = delete;

	public:
		/// @brief Default construct an empty registry (no arrays allocated until first use).
		/// @thread_safety Internally synchronized, vacuously: nothing can name the object until
		///                the constructor returns, so there is nothing to race with.
		Registry() noexcept {
			if constexpr (!ThreadSafe) {
				// No lock-free readers here, so a superseded snapshot has nothing to outlive.
				mNodeBin.setGracePeriod(0);
			}
		}

		/// @brief Destroys all component arrays (each SectorsArray is deleted).
		///
		/// @thread_safety Internally synchronized; blocks. Deleting an array clears it, and
		///                clearing waits until that array carries no pins and no open views, so a
		///                view open on another thread stalls this destructor for as long as it
		///                lives -- in-flight readers are waited for, not run over.
		///
		///                That is a synchronization guarantee, not a lifetime one. Nothing may
		///                *start* using the registry once destruction has begun; the waiting
		///                covers the readers already inside, and cannot cover the ones that
		///                arrive afterwards.
		~Registry() noexcept {
			for (auto array : mComponentsArrays) delete array;
			// Superseded snapshots are in the bin, which frees them on destruction; the one
			// still published is not, so it is freed here.
			std::free(mRegistered.load(std::memory_order_relaxed));
		}

		/**
		 * @brief Maintenance pass (thread-safe build): process deferred erases, free retired memory, and optionally defragment.
		 *
		 * Safe to call from anywhere, including from inside a loop over a view: nothing here
		 * waits. Deferred erases only destroy components in place, and compaction is attempted
		 * rather than awaited -- an array that something is iterating right now is left for the
		 * next call. Calling it more than once a frame is harmless; calling it at a quiet point
		 * simply means more of the work lands on the first try.
		 *
		 * To compact regardless of who is iterating, and to wait for them, call
		 * SectorsArray::defragment() on the array directly.
		 * @param withDefragment If true, arrays that exceed thresholds may compact themselves.
		 * @note Recommended to call once per frame at a stable synchronization point.
		 * @note Automatically frees retired memory that has passed the grace period (default 3 ticks).
		 * @thread_safety Internally synchronized. Nothing here waits, so this is the one
		 *                maintenance entry point that is safe to call from inside a loop over
		 *                a view. clear(), defragment() and the array's own defragment() are not.
		 */
		void update(bool withDefragment = true) noexcept requires(ThreadSafe) {
			mNodeBin.tick();    // superseded snapshots past their grace period
			const auto [begin, end] = registeredArrays();
			for (auto it = begin; it != end; ++it) {
				(*it)->tick();  // Free retired memory older than grace period
				(*it)->processPendingErases(withDefragment);
			}
		}

		/**
		 * @brief Maintenance pass (non-thread-safe build): optionally defragment arrays immediately.
		 * @param withDefragment If true, compacts arrays that request it.
		 * @thread_safety Not applicable (single-threaded build).
		 */
		void update(bool withDefragment = true) noexcept requires(!ThreadSafe) {
			for (auto* array : mComponentsArrays) {
				if (withDefragment) {
					if (array->needDefragment()) {
						array->defragment();
					}
				}
			}
		}

		/**
		 * @brief Process one tick of the grace period for retired memory.
		 * 
		 * Call this once per frame/update cycle. Memory blocks that have waited
		 * the full grace period (default 3 ticks) will be freed.
		 * 
		 * This is safe to call while iterators may be active in other threads -
		 * only sufficiently old memory (older than grace period) will be freed.
		 * 
		 * @note In non-thread-safe mode memory is freed as it is released, so this is a
		 *       no-op returning zero. It stays callable in both modes.
		 * 
		 * @return Total number of memory blocks freed across all arrays
		 */
		/// @thread_safety Internally synchronized. Advances the grace-period counters and frees
		///                what has expired; takes each bin's mutex only when it has something in
		///                it, and never waits for a reader.
		size_t tick() noexcept {
			size_t freed = mNodeBin.tick();
			const auto [begin, end] = registeredArrays();
			for (auto it = begin; it != end; ++it) {
				freed += (*it)->tick();
			}
			return freed;
		}

		/**
		 * @brief Set the grace period (in ticks) before retired memory is freed.
		 * 
		 * Higher values = safer but more memory usage.
		 * Lower values = less memory but risk of use-after-free if iterators live long.
		 * 
		 * Default is 3 ticks, which is safe for typical game loops where
		 * iterators don't survive across frames.
		 * 
		 * @note The non-thread-safe build fixes this at zero and ignores the setter.
		 * 
		 * @param ticks Number of tick() calls before memory is freed
		 * @thread_safety Internally synchronized. The period each bin holds is an atomic, and a
		 *                block captures its own countdown when it is retired, so lowering the
		 *                period never shortens the life of something already queued. Zero is the
		 *                one value that would be unsafe here and it is refused: see below.
		 */
		void setRetireGracePeriod(uint32_t ticks) noexcept {
			if constexpr (ThreadSafe) {
				// Zero means "free on release, nothing lock-free is reading" -- true of
				// Registry<false> and of nothing else. Honouring it here would hand a reader
				// that is mid-walk a chunk that has already been freed, so it is clamped
				// rather than obeyed.
				assert(ticks != 0
					&& "a thread-safe registry cannot retire with no grace period; "
					   "lock-free readers need the blocks to outlive them");
				if (ticks == 0) { ticks = 1; }
			}

			// Remembered as well as applied. Arrays registered later used to be built with the
			// default and never told, so the setting quietly covered only whatever happened to
			// exist when it was called.
			mRetireGracePeriod.store(ticks, std::memory_order_seq_cst);

			const auto [begin, end] = registeredArrays();
			for (auto it = begin; it != end; ++it) {
				(*it)->setRetireGracePeriod(ticks);
			}
		}

	public:
		/**
		 * @brief Check if an entity has a live component T.
		 * @tparam T Component type.
		 * @param entity Entity id.
		 * @return True if the component exists and is alive; false otherwise.
		 * @complexity O(1).
		 * @thread_safety Internally synchronized. Lock-free once the type is registered: the
		 *                slot lookup and the liveness word both come from published snapshots.
		 *                The very first call for a type is not -- getComponentAccess() registers
		 *                the array under a unique lock -- so touch each type once at startup if
		 *                that matters. Tells you the component exists, not that its value is
		 *                stable: another thread may be writing it. @see access()
		 */
		template <class T>
		FORCE_INLINE bool hasComponent(EntityId entity) noexcept {
			const auto access = getComponentAccess<T>();
			auto container = access.array;
			// No pin and no lock: this hands out no pointer, so there is nothing to keep
			// alive. The slot lookup and the alive word both go through the lock-free
			// snapshots (SparseMap seqlock + DenseArrays seqlock), which is exactly what
			// ArraysView::getComponent already does. Pinning here cost two seq_cst RMWs
			// plus a potential wake syscall, and the shared lock serialised every reader,
			// for an answer that is inherently a point-in-time sample either way.
			// findLinearIdx, not findSlot: the latter also resolves the sector's data pointer
			// through the chunk table, and this answer never looks at the data. That address
			// computation was the whole difference against a hand-rolled check.
			const auto idx = container->template findLinearIdx<false>(entity);
			if (idx == INVALID_IDX) {
				return false;
			}
			return Memory::Sector::isAlive(container->template loadAliveWord<ThreadSafe>(idx),
			                               access.layout->isAliveMask);
		}

		/**
		 * @brief Pin component T for an entity (thread-safe build only).
		 * @tparam T Component type.
		 * @param entity Entity id.
		 * @return PinnedComponent<T> (empty if component missing).
		 * @note The returned object must not outlive concurrent modification epochs.
		 * @thread_safety Internally synchronized. The pin is the point: while it is held, that one
		 *                sector will not be moved, destroyed or reused, so the pointer stays good.
		 *                It also delays anything that needs that sector -- destroyComponent and
		 *                destroyEntity for the same entity wait for it, and holding a pin while
		 *                destroying what it points at deadlocks. Keep it short.
		 */
		template<class T>
		[[nodiscard]] PinnedComponent<T> pinComponent(EntityId entity) noexcept requires(ThreadSafe)  {
			auto* container = getComponentContainer<T>();
			auto pinnedSector = container->pinSector(entity);
			if (!pinnedSector) { return {}; }

			auto component = Memory::Sector::getComponent<T>(pinnedSector.getData(), pinnedSector.getIsAlive(), container->getLayout());
			return component ? PinnedComponent<T>{ std::move(pinnedSector), component } : PinnedComponent<T>{};
		}

		/**
		 * @brief Add or overwrite a component T for an entity.
		 * @tparam T Component type.
		 * @tparam Args Constructor argument types for T.
		 * @param entity Entity id (also used logically as sector id).
		 * @param args Construction / assignment arguments.
		 * @return Pointer to the stored component.
		 * @note Overwrites existing component instance (destructive assign semantics inside sector).
		 */
		/**
		 * @brief Add or overwrite component T on an entity.
		 * @warning Illegal while this thread holds a view or a pin on T's array, unless the id
		 *          is above every id already stored: any other position shifts existing
		 *          sectors, which would invalidate the live iterator. Debug builds assert;
		 *          release builds hang. See the SectorsArray class documentation.
		 * @note Adding M components with ids that are not ascending costs O(M*N) this way.
		 *       Use insertBulk() or addComponents() for a batch -- they merge in one pass.
		 * @thread_safety Internally synchronized; blocks. An id above everything already stored
		 *                appends and waits for nothing. An id that lands in the middle has to shift
		 *                the sectors after it, so it waits for the whole array to carry no pins and
		 *                no open views -- including a view this thread has open, which deadlocks.
		 *                Feed ids in ascending order, or use insertBulk(), to stay on the fast path.
		 *
		 *                To add from inside a loop over a view, record into an ecss::CommandBuffer
		 *                and apply it once the loop is done.
		 */
		template <class T, class ...Args>
		FORCE_INLINE T* addComponent(EntityId entity, Args&&... args) noexcept {
			const auto guard = detail::writeScope<T>(componentTypeId<T>());
			return getComponentContainer<T>()->template push<T>(entity, std::forward<Args>(args)...);
		}

		/**
		 * @brief Bulk add components from a generator, inserted as one batch.
		 * @tparam T Component type.
		 * @tparam Func Callable returning std::pair<EntityId,T>. Return {INVALID_ID, {}} to stop.
		 * @param func Generator invoked repeatedly until it signals the end.
		 * @note Ids may be emitted in any order. The batch is collected, then merged in a
		 *       single pass under one write lock.
		 * @thread_safety Internally synchronized; blocks. Same contract as addComponent(): an
		 *                ascending run appends, anything landing in the middle waits for the array
		 *                to carry no pins and no open views.
		 */
		template <class T, typename Func>
		void addComponents(Func&& func) requires(ThreadSafe) {
			// Drain the generator first. Holding the write lock across it saved the per-element
			// lock traffic but still inserted one id at a time, so a generator that emitted ids
			// out of order paid O(M*N): each middle insert shifts the tail and rewrites the
			// sparse entry of every sector it passes. Sorting the batch once and merging it is
			// linear instead -- see SectorsArray::insertBulk.
			//
			// The generator therefore runs outside the lock now. Its side effects are no longer
			// serialised with the insertion, but the insertion itself is still one atomic batch,
			// and a generator that touched this same container under the lock would have
			// deadlocked before.
			std::vector<std::pair<EntityId, T>> batch;
			auto f = std::forward<Func>(func);
			for (auto res = f(); res.first != INVALID_ID; res = f()) {
				batch.emplace_back(res.first, std::move(res.second));
			}
			if (batch.empty()) { return; }
			const auto guard = detail::writeScope<T>(componentTypeId<T>());
			getComponentContainer<T>()->template insertBulk<T>(batch.begin(), batch.end());
		}

		/**
		 * @brief Bulk insert of component T for a batch of (entity, value) pairs.
		 * @tparam T Component type.
		 * @tparam It Iterator over pair-like {EntityId, T}.
		 * @param first,last Range of pairs. Ids may be in any order and may fall anywhere in the
		 *        range already stored; an id already present is overwritten.
		 * @note Amortizes per-element overhead (existence check, insert-position search, view
		 *       publish) and, in the TS build, the write lock and pin wait across the whole range.
		 *       Prefer this to a loop of addComponent() whenever the ids are not ascending: the
		 *       loop costs O(M*N), this sorts once and merges in a single pass.
		 * @thread_safety Internally synchronized; blocks. A batch entirely above what is stored is
		 *                appended and waits for nothing. Otherwise the batch is merged, which moves
		 *                existing sectors, and that waits for the array to carry no pins and no open
		 *                views -- from a thread holding a view on this array, a deadlock.
		 *
		 *                To add from inside a loop over a view, record into an ecss::CommandBuffer
		 *                and apply it once the loop is done.
		 */
		template <class T, class It>
		void insertBulk(It first, It last) {
			const auto guard = detail::writeScope<T>(componentTypeId<T>());
			getComponentContainer<T>()->template insertBulk<T>(first, last);
		}

		/**
		 * @brief Destroy component T for a single entity (does nothing if not present).
		 * @tparam T Component type.
		 * @param entity Entity id.
		 * @complexity O(1).
		 * @thread_safety Internally synchronized; blocks, but only on this one sector. It waits
		 *                for that entity's sector to carry no pins; pins on other entities and views
		 *                over the array do not delay it. Holding a pin to the component being
		 *                destroyed, on this thread, deadlocks.
		 */
		template <class T>
		void destroyComponent(EntityId entity) noexcept {
			const auto guard = detail::writeScope<T>(componentTypeId<T>());
			if (auto container = getComponentContainer<T>()) {
				if constexpr (ThreadSafe) {
					// Lock-free presence check before locking (see destroySector).
					if (!container->template containsSector<false>(entity)) {
						return;
					}
					// Destroys one member in place -- only that sector must be unpinned.
					container->exclusiveWhenUnpinned(entity, [&] {
						auto idx = container->template findLinearIdx<false>(entity);
						if (idx != INVALID_IDX) {
							auto& isAlive = container->template getIsAliveRef<false>(idx);
							auto before = isAlive;
							Memory::Sector::destroyMember<ThreadSafe>(container->mAllocator.at(idx), isAlive, container->template getLayoutData<T>());
							if (before != isAlive && !Memory::Sector::isSectorAlive(isAlive)) {
								container->incDefragmentSize();
							}
						}
					});
				}
				else {
					auto idx = container->template findLinearIdx<false>(entity);
					if (idx != INVALID_IDX) {
						auto& isAlive = container->template getIsAliveRef<false>(idx);
						auto before = isAlive;
						Memory::Sector::destroyMember<ThreadSafe>(container->mAllocator.at(idx), isAlive, container->template getLayoutData<T>());
						if (before != isAlive && !Memory::Sector::isSectorAlive(isAlive)) {
							container->incDefragmentSize();
						}
					}
				}
			}
		}

		/**
		 * @brief Destroy component T for a batch of entities.
		 * @tparam T Component type.
		 * @param entities Entity id list (will be sorted and truncated to valid sector capacity).
		 * @note Modifies the input vector (sorting, trimming out-of-range ids).
		 * @warning Pins are waited if thread-safe; call outside tight critical paths if possible.
		 * @thread_safety Internally synchronized; blocks, but only on the named sectors. Waits for
		 *                each listed entity to carry no pins; unrelated entities and open views do
		 *                not delay it.
		 */
		template <class T>
		void destroyComponent(std::vector<EntityId>& entities) noexcept {
			if (entities.empty()) {return;}
			const auto guard = detail::writeScope<T>(componentTypeId<T>());

			if (auto container = getComponentContainer<T>()) {
				const auto& layout = container->template getLayoutData<T>();
				if constexpr (ThreadSafe) {
					// A whole batch of sectors is touched, so require quiescence rather than
					// testing them one by one; the wait happens outside the write lock.
					container->exclusiveWhenQuiescent([&] {
						prepareEntities(entities, container->template sparseCapacity<false>());
						if (entities.empty()) {
							return;
						}

						for (const auto sectorId : entities) {
							// Use findSlot for single lookup
							auto slotInfo = container->template findSlot<false>(sectorId);
							if (slotInfo) {
								auto& isAlive = container->template getIsAliveRef<false>(slotInfo.linearIdx);
								auto before = isAlive;
								Memory::Sector::destroyMember<ThreadSafe>(slotInfo.data, isAlive, layout);
								if (before != isAlive && !Memory::Sector::isSectorAlive(isAlive)) {
									container->incDefragmentSize();
								}
							}
						}
					});
				}
				else {
					prepareEntities(entities, container->template sparseCapacity<false>());

					for (const auto sectorId : entities) {
						// Use findSlot for single lookup
						auto slotInfo = container->template findSlot<false>(sectorId);
						if (slotInfo) {
							auto& isAlive = container->template getIsAliveRef<false>(slotInfo.linearIdx);
							auto before = isAlive;
							Memory::Sector::destroyMember<ThreadSafe>(slotInfo.data, isAlive, layout);
							if (before != isAlive && !Memory::Sector::isSectorAlive(isAlive)) {
								container->incDefragmentSize();
							}
						}
					}
				}
			}
		}

		/**
		 * @brief Copy-in an externally built sectors array for component T.
		 *
		 * @warning The source must have been built over the same component types in the same
		 *          order as T's registered array. [A, B] and [B, A] are different layouts, and
		 *          so are [A] and [A, B]. A mismatch leaves this registry unchanged (and
		 *          asserts in debug) rather than repointing the array at a foreign layout:
		 *          the sector size and the liveness bits would no longer describe the bytes,
		 *          and the registry would still route B to a different array anyway.
		 * @thread_safety Internally synchronized; blocks. Replaces the whole array, so it waits
		 *                until the destination carries no pins and no open views.
		 */
		template<typename T, bool TS, typename Alloc>
		FORCE_INLINE void insert(const Memory::SectorsArray<TS, Alloc>& array) noexcept { *getComponentContainer<T>() = array; }

		/// @brief Move-in an externally built sectors array for component T.
		/// @thread_safety Internally synchronized; blocks. Replaces the whole array, so it waits
		///                until the destination carries no pins and no open views.
		template<typename T, bool TS, typename Alloc>
		FORCE_INLINE void insert(Memory::SectorsArray<TS, Alloc>&& array) noexcept { *getComponentContainer<T>() = std::move(array); }

	public:
		/**
		 * @brief Let views carry out the maintenance update() does, for the arrays they touch.
		 *
		 * With this on there is no call to place in the frame: opening a view first gives its
		 * arrays the pass update() would have given them -- retire the memory whose grace
		 * period has run out, apply deferred erases, and compact if the array asked for it and
		 * is free at that moment. A busy array is skipped, exactly as in update(), so this
		 * never blocks and never changes what iteration sees.
		 *
		 * Compaction is done where it pays: right before the iteration that benefits from it.
		 * The total work is the same either way -- an array wants compacting once, and once
		 * done it stops asking -- so this moves the cost rather than adding it.
		 *
		 * Off by default: opening a view is a read, and doing structural work inside one should
		 * be asked for rather than assumed. Set it once at startup, before other threads exist.
		 * update() keeps working and stays the way to control when the work lands.
		 * @thread_safety Internally synchronized. One relaxed store in the thread-safe build,
		 *                a plain one otherwise. Still worth doing at startup: flipping it
		 *                mid-frame changes what opening a view does, so two threads can disagree
		 *                about whether their views maintain.
		 */
		void setAutoMaintenance(bool enabled) noexcept { storeAutoMaintenance(enabled); }

		/**
		 * @brief Watch for two threads touching one component type at once (debug builds).
		 *
		 * The container keeps an array's *shape* safe on its own: it will not be relocated
		 * under an iterator, and a pinned sector will not move or die. A component's *value*
		 * is a different matter -- guarding that would mean locking or pinning per element,
		 * which costs more than the lock-free read paths save. So a system reading Position
		 * while another writes it is a race the container cannot see.
		 *
		 * With this on, it is seen: the first overlap aborts and names the type and the two
		 * threads. A view counts as reading its component types for as long as it lives, and a
		 * mutator as writing one for the duration of the call; re-entering from the same thread
		 * is fine, since a system routinely reads what it just wrote.
		 *
		 * Worth leaving on for the whole of development. It compiles to nothing when NDEBUG is
		 * set, so there is nothing to turn off before shipping.
		 */
		/**
		 * @brief Claim component types for the length of a system, so two of them cannot
		 *        touch the same type at once.
		 *
		 * @code
		 * auto access = reg.access<Read<Position>, Write<Velocity>>();
		 * for (auto [e, p, v] : reg.view<Position, Velocity>()) {
		 *     v->dx += p->x;          // inside, everything runs at full speed as before
		 * }
		 * @endcode
		 *
		 * The container keeps an array's shape safe by itself; it does not keep a component's
		 * value stable while another thread writes it, and guarding that per element would
		 * cost more than the lock-free read paths save. This puts the guarantee at the
		 * granularity systems actually work at -- a whole component type -- so it is paid once
		 * per system rather than once per element. A reader-writer lock per type: 29.5 ns for
		 * one, 84.8 for three, against 27 ns to pin a single component -- and pinning would have
		 * to happen per element rather than per system.
		 *
		 * Name every type the system touches in one call. Taking them one at a time lets two
		 * systems claim the same pair in opposite orders and stop; asked for together, they are
		 * sorted by type id so every caller agrees on the order.
		 *
		 * Nesting from the same thread is fine and does nothing. Asking to write a type this
		 * thread already reads is refused in debug rather than upgraded, since there is no
		 * atomic upgrade and doing it in two steps brings the deadlock back.
		 *
		 * Nothing forces its use: a system that knows it is the only one touching a type can
		 * skip it and lose nothing. setAccessTracking() is what finds the ones that were wrong
		 * to skip.
		 * @thread_safety Internally synchronized; blocks by design. This is the one entry point
		 *                whose whole job is to wait: it takes a reader-writer lock per component type
		 *                and holds it until the guard dies. Name every type the system touches in one
		 *                call -- claiming them one at a time is how two systems deadlock.
		 */
		template <typename... Claims>
		[[nodiscard]] detail::AccessGuard access() {
			static_assert(sizeof...(Claims) > 0, "name at least one component type");
			std::array<detail::AccessGuard::Entry, sizeof...(Claims)> claims{
				detail::AccessGuard::Entry{
					&typeMutex(componentTypeId<typename Claims::Component>()),
					componentTypeId<typename Claims::Component>(),
					Claims::kWrites,
					false }...
			};
			return detail::AccessGuard(claims);
		}

		/// @thread_safety Internally synchronized. One relaxed store to a process-wide atomic
		///                flag, so calling it while threads run is not a race -- but it is still a
		///                startup switch: turning tracking on late cannot show the overlaps that
		///                already happened. Compiled out entirely when NDEBUG is set.
		void setAccessTracking(bool enabled) noexcept {
#ifndef NDEBUG
			detail::AccessTracker::setEnabled(enabled);
#else
			(void)enabled;
#endif
		}

		/// @return Whether views maintain the arrays they open. @see setAutoMaintenance
		/// @thread_safety Internally synchronized. One relaxed load in the thread-safe build,
		///                a plain one otherwise.
		[[nodiscard]] bool autoMaintenance() const noexcept { return loadAutoMaintenance(); }

		/**
		 * @brief Create an iterable view limited to given entity ranges.
		 * @tparam Components Component types to fetch; first drives iteration order.
		 * @param ranges Half-open entity ranges.
		 * @return ArraysView instance (ranged iteration).
		 * @thread_safety Internally synchronized. Any number of threads may iterate at once.
		 *                The view holds the arrays it names for as long as it lives, which is what
		 *                keeps sectors from moving underneath it -- and equally what makes clear(),
		 *                defragment() and a middle insert wait. Keep views short, and do not open one
		 *                around a structural change to the same array.
		 */
		template<typename... Components>
		FORCE_INLINE auto view(const Ranges<EntityId>& ranges) noexcept {
			maintainFor<Components...>();
			return ArraysView<ThreadSafe, Allocator, true, Components...>{ this, ranges };
		}

		/**
		 * @brief Create a full-range iterable view over all entities with the main component.
		 * @tparam Components Component types to access; first drives iteration.
		 * @return ArraysView instance (full range).
		 * @thread_safety Internally synchronized. Any number of threads may iterate at once.
		 *                The view holds the arrays it names for as long as it lives, which is what
		 *                keeps sectors from moving underneath it -- and equally what makes clear(),
		 *                defragment() and a middle insert wait. Keep views short, and do not open one
		 *                around a structural change to the same array.
		 */
		template<typename... Components>
		FORCE_INLINE auto view() noexcept {
			maintainFor<Components...>();
			return ArraysView<ThreadSafe, Allocator, false, Components...>{this};
		}

		/**
		 * @brief Apply a function to each entity in a list, pinning requested component types (thread-safe build).
		 * @tparam Components Component types to pin.
		 * @tparam Func Callable signature: void(EntityId, Components*...).
		 * @param entities Entity ids to process.
		 * @param func Function invoked per entity.
		 * @note Skips entities missing any main pinned component (pointer passed may be nullptr for non-main).
		 * @thread_safety Internally synchronized. Each worker opens its own view over its slice.
		 *                The same rule applies inside func: no structural change to an array being
		 *                iterated.
		 */
		template<typename... Components, typename Func>
		inline void forEachAsync(const std::vector<EntityId>& entities, Func&& func) noexcept requires(ThreadSafe)
		{
			if (entities.empty()) { return; }
			auto f = std::forward<Func>(func);
			for (const auto& entity : entities) { withPinned<Components...>(entity, f); }
		}

	public:
		// ===== Container management ==========================================

		/**
		 * @brief Reserve capacity (in sectors array units) for each listed component type.
		 * @tparam Components Component types to reserve for.
		 * @param newCapacity Target capacity (implementation may round up).
		 * @thread_safety Internally synchronized. Takes each array's write lock to grow it, which
		 *                is brief, and does not wait for pins or views: growth adds chunks and moves
		 *                no sector. Worth doing before the threads start, to keep it off the frame.
		 */
		template <class... Components>
		FORCE_INLINE void reserve(uint32_t newCapacity) noexcept { (getComponentContainer<Components>()->reserve(newCapacity), ...); }

		/**
		 * @brief Clear all component arrays and remove all entities.
		 * @note Does not shrink capacity.
		 * @post contains(id)==false for any previously allocated entity.
		 * @thread_safety Internally synchronized; blocks on every array. Each one waits until it
		 *                carries no pins and no open views. A view open on another thread holds this
		 *                up for as long as it lives; a view open on this thread deadlocks.
		 *
		 *                clearAsync() is the deferred form, safe to call from anywhere.
		 */
		void clear() noexcept {
			if constexpr (ThreadSafe) {
				{
					const auto [begin, end] = registeredArrays();
					for (auto it = begin; it != end; ++it) {
						(*it)->clear();
					}
				}

				std::unique_lock lock(mEntitiesMutex);
				mEntities.clear();
			}
			else {
				for (auto* array : mComponentsArrays) {
					array->clear();
				}

				mEntities.clear();
			}
		}

		/**
		 * @brief Ask every array to clear itself at the next safe point, instead of now.
		 *
		 * The deferred counterpart to clear(). Records the wish and returns, so unlike clear()
		 * it is safe to call from anywhere, including from inside a loop over a view. Each
		 * array performs it the first time update() finds it free.
		 *
		 * Entities are released immediately -- that takes no structural change -- so contains()
		 * stops reporting them at once, while their components go when the arrays come free.
		 *
		 * @note Asked for, not promised. @see clear(), update()
		 * @thread_safety Internally synchronized. One relaxed store per array plus the id set;
		 *                nothing is waited for.
		 */
		void clearAsync() noexcept {
			if constexpr (ThreadSafe) {
				const auto [begin, end] = registeredArrays();
				for (auto it = begin; it != end; ++it) { (*it)->clearAsync(); }

				std::unique_lock lock(mEntitiesMutex);
				mEntities.clear();
			}
			else {
				clear();
			}
		}

		/**
		 * @brief Defragment all arrays (compacts fragmented dead slots).
		 * @note Can be expensive if many arrays large � schedule during low frame-load moments.
		 * @thread_safety Internally synchronized; blocks on every array. Compaction moves sectors,
		 *                so each array waits until it carries no pins and no open views. update() is
		 *                the deferred form: it skips busy arrays and picks them up next time, which
		 *                makes it safe to call from inside a loop over a view.
		 */
		void defragment() noexcept {
			if constexpr(ThreadSafe) {
				const auto [begin, end] = registeredArrays();
				for (auto it = begin; it != end; ++it) {
					(*it)->defragment();
				}
			}
			else {
				for (auto* array : mComponentsArrays) {
					array->defragment();
				}
			}
		}

		/**
		 * @brief Explicitly register (group) component types into a shared sectors array.
		 * @tparam ComponentTypes Component types to co-locate.
		 * @param capacity Initial reserve (optional).
		 * @param allocator Allocator instance to move.
		 * @note All types must either all be new or already co-grouped; partial mixes assert.
		 * @warning Call before first implicit access to any of the grouped types.
		 * @thread_safety Internally synchronized. Publishes a new array snapshot; the superseded one
		 *                is retired rather than freed, so a reader still walking it is safe. Grouping
		 *                decides the memory layout, so do it at startup, before anything stores a
		 *                component of these types.
		 */
		template<typename... ComponentTypes>
		void registerArray(uint32_t capacity = 0, Allocator allocator = {}) noexcept {
			if constexpr (ThreadSafe) {
				Memory::SectorsArray<ThreadSafe, Allocator>* sectorsArray;
				{
					auto lock = std::unique_lock(componentsArrayMapMutex);

					bool anyPresent = ((mComponentsArraysMap.size() > componentTypeId<ComponentTypes>() && mComponentsArraysMap[componentTypeId<ComponentTypes>()] != nullptr) || ...);
					bool allPresent = ((mComponentsArraysMap.size() > componentTypeId<ComponentTypes>() && mComponentsArraysMap[componentTypeId<ComponentTypes>()] != nullptr) && ...);
					if (anyPresent && !allPresent) {
						assert(false && "Partial registerArray across mixed components is not allowed");
						return;
					}

					bool isCreated = true;
					((isCreated = isCreated && mComponentsArraysMap.size() > componentTypeId<ComponentTypes>() && mComponentsArraysMap[componentTypeId<ComponentTypes>()]), ...);
					if (isCreated) {
						return;
					}

					ECSType maxId = 0;
					((maxId = std::max(maxId, componentTypeId<ComponentTypes>())), ...);
					if (maxId >= mComponentsArraysMap.size()) {
						mComponentsArraysMap.resize(maxId + 1);
					}

					sectorsArray = Memory::SectorsArray<ThreadSafe, Allocator>::template create<ComponentTypes...>(std::move(allocator));
					// Whatever setRetireGracePeriod() last asked for applies to this one too.
					// Only in the thread-safe build: the plain one deliberately runs at zero,
					// having no lock-free readers for the blocks to outlive.
					if constexpr (ThreadSafe) {
						sectorsArray->setRetireGracePeriod(
							mRetireGracePeriod.load(std::memory_order_seq_cst));
					}
					mComponentsArrays.push_back(sectorsArray);
					((mComponentsArraysMap[componentTypeId<ComponentTypes>()] = sectorsArray), ...);
					// Resolve each component's LayoutData once, here, where the pack is still a
					// compile-time thing. Every later lookup would otherwise re-derive it by
					// scanning the layout's type tokens, which is what made a grouped array
					// cost more per query the more types it held.
					recordLayouts<ComponentTypes...>(sectorsArray);
					publishRegistered();
				}

				sectorsArray->reserve(capacity);
			}
			else {
				Memory::SectorsArray<ThreadSafe, Allocator>* sectorsArray;
				bool anyPresent = ((mComponentsArraysMap.size() > componentTypeId<ComponentTypes>() && mComponentsArraysMap[componentTypeId<ComponentTypes>()] != nullptr) || ...);
				bool allPresent = ((mComponentsArraysMap.size() > componentTypeId<ComponentTypes>() && mComponentsArraysMap[componentTypeId<ComponentTypes>()] != nullptr) && ...);
				if (anyPresent && !allPresent) {
					assert(false && "Partial registerArray across mixed components is not allowed");
					return;
				}

				bool isCreated = true;
				((isCreated = isCreated && mComponentsArraysMap.size() > componentTypeId<ComponentTypes>() && mComponentsArraysMap[componentTypeId<ComponentTypes>()]), ...);
				if (isCreated) {
					return;
				}

				ECSType maxId = 0;
				((maxId = std::max(maxId, componentTypeId<ComponentTypes>())), ...);
				if (maxId >= mComponentsArraysMap.size()) {
					mComponentsArraysMap.resize(maxId + 1);
				}

				sectorsArray = Memory::SectorsArray<ThreadSafe, Allocator>::template create<ComponentTypes...>(std::move(allocator));
				mComponentsArrays.push_back(sectorsArray);
				((mComponentsArraysMap[componentTypeId<ComponentTypes>()] = sectorsArray), ...);
				recordLayouts<ComponentTypes...>(sectorsArray);
				publishRegistered();

				sectorsArray->reserve(capacity);
			}
		}

		/**
		 * @brief Get (or lazily create) the sectors container for component T.
		 * @tparam T Component type.
		 * @return Pointer to container holding (possibly grouped) T.
		 * @note Will implicitly register a single-type array if not pre-registered.
		 * @thread_safety Internally synchronized. Returns the array, and the array outlives every
		 *                call on this registry, so the pointer stays valid. What you then call on it
		 *                carries its own contract -- see SectorsArray.
		 */
		template <class T>
		[[nodiscard]] Memory::SectorsArray<ThreadSafe, Allocator>* getComponentContainer() noexcept {
			// Lock-free: one acquire load of the published snapshot. This is on the entry of
			// every single registry call, and taking a registry-global shared_lock here made
			// all of them contend on one cache line (measured ~300x per-op latency at 32
			// threads). The snapshot is safe because registration is append-only -- an entry,
			// once set, is never cleared or repointed before destruction.
			const auto componentType = componentTypeId<T>();
			if (const auto* node = mRegistered.load(std::memory_order_acquire)) [[likely]] {
				if (componentType < node->mapCount) {
					if (auto* array = node->map[componentType]) [[likely]] {
						return array;
					}
				}
			}

			return registerArray<T>(), getComponentContainer<T>();
		}

		/// @brief The array holding T together with T's layout record, from one snapshot load.
		struct ComponentAccess {
			Memory::SectorsArray<ThreadSafe, Allocator>* array = nullptr;
			const Memory::LayoutData* layout = nullptr;
			explicit operator bool() const noexcept { return array != nullptr; }
		};

		/**
		 * @brief Resolve T's array and layout in a single lookup.
		 *
		 * The layout was resolved when the array was registered, so the per-call scan over the
		 * layout's type tokens disappears -- that scan is why a query against a grouped array
		 * grew more expensive the more component types the group held.
		 *
		 * The cached record is checked, not assumed: Registry::insert can still repoint an
		 * array at a different layout, and the recorded one would then describe the wrong
		 * sector shape. Comparing the layout the array reports now against the one this entry
		 * was built from costs a single load and compare -- measured at 0.2-0.35 ns against a
		 * 2-6 ns saving -- and turns a stale entry into a slow path rather than a wrong answer.
		 * @thread_safety Internally synchronized. Registers the array on first use, which publishes
		 *                a new snapshot; the old one is retired, not freed, so a concurrent reader
		 *                walking it is safe.
		 */
		template <class T>
		[[nodiscard]] FORCE_INLINE ComponentAccess getComponentAccess() noexcept {
			if (const auto access = lookupComponentAccess<T>()) [[likely]] {
				return access;
			}
			// First use of T: register it, then look again. Written as a second lookup rather
			// than a recursive call because GCC rejects always_inline on a function that calls
			// itself, even down a branch taken once per component type.
			registerArray<T>();
			return lookupComponentAccess<T>();
		}

		/// @brief One snapshot load; empty if T has no array yet. @see getComponentAccess
		/// @thread_safety Internally synchronized. Lock-free; returns a null access if the type has
		///                not been registered rather than registering it.
		template <class T>
		[[nodiscard]] FORCE_INLINE ComponentAccess lookupComponentAccess() noexcept {
			const auto componentType = componentTypeId<T>();
			if (const auto* node = mRegistered.load(std::memory_order_acquire)) [[likely]] {
				if (componentType < node->mapCount) {
					if (auto* array = node->map[componentType]) [[likely]] {
						const auto* layout = node->layout[componentType];
						if (layout && node->meta[componentType] == array->getLayout()) [[likely]] {
							return { array, layout };
						}
						// Repointed since registration: fall back to asking the array itself.
						return { array, &array->template getLayoutData<T>() };
					}
				}
			}
			return {};
		}

	public:
		// ===== Entities API ===================================================

		/// @return True if registry currently owns entityId.
		/// @return True if the registry currently owns entityId. Lock-free: a single load.
		/// @thread_safety Internally synchronized. Lock-free read of the id set.
		FORCE_INLINE bool contains(EntityId entityId) const noexcept { return mEntities.contains(entityId); }

		/// @brief Allocate (take) a new entity id.
		/// @brief Allocate (take) a new entity id.
		/// @note Lock-free in the thread-safe build: the id bitmap claims a bit with a CAS.
		///       Serialising this on the registry mutex cost ~560x per-op latency at 32 threads.
		/// @thread_safety Internally synchronized. Lock-free claim out of the id set.
		FORCE_INLINE EntityId takeEntity() noexcept { return mEntities.take(); }

		/// @brief Allocate @p count entity ids in one pass, appending them to @p out.
		///
		/// Prefer this to a loop of takeEntity() when streaming a region in: the bitmap is
		/// walked once instead of once per id, and in the thread-safe build a free word of 64
		/// ids is claimed with a single atomic.
		/// @thread_safety Internally synchronized. Lock-free, and claims a whole word of ids per
		///                atomic operation rather than one at a time.
		FORCE_INLINE void takeEntities(size_t count, std::vector<EntityId>& out) noexcept {
			mEntities.take(count, out);
		}

		/// @brief Snapshot all entity ids (copy).
		/// @thread_safety Internally synchronized. Snapshot: correct when taken, stale as soon as
		///                another thread takes or destroys an id.
		FORCE_INLINE std::vector<EntityId> getAllEntities() const noexcept
		{
			if constexpr (ThreadSafe) {
				// Shared here only to exclude clear(), which is the one operation the id set
				// cannot absorb concurrently. take/erase run lock-free alongside.
				auto lock = std::shared_lock(mEntitiesMutex); return mEntities.getAll();
			}
			else {
				return mEntities.getAll();
			}
		}

		/**
		 * @brief Destroy a single entity and all of its components.
		 * @param entityId Entity to remove (ignored if not owned).
		 * @complexity O(A) with A = number of component arrays.
		 * @thread_safety Internally synchronized; blocks, but only on this entity's sectors. Each
		 *                array holding the entity is asked to destroy in place, which waits for that
		 *                one sector to carry no pins. Arrays that do not hold it are skipped without
		 *                taking their lock. A pin this thread holds on the entity deadlocks.
		 */
		void destroyEntity(EntityId entityId) noexcept {
			// Presence check: cheap lock-free early-out.
			if (!mEntities.contains(entityId)) return;
			// Destroy components FIRST, under each array's write lock. If a concurrent
			// destroyEntity(same id) races us, per-array destroy is idempotent and safe.
			destroySector(entityId);
			// Release the id only AFTER the components are gone. This ordering is what
			// closes the window where a concurrent takeEntity() could recycle the id while
			// destroySector was still walking the component arrays and would then destroy
			// the freshly emplaced components. It is program order, not the mutex, that
			// provided it -- so it survives the id set becoming lock-free.
			mEntities.erase(entityId);
		}

		/**
		 * @brief Destroy a batch of entities and all their components (sequential per-array).
		 * @param entities List of entities (not modified).
		 * @note Safe to call while other threads query (ThreadSafe=true).
		 * @warning No parallelization here to avoid thread lifetime complexity.
		 */
		/// @brief Destroy a batch of entities across every registered array.
		/// @param entities Ids to destroy. Sorted ascending is the cheap case; any other order
		///        is sorted in place first, since destroyInArray binary-searches this range to
		///        trim ids past each array's sparse map.
		/// @note Far cheaper than a loop of destroyEntity(): one lock and one pass per array
		///       rather than per entity -- 14.3 ns per entity against 74.4.
		/// @thread_safety Internally synchronized; blocks, but only on the named entities' sectors.
		void destroyEntities(std::vector<EntityId>& entities) noexcept {
			if (entities.empty()) {
				return;
			}

			// Checking costs 0.18 ns per entity, sorting 3.4 on an already-ordered list and 47
			// on a shuffled one. Callers usually have order for free -- ids gathered by walking
			// a view or getAllEntities() come out ascending -- and were paying for a pass that
			// had nothing to do.
			if (!std::is_sorted(entities.begin(), entities.end())) {
				std::sort(entities.begin(), entities.end());
			}

			auto destroyInArray = [&](auto* array, const EntityId* begin, const EntityId* end) {
				const auto layout = array->getLayout();
				// The list is sorted, so from the first id past this array's sparse capacity
				// onwards nothing can be in it. Read under the caller's lock.
				const auto cap = static_cast<EntityId>(array->template sparseCapacity<false>());
				const EntityId* const trimmedEnd = std::lower_bound(begin, end, cap);
				if (trimmedEnd == begin) { return; }

				for (auto p = begin; p != trimmedEnd; ++p) {
					auto slotInfo = array->template findSlot<false>(*p);
					if (slotInfo) {
						Memory::Sector::destroySectorData<ThreadSafe>(slotInfo.data, array->template getIsAliveRef<false>(slotInfo.linearIdx), layout);
						array->incDefragmentSize();
					}
				}
			};

			if constexpr (ThreadSafe) {
				const auto [begin, end] = registeredArrays();
				for (auto it = begin; it != end; ++it) {
					auto* array = *it;
					// Destroying in place moves no sector, so only the sectors being destroyed
					// need to be unpinned. exclusiveWhenQuiescent waited for every pin and hold
					// on the array instead, and one unrelated holder is enough to stall it: a
					// camera pinning a Transform stopped a world unload that never touched that
					// entity. This matches what destroySector() already did for a single id.
					//
					// The wait covers the whole list while destroyInArray trims it to the
					// array's capacity, and that direction is the safe one. Trimming the wait to
					// match would mean reading the capacity out here, before the lock; if it grew
					// in between, the destroy would reach an id nothing had waited for -- a
					// pinned sector destroyed underneath its holder.
					array->exclusiveWhenUnpinned(entities.data(), entities.data() + entities.size(), [&] {
						destroyInArray(array, entities.data(), entities.data() + entities.size());
					});
				}
			}
			else {
				for (auto* array : mComponentsArrays) {
					destroyInArray(array, entities.data(), entities.data() + entities.size());
				}
			}

			for (auto id : entities) {
				mEntities.erase(id);
			}
		}

		/// @brief Defragment the container for component T (if it exists).
		/// @thread_safety Internally synchronized; blocks. Waits until this one array carries no
		///                pins and no open views. @see defragment()
		template<typename T>
		FORCE_INLINE void defragment() noexcept { if (auto container = getComponentContainer<T>()) { container->defragment();} }

		/// @brief Set defragment threshold for component T container.
		/// @thread_safety Internally synchronized. One relaxed store; it changes when compaction is
		///                requested, never compaction itself.
		template<typename T>
		FORCE_INLINE void setDefragmentThreshold(float threshold) { if (auto container = getComponentContainer<T>()) { container->setDefragmentThreshold(threshold); } }

	private:
		/**
		 * @brief Destroy a single entity across all arrays (internal helper).
		 * @param entityId Entity id.
		 */
		void destroySector(EntityId entityId) noexcept {
			if constexpr (ThreadSafe) {
				// Snapshot walk: this runs once per destroyEntity, and the old form took a
				// registry-global shared_lock and heap-copied the array list every time.
				const auto [begin, end] = registeredArrays();

				for (auto it = begin; it != end; ++it) {
					auto* array = *it;
					// Lock-free presence check first. Taking the write lock and only then asking
					// whether the entity is even in this array made destroyEntity cost one lock
					// acquisition per *registered* array rather than per array the entity is
					// actually in -- 20.7 ns with one array, 116 ns with nine.
					//
					// A concurrent addComponent for this entity could land just after the check,
					// but destroying an entity while another thread adds components to it is
					// already unordered; within one thread program order settles it.
					if (!array->template containsSector<false>(entityId)) {
						continue;
					}
					// In-place destroy of one sector: only that sector must be unpinned, and
					// the wait must not happen under the write lock (it would deadlock any
					// pin holder that still needs the shared lock).
					array->exclusiveWhenUnpinned(entityId, [&] {
						auto idx = array->template findLinearIdx<false>(entityId);
						if (idx != INVALID_IDX) {
							Memory::Sector::destroySectorData<ThreadSafe>(array->mAllocator.at(idx), array->template getIsAliveRef<false>(idx), array->getLayout());
							array->incDefragmentSize();
						}
					});
				}
			}
			else {
				for (auto array : mComponentsArrays) {
					auto idx = array->template findLinearIdx<false>(entityId);
					if (idx != INVALID_IDX) {
						Memory::Sector::destroySectorData<ThreadSafe>(array->mAllocator.at(idx), array->template getIsAliveRef<false>(idx), array->getLayout());
						array->incDefragmentSize();
					}
				}
			}
		}

		/**
		 * @brief Utility: pin multiple components and invoke f(entity, comps...).
		 * @tparam Ts Component types to pin.
		 * @tparam F Functor type.
		 */
		template<class... Ts, class F>
		void withPinned(EntityId entity, F&& f) noexcept requires(ThreadSafe) {
			auto pins = std::make_tuple(pinComponent<Ts>(entity)...);
			std::apply([&](auto&... pc) { std::forward<F>(f)(entity, pc.get()...); }, pins);
		}

		/**
		 * @brief Internal helper: ensure entities vector is sorted & clamped to valid capacity.
		 * @param entities [in/out] Vector of entity ids.
		 * @param sparseCapacity Max valid sector index (exclusive).
		 */
		static void prepareEntities(std::vector<EntityId>& entities, size_t sparseCapacity) {
			if (entities.empty()) { return; }
			std::sort(entities.begin(), entities.end());

			if (entities.front() >= sparseCapacity) {
				entities.clear();
				return;
			}

			if (entities.back() >= sparseCapacity) {
				int distance = static_cast<int>(entities.size());
				for (auto it = entities.rbegin(); it != entities.rend(); ++it) {
					if (*it < sparseCapacity) {
						break;
					}
					distance--;
				}

				entities.erase(entities.begin() + distance, entities.end());
			}
		}

	private:
		/// @brief Immutable snapshot of the registered arrays, published on registration.
		/// `map` is indexed by componentTypeId; `list` is the de-duplicated array list.
		struct Registered {
			size_t mapCount = 0;
			Memory::SectorsArray<ThreadSafe, Allocator>** map = nullptr;
			/// Per component type, the layout record and the layout it was resolved against.
			/// Kept in this snapshot rather than a cache beside it so one acquire load still
			/// answers everything, and so they cannot disagree about which registration they
			/// belong to.
			const Memory::LayoutData** layout = nullptr;
			const Memory::SectorLayoutMeta** meta = nullptr;
			size_t listCount = 0;
			Memory::SectorsArray<ThreadSafe, Allocator>** list = nullptr;
		};

		/// @brief The reader-writer lock guarding one component type's values. @see access()
		///
		/// Allocated one at a time and never moved, so a guard can hold a pointer across the
		/// growth another thread's first claim on a new type causes.
		std::shared_mutex& typeMutex(ECSType type) {
			{
				auto shared = std::shared_lock(mTypeMutexesGrowth);
				if (type < mTypeMutexes.size() && mTypeMutexes[type]) { return *mTypeMutexes[type]; }
			}
			auto unique = std::unique_lock(mTypeMutexesGrowth);
			if (type >= mTypeMutexes.size()) { mTypeMutexes.resize(static_cast<size_t>(type) + 1); }
			if (!mTypeMutexes[type]) { mTypeMutexes[type] = std::make_unique<std::shared_mutex>(); }
			return *mTypeMutexes[type];
		}

		/// @brief Note where each component's layout record lives, while the pack is still a
		/// compile-time list. Caller holds componentsArrayMapMutex (TS build).
		template <class... ComponentTypes>
		void recordLayouts(Memory::SectorsArray<ThreadSafe, Allocator>* array) {
			ECSType maxId = 0;
			((maxId = std::max(maxId, componentTypeId<ComponentTypes>())), ...);
			if (maxId >= mComponentsLayoutsMap.size()) {
				mComponentsLayoutsMap.resize(maxId + 1, nullptr);
				mComponentsMetaMap.resize(maxId + 1, nullptr);
			}
			((mComponentsLayoutsMap[componentTypeId<ComponentTypes>()] =
				&array->template getLayoutData<ComponentTypes>()), ...);
			((mComponentsMetaMap[componentTypeId<ComponentTypes>()] = array->getLayout()), ...);
		}

		/// @brief Give the arrays a view is about to open the pass update() would have given
		/// them. Nothing here waits: an array someone is iterating is left alone.
		template<typename... Components>
		FORCE_INLINE void maintainFor() noexcept {
			if (!loadAutoMaintenance()) [[likely]] { return; }
			(maintainArrayFor<Components>(), ...);
			maintainOneInRotation();
		}

		template<typename T>
		void maintainArrayFor() noexcept {
			if (auto* array = getComponentContainer<T>()) { maintainArray(array); }
		}

		/// @brief Also give one array nobody is looking at its turn.
		///
		/// Without this, auto maintenance would only ever reach arrays that get iterated: a
		/// component type that is only ever looked up by id would keep its dead sectors and its
		/// retired buffers forever. One extra array per view creation is enough -- an idle one
		/// costs two lock-free loads to skip -- and it is what lets update() be dropped rather
		/// than merely moved.
		void maintainOneInRotation() noexcept {
			// Not on every view. The rotation exists so an array nobody iterates is still
			// reached eventually, and once every kRotationStride views is enough for that.
			// Charging every view was measurable at +36 ns, a quarter of what opening a view
			// costs; the curve flattens by a stride of four (+23 ns) and gains nothing after,
			// so four it is -- four times the coverage of sixteen for the same price.
			// Racy on purpose: the ticket only decides whose turn it is.
			const auto ticket = mMaintainCursor.fetch_add(1, std::memory_order_relaxed);
			if ((ticket % kRotationStride) != 0) [[likely]] { return; }

			const auto [begin, end] = registeredArrays();
			const auto count = static_cast<size_t>(end - begin);
			if (count == 0) { return; }
			maintainArray(begin[(ticket / kRotationStride) % count]);
		}

		static constexpr size_t kRotationStride = 4;

		void maintainArray(Memory::SectorsArray<ThreadSafe, Allocator>* array) noexcept {
			if constexpr (ThreadSafe) {
				// Deliberately not ticking the snapshot bin here. This runs per array per view,
				// and the snapshot grace period measures how long a reader might still be
				// walking a list it loaded, not how many views have opened since. Draining it
				// at that rate would collapse the window. update() ticks it instead, and a
				// program that never calls update() only holds snapshots it stopped publishing
				// once registration settled.
				array->tick();
				array->processPendingErases(true);
			}
			else {
				// No holds exist here to make compaction unsafe, but a view that is already
				// open would still be reading the array this is about to move sectors in, and
				// the plain build has no way to know. Callers who nest views this way should
				// leave the switch off; the same rule as calling update() mid-iteration.
				if (array->needDefragment()) { array->defragment(); }
			}
		}

		/// @brief Publish a fresh snapshot. Caller holds componentsArrayMapMutex (TS build).
		///
		/// The node and its four arrays are one allocation, so retiring a superseded snapshot
		/// is a single free and the arrays a reader walks sit next to the node it loaded.
		///
		/// Superseded nodes go to a retire bin rather than being kept until the registry dies.
		/// A lock-free reader may still be inside one, which is exactly what the grace period
		/// is for -- the same mechanism the arrays already use for their own buffers. Keeping
		/// every node instead made a registry's snapshot memory quadratic in the number of
		/// component types registered: one node per registerArray(), each sized by the type
		/// count, which reached 4.4 MB at 400 types.
		void publishRegistered() {
			using ArrayPtr = Memory::SectorsArray<ThreadSafe, Allocator>*;

			const size_t mapCount = mComponentsArraysMap.size();
			const size_t listCount = mComponentsArrays.size();

			// Every member is pointer-sized, and sizeof(Registered) is a multiple of that, so
			// laying the arrays out back to back keeps each of them aligned.
			static_assert(sizeof(Registered) % alignof(void*) == 0, "array suffix would be misaligned");
			const size_t bytes = sizeof(Registered)
				+ mapCount * sizeof(ArrayPtr)
				+ mapCount * sizeof(const Memory::LayoutData*)
				+ mapCount * sizeof(const Memory::SectorLayoutMeta*)
				+ listCount * sizeof(ArrayPtr);

			auto* raw = static_cast<std::byte*>(std::malloc(bytes));
			if (!raw) { return; }                     // out of memory: keep serving the old one
			auto* node = new (raw) Registered{};

			std::byte* cursor = raw + sizeof(Registered);
			const auto carve = [&cursor](size_t count, size_t size) {
				std::byte* p = cursor;
				cursor += count * size;
				return p;
			};
			node->mapCount = mapCount;
			node->map = reinterpret_cast<ArrayPtr*>(carve(mapCount, sizeof(ArrayPtr)));
			node->layout = reinterpret_cast<const Memory::LayoutData**>(carve(mapCount, sizeof(const Memory::LayoutData*)));
			node->meta = reinterpret_cast<const Memory::SectorLayoutMeta**>(carve(mapCount, sizeof(const Memory::SectorLayoutMeta*)));
			node->listCount = listCount;
			node->list = reinterpret_cast<ArrayPtr*>(carve(listCount, sizeof(ArrayPtr)));

			for (size_t i = 0; i < mapCount; ++i) {
				node->map[i] = mComponentsArraysMap[i];
				node->layout[i] = i < mComponentsLayoutsMap.size() ? mComponentsLayoutsMap[i] : nullptr;
				node->meta[i] = i < mComponentsMetaMap.size() ? mComponentsMetaMap[i] : nullptr;
			}
			for (size_t i = 0; i < listCount; ++i) { node->list[i] = mComponentsArrays[i]; }

			auto* superseded = mRegistered.exchange(node, std::memory_order_release);
			if (superseded) { mNodeBin.retire(superseded); }
		}

		/// @brief Lock-free view over the registered arrays, for maintenance walks.
		/// Returns {begin, end}; empty when nothing has been registered yet.
		std::pair<Memory::SectorsArray<ThreadSafe, Allocator>* const*, Memory::SectorsArray<ThreadSafe, Allocator>* const*>
		registeredArrays() const noexcept {
			if (const auto* node = mRegistered.load(std::memory_order_acquire)) {
				return { node->list, node->list + node->listCount };
			}
			return { nullptr, nullptr };
		}

	private:
		static_assert(types::isLockFreeAtomic<Registered*>, "the registered-arrays snapshot must be lock-free");
		/// Per component type, the LayoutData record and the layout it came from. Written only
		/// while registering, then copied into each published snapshot.
		std::vector<const Memory::LayoutData*>       mComponentsLayoutsMap;
		std::vector<const Memory::SectorLayoutMeta*> mComponentsMetaMap;

		/// One reader-writer lock per component type, for access(). Held in unique_ptrs because
		/// a guard keeps a pointer while another thread may be growing the vector.
		std::vector<std::unique_ptr<std::shared_mutex>> mTypeMutexes;
		mutable std::shared_mutex mTypeMutexesGrowth;

		/// @brief Read the auto-maintenance flag with whatever ordering this build needs.
		/// @thread_safety Internally synchronized. Relaxed, so it compiles to a plain load.
		FORCE_INLINE bool loadAutoMaintenance() const noexcept {
			if constexpr (ThreadSafe) {
				return std::atomic_ref<bool>(const_cast<bool&>(mAutoMaintenance))
					.load(std::memory_order_relaxed);
			}
			else {
				return mAutoMaintenance;
			}
		}

		/// @brief Write it with the matching ordering. @see loadAutoMaintenance
		/// @thread_safety Internally synchronized. Relaxed, so it compiles to a plain store.
		FORCE_INLINE void storeAutoMaintenance(bool value) noexcept {
			if constexpr (ThreadSafe) {
				std::atomic_ref<bool>(mAutoMaintenance).store(value, std::memory_order_relaxed);
			}
			else {
				mAutoMaintenance = value;
			}
		}

		/// The grace period asked for by setRetireGracePeriod(), so that an array registered
		/// after the call gets it too. Atomic because registration happens on any thread.
		std::atomic<uint32_t> mRetireGracePeriod{ Memory::RetireBin::DEFAULT_GRACE_PERIOD };

		/// Whether opening a view maintains its arrays. Read on every view and written by
		/// setAutoMaintenance(), so the thread-safe build has to order it -- a bare bool written
		/// while views are being opened is a data race. Plain storage with atomic_ref on top
		/// rather than an atomic member, so Registry<false> carries no atomic at all; the same
		/// shape Sector::setAlive uses for the liveness word.
		bool mAutoMaintenance = false;

		/// Which array gets the spare maintenance slot next. @see maintainOneInRotation
		mutable std::atomic<size_t> mMaintainCursor{ 0 };

		std::atomic<Registered*> mRegistered{ nullptr }; ///< Published snapshot (lock-free reads).
		/// Superseded snapshots, freed once no reader can still be inside one. @see tick()
		mutable Memory::RetireBin mNodeBin;


		/// @brief Live entity ids, one bit each.
		///
		/// This used to be a Ranges<EntityId> interval list. Entities die in arbitrary order,
		/// and every erase inside a run split it -- a vector insert over an interval array
		/// that grew towards N/2 entries, so destroying half of N entities was O(N^2)
		/// (measured 166 ms at N=200k, 1084 ms at N=400k, against 0.14 / 0.32 ms here).
		/// Ranges is still the right shape for the view range filters; it was the wrong one
		/// for a set that fragments by design.
		IdSet<EntityId, ThreadSafe> mEntities;

		/// @brief Mapping: component type id -> sectors array (may group several component types).
		std::vector<Memory::SectorsArray<ThreadSafe, Allocator>*> mComponentsArraysMap;

		/// @brief Flat list of all unique sectors arrays for iteration/maintenance.
		std::vector<Memory::SectorsArray<ThreadSafe, Allocator>*> mComponentsArrays;

		struct Dummy{};
		mutable std::conditional_t<ThreadSafe, std::shared_mutex, Dummy> mEntitiesMutex;          ///< Protects entities container (ThreadSafe build).
		mutable std::conditional_t<ThreadSafe, std::shared_mutex, Dummy> componentsArrayMapMutex; ///< Protects component arrays map/list (ThreadSafe build).
	};

	/**
	 * @brief Metadata for accessing a component type inside a sectors array.
	 *
	 * Used internally by ArraysView iterator to map component offsets/masks.
	 */
	struct TypeAccessInfo {
		static constexpr uint8_t kMainIteratorIdx = 255; ///< Sentinel for "main" component (no secondary iterator).

		uint32_t typeAliveMask		= 0; ///< Bit mask for liveness.
		uint16_t typeOffsetInSector = 0; ///< Byte offset within sector memory.
		uint8_t  iteratorIdx		= kMainIteratorIdx; ///< Which secondary iterator provides this type (or 255 if main).
	};

	/**
	 * @brief Iterable view over entities with one main component and optional additional components.
	 *
	 * @tparam ThreadSafe      Mirrors Registry thread-safe flag (affects pinning).
	 * @tparam Allocator       Allocator used by sectors.
	 * @tparam Ranged          Whether this view limits iteration to provided ranges.
	 * @tparam T               Main component type (drives iteration order).
	 * @tparam CompTypes       Additional component types optionally retrieved per entity.
	 *
	 * Semantics:
	 *   - Iterates only sectors where main component T is alive.
	 *   - For each entity id, returns pointers (T*, optional others may be nullptr if absent).
	 *   - In ranged mode, skips entities outside the filter by jumping to the next range start.
	 *
	 * Thread safety:
	 *   - ThreadSafe=true: Back sector pinning ensures iteration upper bound stability.
	 *   - Non-main components may be null if not present or not grouped in same array.
	 *
	 * @warning Do not cache raw pointers across mutating frames unless externally synchronized.
	 */
	namespace detail {
		/**
		 * @brief Walk the alive slots of one array, handing each slot's base address to a
		 *        callback.
		 *
		 * Outside ArraysView on purpose. The body says nothing about component types -- they
		 * reach it as runtime offsets inside ctx -- yet as a static member it was instantiated
		 * once per view, so a translation unit opening 48 different views compiled 48 copies
		 * of the same loop. Parameterised on ThreadSafe alone, there are at most two.
		 */
		/// @brief Walk the live slots of a chunked array, calling @p callback with each.
		///
		/// The callback is a template parameter rather than a function pointer, and that is the
		/// whole point: with a pointer the compiler cannot see through the call, so it inlined
		/// nothing, unrolled nothing and vectorized nothing, and every element paid an indirect
		/// call. Measured over a million single-component sectors, the pointer form ran at
		/// 1.86 ms against 0.82 for this one -- same elements visited, same checksum.
		/// @thread_safety Internally synchronized. Reads a snapshot the caller already holds.
		template<bool ThreadSafe, class Callback>
		FORCE_INLINE void forEachAliveSlot(
			void* const* chunks, size_t numChunks, size_t size,
			const uint32_t* isAliveData, uint32_t aliveMask,
			size_t stride, size_t chunkCapacity,
			Callback&& callback)
		{
			size_t idx = 0;
			for (size_t chunkIdx = 0; chunkIdx < numChunks && idx < size; ++chunkIdx) {
				auto* base = static_cast<std::byte*>(chunks[chunkIdx]);
				const size_t chunkEnd = std::min(idx + chunkCapacity, size);
				for (size_t localIdx = 0; idx < chunkEnd; ++idx, ++localIdx) {
					if ((Memory::detail::loadRelaxed<ThreadSafe>(isAliveData, idx) & aliveMask) == aliveMask) {
						callback(base + localIdx * stride);
					}
				}
			}
		}
	}

	/**
	 * @brief Iteration over one or more component arrays, driven by the first type named.
	 *
	 * @thread_safety Thread-confined -- the view object, not the
	 *                arrays. A view belongs to the thread that opened it; any number of threads
	 *                may each hold their own over the same arrays at the same time.
	 *
	 *                Its whole lifetime is a structural hold on every array it names: while it
	 *                lives, no sector in those arrays may be relocated, so the pointers it hands
	 *                out stay good. That is also what makes writers wait. Registry::clear(),
	 *                Registry::defragment() and an insert landing in the middle of one of these
	 *                arrays cannot finish until the view is destroyed -- from another thread
	 *                they block, from this one they deadlock. Keep views short, and close one
	 *                before restructuring what it was reading.
	 *
	 *                Iterating tells you a component is alive; it does not stop another thread
	 *                writing its value. @see Registry::access()
	 */
	template <bool ThreadSafe, typename Allocator, bool Ranged, typename T, typename ...CompTypes>
	class ArraysView final {
		using Sectors = Memory::SectorsArray<ThreadSafe, Allocator>;
		using SectorsIt = Sectors::IteratorAlive;
		using SectorsRangeIt = Sectors::RangedIterator;
		using TypeInfo = TypeAccessInfo;
		using SlotInfo = typename Sectors::SlotInfo;

		constexpr static size_t CTCount = sizeof...(CompTypes);
		constexpr static size_t TypesCount = sizeof...(CompTypes) + 1;
		static_assert(TypesCount <= TypeInfo::kMainIteratorIdx - 1, "Too many component types for int8_t iteratorIdx");

	public:
		/// @brief Sentinel end iterator tag.
		struct EndIterator {};

		/**
		 * @brief Forward iterator over alive sectors of the main component type.
		 *
		 * Dereferencing produces a tuple (EntityId, T*, CompTypes*...).
		 * Non-main pointers may be nullptr if component not present for that entity.
		 *
		 * @note Iterator validity is bounded by the pinned back-sector (thread-safe mode).
		 */
		class Iterator {
		public:
			using SectorArrays = std::array<Sectors*, TypesCount>;
			using TypeAccessTuple = std::tuple<TypeInfo, decltype((void)sizeof(CompTypes), TypeInfo{})...>;

			using iterator_category = std::forward_iterator_tag;
			using value_type = std::tuple<EntityId, T*, CompTypes*...>;
			using difference_type = std::ptrdiff_t;
			using pointer = value_type*;
			using reference = value_type&;

		public:
			Iterator() noexcept = default;

			/**
			 * @brief Construct iterator with main iterator + secondary arrays.
			 * @param arrays Array of sector arrays for all involved component types.
			 * @param iterator Alive iterator for main component.
			 * @param secondary Arrays (+ iterators for ThreadSafe) for component lookup.
			 */
			Iterator(const SectorArrays& arrays, SectorsIt iterator, const std::vector<std::pair<Sectors*, SectorsRangeIt>>& secondary, const Ranges<EntityId>* rangeFilter = nullptr)
				: mIterator(std::move(iterator)), mRangeFilter(rangeFilter) {
				initTypeAccessInfo<T, CompTypes...>(arrays, secondary);
				skipOutOfRange();
			}

			FORCE_INLINE value_type operator*() const noexcept { 
				auto slot = *mIterator;
				return { slot.id, 
				         reinterpret_cast<T*>(slot.data + mMainOffset), 
				         getComponent<CompTypes>(slot)... }; 
			}
			FORCE_INLINE Iterator& operator++() noexcept {
				++mIterator;
				skipOutOfRange();
				return *this;
			}

			FORCE_INLINE bool operator!=(const Iterator& other) const noexcept { return mIterator != other.mIterator; }
			FORCE_INLINE bool operator==(const Iterator& other) const noexcept { return mIterator == other.mIterator; }

			// Alive iterator self-checks end condition by returning nullptr.
			FORCE_INLINE bool operator==(const EndIterator&) const noexcept { return !mIterator; }
			FORCE_INLINE bool operator!=(const EndIterator&) const noexcept { return static_cast<bool>(mIterator); }
			FORCE_INLINE friend bool operator==(const EndIterator endIt, const Iterator& it) noexcept { return it == endIt; }
			FORCE_INLINE friend bool operator!=(const EndIterator endIt, const Iterator& it) noexcept { return it != endIt; }

			/// @brief Invoke func directly without tuple creation. Returns true if all components found.
			template<typename Func>
			FORCE_INLINE bool tryInvoke(Func&& func) const noexcept {
				auto slot = *mIterator;
				T* main = reinterpret_cast<T*>(slot.data + mMainOffset);
				if (!(slot.isAlive & std::get<0>(mTypeAccessInfo).typeAliveMask)) return false;
				
				if constexpr (sizeof...(CompTypes) == 0) {
					func(*main);
					return true;
				} else {
					// Recursive template expansion - no tuple allocation
					return tryInvokeRec(std::forward<Func>(func), slot, main);
				}
			}

		private:
			/// @brief Recursive helper that collects component pointers without tuple
			template<typename Func, typename... Got>
			FORCE_INLINE bool tryInvokeRec(Func&& func, const SlotInfo& slot, T* main, Got*... got) const noexcept {
				if constexpr (sizeof...(Got) == sizeof...(CompTypes)) {
					// All components collected - invoke
					func(*main, (*got)...);
					return true;
				} else {
					// Get next component type
					using Next = std::tuple_element_t<sizeof...(Got), std::tuple<CompTypes...>>;
					Next* next = getComponent<Next>(slot);
					if (!next) return false;
					return tryInvokeRec(std::forward<Func>(func), slot, main, got..., next);
				}
			}

		private:
			/// @brief Fetch component pointer for specific entity id (may be nullptr).
			template<typename ComponentType>
			FORCE_INLINE ComponentType* getComponent(const SlotInfo& slot) const noexcept {
				constexpr auto idx = getIndex<ComponentType>();
				const auto& info = std::get<idx>(mTypeAccessInfo);

				if (info.iteratorIdx == TypeInfo::kMainIteratorIdx) [[likely]] {
					return (slot.isAlive & info.typeAliveMask) ? reinterpret_cast<ComponentType*>(slot.data + info.typeOffsetInSector) : nullptr;
				}
			// Lock-free: one sparse load for the linear index, one alive-word load through the
			// dense seqlock, then the address from the chunk snapshot cached at construction.
			// Resolving through findSlot() instead would re-read the chunk seqlock per element.
			auto* arr = mSecondaryArrays[info.iteratorIdx];
			const auto linearIdx = arr->template findLinearIdx<false>(slot.id);
			if (linearIdx == INVALID_IDX) [[unlikely]] {
				return nullptr;
			}
			// The alive word goes through the seqlock snapshot (loadView) so we don't race
			// with a concurrent push_back reallocating the isAlive vector; RetireAllocator
			// keeps the old buffer valid, so the snapshot is always safe.
			const auto isAlive = arr->loadAliveWord(linearIdx);
			if (!(isAlive & info.typeAliveMask)) [[unlikely]] {
				return nullptr;
			}
			auto* data = arr->dataAt(mSecondaryChunks[info.iteratorIdx], linearIdx);
			return data ? reinterpret_cast<ComponentType*>(data + info.typeOffsetInSector) : nullptr;
			}

			template<typename... Types>
			void initTypeAccessInfo(const SectorArrays& arrays, const std::vector<std::pair<Sectors*, SectorsRangeIt>>& secondary) noexcept {
				uint8_t arrayIndexes[TypesCount];
				std::fill_n(arrayIndexes, TypesCount, TypeInfo::kMainIteratorIdx);

				for (const auto& entry : secondary) {
					auto* arr = entry.first;
					mSecondaryArrays[mSecondaryCount] = arr;
					mSecondaryChunks[mSecondaryCount] = arr->loadChunks();
					for (size_t a = 0; a < TypesCount; ++a) {
						if (arrays[a] == arr) {
							arrayIndexes[a] = mSecondaryCount;
						}
					}
					++mSecondaryCount;
				}

				(initTypeAccessInfoImpl<Types>(arrays[getIndex<T>()], arrays[getIndex<Types>()], arrayIndexes), ...);
				mMainOffset = std::get<0>(mTypeAccessInfo).typeOffsetInSector;
			}

			template<typename ComponentType>
			FORCE_INLINE void initTypeAccessInfoImpl(Sectors* main, Sectors* sectorArray, uint8_t* iteratorIndexes) noexcept {
				constexpr auto idx = getIndex<ComponentType>();
				auto& info = std::get<idx>(mTypeAccessInfo);
				const auto& layout = sectorArray->template getLayoutData<ComponentType>();
				info.typeAliveMask = layout.isAliveMask;
				info.typeOffsetInSector = layout.offset;
				if (sectorArray != main) {
					info.iteratorIdx = iteratorIndexes[idx];
				}
			}

		private:
			FORCE_INLINE void skipOutOfRange() {
				if (!mRangeFilter) return;
				while (mIterator) {
					auto slot = *mIterator;
					if (mRangeFilter->contains(slot.id)) return;
					EntityId next{};
					if (!mRangeFilter->nextStartAfter(slot.id, next)) {
						mIterator.becomeEnd();
						return;
					}
					mIterator.advanceToId(next);
				}
			}

			TypeAccessTuple mTypeAccessInfo;
			Sectors*		mSecondaryArrays[CTCount ? CTCount : 1] = {};
			/// Chunk snapshot per secondary array, taken once so the per-element lookup does
			/// not re-read the seqlock.
			typename Allocator::ChunksView mSecondaryChunks[CTCount ? CTCount : 1] = {};
			SectorsIt		mIterator;
			const Ranges<EntityId>* mRangeFilter = nullptr;
			uint16_t		mMainOffset = 0;
			uint8_t			mSecondaryCount = 0;
		};

		/// @return Iterator to first alive element (or end if empty).
		FORCE_INLINE Iterator begin() const noexcept { return mBeginIt; }

		/// @return Sentinel end marker.
		FORCE_INLINE EndIterator end() const noexcept { return {}; }

	public:
		/// @brief Construct a full-range view (Ranged=false specialization).
		explicit ArraysView(Registry<ThreadSafe, Allocator>* manager) noexcept requires (!Ranged) { init(manager); }
		explicit ArraysView(Registry<ThreadSafe, Allocator>* manager, const Ranges<EntityId>& ranges = {}) noexcept requires (Ranged) { init(manager, ranges); }

		FORCE_INLINE bool empty() const noexcept { return mBeginIt == end(); }

		/// @brief Fast iteration without tuple overhead.
		/// Single component: func(T&), Multi component grouped: func(T&, CompTypes&...)
		template<typename Func>
		FORCE_INLINE void each(Func&& func) const {
			if constexpr (sizeof...(CompTypes) == 0 && !Ranged) {
				// Single component fast path - direct chunk iteration
				eachSingle(std::forward<Func>(func));
			} else if constexpr (!Ranged) {
				// Try grouped multi-component fast path
				eachGrouped(std::forward<Func>(func));
			} else {
				for (auto it = mBeginIt; it != end(); ++it) {
					auto val = *it;
					auto* main = std::get<1>(val);
					if (main) {
						std::apply([&](auto, auto* m, auto*... rest) {
							if constexpr (sizeof...(rest) == 0) {
								func(*m);
							} else if ((rest && ...)) {
								func(*m, (*rest)...);
							}
						}, val);
					}
				}
			}
		}

	private:
		template<typename Func>
		FORCE_INLINE void eachSingle(Func&& func) const requires (sizeof...(CompTypes) == 0 && !Ranged) {
			if (!mMainArray || mSize == 0) return;
			const auto& layout = mMainArray->template getLayoutData<T>();

			const auto offset = layout.offset;
			auto view = mMainArray->mDenseArrays.loadView();
			detail::forEachAliveSlot<ThreadSafe>(
				mChunksSnapshot, mChunksCount, mSize,
				view.isAlive, layout.isAliveMask,
				mMainArray->mAllocator.mSectorSize,
				std::remove_reference_t<decltype(mMainArray->mAllocator)>::mChunkCapacity,
				[&](std::byte* slot) { func(*reinterpret_cast<T*>(slot + offset)); }
			);
		}

		template<typename Func>
		FORCE_INLINE void eachGrouped(Func&& func) const requires (sizeof...(CompTypes) > 0 && !Ranged) {
			if (!mMainArray || mSize == 0) return;
			
			if (!mIsGrouped) {
				for (auto it = mBeginIt; it != end(); ++it) {
					it.tryInvoke(std::forward<Func>(func));
				}
				return;
			}

			const uint16_t mainOff = mMainArray->template getLayoutData<T>().offset;
			const std::array<uint16_t, CTCount> compOffs{
				mMainArray->template getLayoutData<CompTypes>().offset...
			};

			uint32_t combinedMask = 0;
			combinedMask |= mMainArray->template getLayoutData<T>().isAliveMask;
			((combinedMask |= mMainArray->template getLayoutData<CompTypes>().isAliveMask), ...);

			auto view = mMainArray->mDenseArrays.loadView();
			detail::forEachAliveSlot<ThreadSafe>(
				mChunksSnapshot, mChunksCount, mSize,
				view.isAlive, combinedMask,
				mMainArray->mAllocator.mSectorSize,
				std::remove_reference_t<decltype(mMainArray->mAllocator)>::mChunkCapacity,
				[&](std::byte* slot) {
					func(*reinterpret_cast<T*>(slot + mainOff),
					     *reinterpret_cast<CompTypes*>(slot + compOffs[types::typeIndex<CompTypes, CompTypes...>])...);
				}
			);
		}

	private:
		void init(Registry<ThreadSafe, Allocator>* manager, const Ranges<EntityId>& ranges = {}) {
			auto arrays = initArrays<CompTypes..., T>(manager);
			SectorsIt it;

			{
				auto mainArr = arrays[getIndex<T>()];
				mMainArray = mainArr;
				// No lock: the size, the chunk table and the dense view all come from
				// lock-free snapshots, and pinning validates itself against the structural
				// epoch rather than relying on the shared lock.
				mSize = mainArr->template size<false>();
				const auto chunks = mainArr->mAllocator.loadChunks();
				mChunksSnapshot = chunks.chunks;
				mChunksCount = chunks.count;
				auto effectiveRanges = initRange(mainArr, ranges, getIndex<T>());
				
				// Determine iteration bounds
				size_t startIdx = 0;
				size_t endIdx = mSize;
				
				if constexpr (Ranged) {
					if (!effectiveRanges.empty()) {
						// Convert SectorId range bounds to linear indices using binary search
						// Load atomic view snapshot for thread-safe access
						auto view = mainArr->mDenseArrays.loadView();
						const auto* ids = view.ids;
						// Find start: first linear index where mIds[idx] >= range.first
						{
							size_t lo = 0, hi = mSize;
							while (lo < hi) {
								size_t mid = lo + (hi - lo) / 2;
								if (ids[mid] < effectiveRanges.ranges.front().first) lo = mid + 1;
								else hi = mid;
							}
							startIdx = lo;
						}
						// Find end: first linear index where mIds[idx] >= range.last (for last range)
						{
							size_t lo = 0, hi = mSize;
							while (lo < hi) {
								size_t mid = lo + (hi - lo) / 2;
								if (ids[mid] < effectiveRanges.ranges.back().second) lo = mid + 1;
								else hi = mid;
							}
							endIdx = lo;
						}
					}
				}
				
				// Note: isPacked=false because we're filtering by a specific component's alive mask,
				// not just checking if any component is alive. mDefragmentSize==0 only means no dead
				// sectors, not that all sectors have this specific component.
				it = SectorsIt(mainArr, startIdx, endIdx, mainArr->template getLayoutData<T>().isAliveMask, false);
				if constexpr (Ranged) {
					mRanges = effectiveRanges;
				}
			}

		auto secondary = collectSecondaryArrays(arrays, ranges);
		if constexpr (Ranged) {
			mBeginIt = Iterator{ arrays, it, secondary, &mRanges };
		} else {
			mBeginIt = Iterator{ arrays, it, secondary };
		}
			
			mIsGrouped = secondary.empty();
		}
		
		/// @brief Initialize effective iteration ranges (and pin upper bound if thread-safe).
		Ranges<EntityId> initRange(Sectors* sectorsArray, const Ranges<EntityId>& _ranges, size_t i = 0) {
			Ranges<EntityId> ranges = _ranges;

			if constexpr (Ranged) {
				// Convert entity id ranges to linear index ranges
				// For simplicity, keep ranges as-is (they will be filtered during iteration)
				ranges.mergeIntersections();

				if constexpr (ThreadSafe) {
					// A hold, not a pin: what iteration needs is that the array is not
					// compacted, and a hold expresses exactly that without every thread
					// piling onto one sector counter.
					if (!ranges.empty() && sectorsArray->template size<false>() != 0) {
						mHolds[i] = sectorsArray->holdStructure();
					}
				}
			}
			else {
				size_t last;
				if constexpr (ThreadSafe) {
					mHolds[i] = sectorsArray->holdStructure();
					last = sectorsArray->template size<false>();
				}
				else {
					last = sectorsArray->size();
				}
				ranges.ranges.clear();
				ranges.ranges.emplace_back(0u, static_cast<SectorId>(last));
			}

			return ranges;
		}

		/// @brief Collect secondary arrays (with iterators for ThreadSafe mode).
		std::vector<std::pair<Sectors*, SectorsRangeIt>> collectSecondaryArrays(const std::array<Sectors*, TypesCount>& arrays, const Ranges<EntityId>& ranges) {
			std::vector<std::pair<Sectors*, SectorsRangeIt>> secondary;
			secondary.reserve(TypesCount - 1);
			auto main = arrays[0];
			for (auto i = 1u; i < arrays.size(); i++) {
				auto arr = arrays[i];
				if (arr == main || std::find_if(secondary.begin(), secondary.end(), [arr](const auto& p){ return p.first == arr; }) != secondary.end()) { continue; }
				if constexpr (ThreadSafe) {
					// Pin the back sector so the iteration upper bound stays valid. The
					// secondary RangedIterator is never read (component lookups go through
					// findSlot), so we do not build it.
					initRange(arr, ranges, i);
				}
				// Non-ThreadSafe uses direct lookup; iterator is unused either way.
				secondary.emplace_back(arr, SectorsRangeIt{});
			}
			return secondary;
		}

		/// @brief Get compile-time index of a given component type within the typelist.
		template<typename ComponentType>
		FORCE_INLINE static size_t consteval getIndex() noexcept {
			if constexpr (std::is_same_v<T, ComponentType>) { return 0; }
			else { return types::typeIndex<ComponentType, CompTypes...> + 1; }
		}

		/// @brief Resolve and fetch all involved sectors arrays (lazily creates if needed).
		template<typename... Types>
		FORCE_INLINE std::array<Sectors*, TypesCount> initArrays(Registry<ThreadSafe, Allocator>* registry) noexcept {
			std::array<Sectors*, TypesCount> arrays;

			static_assert(types::areUnique<Types...>, "Duplicates detected in types");
			((arrays[getIndex<Types>()] = registry->template getComponentContainer<Types>()), ...);

			return arrays;
		}

	private:
		struct Dummy{};
		/// One structural hold per distinct array, keeping the iteration bounds valid for the
		/// lifetime of the view.
		std::conditional_t<ThreadSafe, std::array<Memory::StructuralHold, TypesCount>, Dummy> mHolds;

#ifndef NDEBUG
		/// A view reads its component types for as long as it lives, which is what lets the
		/// tracker notice another thread writing one of them meanwhile. Debug only, so the
		/// released build's view carries nothing extra. @see Registry::setAccessTracking
		std::array<detail::AccessScope, TypesCount> mReadScopes{
			detail::readScope<T>(Registry<ThreadSafe, Allocator>::template componentTypeId<T>()),
			detail::readScope<CompTypes>(Registry<ThreadSafe, Allocator>::template componentTypeId<CompTypes>())...
		};
#endif
		Iterator mBeginIt;                                   ///< Cached begin iterator.

		Sectors* mMainArray = nullptr;
		void* const* mChunksSnapshot = nullptr;
		size_t mChunksCount = 0;
		size_t mSize = 0;
		bool mIsGrouped = false;
		std::conditional_t<Ranged, Ranges<EntityId>, Dummy> mRanges;
	};
} // namespace ecss
