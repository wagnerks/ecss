#pragma once
/**
 * @file SectorsArray.h
 * @brief Sector-based sparse storage for one (or a grouped set of) ECS component type(s) with optional thread-safety.
 *
 * Core responsibilities:
 *  - Owns a set of variable-sized "sectors" (fixed layout defined by SectorLayoutMeta).
 *  - Provides O(1) random access by SectorId through a direct pointer map.
 *  - Supports insertion / emplacement / overwrite of component members inside sectors.
 *  - Supports conditional & ranged erasure, deferred (asynchronous) erase, and defragmentation.
 *  - Exposes multiple iterator flavours: linear, alive-only, ranged, ranged+alive.
 *  - Coordinates with PinCounters so that erasure / relocation waits for readers (ThreadSafe build).
 *
 * Thread safety model (when ThreadSafe=true):
 *  - Read-only APIs acquire a shared (reader) lock.
 *  - Mutating APIs acquire a unique (writer) lock.
 *  - Structural operations (erase / move / defragment) wait on PinCounters before relocating memory.
 *  - Vector memory reclamation is deferred via RetireBin until no reader references remain.
 *
 * Performance notes:
 *  - Lookup of sector pointer by id: O(1) (array indexing).
 *  - Insert of a new sector id: amortized O(N) worst-case (due to ordered insertion shift); O(1) if appended.
 *  - Defragmentation: compacts only alive runs; complexity proportional to total sectors.
 *
 * @see ecss::Registry (higher-level orchestration)
 * @see Sector / SectorLayoutMeta (layout & storage details)
 */

#include <algorithm>
#include <cassert>
#include <shared_mutex>
#include <utility>
#include <vector>

#include <ecss/memory/Sector.h>
#include <ecss/Ranges.h>
#include <ecss/threads/PinCounters.h>
#include <ecss/memory/ChunksAllocator.h>

namespace ecss
{
	template <bool ThreadSafe, typename Allocator>
	class Registry;

	template <bool ThreadSafe, typename Allocator, bool Ranged, typename T, typename ...ComponentTypes>
	class ArraysView;
}

namespace ecss::Memory {

namespace detail {
	template<bool TS>
	struct SectorsMap;

	template<>
	struct SectorsMap<true> {
		FORCE_INLINE Sector* findSector(SectorId id) const {
			auto map = sectorsMapView.load(std::memory_order_acquire);
			return id < map.size ? map.vectorData[id] : nullptr;
		}

		FORCE_INLINE Sector* getSector(SectorId id)	const {
			assert(id < sectorsMapView.load().size);
			return sectorsMapView.load(std::memory_order_acquire).vectorData[id];
		}

		FORCE_INLINE size_t capacity() const { return sectorsMapView.load(std::memory_order_acquire).size; }
		FORCE_INLINE void storeVector() {
			sectorsMapView.store(SectorsView { sectorsMap.data(), sectorsMap.size() }, std::memory_order_release);
			bin.drainAll();
		}

	private:
		mutable Memory::RetireBin bin; ///< Retirement bin for old buffers (ThreadSafe).

	public:
		// Allocator for sector map (retirement-aware in ThreadSafe builds)
		std::vector<Sector*, Memory::RetireAllocator<Sector*>> sectorsMap{ Memory::RetireAllocator<Sector*>{&bin} }; ///< Id -> Sector* map (sparse).

	private:
		/// @brief Atomic snapshot view for lock-free read of sector pointers (no iteration guarantees).
		struct SectorsView {
			Sector* const* vectorData;
			size_t  size;
		};

		mutable std::atomic<SectorsView> sectorsMapView{ SectorsView{nullptr, 0} };
	};

	template<>
	struct SectorsMap<false> {
		FORCE_INLINE Sector* findSector(SectorId id) const { return id < sectorsMap.size() ? sectorsMap[id] : nullptr; }
		FORCE_INLINE Sector* getSector(SectorId id)	const {	assert(id < sectorsMap.size());	return sectorsMap[id]; }
		FORCE_INLINE size_t capacity() const { return sectorsMap.size(); }
		FORCE_INLINE void storeVector() {} //dummy

		std::vector<Sector*> sectorsMap;
	};
} // namespace ecss::Memory::detail

/**
 * @name Internal lock helper macros
 * @warning Intended only inside this header; not for external use.
 * @{ */
#define SHARED_LOCK() auto lock = readLock()
#define UNIQUE_LOCK() auto lock = writeLock()
/** @} */

/**
 * @brief Execute an expression with (optional) locking depending on template TS_FLAG.
 * @param TS_FLAG Compile-time boolean (usually template ThreadSafe).
 * @param LOCK_MACRO Either SHARED or UNIQUE (without trailing _LOCK).
 * @param EXPR Expression executed under the lock (or directly if TS_FLAG==false).
 */
#define TS_GUARD(TS_FLAG, LOCK_MACRO, EXPR) \
	do {enforceTSMode<TS>(); if constexpr (TS_FLAG) { LOCK_MACRO##_LOCK(); EXPR; } else { EXPR; }} while(0)

/**
 * @brief Same as TS_GUARD but allows an additional pre-expression executed under the lock.
 * @param TS_FLAG Compile-time boolean.
 * @param LOCK_MACRO SHARED or UNIQUE.
 * @param ADDITIONAL_SINK Additional code executed (under lock) before EXPR.
 * @param EXPR Main expression executed under the same lock.
 */
#define TS_GUARD_S(TS_FLAG, LOCK_MACRO, ADDITIONAL_SINK, EXPR) \
	do {enforceTSMode<TS>(); if constexpr (TS_FLAG) { LOCK_MACRO##_LOCK(); ADDITIONAL_SINK; EXPR; } else { EXPR; }} while(0)

/**
 * @brief RAII pin for a sector to prevent relocation / destruction while in use.
 *
 * Life-cycle:
 *  - On construction, increments pin counter for the sector id (if valid).
 *  - On move, transfers pin ownership.
 *  - On destruction or release(), decrements the pin counter.
 *
 * @warning Do NOT keep a PinnedSector beyond the lifetime of its originating SectorsArray.
 * @note A default-constructed (or moved-from) instance is considered "empty".
 */
	struct PinnedSector {
		PinnedSector() = default;

		/**
		 * @brief Pin a sector.
		 * @param o Pin counters container (PinCounters instance).
		 * @param s Raw sector pointer (may be nullptr => empty pin).
		 * @param sid Sector id (must not be INVALID_ID if s != nullptr).
		 */
		PinnedSector(const Threads::PinCounters& o, Sector* s, SectorId sid) : sec(s), owner(&o), id(sid) {
			assert(id != INVALID_ID);
			const_cast<Threads::PinCounters*>(owner)->pin(id);
		}

		PinnedSector(const PinnedSector&) = delete;
		PinnedSector& operator=(const PinnedSector&) = delete;

		PinnedSector(PinnedSector&& other) noexcept { *this = std::move(other); }
		PinnedSector& operator=(PinnedSector&& other) noexcept {
			if (this == &other) { return *this; }
			release();
			owner = other.owner;
			sec = other.sec;
			id = other.id;
			other.owner = nullptr;
			other.sec = nullptr;
			other.id = static_cast<SectorId>(INVALID_ID);
			return *this;
		}

		~PinnedSector() { release(); }

		/**
		 * @brief Manually release pin (safe to call multiple times).
		 */
		void release() {
			if (owner) {
				const_cast<Threads::PinCounters*>(owner)->unpin(id);
			}
			owner = nullptr;
			sec = nullptr;
			id = static_cast<SectorId>(INVALID_ID);
		}

		/// @return Raw sector pointer or nullptr.
		Sector* get() const { return sec; }
		/// @return Raw sector pointer for member access.
		Sector* operator->() const { return sec; }
		/// @return True if non-empty pin.
		explicit operator bool() const { return sec != nullptr; }
		/// @return Pinned sector id (INVALID_ID when empty).
		SectorId getId() const { return id; }
	private:
		Sector* sec = nullptr;                          ///< Pinned sector pointer (or nullptr).
		const Threads::PinCounters* owner = nullptr;    ///< Pin counter owner (or nullptr if released).
		SectorId id = INVALID_ID;						///< Sector id (valid only if pinned).
	};

	/**
	 * @brief Container managing sectors of one (or grouped) component layouts with optional thread-safety.
	 *
	 * @tparam ThreadSafe If true, public calls are internally synchronized & relocation waits on pins.
	 * @tparam Allocator  Allocation policy (e.g. ChunksAllocator).
	 *
	 * Main features:
	 *  - Sector acquisition by id (insertion keeps sectors ordered by id).
	 *  - Per-member (component) construction, move, copy, overwrite.
	 *  - Iterators for full scan, alive-only, ranged, and ranged-alive traversal.
	 *  - Deferred erasure via eraseAsync() drained by processPendingErases().
	 *  - Defragmentation collapses dead (erased) holes to the left while preserving order stability.
	 *
	 * Thread-safe mode guarantees:
	 *  - No sector memory relocation while pinned (pin counters + waiting writers).
	 *  - Safe concurrent readers & single writer semantics on container-level operations.
	 *
	 * @note Most public template methods accept an optional TS template parameter (defaulting
	 *       to the class ThreadSafe flag) so internal code can bypass extra locking when it already holds locks.
	 */
	template<bool ThreadSafe = true, typename Allocator = ChunksAllocator<8192>>
	class SectorsArray final {
		template<bool, typename>
		friend class SectorsArray;

		template<bool, typename>
		friend class ecss::Registry;

		template <bool TS, typename Alloc, bool Ranged, typename T, typename ...ComponentTypes>
		friend class ecss::ArraysView;

	public:
		/**
		 * @name Iterator macro injection
		 * @brief Common typedefs & operators for all iterator variants.
		 * @{
		 */
#define ITERATOR_COMMON_USING(IteratorName)															\
		using iterator_concept  = std::forward_iterator_tag;										\
		using iterator_category = std::forward_iterator_tag;										\
		using value_type = Sector*;																	\
		using difference_type = std::ptrdiff_t;														\
		using pointer = Sector*;																	\
		using reference = Sector*;																	\
		IteratorName() = default;																	\
		FORCE_INLINE IteratorName operator++(int) { auto tmp = *this; ++(*this); return tmp; }	\
		FORCE_INLINE bool operator!=(const IteratorName& other) const { return !(*this == other); }	\
		FORCE_INLINE bool operator==(const IteratorName& other) const { return cursor == other.cursor; }	\
		FORCE_INLINE value_type operator*() const { return *cursor; }								\
		FORCE_INLINE value_type operator->() const { return *cursor; }								\
		FORCE_INLINE size_t linearIndex() const { return cursor.linearIndex(); }					\
		FORCE_INLINE std::byte* rawPtr()  const { return cursor.rawPtr(); }
		/** @} */

		/**
		 * @brief Forward iterator over every sector (alive or dead).
		 * @note Returned Sector* may contain dead members. Use IteratorAlive to skip dead ones.
		 */
		class Iterator {
		public:
			ITERATOR_COMMON_USING(Iterator)

			/**
			 * @brief Construct from owning container and linear index.
			 * @param array Owning SectorsArray.
			 * @param idx Starting linear index (clamped to size).
			 */
			Iterator(const SectorsArray* array, size_t idx) : cursor(array->mAllocator.getCursor(std::min(idx, array->sizeImpl()))) { }

			FORCE_INLINE Iterator& operator++() noexcept { return ++cursor, *this; }
			FORCE_INLINE Iterator& operator+=(difference_type n) noexcept { cursor = cursor + n; return *this; }
			FORCE_INLINE Iterator operator+(difference_type n) const noexcept { Iterator t(*this); t += n; return t; }
			friend Iterator operator+(difference_type n, Iterator it) noexcept { it += n; return it; }
			FORCE_INLINE reference operator[](difference_type n) const noexcept { return *(*this + n); }
		private:
			typename Allocator::Cursor cursor;
		};

		/// @name Basic full-range iteration
		/// @return Begin/end iterators (may include dead sectors).
		/// @{
		template<bool TS = ThreadSafe> Iterator begin() const { TS_GUARD(TS, SHARED, return Iterator(this, 0);); }
		template<bool TS = ThreadSafe> Iterator end()   const { TS_GUARD(TS, SHARED, return Iterator(this, sizeImpl());); }
		/// @}

		/**
		 * @brief Forward iterator skipping sectors where a specific component (type mask) isn't alive.
		 * @details Filtering uses a bitmask (aliveMask) and Sector::isAliveData.
		 */
		class IteratorAlive {
		public:
			ITERATOR_COMMON_USING(IteratorAlive)

			/**
			 * @brief Constructor over a linear range [idx, sz).
			 * @param array Owning container.
			 * @param idx Start linear index.
			 * @param sz End linear size.
			 * @param aliveMask Bitmask to test sector liveness for the requested type.
			 */
			IteratorAlive(const SectorsArray* array, size_t idx, size_t sz, uint32_t aliveMask)
			: IteratorAlive(array, Ranges<SectorId>{Ranges<SectorId>::Range{ static_cast<SectorId>(idx), static_cast<SectorId>(sz) } }, aliveMask) {}

			/**
			 * @brief Ranged constructor (uses pre-built ranges object).
			 * @param array Owning container.
			 * @param range Ranges describing sector indices.
			 * @param aliveMask Liveness bitmask.
			 */
			IteratorAlive(const SectorsArray* array, const Ranges<SectorId>& range, uint32_t aliveMask) : cursor(array->mAllocator.getRangesCursor(range, array->sizeImpl())), mTypeAliveMask(aliveMask) {
				while (cursor && !(cursor->isAliveData & mTypeAliveMask)) { cursor.step(); }
			}

			/// @brief Advance to next alive sector matching mask.
			FORCE_INLINE IteratorAlive& operator++() { do { cursor.step(); } while (cursor && !(cursor->isAliveData & mTypeAliveMask)); return *this; }

		private:
			typename Allocator::RangesCursor cursor;
			uint32_t mTypeAliveMask = 0;
		};

		/// @name Alive-only iteration helpers
		/// @param T Component type used to fetch alive mask.
		/// @{
		template<class T, bool TS = ThreadSafe>
		IteratorAlive beginAlive()								const { TS_GUARD(TS, SHARED, return IteratorAlive(this, 0, sizeImpl(), getLayoutData<T>().isAliveMask);); }
		template<class T, bool TS = ThreadSafe>
		IteratorAlive beginAlive(const Ranges<SectorId>& ranges)  const { TS_GUARD(TS, SHARED, return IteratorAlive( this, ranges, getLayoutData<T>().isAliveMask );); }
		template<bool TS = ThreadSafe> IteratorAlive endAlive() const { TS_GUARD(TS, SHARED, return IteratorAlive();); }
		/// @}

		/**
		 * @brief Iterator over specified index ranges (includes dead sectors).
		 * @see RangedIteratorAlive for filtered version.
		 */
		class RangedIterator {
		public:
			ITERATOR_COMMON_USING(RangedIterator)

			/**
			 * @brief Construct with range definition.
			 * @param a Owning container.
			 * @param r Ranges object (half-open segments).
			 */
			RangedIterator(const SectorsArray* a, const Ranges<SectorId>& r) : cursor(a->mAllocator.getRangesCursor(r, a->sizeImpl())) {}

			/// @brief Fast-forward internal cursor near sector id (used by multi-array stitched iteration).
			FORCE_INLINE void advanceToId(SectorId id) {	cursor.advanceToId(id); }

			FORCE_INLINE RangedIterator& operator++() noexcept { cursor.step(); return *this; }

		private:
			typename Allocator::RangesCursor cursor;
		};

		/// @name Ranged iteration helpers
		/// @{
		template<bool TS = ThreadSafe> RangedIterator beginRanged(const Ranges<SectorId>& ranges) const { TS_GUARD(TS, SHARED, return RangedIterator(this, ranges);); }
		template<bool TS = ThreadSafe> RangedIterator endRanged()   const { TS_GUARD(TS, SHARED, return RangedIterator();); }
		/// @}

	public:
		// ---------- Copy / Move Semantics -------------------------------------------------

		/**
		 * @brief Cross-template copy constructor (allows copying between differently configured SectorsArray types if layout compatible).
		 * @tparam T Other ThreadSafe flag.
		 * @tparam Alloc Other allocator type.
		 */
		template<bool T, typename Alloc>
		SectorsArray(const SectorsArray<T, Alloc>& other) { *this = other; }
		/// @brief Same-type copy constructor.
		SectorsArray(const SectorsArray& other)			  { *this = other; }

		template<bool T, typename Alloc>
		SectorsArray& operator=(const SectorsArray<T, Alloc>& other) { if (!isSameAdr(this, &other)) { copy(other); }  return *this; }
		SectorsArray& operator=(const SectorsArray& other) { if (this != &other) { copy(other); }  return *this; }

		/// @brief Cross-template move constructor.
		template<bool T, typename Alloc>
		SectorsArray(SectorsArray<T, Alloc>&& other) noexcept { *this = std::move(other); }
		/// @brief Same-type move constructor.
		SectorsArray(SectorsArray&& other)			 noexcept { *this = std::move(other); }

		template<bool T, typename Alloc>
		SectorsArray& operator=(SectorsArray<T, Alloc>&& other) noexcept { if (!isSameAdr(this, &other)) { move(std::move(other)); }  return *this; }
		SectorsArray& operator=(SectorsArray&& other)			noexcept { if (this != &other) { move(std::move(other)); } return *this; }

	private:
		/**
		 * @brief Internal ctor with provided layout metadata + allocator.
		 * @param meta Precomputed layout meta (SectorLayoutMeta owns offsets & liveness masks).
		 * @param allocator Allocator instance.
		 */
		SectorsArray(SectorLayoutMeta* meta, Allocator&& allocator = {}) : mAllocator(std::move(allocator)) { mAllocator.init(meta);
			mSectorsMap.storeVector();
		}

	public:
		/// @brief Destructor clears all sectors and releases underlying memory.
		~SectorsArray() { clear(); shrinkToFit(); }

		/**
		 * @brief Factory to allocate a SectorsArray for the unique component type set @p Types.
		 * @tparam Types Component types grouped in each sector (must be unique).
		 * @param allocator Allocator instance to move in.
		 * @return Newly allocated heap object (caller owns via delete).
		 */
		template <typename... Types>
		static SectorsArray* create(Allocator&& allocator = {}) { static_assert(types::areUnique<Types...>, "Duplicates detected in SectorsArray types!");
			static SectorLayoutMeta* meta = SectorLayoutMeta::create<Types...>();
			return new SectorsArray(meta, std::move(allocator));
		}

	public: // ---- Layout helpers ---------------------------------------------------------
		/// @brief Access per-type layout metadata (offset, alive mask, etc.).
		template<typename T>
		FORCE_INLINE const LayoutData& getLayoutData() const { return getLayout()->template getLayoutData<T>(); }

		/// @return The sector layout metadata used by this array.
		FORCE_INLINE SectorLayoutMeta* getLayout()	  const { return mAllocator.getSectorLayout(); }

	public: // ---- Pin API (ThreadSafe builds) --------------------------------------------

		/**
		 * @brief Pin a specific sector pointer (if not null).
		 * @tparam TS If false, skip acquiring a shared lock (caller must ensure safety).
		 * @param sector Sector pointer (maybe null).
		 * @return PinnedSector (empty if sector==nullptr).
		 */
		template<bool TS = true>
		[[nodiscard]] PinnedSector pinSector(Sector* sector) const requires(ThreadSafe) { TS_GUARD(TS, SHARED, return pinSectorImpl(sector)); }

		/**
		 * @brief Pin sector by id if found.
		 * @tparam TS If false, expect external synchronization.
		 * @param id Sector id.
		 * @return PinnedSector or empty if not present.
		 */
		template<bool TS = true>
		[[nodiscard]] PinnedSector pinSector(SectorId id)	 const requires(ThreadSafe) { TS_GUARD(TS, SHARED, return pinSectorImpl(findSectorImpl(id));); }

		/**
		 * @brief Pin sector at a given linear index.
		 * @tparam TS If false, skip locking.
		 * @param idx Linear index (0..size()).
		 * @return PinnedSector (empty if out-of-range).
		 */
		template<bool TS = true>
		[[nodiscard]] PinnedSector pinSectorAt(size_t idx)	 const requires(ThreadSafe) { TS_GUARD(TS, SHARED, return pinSectorImpl(mAllocator.at(idx));); }

		/**
		 * @brief Pin last sector (by linear order).
		 * @tparam TS If false, skip locking.
		 * @return Last sector pinned or empty if array empty.
		 */
		template<bool TS = true>
		[[nodiscard]] PinnedSector pinBackSector()			 const requires(ThreadSafe) {
			TS_GUARD(TS, SHARED, auto contSize = sizeImpl(); return contSize == 0 ? PinnedSector{} : pinSectorImpl(mAllocator.at(contSize - 1)););
		}

	public: // ---- Erase & maintenance ----------------------------------------------------
		/**
		 * @brief Erase sectors by linear index range (optionally defragment immediately).
		 * @param beginIdx First index.
		 * @param count Number of sectors to erase (>=1).
		 * @param defragment If true, compacts immediately (costly).
		 * @tparam TS Allows skipping internal locking when false.
		 */
		template<bool TS = ThreadSafe>
		void erase(size_t beginIdx, size_t count = 1, bool defragment = false) {
			if constexpr(TS && ThreadSafe) {
				UNIQUE_LOCK();
				auto begin = Iterator(this, beginIdx);
				if (!*begin) { return; }
				mPinsCounter.waitUntilChangeable(begin->id);
				if (count == 1) {
					erase<false>(begin, defragment);
				}
				else if (count > 1) {
					erase<false>(begin, Iterator(this, beginIdx + count), defragment);
				}
			}
			else {
				if (count == 1) {
					erase(Iterator(this, beginIdx), defragment);
				}
				else if (count > 1) {
					erase(Iterator(this, beginIdx), Iterator(this, beginIdx + count), defragment);
				}
			}
		}

		/**
		 * @brief Erase a single sector at iterator position.
		 * @param it Iterator referencing the sector.
		 * @param defragment If true, compact storage afterwards.
		 * @return Iterator positioned at erased slot (or unchanged if invalid).
		 */
		template<bool TS = ThreadSafe>
		Iterator erase(Iterator it, bool defragment = false) noexcept {
			if (!(*it)) { return it; }
			TS_GUARD_S(TS && ThreadSafe, UNIQUE, mPinsCounter.waitUntilChangeable(it->id);, return eraseImpl(it, defragment););
		}

		/**
		 * @brief Erase range [first, last).
		 * @param first Begin iterator.
		 * @param last End iterator.
		 * @param defragment If true, compact storage after removal.
		 * @return Iterator at the starting position.
		 */
		template<bool TS = ThreadSafe>
		Iterator erase(Iterator first, Iterator last, bool defragment = false) noexcept {
			if (first == last || !(*first)) { return first; }
			TS_GUARD_S(TS && ThreadSafe, UNIQUE, mPinsCounter.waitUntilChangeable(first->id);, return eraseImpl(first, last, defragment););
		}

		/**
		 * @brief Conditionally erase sectors in [first,last), invoking predicate on each Sector*.
		 * @tparam Func Predicate (Sector*) returning bool; if true sector is destroyed.
		 * @param defragment If true, triggers defragment at end (if something erased).
		 */
		template<typename Func, bool TS = ThreadSafe>
		void erase_if(Iterator first, Iterator last, Func&& func, bool defragment = false) noexcept {
			if (first == last || !(*first)) { return; }
			TS_GUARD_S(TS && ThreadSafe, UNIQUE, mPinsCounter.waitUntilChangeable(first->id);, erase_ifImpl(first, last, std::forward<Func>(func), defragment););
		}

		/**
		 * @brief Queue asynchronous erase by id (actual removal deferred).
		 * @param id First sector id.
		 * @param count Number of consecutive ids to queue.
		 * @note Removal occurs in processPendingErases(); pinned sectors remain until unpinned.
		 */
		void eraseAsync(SectorId id, size_t count = 1) requires(ThreadSafe) {
			for (auto i = id; i < id + count; ++i) {
				eraseAsyncImpl(i);
			}
		}

		/// @name Sector lookup utilities
		/// @{
		/**
		 * @brief Find the first linear index whose sector id >= sectorId.
		 * @return Linear index or size() if none.
		 */
		template<bool TS = ThreadSafe> size_t findRightNearestSectorIndex(SectorId sectorId)  const { TS_GUARD(TS && ThreadSafe, SHARED, return findRightNearestSectorIndexImpl(sectorId)); }
		template<bool TS = ThreadSafe> bool containsSector(SectorId id)						  const { TS_GUARD(TS && ThreadSafe, SHARED, return containsSectorImpl(id)); }
		template<bool TS = ThreadSafe> Sector* at(size_t sectorIndex)						  const { TS_GUARD(TS && ThreadSafe, SHARED, return atImpl(sectorIndex)); }
		template<bool TS = ThreadSafe> Sector* findSector(SectorId id)						  const { TS_GUARD(TS && ThreadSafe, SHARED, return findSectorImpl(id)); }
		template<bool TS = ThreadSafe> Sector* getSector(SectorId id)						  const { TS_GUARD(TS && ThreadSafe, SHARED, return getSectorImpl(id)); }
		/// @}

		/// @name Index and capacity queries
		/// @{
		template<bool TS = ThreadSafe> size_t getSectorIndex(SectorId id)					  const { TS_GUARD(TS && ThreadSafe, SHARED, return getSectorIndexImpl(id)); }
		template<bool TS = ThreadSafe> size_t getSectorIndex(Sector* sector)			      const { TS_GUARD(TS && ThreadSafe, SHARED, return getSectorIndexImpl(sector)); }
		template<bool TS = ThreadSafe> size_t sectorsMapCapacity()							  const { TS_GUARD(TS && ThreadSafe, SHARED, return sectorsMapCapacityImpl()); }
		template<bool TS = ThreadSafe> size_t capacity()									  const { TS_GUARD(TS && ThreadSafe, SHARED, return capacityImpl()); }
		template<bool TS = ThreadSafe> size_t size()										  const { TS_GUARD(TS && ThreadSafe, SHARED, return sizeImpl()); }
		template<bool TS = ThreadSafe> bool empty()											  const { TS_GUARD(TS && ThreadSafe, SHARED, return emptyImpl()); }
		template<bool TS = ThreadSafe> void shrinkToFit()										    { TS_GUARD(TS && ThreadSafe, UNIQUE, shrinkToFitImpl()); }
		/// @}

		/// @name Storage management
		/// @{
		template<bool TS = ThreadSafe> void reserve(uint32_t newCapacity) { TS_GUARD(TS && ThreadSafe, UNIQUE, reserveImpl(newCapacity)); }
		template<bool TS = ThreadSafe> void clear()					      { TS_GUARD_S(TS && ThreadSafe, UNIQUE, mPinsCounter.waitUntilChangeable();, clearImpl(););}
		/// @}

		/**
		 * @brief Defragment by collapsing dead (erased) sectors; preserves relative order of alive ones.
		 * @note Blocks until all pins are released (ThreadSafe build).
		 */
		template<bool TS = ThreadSafe>
		void defragment() { TS_GUARD_S(TS && ThreadSafe, UNIQUE, mPinsCounter.waitUntilChangeable(); , defragmentImpl();); }
		
		/**
		 * @brief Attempt a defragment only if array not logically pinned (non-blocking attempt).
		 */
		template<bool TS = ThreadSafe>
		void tryDefragment() { TS_GUARD_S(TS && ThreadSafe, UNIQUE, if (mPinsCounter.isArrayLocked()){ return;} , defragmentImpl();); }

		/// @brief Increment deferred defragment bytes counter.
		void incDefragmentSize(uint32_t count = 1) { mDefragmentSize += count; }

		/// @name Defragment metrics / thresholds
		/// @{
		template<bool TS = ThreadSafe> auto getDefragmentationSize()	const { TS_GUARD(TS && ThreadSafe, SHARED, return mDefragmentSize; ); }
		template<bool TS = ThreadSafe> auto getDefragmentationRatio()	const { TS_GUARD(TS && ThreadSafe, SHARED, return mSize ? (static_cast<float>(mDefragmentSize) / static_cast<float>(mSize)) : 0.f;); }
		template<bool TS = ThreadSafe> bool needDefragment()			const { TS_GUARD(TS && ThreadSafe, SHARED, return getDefragmentationRatio<false>() > mDefragThreshold; ); }
		/**
		 * @brief Set ratio threshold (0..1) above which needDefragment()==true.
		 * @param threshold Clamped into [0..1].
		 */
		template<bool TS = ThreadSafe> void setDefragmentThreshold(float threshold)	  { TS_GUARD(TS && ThreadSafe, UNIQUE, mDefragThreshold = std::max(0.f, std::min(threshold, 1.f)); ); }
		/// @}

		/**
		 * @brief Insert / overwrite a member (or whole Sector) at sectorId.
		 * @tparam T Member type or Sector (for whole copy/move).
		 * @param sectorId Sector id (allocated if missing).
		 * @param data Source object (moved if rvalue).
		 * @return Pointer to inserted member (or sector pointer if T == Sector).
		 */
		template<typename T, bool TS = ThreadSafe>
		std::remove_cvref_t<T>* insert(SectorId sectorId, T&& data) {
			TS_GUARD_S(TS && ThreadSafe, UNIQUE, mPinsCounter.waitUntilChangeable(sectorId);, return insertImpl(sectorId, std::forward<T>(data)););
		}

		/**
		 * @brief Construct in-place a member of type T.
		 * @tparam T Component/member type.
		 * @param sectorId Sector id.
		 * @param args Constructor arguments.
		 * @return Pointer to newly constructed member.
		 */
		template<typename T, bool TS = ThreadSafe, class... Args>
		T* emplace(SectorId sectorId, Args&&... args)  {
			TS_GUARD_S(TS && ThreadSafe, UNIQUE, mPinsCounter.waitUntilChangeable(sectorId);, return emplaceImpl<T>(sectorId, std::forward<Args>(args)...););
		}

		/**
		 * @brief Convenience insertion: if single T argument => insert(); else emplace().
		 * @tparam T Component/member type.
		 * @param sectorId Sector id.
		 * @param args Forwarded creation args.
		 */
		template<typename T, bool TS = ThreadSafe, class... Args>
		T* push(SectorId sectorId, Args&&... args) {
			if constexpr (sizeof...(Args) == 1 && (std::is_same_v<std::remove_cvref_t<Args>, T> && ...)) {
				return insert<Args..., TS>(sectorId, std::forward<Args>(args)...);
			}
			else {
				return emplace<T, TS>(sectorId, std::forward<Args>(args)...);
			}
		}

		/**
		 * @brief Drain deferred erase queue and optionally defragment.
		 * @param withDefragment If true and ratio above threshold, compacts after processing.
		 * @param sync If true acquire internal lock (set false if caller already holds lock).
		 * @note Old retired buffers freed at end (ThreadSafe only).
		 */
		template<bool Lock = false>
		void processPendingErases(bool withDefragment = true) requires(ThreadSafe) {
			if constexpr(Lock) {
				auto lock = std::unique_lock(mtx);
				processPendingErasesImpl(withDefragment);
			}
			else {
				processPendingErasesImpl(withDefragment);
			}
		}

	private: // ---- High-level copy / move wrappers (locking aware) ----------------------
		template<bool T, typename Alloc, bool TS = ThreadSafe>
		void copy(const SectorsArray<T, Alloc>& other)  {
			if constexpr (TS || T) {
				auto lock = writeLock();
				auto otherLock = other.readLock();
				mPinsCounter.waitUntilChangeable();
				copyImpl(other);
			}
			else {
				copyImpl(other);
			}
		}

		template<bool T, typename Alloc, bool TS = ThreadSafe>
		void move(SectorsArray<T, Alloc>&& other) {
			if constexpr (TS || T) {
				auto lock = writeLock();
				auto otherLock = other.writeLock();
				mPinsCounter.waitUntilChangeable();
				other.mPinsCounter.waitUntilChangeable();
				moveImpl(std::move(other));
			}
			else {
				moveImpl(std::move(other));
			}
		}

	private: // ---- Internal erase / shift / defrag primitives ---------------------------
		void processPendingErasesImpl(bool withDefragment = true) requires(ThreadSafe) {
			if (mPendingErase.empty()) {
				if (needDefragment<false>()) {
					if (withDefragment) {
						mPinsCounter.waitUntilChangeable();
						defragmentImpl();
					}
				}
				return;
			}

			auto tmp = std::move(mPendingErase);
			std::ranges::sort(tmp);
			tmp.erase(std::ranges::unique(tmp).begin(), tmp.end());
			for (auto id : tmp) {
				if (mPinsCounter.canMoveSector(id)) {
					Sector::destroySector(getSectorImpl(id), getLayout());
					incDefragmentSize();
					mSectorsMap.sectorsMap[id] = nullptr;
				}
				else {
					mPendingErase.emplace_back(id);
				}
			}

			if (needDefragment<false>()) {
				if (withDefragment) {
					mPinsCounter.waitUntilChangeable();
					defragmentImpl();
				}
			}
		}

		Iterator eraseImpl(Iterator it, bool defragment = false) noexcept {
			if (!(*it) || sectorsMapCapacityImpl() <= it->id) {
				return it;
			}
			auto idx = it.linearIndex();
			mSectorsMap.sectorsMap[it->id] = nullptr;
			Sector::destroySector(*it, getLayout());
			if (defragment) {
				shiftSectorsImpl<Left>(idx + 1);
				mSize--;
			}
			else {
				incDefragmentSize();
			}
			return Iterator(this, idx);
		}

		Iterator eraseImpl(Iterator first, Iterator last, bool defragment = false) noexcept {
			if (first == last || !(*first)) { return first; }

			auto idx = first.linearIndex();
			auto lastIdx = last.linearIndex();
			for (auto it = first; it != last; ++it) {
				if (mSectorsMap.sectorsMap.size() > it->id) {
					mSectorsMap.sectorsMap[it->id] = nullptr;
				}
				Sector::destroySector(*it, getLayout());
			}
			if (defragment) {
				shiftSectorsImpl<Left>(lastIdx, lastIdx - idx);
				mSize -= lastIdx - idx;
			} else {
				incDefragmentSize(static_cast<uint32_t>(lastIdx - idx));
			}
			return Iterator(this, idx);
		}

		template<typename Func>
		void erase_ifImpl(Iterator first, Iterator last, Func&& func, bool defragment = false) noexcept {
			if (first == last || !(*first)) { return; }
			auto idx = first.linearIndex();
			auto lastIdx = last.linearIndex();
			for (auto it = first; it != last; ++it) {
				if (std::forward<Func>(func)(*it)) {
					mSectorsMap.sectorsMap[it->id] = nullptr;
					Sector::destroySector(*it, getLayout());
					incDefragmentSize();
				}
			}
			if (defragment) {
				defragmentImpl(idx);
			}
		}

		size_t findRightNearestSectorIndexImpl(SectorId sectorId) const {
			if (sectorId < sectorsMapCapacityImpl()) {
				for (auto it = mSectorsMap.sectorsMap.begin() + sectorId, end = mSectorsMap.sectorsMap.end(); it != end; ++it) {
					if (*it) {
						return mAllocator.find(*it);
					}
				}
			}
			return mSize;
		}

		FORCE_INLINE bool containsSectorImpl(SectorId id)	const { return findSectorImpl(id) != nullptr; }
		FORCE_INLINE Sector* atImpl(size_t sectorIndex)		const { assert(sectorIndex < mSize); return mAllocator.at(sectorIndex); }
		FORCE_INLINE Sector* findSectorImpl(SectorId id)	const { return mSectorsMap.findSector(id); }

		FORCE_INLINE Sector* getSectorImpl(SectorId id)		const { return mSectorsMap.getSector(id); }
		FORCE_INLINE size_t getSectorIndexImpl(SectorId id)	const { return mAllocator.find(findSectorImpl(id)); }
		FORCE_INLINE size_t getSectorIndexImpl(Sector* sector)	const { return mAllocator.find(sector); }
		FORCE_INLINE size_t sectorsMapCapacityImpl()			const { return mSectorsMap.capacity(); }
		FORCE_INLINE size_t capacityImpl()					const { return mAllocator.capacity(); }
		FORCE_INLINE size_t sizeImpl()						const { return mSize; }
		FORCE_INLINE bool   emptyImpl()						const { return !mSize; }

		FORCE_INLINE void shrinkToFitImpl() { mAllocator.deallocate(mSize, mAllocator.capacity()); }

		void clearImpl() {
			if (mSize) {
				if (!getLayout()->isTrivial()) {
					for (auto it = Iterator(this, 0), endIt = Iterator(this, sizeImpl()); it != endIt; ++it) {
						Sector::destroySector(*it, getLayout());
					}
				}
				mSectorsMap.sectorsMap.clear();
				mSectorsMap.storeVector();
				mPendingErase.clear();
				mSize = 0;
				mDefragmentSize = 0;
			}
		}

		void reserveImpl(uint32_t newCapacity) {
			if (mAllocator.capacity() < newCapacity) {
				mAllocator.allocate(newCapacity);
				if (newCapacity > sectorsMapCapacityImpl()) {
					mSectorsMap.sectorsMap.resize(newCapacity, nullptr);
					mSectorsMap.storeVector();
				}
			}
		}

		template<typename T>
		std::remove_reference_t<T>* insertImpl(SectorId sectorId, T&& data) {
			using U = std::remove_cvref_t<T>;

			Sector* sector = acquireSectorImpl(sectorId);
			if constexpr (std::is_same_v<U, Sector>) {
				if constexpr (std::is_lvalue_reference_v<T>) {
					return Sector::copySector(std::addressof(data), sector, getLayout());
				}
				else {
					return Sector::moveSector(std::addressof(data), sector, getLayout());
				}
			}
			else {
				if constexpr (std::is_lvalue_reference_v<T>) {
					return Sector::copyMember<U>(std::forward<T>(data), sector, getLayoutData<U>());
				}
				else {
					return Sector::moveMember<U>(std::forward<T>(data), sector, getLayoutData<U>());
				}
			}
		}

		template<typename T, class... Args>
		T* emplaceImpl(SectorId sectorId, Args&&... args) {	return acquireSectorImpl(sectorId)->template emplaceMember<T>(getLayoutData<T>(), std::forward<Args>(args)...); }

		template<bool T, typename Alloc>
		void copyImpl(const SectorsArray<T, Alloc>& other) {
			clearImpl();
			shrinkToFitImpl();
			if constexpr (ThreadSafe || T) {
				processPendingErases<false>(true);
			}
			mSize = other.mSize;
			mAllocator = other.mAllocator;
			mSectorsMap.sectorsMap.resize(other.mSectorsMap.sectorsMap.size(), nullptr);
			mSectorsMap.storeVector();
			for (auto it = Iterator(this, 0), endIt = Iterator(this, sizeImpl()); it != endIt; ++it) {
				auto sector = *it;
				mSectorsMap.sectorsMap[sector->id] = sector;
			}
			defragmentImpl();
		}

		template<bool T, typename Alloc>
		void moveImpl(SectorsArray<T, Alloc>&& other) {
			clearImpl();
			shrinkToFitImpl();
			if constexpr (ThreadSafe || T) {
				processPendingErases<false>(true);
				other.template processPendingErases<false>(true);
			}
			mSize = other.mSize;
			mAllocator = std::move(other.mAllocator);
			mSectorsMap.sectorsMap.assign(other.mSectorsMap.sectorsMap.size(), nullptr);
			mSectorsMap.storeVector();

			size_t i = 0;
			auto cursor = mAllocator.getCursor(0);
			while (cursor && i++ < sizeImpl()) {
				mSectorsMap.sectorsMap[cursor->id] = *cursor;
				++cursor;
			}

			mDefragmentSize   = other.mDefragmentSize;
			mDefragThreshold  = other.mDefragThreshold;
			other.mSize = 0;
			other.shrinkToFitImpl();
		}

		enum ShiftDirection : uint8_t { Left = 0, Right = 1 };

		/**
		 * @brief Shift sectors left or right to open/close gaps.
		 * @tparam ShiftDir Direction (Left / Right).
		 * @param from Starting linear index where shift begins.
		 * @param count How many slots to open/close.
		 */
		template<ShiftDirection ShiftDir = Left>
		void shiftSectorsImpl(size_t from, size_t count = 1)  {
			if (!count) return;

			if constexpr (ShiftDir == Left) {
				if (from < count) return;
				if (auto tail = from > mSize ? 0 : mSize - from) {
					mAllocator.moveSectors(from - count, from, tail);
					for (auto it = Iterator(this, from - count), end = Iterator(this, from + tail); it != end; ++it) {
						mSectorsMap.sectorsMap[it->id] = *it;
					}
				}
			}
			else {
				const size_t oldSize = mSize - count;
				if (const size_t tail = oldSize > from ? (oldSize - from) : 0) {
					mAllocator.moveSectors(from + count, from, tail);
					for (auto it = Iterator(this, from + count), end = Iterator(this, from + count + tail); it != end; ++it) {
						mSectorsMap.sectorsMap[it->id] = *it;
					}
				}
			}
		}

		size_t findInsertPositionImpl(SectorId sectorId, size_t size) const  {
			if (size == 0) return 0;
			if (mAllocator.at(size - 1)->id < sectorId) return size;
			if (mAllocator.at(0)->id > sectorId) return 0;

			size_t right = size;
			size_t left = 0;
			while (right - left > 1) {
				size_t mid = left + (right - left) / 2;
				auto mid_id = mAllocator.at(mid)->id;
				if (mid_id < sectorId) left = mid; else right = mid;
			}
			return right;
		}

		Sector* acquireSectorImpl(SectorId sectorId) {
			if (sectorId >= sectorsMapCapacityImpl()) [[unlikely]] {
				mSectorsMap.sectorsMap.resize(static_cast<size_t>(sectorId) + 1, nullptr);
				mSectorsMap.storeVector();
			}
			mAllocator.allocate(mSize + 1);

			auto& sector = mSectorsMap.sectorsMap[sectorId];
			if (sector) [[unlikely]] {
				return sector;
			}

			size_t pos = mSize++;
			if (!((pos == 0) || (sectorId > mAllocator.at(pos - 1)->id))) [[unlikely]] {
				pos = findInsertPositionImpl(sectorId, pos);
				if (pos != mSize - 1) [[unlikely]] {
					shiftSectorsImpl<Right>(pos);
				}
			}

			sector = mAllocator.at(pos);
			sector->id = sectorId;
			return sector;
		}

		/**
		 * @brief Compact holes created by erased sectors starting from index `from`.
		 * @param from Linear index to start scanning.
		 */
		void defragmentImpl(size_t from = 0)  {
			if constexpr (ThreadSafe) {
				if (mPinsCounter.isArrayLocked()) { return; }
			}

			size_t read = from;
			size_t write = from;
			size_t deleted = 0;
			const size_t n = mSize;

			while (read < n) {
				while (read < n) {
					auto s = mAllocator.at(read);
					if (s->isSectorAlive()) break;
					mSectorsMap.sectorsMap[s->id] = nullptr;
					++read; ++deleted;
				}
				if (read >= n) break;

				size_t runBeg = read;
				while (read < n) {
					auto s = mAllocator.at(read);
					if (!s->isSectorAlive()) break;
					++read;
				}
				const size_t runLen = read - runBeg;
				if (write != runBeg) {
					mAllocator.moveSectors(write, runBeg, runLen);
					for (size_t i = 0; i < runLen; ++i) {
						auto ns = mAllocator.at(write + i);
						mSectorsMap.sectorsMap[ns->id] = ns;
					}
				} else {
					for (size_t i = 0; i < runLen; ++i) {
						auto ns = mAllocator.at(write + i);
						mSectorsMap.sectorsMap[ns->id] = ns;
					}
				}
				write += runLen;
			}

			mSize -= deleted;
			mDefragmentSize -= std::min(mDefragmentSize, static_cast<uint32_t>(deleted));

			shrinkToFitImpl();
		}

		void eraseAsyncImpl(SectorId id) requires(ThreadSafe) {
			if (!findSector(id)) return;

			if (!mPinsCounter.isPinned(id)) {
				UNIQUE_LOCK();
				if (mPinsCounter.canMoveSector(id)){
					if (auto sector = findSector<false>(id)) {
						Sector::destroySector(sector, getLayout());
						incDefragmentSize();
						mSectorsMap.sectorsMap[id] = nullptr;
					}
				}
			}
			else {
				UNIQUE_LOCK();
				mPendingErase.push_back(id);
			}
		}

		[[nodiscard]] PinnedSector pinSectorImpl(Sector* sector) const requires(ThreadSafe) { return sector ? PinnedSector{ mPinsCounter, sector, sector->id } : PinnedSector{}; }

	private:
		/// @return Shared read lock if ThreadSafe, otherwise a dummy object.
		auto readLock()  const requires(ThreadSafe) { return std::shared_lock(mtx); }
		auto readLock()  const requires(!ThreadSafe) { return Dummy{}; }
		/// @return Unique write lock if ThreadSafe, otherwise a dummy object.
		auto writeLock() const requires(ThreadSafe) { return std::unique_lock(mtx); }
		auto writeLock() const requires(!ThreadSafe) { return Dummy{}; }

		template<bool UseLock>
		static consteval void enforceTSMode() {
			if constexpr (!ThreadSafe && UseLock) {
				static_assert(!UseLock, "Invalid use: TS=true on SectorsArray<ThreadSafe=false>. "
					"Either instantiate a thread-safe SectorsArray or call with TS=false.");
			}
		}

	private:
		Allocator mAllocator;                         ///< Underlying memory & cursor provider.

		struct Dummy{};

		mutable std::conditional_t<ThreadSafe, std::shared_mutex, Dummy> mtx;                ///< Container-level RW mutex (ThreadSafe builds).
		mutable std::conditional_t<ThreadSafe, Threads::PinCounters, Dummy> mPinsCounter;    ///< Per-sector pin counters + global array lock state.

		detail::SectorsMap<ThreadSafe> mSectorsMap;   ///< Id -> Sector* map (sparse).

		std::vector<SectorId> mPendingErase;          ///< Deferred erase queue (ThreadSafe).

		size_t   mSize = 0;                           ///< Linear alive+dead sector count (post-defrag shrinks).
		float    mDefragThreshold = 0.2f;             ///< Ratio threshold for needDefragment().
		uint32_t mDefragmentSize = 0;                 ///< Accumulated candidate size for defragmentation.
	};
}
