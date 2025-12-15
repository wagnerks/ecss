#pragma once
/**
 * @file
 * @brief Core ECS registry and iterator/view implementation (SoA-based storage).
 *
 * The Registry manages:
 *   - Allocation and ownership of entity ids (dense id ranges via Ranges<>).
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
#include <ranges>
#include <shared_mutex>
#include <tuple>
#include <vector>

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
	 * Thread safety modes:
	 *   - ThreadSafe == true:
	 *       * Shared mutex protects component array mapping and entity container.
	 *       * Pin counters block structural mutation while components are in use.
	 *   - ThreadSafe == false:
	 *       * No internal synchronization � single-threaded performance focus.
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
		Registry() noexcept = default;

		/// @brief Destroys all component arrays (each SectorsArray is deleted).
		~Registry() noexcept { for (auto array : mComponentsArrays) delete array; }

		/**
		 * @brief Maintenance pass (thread-safe build): process deferred erases and optionally defragment.
		 * @param withDefragment If true, arrays that exceed thresholds may compact themselves.
		 * @note Recommended to call once per frame at a stable synchronization point.
		 * @thread_safety Internally synchronized.
		 */
		void update(bool withDefragment = true) noexcept requires(ThreadSafe) {
			decltype(mComponentsArrays) arrays;
			{
				std::shared_lock lock(componentsArrayMapMutex);
				arrays = mComponentsArrays;
			}

			for (auto* array : arrays) {
				array->processPendingErases(withDefragment);
			}
		}

		/**
		 * @brief Maintenance pass (non-thread-safe build): optionally defragment arrays immediately.
		 * @param withDefragment If true, compacts arrays that request it.
		 * @thread_safety Caller must ensure exclusive access.
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

	public:
		/**
		 * @brief Check if an entity has a live component T.
		 * @tparam T Component type.
		 * @param entity Entity id.
		 * @return True if the component exists and is alive; false otherwise.
		 * @complexity O(1).
		 * @thread_safety Locking/pinning applied if ThreadSafe=true.
		 */
		template <class T>
		FORCE_INLINE bool hasComponent(EntityId entity) noexcept {
			if constexpr (ThreadSafe) {
				if (auto container = getComponentContainer<T>()) {
					if (auto pin = container->pinSector(entity)) {
						return Memory::Sector::isAlive(pin.getIsAlive(), container->template getLayoutData<T>().isAliveMask);
					}
				}
			}
			else {
				if (auto container = getComponentContainer<T>()) {
					auto idx = container->template findLinearIdx<false>(entity);
					if (idx != INVALID_IDX) {
						return Memory::Sector::isAlive(container->template getIsAliveRef<false>(idx), container->template getLayoutData<T>().isAliveMask);
					}
				}
			}

			return false;
		}

		/**
		 * @brief Pin component T for an entity (thread-safe build only).
		 * @tparam T Component type.
		 * @param entity Entity id.
		 * @return PinnedComponent<T> (empty if component missing).
		 * @note The returned object must not outlive concurrent modification epochs.
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
		template <class T, class ...Args>
		FORCE_INLINE T* addComponent(EntityId entity, Args&&... args) noexcept {
			return getComponentContainer<T>()->template push<T>(entity, std::forward<Args>(args)...);
		}

		/**
		 * @brief Bulk add components via generator functor under a single write lock (thread-safe build).
		 * @tparam T Component type.
		 * @tparam Func Callable returning std::pair<EntityId,T>. Return {INVALID_ID, {}} to stop.
		 * @param func Generator invoked repeatedly while lock is held.
		 * @note Optimizes many insertions by avoiding enter/leave lock per element.
		 */
		template <class T, typename Func>
		void addComponents(Func&& func) requires(ThreadSafe) {
			auto container = getComponentContainer<T>();
			auto lock = container->writeLock();
			auto f = std::forward<Func>(func);
			auto res = f();
			while (res.first != INVALID_ID) {
				container->template push<T, false>(res.first, res.second);
				res = f();
			}
		}

		/**
		 * @brief Destroy component T for a single entity (does nothing if not present).
		 * @tparam T Component type.
		 * @param entity Entity id.
		 * @complexity O(1).
		 */
		template <class T>
		void destroyComponent(EntityId entity) noexcept {
			if (auto container = getComponentContainer<T>()) {
				if constexpr (ThreadSafe) {
					auto lock = container->writeLock();
					container->mPinsCounter.waitUntilChangeable(entity);

					auto idx = container->template findLinearIdx<false>(entity);
					if (idx != INVALID_IDX) {
						auto& isAlive = container->template getIsAliveRef<false>(idx);
						auto before = isAlive;
						Memory::Sector::destroyMember(container->mAllocator.at(idx), isAlive, container->template getLayoutData<T>());
						if (before != isAlive) {
							container->incDefragmentSize();
						}
					}
				}
				else {
					auto idx = container->template findLinearIdx<false>(entity);
					if (idx != INVALID_IDX) {
						auto& isAlive = container->template getIsAliveRef<false>(idx);
						auto before = isAlive;
						Memory::Sector::destroyMember(container->mAllocator.at(idx), isAlive, container->template getLayoutData<T>());
						if (before != isAlive) {
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
		 */
		template <class T>
		void destroyComponent(std::vector<EntityId>& entities) noexcept {
			if (entities.empty()) {return;}

			if (auto container = getComponentContainer<T>()) {
				const auto& layout = container->template getLayoutData<T>();
				if constexpr (ThreadSafe) {
					auto lock = container->writeLock();

					prepareEntities(entities, container->template sparseCapacity<false>());
					if (entities.empty()) {
						return;
					}

					container->mPinsCounter.waitUntilChangeable(entities.front());
					for (const auto sectorId : entities) {
						auto idx = container->template findLinearIdx<false>(sectorId);
						if (idx != INVALID_IDX) {
							auto& isAlive = container->template getIsAliveRef<false>(idx);
							auto before = isAlive;
							Memory::Sector::destroyMember(container->mAllocator.at(idx), isAlive, layout);
							if (before != isAlive) {
								container->incDefragmentSize();
							}
						}
					}
				}
				else {
					prepareEntities(entities, container->template sparseCapacity<false>());

					for (const auto sectorId : entities) {
						auto idx = container->template findLinearIdx<false>(sectorId);
						if (idx != INVALID_IDX) {
							auto& isAlive = container->template getIsAliveRef<false>(idx);
							auto before = isAlive;
							Memory::Sector::destroyMember(container->mAllocator.at(idx), isAlive, layout);
							if (before != isAlive) {
								container->incDefragmentSize();
							}
						}
					}
				}
			}
		}

		/// @brief Copy-in an externally built sectors array for component T.
		template<typename T, bool TS, typename Alloc>
		FORCE_INLINE void insert(const Memory::SectorsArray<TS, Alloc>& array) noexcept { *getComponentContainer<T>() = array; }

		/// @brief Move-in an externally built sectors array for component T.
		template<typename T, bool TS, typename Alloc>
		FORCE_INLINE void insert(Memory::SectorsArray<TS, Alloc>&& array) noexcept { *getComponentContainer<T>() = std::move(array); }

	public:
		/**
		 * @brief Create an iterable view limited to given entity ranges.
		 * @tparam Components Component types to fetch; first drives iteration order.
		 * @param ranges Half-open entity ranges.
		 * @return ArraysView instance (ranged iteration).
		 */
		template<typename... Components>
		FORCE_INLINE auto view(const Ranges<EntityId>& ranges) noexcept { return ArraysView<ThreadSafe, Allocator, true, Components...>{ this, ranges }; }

		/**
		 * @brief Create a full-range iterable view over all entities with the main component.
		 * @tparam Components Component types to access; first drives iteration.
		 * @return ArraysView instance (full range).
		 */
		template<typename... Components>
		FORCE_INLINE auto view() noexcept { return ArraysView<ThreadSafe, Allocator, false, Components...>{this}; }

		/**
		 * @brief Apply a function to each entity in a list, pinning requested component types (thread-safe build).
		 * @tparam Components Component types to pin.
		 * @tparam Func Callable signature: void(EntityId, Components*...).
		 * @param entities Entity ids to process.
		 * @param func Function invoked per entity.
		 * @note Skips entities missing any main pinned component (pointer passed may be nullptr for non-main).
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
		 */
		template <class... Components>
		FORCE_INLINE void reserve(uint32_t newCapacity) noexcept { (getComponentContainer<Components>()->reserve(newCapacity), ...); }

		/**
		 * @brief Clear all component arrays and remove all entities.
		 * @note Does not shrink capacity.
		 * @post contains(id)==false for any previously allocated entity.
		 */
		void clear() noexcept {
			if constexpr (ThreadSafe) {
				{
					decltype(mComponentsArrays) arrays;
					{
						std::shared_lock lock(componentsArrayMapMutex);
						arrays = mComponentsArrays;
					}

					for (auto* array : arrays) {
						array->clear();
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
		 * @brief Defragment all arrays (compacts fragmented dead slots).
		 * @note Can be expensive if many arrays large � schedule during low frame-load moments.
		 */
		void defragment() noexcept {
			if constexpr(ThreadSafe) {
				decltype(mComponentsArrays) arrays;
				{
					auto lock = std::unique_lock(componentsArrayMapMutex);
					arrays = mComponentsArrays;
				}

				for (auto* array : arrays) {
					array->defragment();
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
					mComponentsArrays.push_back(sectorsArray);
					((mComponentsArraysMap[componentTypeId<ComponentTypes>()] = sectorsArray), ...);
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

				sectorsArray->reserve(capacity);
			}
		}

		/**
		 * @brief Get (or lazily create) the sectors container for component T.
		 * @tparam T Component type.
		 * @return Pointer to container holding (possibly grouped) T.
		 * @note Will implicitly register a single-type array if not pre-registered.
		 */
		template <class T>
		[[nodiscard]] Memory::SectorsArray<ThreadSafe, Allocator>* getComponentContainer() noexcept {
			const auto componentType = componentTypeId<T>();
			if constexpr (ThreadSafe) {
				std::shared_lock readLock(componentsArrayMapMutex);
				if (mComponentsArraysMap.size() > componentType) {
					if (auto array = mComponentsArraysMap[componentType]) {
						return array;
					}
				}
			}
			else {
				if (mComponentsArraysMap.size() > componentType) {
					if (auto array = mComponentsArraysMap[componentType]) {
						return array;
					}
				}
			}

			return registerArray<T>(), getComponentContainer<T>();
		}

	public:
		// ===== Entities API ===================================================

		/// @return True if registry currently owns entityId.
		FORCE_INLINE bool contains(EntityId entityId) const noexcept {
			if constexpr (ThreadSafe) {
				auto lock = std::shared_lock(mEntitiesMutex);
				return mEntities.contains(entityId);
			}
			else {
				return mEntities.contains(entityId);
			}
		}

		/// @brief Allocate (take) a new entity id.
		FORCE_INLINE EntityId takeEntity() noexcept
		{
			if constexpr (ThreadSafe) {
				auto lock = std::unique_lock(mEntitiesMutex);
				return mEntities.take();
			}
			else {
				return mEntities.take();
			}
		}

		/// @brief Snapshot all entity ids (copy).
		FORCE_INLINE std::vector<EntityId> getAllEntities() const noexcept
		{
			if constexpr (ThreadSafe) {
				auto lock = std::unique_lock(mEntitiesMutex); return mEntities.getAll();
			}
			else {
				return mEntities.getAll();
			}
		}

		/**
		 * @brief Destroy a single entity and all of its components.
		 * @param entityId Entity to remove (ignored if not owned).
		 * @complexity O(A) with A = number of component arrays.
		 */
		void destroyEntity(EntityId entityId) noexcept {
			if (!contains(entityId)) {
				return;
			}

			if constexpr (ThreadSafe) {
				auto lock = std::unique_lock(mEntitiesMutex);
				mEntities.erase(entityId);
			}
			else {
				mEntities.erase(entityId);
			}

			destroySector(entityId);
		}

		/**
		 * @brief Destroy a batch of entities and all their components (sequential per-array).
		 * @param entities List of entities (not modified).
		 * @note Safe to call while other threads query (ThreadSafe=true).
		 * @warning No parallelization here to avoid thread lifetime complexity.
		 */
		void destroyEntities(std::vector<EntityId>& entities) noexcept {
			if (entities.empty()) {
				return;
			}

			if constexpr (ThreadSafe) {
				decltype(mComponentsArrays) compArrays;
				{
					auto lock = std::shared_lock(componentsArrayMapMutex);
					compArrays = mComponentsArrays;
				}

				for (auto* array : compArrays) {
					auto arrLock = array->writeLock();
					const auto layout = array->getLayout();

					auto localEntities = entities;
					prepareEntities(localEntities, array->template sparseCapacity<false>());
					if (localEntities.empty()) {
						continue;
					}

					array->mPinsCounter.waitUntilChangeable(localEntities.front());
					array->incDefragmentSize(static_cast<uint32_t>(localEntities.size()));

					for (const auto sectorId : localEntities) {
						auto idx = array->template findLinearIdx<false>(sectorId);
						if (idx != INVALID_IDX) {
							Memory::Sector::destroySectorData(array->mAllocator.at(idx), array->template getIsAliveRef<false>(idx), layout);
						}
					}
				}
			}
			else {
				for (auto* array : mComponentsArrays) {
					auto localEntities = entities;
					prepareEntities(localEntities, array->template sparseCapacity<false>());
					const auto layout = array->getLayout();
					for (const auto sectorId : localEntities) {
						auto idx = array->template findLinearIdx<false>(sectorId);
						if (idx != INVALID_IDX) {
							Memory::Sector::destroySectorData(array->mAllocator.at(idx), array->template getIsAliveRef<false>(idx), layout);
							array->incDefragmentSize();
						}
					}
				}
			}

			if constexpr(ThreadSafe) {
				auto lock = std::unique_lock(mEntitiesMutex);
				for (auto id : entities) {
					mEntities.erase(id);
				}
			}
			else {
				for (auto id : entities) {
					mEntities.erase(id);
				}
			}
		}

		/// @brief Defragment the container for component T (if it exists).
		template<typename T>
		FORCE_INLINE void defragment() noexcept { if (auto container = getComponentContainer<T>()) { container->defragment();} }

		/// @brief Set defragment threshold for component T container.
		template<typename T>
		FORCE_INLINE void setDefragmentThreshold(float threshold) { if (auto container = getComponentContainer<T>()) { container->setDefragmentThreshold(threshold); } }

	private:
		/**
		 * @brief Destroy a single entity across all arrays (internal helper).
		 * @param entityId Entity id.
		 */
		void destroySector(EntityId entityId) noexcept {
			if constexpr (ThreadSafe) {
				decltype(mComponentsArrays) arrays;
				{
					auto lock = std::unique_lock(componentsArrayMapMutex);
					arrays = mComponentsArrays;
				}

				for (auto array : arrays) {
					auto lock = array->writeLock();
					array->mPinsCounter.waitUntilChangeable(entityId);

					auto idx = array->template findLinearIdx<false>(entityId);
					if (idx != INVALID_IDX) {
						Memory::Sector::destroySectorData(array->mAllocator.at(idx), array->template getIsAliveRef<false>(idx), array->getLayout());
						array->incDefragmentSize();
					}
				}
			}
			else {
				for (auto array : mComponentsArrays) {
					auto idx = array->template findLinearIdx<false>(entityId);
					if (idx != INVALID_IDX) {
						Memory::Sector::destroySectorData(array->mAllocator.at(idx), array->template getIsAliveRef<false>(idx), array->getLayout());
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
		 * @brief Internal helper: pin a sector from one of several arrays (thread-safe build).
		 * @tparam T Component type stored in the pinned sector.
		 * @tparam ArraysArr Array-like container of SectorsArray*.
		 * @param pinId Sector id to pin.
		 * @param arrays Container of sectors arrays to search.
		 * @param lockedArr If not null, this array is already write-locked (skip locking).
		 * @param index Index in arrays to pin from.
		 * @return PinnedSector handle (may be empty if sector not found).
		 */
		template<typename T, class ArraysArr>
		static Memory::PinnedSector pinSector(SectorId pinId, const ArraysArr& arrays, Memory::SectorsArray<ThreadSafe, Allocator>* lockedArr, size_t index) requires(ThreadSafe) {
			auto container = arrays[index];
			if (lockedArr == container) {
				return Memory::PinnedSector(container->mPinsCounter, nullptr, pinId);
			}

			auto lock = container->readLock();
			return Memory::PinnedSector(container->mPinsCounter, nullptr, pinId);
		}

		/**
		 * @brief Internal helper: ensure entities vector is sorted & clamped to valid capacity.
		 * @param entities [in/out] Vector of entity ids.
		 * @param sparseCapacity Max valid sector index (exclusive).
		 */
		static void prepareEntities(std::vector<EntityId>& entities, size_t sparseCapacity) {
			if (entities.empty()) { return; }
			std::ranges::sort(entities);

			if (entities.front() >= sparseCapacity) {
				entities.clear();
				return;
			}

			if (entities.back() >= sparseCapacity) {
				int distance = static_cast<int>(entities.size());
				for (auto entity : std::ranges::reverse_view(entities)) {
					if (entity < sparseCapacity) {
						break;
					}
					distance--;
				}

				entities.erase(entities.begin() + distance, entities.end());
			}
		}

	private:
		Ranges<EntityId> mEntities; ///< Dense range-managed entity id storage.

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
	 *   - In ranged mode, entity ranges are translated to nearest sector indices (clamped).
	 *
	 * Thread safety:
	 *   - ThreadSafe=true: Back sector pinning ensures iteration upper bound stability.
	 *   - Non-main components may be null if not present or not grouped in same array.
	 *
	 * @warning Do not cache raw pointers across mutating frames unless externally synchronized.
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
			Iterator(const SectorArrays& arrays, SectorsIt iterator, const std::vector<std::pair<Sectors*, SectorsRangeIt>>& secondary) : mIterator(std::move(iterator)) {
				initTypeAccessInfo<T, CompTypes...>(arrays, secondary);
				// Lazy sync: secondary iterators will be synced on-demand in getComponent
			}

			FORCE_INLINE value_type operator*() const noexcept { 
				auto slot = *mIterator;
				return { slot.id, 
				         reinterpret_cast<T*>(slot.data + mMainOffset), 
				         getComponent<CompTypes>(slot)... }; 
			}
			FORCE_INLINE Iterator& operator++() noexcept { 
				++mIterator;
				// Lazy sync: don't advance secondary iterators here
				// They will be synced on-demand in getComponent when needed
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
			if constexpr (ThreadSafe) {
				// Lazy sync: advance iterator only when component is actually needed
				auto& it = mSecondaryIterators[info.iteratorIdx];
				if (!it) [[unlikely]] {
					return nullptr;  // Secondary array is empty or exhausted
				}
				if ((*it).linearIdx < slot.linearIdx) [[unlikely]] {
					it.advanceToLinearIdx(slot.linearIdx);
				}
				if (!it) [[unlikely]] {
					return nullptr;  // Iterator became invalid after advance
				}
				auto otherSlot = *it;
				return (otherSlot.id == slot.id && (otherSlot.isAlive & info.typeAliveMask)) ? reinterpret_cast<ComponentType*>(otherSlot.data + info.typeOffsetInSector) : nullptr;
			}
			else {
					// Sequential iteration - IDs are sorted, use iterator advance
					auto& it = mSecondaryIterators[info.iteratorIdx];
					if (!it) [[unlikely]] {
						return nullptr;
					}
					// Advance to current entity's linear index
					if ((*it).linearIdx < slot.linearIdx) [[unlikely]] {
						it.advanceToLinearIdx(slot.linearIdx);
					}
					if (!it) [[unlikely]] {
						return nullptr;
					}
					auto otherSlot = *it;
					return (otherSlot.id == slot.id && (otherSlot.isAlive & info.typeAliveMask)) 
						? reinterpret_cast<ComponentType*>(otherSlot.data + info.typeOffsetInSector) 
						: nullptr;
				}
			}

			template<typename... Types>
			void initTypeAccessInfo(const SectorArrays& arrays, const std::vector<std::pair<Sectors*, SectorsRangeIt>>& secondary) noexcept {
				uint8_t arrayIndexes[TypesCount];
				std::fill_n(arrayIndexes, TypesCount, TypeInfo::kMainIteratorIdx);

				for (const auto& [arr, it] : secondary) {
					mSecondaryArrays[mSecondaryCount] = arr;
					mSecondaryIterators[mSecondaryCount] = it;  // Use iterators for both TS and non-TS
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
			TypeAccessTuple mTypeAccessInfo;
			Sectors*		mSecondaryArrays[CTCount ? CTCount : 1] = {};
			mutable SectorsRangeIt mSecondaryIterators[CTCount ? CTCount : 1];  // Used only for ThreadSafe, mutable for lazy sync
			SectorsIt		mIterator;
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
				// Ranged - use regular iterator
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
		/// @brief Fastest path for single component iteration
		template<typename Func>
		FORCE_INLINE void eachSingle(Func&& func) const requires (sizeof...(CompTypes) == 0 && !Ranged) {
			if (!mMainArray || mSize == 0) return;
			
			const auto& layout = mMainArray->template getLayoutData<T>();
			const uint16_t offset = layout.offset;
			
			// Compile-time stride for vectorization!
			constexpr size_t stride = types::OffsetArray<types::EmptyBase, T>::totalSize;
			
			auto& allocator = mMainArray->mAllocator;
			constexpr size_t chunkCapacity = std::remove_reference_t<decltype(allocator)>::mChunkCapacity;
			
			if (mMainArray->template isPacked<false>()) {
				// FAST PATH: No dead slots - pure loop, SIMD vectorizable
				size_t idx = 0;
				for (size_t chunkIdx = 0; chunkIdx < allocator.mChunks.size() && idx < mSize; ++chunkIdx) {
					T* ptr = reinterpret_cast<T*>(static_cast<std::byte*>(allocator.mChunks[chunkIdx]) + offset);
					const size_t chunkEnd = std::min(idx + chunkCapacity, mSize);
					const size_t count = chunkEnd - idx;
					idx = chunkEnd;
					for (size_t i = 0; i < count; ++i) {
						func(ptr[i]);
					}
				}
			} else {
				// SLOW PATH: Check alive mask
				const uint32_t aliveMask = layout.isAliveMask;
				// Load atomic view snapshot for thread-safe access
				auto view = mMainArray->mDenseArrays.loadView();
				const auto* isAliveData = view.isAlive;
				size_t idx = 0;
				for (size_t chunkIdx = 0; chunkIdx < allocator.mChunks.size() && idx < mSize; ++chunkIdx) {
					T* ptr = reinterpret_cast<T*>(static_cast<std::byte*>(allocator.mChunks[chunkIdx]) + offset);
					const size_t chunkEnd = std::min(idx + chunkCapacity, mSize);
					for (size_t localIdx = 0; idx < chunkEnd; ++idx, ++localIdx) {
						if (isAliveData[idx] & aliveMask) {
							func(ptr[localIdx]);
						}
					}
				}
			}
		}

		/// @brief Fast path for grouped multi-component iteration (all components in same sector)
		template<typename Func>
		FORCE_INLINE void eachGrouped(Func&& func) const requires (sizeof...(CompTypes) > 0 && !Ranged) {
			if (!mMainArray || mSize == 0) return;
			
			// Check if all components are in the same sector (grouped)
			if (!mIsGrouped) {
				// Not grouped - use tryInvoke to avoid tuple allocation
				for (auto it = mBeginIt; it != end(); ++it) {
					it.tryInvoke(std::forward<Func>(func));
				}
				return;
			}
			
			// All components in same sector - use SIMD-friendly iteration
			// Compile-time stride and offsets for all types
			constexpr size_t stride = types::OffsetArray<types::EmptyBase, T, CompTypes...>::totalSize;
			constexpr std::array<size_t, TypesCount> offsets = types::OffsetArray<types::EmptyBase, T, CompTypes...>::offsets;
			
			auto& allocator = mMainArray->mAllocator;
			constexpr size_t chunkCapacity = std::remove_reference_t<decltype(allocator)>::mChunkCapacity;
			
			// Get alive mask that covers ALL components
			uint32_t combinedMask = 0;
			combinedMask |= mMainArray->template getLayoutData<T>().isAliveMask;
			((combinedMask |= mMainArray->template getLayoutData<CompTypes>().isAliveMask), ...);
			
			if (mMainArray->template isPacked<false>()) {
				// FAST PATH: No dead slots - SIMD vectorizable
				size_t idx = 0;
				for (size_t chunkIdx = 0; chunkIdx < allocator.mChunks.size() && idx < mSize; ++chunkIdx) {
					std::byte* base = static_cast<std::byte*>(allocator.mChunks[chunkIdx]);
					const size_t chunkEnd = std::min(idx + chunkCapacity, mSize);
					const size_t count = chunkEnd - idx;
					idx = chunkEnd;
					
					for (size_t i = 0; i < count; ++i) {
						std::byte* slot = base + i * stride;
						func(*reinterpret_cast<T*>(slot + offsets[0]),
						     *reinterpret_cast<CompTypes*>(slot + offsets[getIndex<CompTypes>()])...);
					}
				}
			}
			else {
				// SLOW PATH: Check alive mask
				// Load atomic view snapshot for thread-safe access
				auto view = mMainArray->mDenseArrays.loadView();
				const auto* isAliveData = view.isAlive;
				size_t idx = 0;
				for (size_t chunkIdx = 0; chunkIdx < allocator.mChunks.size() && idx < mSize; ++chunkIdx) {
					std::byte* base = static_cast<std::byte*>(allocator.mChunks[chunkIdx]);
					const size_t chunkEnd = std::min(idx + chunkCapacity, mSize);
					for (size_t localIdx = 0; idx < chunkEnd; ++idx, ++localIdx) {
						if ((isAliveData[idx] & combinedMask) == combinedMask) {
							std::byte* slot = base + localIdx * stride;
							func(*reinterpret_cast<T*>(slot + offsets[0]),
							     *reinterpret_cast<CompTypes*>(slot + offsets[getIndex<CompTypes>()])...);
						}
					}
				}
			}
		}

	private:
		void init(Registry<ThreadSafe, Allocator>* manager, const Ranges<EntityId>& ranges = {}) {
			auto arrays = initArrays<CompTypes..., T>(manager);
			SectorsIt it;

			{
				auto mainArr = arrays[getIndex<T>()];
				mMainArray = mainArr;
				auto lock = mainArr->readLock();
				mSize = mainArr->template size<false>();
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
			}

		auto secondary = collectSecondaryArrays(arrays, ranges);
		mBeginIt = Iterator{ arrays, it, secondary };
			
			// Check if all components are in the same sector (grouped)
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
					auto sz = sectorsArray->template size<false>();
					mPins[i] = ranges.empty() || sz == 0 
						? Memory::PinnedSector{} 
						: sectorsArray->template pinSectorAt<false>(sz - 1);
				}
			}
			else {
				size_t last;
				if constexpr (ThreadSafe) {
					mPins[i] = sectorsArray->template pinBackSector<false>();
					last = mPins[i] ? sectorsArray->template size<false>() : 0;
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
					// Acquire read lock to protect iterator creation from concurrent modifications
					auto lock = arr->readLock();
					secondary.emplace_back(arr, SectorsRangeIt(arr, initRange(arr, ranges, i)));
				} else {
					// Non-ThreadSafe uses direct lookup, no iterator needed
					secondary.emplace_back(arr, SectorsRangeIt{});
				}
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
		std::conditional_t<ThreadSafe, std::array<Memory::PinnedSector, TypesCount>, Dummy> mPins;  ///< Back-sector pinning for bound safety (thread-safe build).
		Iterator mBeginIt;                                   ///< Cached begin iterator.

		Sectors* mMainArray = nullptr;
		size_t mSize = 0;
		bool mIsGrouped = false; // True if all components are in the same sector
	};
} // namespace ecss
