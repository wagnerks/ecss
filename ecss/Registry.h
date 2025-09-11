#pragma once

// ecss - Entity Component System with Sectors
// "Sectors" refers to the logic of storing components.
// Multiple components of different types can be stored in one memory location, which I've named a "sector."

#include <algorithm>
#include <array>
#include <ranges>
#include <shared_mutex>
#include <thread>
#include <tuple>
#include <vector>

#include <ecss/EntitiesRanges.h>
#include <ecss/memory/SectorsArray.h>
#include <ecss/memory/Sector.h>
#include <ecss/threads/SyncManager.h>

namespace ecss {
	/**
	 * \brief RAII wrapper that pins the sector containing T and exposes a pointer to T.
	 *
	 * \tparam T Component type stored in the sector.
	 *
	 * \details Holds a \ref Memory::PinnedSector (sector-level pin) and a raw T*.
	 * Releasing or destroying the object unpins the sector and nulls the pointer.
	 */
	template<class T>
	struct PinnedComponent {
		PinnedComponent(const PinnedComponent& other) = delete;
		PinnedComponent(PinnedComponent&& other) noexcept = default;
		PinnedComponent& operator=(const PinnedComponent& other) = delete;
		PinnedComponent& operator=(PinnedComponent&& other) noexcept = default;
		PinnedComponent() = default;

		PinnedComponent(Memory::PinnedSector&& pinnedSector, T* ptr) : sec(std::move(pinnedSector)), ptr(ptr) {}
		~PinnedComponent() { release(); }
		T* get() const noexcept { return ptr; }

		T* operator->() const noexcept { return ptr; }
		T& operator* () const noexcept { return *ptr; }
		explicit operator bool() const noexcept { return ptr != nullptr; }
		void release() { sec.release(); ptr = nullptr; }

	private:
		Memory::PinnedSector sec; // RAII pin сектора
		T* ptr = nullptr;
	};

	template <bool ThreadSafe, typename Allocator, bool Ranged, typename T, typename ...ComponentTypes>
	class ArraysView;

	/**
	 * \brief Central ECS registry that owns per-type sector arrays, entities, and iteration helpers.
	 *
	 * \tparam ThreadSafe If true, public APIs use internal synchronization and pinning rules.
	 * \tparam Allocator  Sector allocator type used by component arrays.
	 */
	template<bool ThreadSafe = true, typename Allocator = Memory::ChunksAllocator<8192>>
	class Registry final {
		template <bool TS, typename Alloc, bool Ranged, typename T, typename ...ComponentTypes>
		friend class ArraysView;

		template <bool TS, typename Alloc>
		friend class Registry;

	public:
		/// \brief Get a stable numeric type id for T within this registry.
		template<typename T>
		ECSType componentTypeId() const noexcept { return mReflectionHelper.getTypeId<T>(); }

	public:
		Registry(const Registry& other) noexcept = delete;
		Registry& operator=(const Registry& other) noexcept = delete;
		Registry(Registry&& other) noexcept = delete;
		Registry& operator=(Registry&& other) noexcept = delete;

	public:
		Registry() noexcept = default;
		~Registry() noexcept { for (auto array : mComponentsArrays) delete array; }

		/**
		 * \brief Maintenance pass (thread-safe build): process deferred erases and optional defragmentation.
		 * \param withDefragment If true, allows arrays to defragment when their thresholds are exceeded.
		 * \note Should be called from “safe” points in your app (e.g., end of frame).
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
		 * \brief Maintenance pass (single-threaded build): optional defragmentation only.
		 * \param withDefragment If true, defragment arrays that report needDefragment().
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
		 * \brief Check if entity has a live component T.
		 * \tparam T Component type.
		 * \param entity Entity id.
		 * \return True if sector exists and component’s liveness bit is set.
		 */
		template <class T>
		bool hasComponent(EntityId entity) noexcept {
			if constexpr (ThreadSafe) {
				if (auto container = getComponentContainer<T>()) {
					if (auto sector = container->pinSector(entity)) {
						return sector->isAlive(container->template getLayoutData<T>().isAliveMask);
					}
				}
			}
			else {
				if (auto container = getComponentContainer<T>()) {
					if (auto sector = container->findSector(entity)) {
						return sector->isAlive(container->template getLayoutData<T>().isAliveMask);
					}
				}
			}
			
			return false;
		}

		/**
		 * \brief Pin the sector for entity and return an RAII handle to T (thread-safe build).
		 * \tparam T Component type.
		 * \param entity Entity id.
		 * \return \ref PinnedComponent<T> that unpins on destruction; empty if missing.
		 */
		template<class T>
		[[nodiscard]] PinnedComponent<T> pinComponent(EntityId entity) noexcept requires(ThreadSafe)  {
			auto* container = getComponentContainer<T>();
			auto pinnedSector = container->pinSector(entity);
			if (!pinnedSector) { return {}; }

			auto component = Memory::Sector::getComponentFromSector<T>(pinnedSector.get(), container->getLayout());
			return component ? PinnedComponent<T>{ std::move(pinnedSector), component } : PinnedComponent<T>{};
		}

		/**
		 * \brief Add or overwrite component T for entity using perfect-forwarded arguments.
		 * \tparam T Component type.
		 * \param entity Entity id (used as sector id).
		 */
		template <class T, class ...Args>
		T* addComponent(EntityId entity, Args&&... args) noexcept {
			return getComponentContainer<T>()->template push<T>(entity, std::forward<Args>(args)...);
		}

		/**
		 * \brief Add multiple components T under one write lock via a generator functor (thread-safe build).
		 * \tparam T Component type.
		 * \tparam Func Callable returning std::pair<EntityId, T>. When \c first==INVALID_ID the loop stops.
		 * \param func Generator invoked repeatedly under the array write lock.
		 */
		template <class T, typename Func>
		void addComponents(Func&& func) requires(ThreadSafe) {
			auto container = getComponentContainer<T>();
			container->writeLock();

			auto res = std::forward<Func>(func)();
			while (res.first != INVALID_ID) {
				container->template push<T, false>(res.first, res.second);
				res = std::forward<Func>(func)();
			}
		}

		/**
		 * \brief Destroy component T for a single entity (no-op if missing).
		 * \tparam T Component type.
		 * \param entity Entity id.
		 */
		template <class T>
		void destroyComponent(EntityId entity) noexcept {
			if (auto container = getComponentContainer<T>()) {
				if constexpr (ThreadSafe) {
					auto lock = container->writeLock();
					container->mPinsCounter.waitUntilChangeable(entity);

					if (auto sector = container->template findSector<false>(entity)) {
						auto before = sector->isAliveData;
						sector->destroyMember(container->template getLayoutData<T>());
						if (before != sector->isAliveData) {
							container->incDefragmentSize();
						}
					}
				}
				else {
					if (auto sector = container->template findSector<false>(entity)) {
						auto before = sector->isAliveData;
						sector->destroyMember(container->template getLayoutData<T>());
						if (before != sector->isAliveData) {
							container->incDefragmentSize();
						}
					}
				}
			}
		}

		/**
		 * \brief Destroy component T for a batch of entities (in-place reorder & clamp by container capacity).
		 * \param entities List of entity ids; will be sorted and truncated to valid ids.
		 */
		template <class T>
		void destroyComponent(std::vector<EntityId>& entities) noexcept {
			if (entities.empty()) {return;}

			if (auto container = getComponentContainer<T>()) {
				const auto& layout = container->template getLayoutData<T>();
				if constexpr (ThreadSafe) {
					auto lock = container->writeLock();

					prepareEntities(entities, container->template sectorsMapCapacity<false>());
					if (entities.empty()) {
						return;
					}

					container->mPinsCounter.waitUntilChangeable(entities.front());
					for (const auto sectorId : entities) {

						if (auto sector = container->template findSector<false>(sectorId)) {
							auto before = sector->isAliveData;
							sector->destroyMember(layout);
							if (before != sector->isAliveData) {
								container->incDefragmentSize();
							}
						}
					}
				}
				else {
					prepareEntities(entities, container->template sectorsMapCapacity<false>());

					for (const auto sectorId : entities) {
						if (auto sector = container->template findSector<false>(sectorId)) {
							auto before = sector->isAliveData;
							sector->destroyMember(layout);
							if (before != sector->isAliveData) {
								container->incDefragmentSize();
							}
						}
					}
				}
				
			}
		}

		/// \brief Replace component array for T by copy from an external array.
		template<typename T, bool TS, typename Alloc>
		void insert(const Memory::SectorsArray<TS, Alloc>& array) noexcept { *getComponentContainer<T>() = array; }

		/// \brief Replace component array for T by move from an external array.
		template<typename T, bool TS, typename Alloc>
		void insert(Memory::SectorsArray<TS, Alloc>&& array) noexcept { *getComponentContainer<T>() = std::move(array); }

	public:
		/**
		 * \brief Create an iterable view limited to given entity ranges.
		 * \tparam Components Component set to access per entity (first type drives iteration).
		 * \param ranges Half-open ranges of entity ids to iterate.
		 */
		template<typename... Components>
		auto view(const EntitiesRanges& ranges) noexcept { return ArraysView<ThreadSafe, Allocator, true, Components...>{ this, ranges }; }

		/**
		 * \brief Create an iterable view over all entities for a component set.
		 * \tparam Components Component set to access per entity (first type drives iteration).
		 */
		template<typename... Components>
		auto view() noexcept { return ArraysView<ThreadSafe, Allocator, false, Components...>{this}; }

		/**
		 * \brief Apply a functor to each entity in \p entities, passing pinned component pointers (thread-safe build).
		 * \tparam Components Component set to pin for each entity.
		 * \param entities List of entity ids.
		 * \param func Callable with signature f(EntityId, Components*...).
		 */
		template<typename... Components, typename Func>
		inline void forEachAsync(const std::vector<EntityId>& entities, Func&& func) noexcept requires(ThreadSafe) { for (auto entity : entities) withPinned<Components...>(entity, func); }

	public:
		// ===== Container management ==========================================

		template <class... Components>
		void reserve(uint32_t newCapacity) noexcept { (getComponentContainer<Components>()->reserve(newCapacity), ...); }

		/**
		 * \brief Clear all component arrays and all entities.
		 * \note In thread-safe build, each array is cleared under its own lock,
		 *       then entity set is cleared under the registry lock.
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
		 * \brief Defragment all component arrays.
		 * \note In thread-safe build, snapshots container list then defrag under each array’s lock.
		 */
		void defragment() noexcept {
			if constexpr(ThreadSafe) {
				decltype(mComponentsArrays) arrays;
				{
					auto lock = ecss::Threads::uniqueLock(componentsArrayMapMutex, ThreadSafe);
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
		 * \brief Pre-register a multi-component array (or single-type with reserved capacity).
		 *
		 * \details Must be called before \ref getComponentContainer for any of \p ComponentTypes...
		 * to ensure they share the same underlying \ref Memory::SectorsArray.
		 *
		 * \param capacity  Optional initial capacity.
		 * \param allocator Allocator instance to move into the array.
		 * \note All \p ComponentTypes must be either entirely new or already mapped to the same array.
		 */
		template<typename... ComponentTypes>
		void registerArray(uint32_t capacity = 0, Allocator allocator = {}) noexcept {
			Memory::SectorsArray<ThreadSafe, Allocator>* sectorsArray;
			{
				auto lock = ecss::Threads::uniqueLock(componentsArrayMapMutex, ThreadSafe);

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

		/**
		 * \brief Get (or lazily create) the component container for T.
		 * \tparam T Component type.
		 * \return Pointer to the owning \ref Memory::SectorsArray for T.
		 */
		template <class T>
		Memory::SectorsArray<ThreadSafe, Allocator>* getComponentContainer() noexcept {
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

		/// \return True if the registry currently owns \p entityId.
		bool contains(EntityId entityId) const noexcept { auto lock = ecss::Threads::sharedLock(mEntitiesMutex, ThreadSafe); return mEntities.contains(entityId); }

		/// \brief Take (allocate) a new entity id.
		EntityId takeEntity() noexcept { auto lock = ecss::Threads::uniqueLock(mEntitiesMutex, ThreadSafe); return mEntities.take(); }

		/// \brief Copy out all entity ids.
		std::vector<EntityId> getAllEntities() const noexcept { auto lock = ecss::Threads::sharedLock(mEntitiesMutex, ThreadSafe); return mEntities.getAll(); }

		/**
		 * \brief Destroy one entity and all its components.
		 * \param entityId Entity to delete (no-op if not owned).
		 */
		void destroyEntity(EntityId entityId) noexcept {
			if (!contains(entityId)) {
				return;
			}

			{
				auto lock = ecss::Threads::uniqueLock(mEntitiesMutex, ThreadSafe);
				mEntities.erase(entityId);
			}

			destroySector(entityId);
		}

		/**
		 * \brief Destroy a batch of entities and all their components.
		 * \param entities Entity ids to delete; vector content is not modified.
		 * \note Thread-safe build runs per-array work in parallel (one thread per array).
		 */
		void destroyEntities(std::vector<EntityId>& entities) noexcept {
			if (entities.empty()) {
				return;
			}

			if constexpr (ThreadSafe) {
				std::vector<std::thread> destroyThreads;
				decltype(mComponentsArrays) compArrays;
				{
					auto lock = ecss::Threads::sharedLock(componentsArrayMapMutex, ThreadSafe);
					compArrays = mComponentsArrays;
				}

				for (auto* array : compArrays) {
					//todo too much threads
					destroyThreads.emplace_back([array = array, entities = entities]() mutable
					{
						const auto layout = array->getLayout();

						auto arrLock = array->writeLock();
						prepareEntities(entities, array->template sectorsMapCapacity<false>());
						if (entities.empty()) {
							return;
						}

						array->mPinsCounter.waitUntilChangeable(entities.front());
						array->incDefragmentSize(static_cast<uint32_t>(entities.size()));

						for (const auto sectorId : entities) {
							Memory::Sector::destroySector(array->template findSector<false>(sectorId), layout);
						}
					});
				}

				{
					auto lock = ecss::Threads::uniqueLock(mEntitiesMutex, ThreadSafe);
					for (auto id : entities) {
						mEntities.erase(id);
					}
				}

				for (auto& thread : destroyThreads) {
					thread.join();
				}
			}
			else {
				for (auto* array : mComponentsArrays) {
					auto ents = entities;
					prepareEntities(ents, array->template sectorsMapCapacity<false>());
					const auto layout = array->getLayout();
					for (const auto sectorId : ents) {
						Memory::Sector::destroySector(array->template findSector<false>(sectorId), layout);
						array->incDefragmentSize();
					}
				}
				
				for (auto id : entities) {
					mEntities.erase(id);
				}
			}
		}

		/// \brief Defragment the container for component T.
		template<typename T>
		void defragment() noexcept { if (auto container = getComponentContainer<T>()) { container->defragment();} }

		/// \brief Set defragment threshold for container of T.
		template<typename T>
		void setDefragmentThreshold(float threshold) { if (auto container = getComponentContainer<T>()) { container->setDefragmentThreshold(threshold); } }

	private:
		void destroySector(EntityId entityId) noexcept {
			decltype(mComponentsArrays) arrays;
			{
				auto lock = ecss::Threads::uniqueLock(componentsArrayMapMutex, ThreadSafe);
				arrays = mComponentsArrays;
			}

			for (auto array : arrays) {
				if constexpr (ThreadSafe) {
					auto lock = array->writeLock();
					array->mPinsCounter.waitUntilChangeable(entityId);

					Memory::Sector::destroySector(array->template findSector<false>(entityId), array->getLayout());
					array->incDefragmentSize();
				}
				else {
					Memory::Sector::destroySector(array->findSector(entityId), array->getLayout());
					array->incDefragmentSize();
				}
			}
		}

		template<class... Ts, class F>
		void withPinned(EntityId entity, F&& f) noexcept requires(ThreadSafe) {
			auto pins = std::make_tuple(pinComponent<Ts>(entity)...);
			std::apply([&](auto&... pc) { std::forward<F>(f)(entity, pc.get()...); }, pins);
		}
        
        template<typename T, class ArraysArr>
        static Memory::PinnedSector pinSector(SectorId pinId, const ArraysArr& arrays, Memory::SectorsArray<ThreadSafe, Allocator>* lockedArr, size_t index) {
            auto container = arrays[index];
            if (lockedArr == container){
                return Memory::PinnedSector(container->mPinsCounter, nullptr, pinId);
            }
            
            auto lock = container->readLock();
            return Memory::PinnedSector(container->mPinsCounter, nullptr, pinId);
        }
        

		template <class... T>
		static std::array<Memory::PinnedSector, sizeof...(T)> pinContainers(SectorId pinId, const std::array<Memory::SectorsArray<ThreadSafe, Allocator>*, sizeof...(T)>& arrays, Memory::SectorsArray<ThreadSafe, Allocator>* lockedArr) noexcept requires(ThreadSafe) {
			using PinnedArr = std::array<Memory::PinnedSector, sizeof...(T)>;
			size_t i = 0;
			return pinId != INVALID_ID ? PinnedArr{ pinSector<T>(pinId, arrays, lockedArr, i++)... } : PinnedArr{};
		}

		static void prepareEntities(std::vector<EntityId>& entities, size_t sectorsCapacity) {
			if (entities.empty()) { return; }
			std::ranges::sort(entities);

			if (entities.front() >= sectorsCapacity) {
				entities.clear();
				return;
			}

			if (entities.back() >= sectorsCapacity) {
				int distance = static_cast<int>(entities.size());
				for (auto entity : std::ranges::reverse_view(entities)) {
					if (entity < sectorsCapacity) {
						break;
					}
					distance--;
				}

				entities.erase(entities.begin() + distance, entities.end());
			}

		}

	private:
		mutable Memory::ReflectionHelper mReflectionHelper;

		EntitiesRanges mEntities;

		std::vector<Memory::SectorsArray<ThreadSafe, Allocator>*> mComponentsArraysMap;
		std::vector<Memory::SectorsArray<ThreadSafe, Allocator>*> mComponentsArrays;

		mutable std::shared_mutex mEntitiesMutex;
		mutable std::shared_mutex componentsArrayMapMutex;
	};

	/**
	 * \brief Metadata for accessing a component type inside a sector array.
	 * \tparam ThreadSafe Mirrors the owning array’s thread-safety flag.
	 * \tparam Allocator  Mirrors the owning array’s allocator type.
	 */
	template<bool ThreadSafe, typename Allocator>
	struct TypeAccessInfo {
		Memory::SectorsArray<ThreadSafe, Allocator>* array = nullptr;
		uint32_t typeAliveMask = 0;
		uint32_t typeOffsetInSector = 0;
		bool isMain = false;
	};

	/**
	 * \brief Iterable view over entities (⚠ may yield nullptr for missing non-main components) with a selected set of components.
	 * \details Scans the container of the first template component (the "main" type) and,
	 * for each sector, exposes pointers to the requested components as a tuple-like set.
	 * Components live in separate containers, so non-main components may be missing for an entity.
	 * \
	 *
	 * Layout:\n
	 * component1Cont    component2Cont    component3Cont\n
	 * 0 [  data   ]     [   data   ]      [   data   ]\n
	 * - [  null   ]     [   data   ]      [   data   ]\n
	 * 1 [  data   ]     [   data   ]      [   data   ]\n
	 * 2 [  data   ]     [   data   ]      [   null   ]\n
	 * - [  null   ]     [   null   ]      [   data   ]\n
	 * 3 [  data   ]     [   null   ]      [   data   ]\n
	 *
	 * Iteration follows indices of the main component's container: 0, 1, 2, 3, ...
	 *
	 * \tparam ThreadSafe  Enable thread-safe iteration/pinning policy.
	 * \tparam Allocator   Sector storage allocator type.
	 * \tparam Ranged      If true, iterates only over provided entity ranges.
	 * \tparam T           Main component type (drives iteration order).
	 * \tparam ComponentTypes Other component types to fetch per entity.
	 *
	 * \note In thread-safe mode, iteration is bounded by a snapshot (pinned back sector).
	 * \see begin(), end(), beginRangedAlive(), beginAlive()
	 */
	template <bool ThreadSafe, typename Allocator, bool Ranged, typename T, typename ...ComponentTypes>
	class ArraysView final {
        using SectorItType = std::conditional_t<Ranged, typename Memory::SectorsArray<ThreadSafe, Allocator>::RangedIteratorAlive, typename Memory::SectorsArray<ThreadSafe, Allocator>::IteratorAlive>;
	public:
		class Iterator {
		public:
			using iterator_category = std::forward_iterator_tag;
			using value_type = std::tuple<EntityId, T*, ComponentTypes*...>;
			using difference_type = std::ptrdiff_t;
			using pointer = value_type*;
			using reference = value_type&;

		public:
			using SectorArrays = std::array<Memory::SectorsArray<ThreadSafe, Allocator>*, sizeof...(ComponentTypes) + 1>;

			Iterator(const SectorArrays& arrays, typename Memory::SectorsArray<ThreadSafe, Allocator>::RangedIteratorAlive iterator) noexcept requires (Ranged) : mIterator(std::move(iterator)) { initTypeAccessInfo<T, ComponentTypes...>(arrays); }
			Iterator(const SectorArrays& arrays, typename Memory::SectorsArray<ThreadSafe, Allocator>::IteratorAlive iterator) noexcept requires (!Ranged) : mIterator(std::move(iterator)) { initTypeAccessInfo<T, ComponentTypes...>(arrays); }

			inline value_type operator*() noexcept { return { (*mIterator)->id, reinterpret_cast<T*>(reinterpret_cast<char*>(*mIterator) + getTypeAccessInfo<T>().typeOffsetInSector), getComponent<ComponentTypes>((*mIterator)->id)... }; }

			inline Iterator& operator++() noexcept { return ++mIterator, * this; }

			bool operator!=(const Iterator& other) const noexcept { return mIterator != other.mIterator; }
			bool operator==(const Iterator& other) const noexcept { return mIterator == other.mIterator; }

		private:
			template <typename ComponentType>
			inline constexpr TypeAccessInfo<ThreadSafe, Allocator>& getTypeAccessInfo() noexcept { return std::get<getIndex<ComponentType>()>(mTypeAccessInfo); }

			template<typename... Types>
			inline void initTypeAccessInfo(const SectorArrays& arrays) noexcept { (initTypeAccessInfoImpl<Types>(arrays[getIndex<Types>()]), ...); }

			template<typename ComponentType>
			inline void initTypeAccessInfoImpl(Memory::SectorsArray<ThreadSafe, Allocator>* sectorArray) noexcept {
				auto& info = getTypeAccessInfo<ComponentType>();
				info.array = sectorArray;
				info.typeAliveMask = sectorArray->template getLayoutData<ComponentType>().isAliveMask;
				info.typeOffsetInSector = sectorArray->template getLayoutData<ComponentType>().offset;
				info.isMain = sectorArray == getTypeAccessInfo<T>().array;
			}

			template<typename ComponentType>
			inline ComponentType* getComponent(EntityId sectorId) noexcept { return Iterator::getComponent<ComponentType>((getTypeAccessInfo<ComponentType>().isMain ? *mIterator : getTypeAccessInfo<ComponentType>().array->template findSector<false>(sectorId)), getTypeAccessInfo<ComponentType>()); }

			template<typename ComponentType>
			inline static ComponentType* getComponent(Memory::Sector* sector, const TypeAccessInfo<ThreadSafe, Allocator>& meta) noexcept { return sector && (sector->isAliveData & meta.typeAliveMask) ? reinterpret_cast<ComponentType*>(reinterpret_cast<char*>(sector) + meta.typeOffsetInSector) : nullptr; }

		private:
			std::conditional_t<Ranged, typename Memory::SectorsArray<ThreadSafe, Allocator>::RangedIteratorAlive, typename Memory::SectorsArray<ThreadSafe, Allocator>::IteratorAlive> mIterator;

			std::tuple<TypeAccessInfo<ThreadSafe, Allocator>, decltype((void)sizeof(ComponentTypes), TypeAccessInfo<ThreadSafe, Allocator>{})...> mTypeAccessInfo;
		};

        inline Iterator begin() noexcept {
            return Iterator{ mArrays, beginIt };
		}

		inline Iterator end() noexcept {
            return Iterator{ mArrays, endIt };
		}

	public:
		explicit ArraysView(Registry<ThreadSafe, Allocator>* manager) noexcept requires (!Ranged) {
			initArrays<ComponentTypes..., T>(manager);
			{
				auto lock = mArrays[getIndex<T>()]->readLock();
				if constexpr (ThreadSafe) {
				
					mPins[0] = mArrays[getIndex<T>()]->template pinBackSector<false>();
					auto index = mPins[0].getId();
					mLast = index == INVALID_ID ? 0 : mArrays[getIndex<T>()]->template getSectorIndex<false>(mPins[0].get()) + 1; // we pinned sector, but its size can be changed in other tread, use pinned index
				}
				else {
					mLast = mArrays[getIndex<T>()]->size();
				}

				beginIt = SectorItType(mArrays[getIndex<T>()], 0, mLast, mArrays[getIndex<T>()]->template getLayoutData<T>().isAliveMask);
				endIt = SectorItType(mArrays[getIndex<T>()], mLast);
			}

			if constexpr (ThreadSafe) {
				auto oldPin = std::move(mPins[0]);
				mPins = manager->template pinContainers<T, ComponentTypes...>(oldPin.getId(), mArrays, mArrays[getIndex<T>()]);
			}
		}

		explicit ArraysView(Registry<ThreadSafe, Allocator>* manager, EntitiesRanges ranges = {}) noexcept requires (Ranged) : mRanges(std::move(ranges)) {
			initArrays<ComponentTypes..., T>(manager);

			{
				auto sectorsArray = mArrays[getIndex<T>()];
				auto lock = sectorsArray->readLock();

				for (auto& range : mRanges.ranges) {
					range.first = static_cast<EntityId>(sectorsArray->template findRightNearestSectorIndex<false>(range.first));
					range.second = static_cast<EntityId>(sectorsArray->template findRightNearestSectorIndex<false>(range.second));
				}
				mRanges.mergeIntersections();

				if constexpr (ThreadSafe) {
					mPins[0] = mRanges.empty() ? Memory::PinnedSector{} : Memory::PinnedSector(sectorsArray->mPinsCounter, nullptr, mRanges.back().second);
				}

				mLast = mRanges.empty() ? 0 : mRanges.back().second;

				beginIt = SectorItType(mArrays[getIndex<T>()], mRanges, mArrays[getIndex<T>()]->template getLayoutData<T>().isAliveMask);
				endIt = SectorItType(mArrays[getIndex<T>()], mLast);
			}

			if constexpr (ThreadSafe) {
				auto oldPin = std::move(mPins[0]);
				mPins = manager->template pinContainers<T, ComponentTypes...>(oldPin.getId(), mArrays, mArrays[getIndex<T>()]);
			}
		}

		bool empty() const noexcept {
			if constexpr (Ranged) {
				return mRanges.empty() || mArrays[getIndex<T>()]->empty();
			}
			else {
				return mArrays[getIndex<T>()]->empty();
			}
		}

	private:
		template<typename ComponentType>
		static inline size_t constexpr getIndex() noexcept {
			if constexpr (std::is_same_v<T, ComponentType>) {
				return 0;
			}
			else {
				return types::getIndex<ComponentType, ComponentTypes...>() + 1;
			}
		}

		template<typename... Types>
		void initArrays(Registry<ThreadSafe, Allocator>* registry) noexcept {
			static_assert(types::areUnique<Types...>(), "Duplicates detected in types");
			((mArrays[getIndex<Types>()] = registry->template getComponentContainer<Types>()), ...);
		}

	private:
        SectorItType beginIt;
        SectorItType endIt;
		std::array<Memory::SectorsArray<ThreadSafe, Allocator>*, sizeof...(ComponentTypes) + 1> mArrays;
		std::array<Memory::PinnedSector, sizeof...(ComponentTypes) + 1> mPins;
		size_t mLast = 0;
		EntitiesRanges mRanges;
	};
}
