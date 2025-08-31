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

	template<bool ThreadSafe = true, typename Allocator = Memory::ChunksAllocator<8192>>
	class Registry final {
		template <bool TS, typename Alloc, bool Ranged, typename T, typename ...ComponentTypes>
		friend class ArraysView;

		template <bool TS, typename Alloc>
		friend class Registry;

	public:
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

		// todo defragmentation koef

		// in multithread environment you need to call update for registry in "safe" parts of your app, to support pending erases and memory safe array reallocations
		// you can also control defragmentation via flag, call defragment once per few frames, or call it manually for needed container
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

	public:
		// returns true if sector with such component exists, and component alive
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

		template<class T>
		[[nodiscard]] PinnedComponent<T> pinComponent(EntityId entity) noexcept requires(ThreadSafe)  {
			auto* container = getComponentContainer<T>();
			auto pinnedSector = container->pinSector(entity);
			if (!pinnedSector) { return {}; }

			auto component = Memory::Sector::getComponentFromSector<T>(pinnedSector.get(), container->getLayout());
			return component ? PinnedComponent<T>{ std::move(pinnedSector), component } : PinnedComponent<T>{};
		}

		// you can emplace component into sector using initializer list
		template <class T, class ...Args>
		T* addComponent(EntityId entity, Args&&... args) noexcept {
			return getComponentContainer<T>()->template push<T>(entity, std::forward<Args>(args)...);
		}

		// add components under one lock via functor, func returns std::pair<EntityId, T>, if EntityId == INVALID_ID - cycle stops
		template <class T, typename Func>
		void addComponents(Func&& func) requires(ThreadSafe) {
			auto container = getComponentContainer<T>();
			container->writeLock();

			auto res = std::forward<T>(func);
			while (res.first != INVALID_ID) {
				container->template push<T, false>(res.first, res.second);
				res = std::forward<T>(func);
			}
		}

		template <class T>
		void destroyComponent(EntityId entity) noexcept {
			if (auto container = getComponentContainer<T>()) {
				if constexpr (ThreadSafe) {
					auto lock = container->writeLock();
					container->mPinsCounter.waitUntilSectorChangeable(entity);

					if (auto sector = container->template findSector<false>(entity)) {
						sector->destroyMember(container->template getLayoutData<T>());
					}
				}
				else {
					if (auto sector = container->template findSector<false>(entity)) {
						sector->destroyMember(container->template getLayoutData<T>());
					}
				}
			}
		}

		template <class T>
		void destroyComponent(std::vector<EntityId>& entities) noexcept {
			if (entities.empty()) {return;}

			if (auto container = getComponentContainer<T>()) {
				const auto& layout = container->template getLayoutData<T>();
				if constexpr (ThreadSafe) {
					auto lock = container->writeLock();

					prepareEntities(entities, container->template sectorsMapCapacity<false>());

					for (const auto sectorId : entities) {
						container->mPinsCounter.waitUntilSectorChangeable(sectorId);

						if (auto sector = container->template findSector<false>(sectorId)) {
							sector->destroyMember(layout);
						}
					}
				}
				else {
					prepareEntities(entities, container->template sectorsMapCapacity<false>());

					for (const auto sectorId : entities) {
						if (auto sector = container->template findSector<false>(sectorId)) {
							sector->destroyMember(layout);
						}
					}
				}
				
			}
		}

		template<typename T, bool TS, typename Alloc>
		void insert(const Memory::SectorsArray<TS, Alloc>& array) noexcept { *getComponentContainer<T>() = array; }

		template<typename T, bool TS, typename Alloc>
		void insert(Memory::SectorsArray<TS, Alloc>&& array) noexcept { *getComponentContainer<T>() = std::move(array); }

	public:
		template<typename... Components>
		auto view(const EntitiesRanges& ranges) noexcept { return ArraysView<ThreadSafe, Allocator, true, Components...>{ this, ranges }; }

		template<typename... Components>
		auto view() noexcept { return ArraysView<ThreadSafe, Allocator, false, Components...>{this}; }

		template<typename... Components, typename Func>
		inline void forEachAsync(const std::vector<EntityId>& entities, Func&& func) noexcept requires(ThreadSafe) { for (auto entity : entities) withPinned<Components...>(entity, std::forward<Func>(func)); }

	public:
		// container handles
		template <class... Components>
		void reserve(uint32_t newCapacity) noexcept { (getComponentContainer<Components>()->reserve(newCapacity), ...); }

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
		 * \brief Pre-initialize container for multi-component sectors, or single-component with reserved capacity.
		 *
		 * Layout:\n
		 * \code
		 * 0x..[sector info]
		 * 0x..[component 1]
		 * 0x..[component 2]
		 * 0x..[    ...    ]
		 * 0x..[component N]
		 * \endcode
		 * \note Call this before any getContainer() calls if you want multi-component array.
		 */
		template<typename... ComponentTypes>
		void registerArray(uint32_t capacity = 0, Allocator allocator = {}) noexcept {
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

			auto sectorArray = Memory::SectorsArray<ThreadSafe, Allocator>::template create<ComponentTypes...>(std::move(allocator));
			sectorArray->reserve(capacity);
			mComponentsArrays.push_back(sectorArray);
			((mComponentsArraysMap[componentTypeId<ComponentTypes>()] = sectorArray), ...);
		}

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
		// entities
		bool contains(EntityId entityId) const noexcept { auto lock = ecss::Threads::sharedLock(mEntitiesMutex, ThreadSafe); return mEntities.contains(entityId); }

		EntityId takeEntity() noexcept { auto lock = ecss::Threads::uniqueLock(mEntitiesMutex, ThreadSafe); return mEntities.take(); }

		std::vector<EntityId> getAllEntities() const noexcept { auto lock = ecss::Threads::sharedLock(mEntitiesMutex, ThreadSafe); return mEntities.getAll(); }

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

		void destroyEntities(std::vector<EntityId>& entities) noexcept {
			if (entities.empty()) {
				return;
			}

			if constexpr (ThreadSafe) {
				std::vector<std::thread> destroyThreads;
				{
					auto lock = ecss::Threads::sharedLock(componentsArrayMapMutex, ThreadSafe);
					for (auto* array : mComponentsArrays) {
						//todo too much threads
						destroyThreads.emplace_back([array = array, entities = entities]() mutable
						{
							const auto layout = array->getLayout();

							auto arrLock = array->writeLock();
							prepareEntities(entities, array->template sectorsMapCapacity<false>());

							for (const auto sectorId : entities) {
								array->mPinsCounter.waitUntilSectorChangeable(sectorId);
								Memory::Sector::destroySector(array->template findSector<false>(sectorId), layout);
							}
						});
					}
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
					}
				}
				
				for (auto id : entities) {
					mEntities.erase(id);
				}
			}
		}

		template<typename T>
		void defragment() noexcept { if (auto container = getComponentContainer<T>()) container->defragment(); }

	private:
		void destroySector(EntityId entityId) noexcept {
			for (auto array : mComponentsArrays) {
				if constexpr (ThreadSafe) {
					auto lock = array->writeLock();
					array->mPinsCounter.waitUntilSectorChangeable(entityId);
					Memory::Sector::destroySector(array->template findSector<false>(entityId), array->getLayout());
				}
				else {
					Memory::Sector::destroySector(array->findSector(entityId), array->getLayout());
				}
			}
		}

		template<class... Ts, class F>
		void withPinned(EntityId entity, F&& f) noexcept requires(ThreadSafe) {
			auto pins = std::make_tuple(pinComponent<Ts>(entity)...);
			std::apply([&](auto&... pc) { std::forward<F>(f)(entity, pc.get()...); }, pins);
		}

		template <class... T>
		std::array<Memory::PinnedSector, sizeof...(T)> pinContainers(SectorId pinId) noexcept requires(ThreadSafe) {
			using PinnedArr = std::array<Memory::PinnedSector, sizeof...(T)>;
			return pinId != INVALID_ID ? PinnedArr{ Memory::PinnedSector(getComponentContainer<T>()->mPinsCounter, nullptr, pinId)... } : PinnedArr{};
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



	template<bool ThreadSafe, typename Allocator>
	struct TypeAccessInfo {
		Memory::SectorsArray<ThreadSafe, Allocator>* array = nullptr;
		uint32_t typeAliveMask = 0;
		uint16_t typeOffsetInSector = 0;
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
			using SectorItType = std::conditional_t<Ranged, typename Memory::SectorsArray<ThreadSafe, Allocator>::RangedIteratorAlive, typename Memory::SectorsArray<ThreadSafe, Allocator>::IteratorAlive>;
			if constexpr (Ranged) {
				return Iterator{ mArrays, SectorItType(mArrays[getIndex<T>()], mRanges, mArrays[getIndex<T>()]->template getLayoutData<T>().isAliveMask) };
			}
			else {
				return Iterator{ mArrays, SectorItType(mArrays[getIndex<T>()], 0, mLast, mArrays[getIndex<T>()]->template getLayoutData<T>().isAliveMask) };
			}
		}

		inline Iterator end() noexcept {
			using SectorItType = std::conditional_t<Ranged, typename Memory::SectorsArray<ThreadSafe, Allocator>::RangedIteratorAlive, typename Memory::SectorsArray<ThreadSafe, Allocator>::IteratorAlive>;
			return Iterator{ mArrays, SectorItType(mArrays[getIndex<T>()], mLast) };
		}

	public:
		explicit ArraysView(Registry<ThreadSafe, Allocator>* manager) noexcept requires (!Ranged) {
			initArrays<ComponentTypes..., T>(manager);
			
			if constexpr (ThreadSafe) {
				auto pinnedBack = mArrays[getIndex<T>()]->pinBackSector();
				auto index = pinnedBack.getId();
				mPins = manager->template pinContainers<T, ComponentTypes...>(index);
				mLast = index == INVALID_ID ? 0 : mArrays[getIndex<T>()]->template getSectorIndex<false>(index) + 1; // we pinned sector, but its size can be changed in other tread, use pinned index
			}
			else {
				mLast = mArrays[getIndex<T>()]->size();
			}
		}

		explicit ArraysView(Registry<ThreadSafe, Allocator>* manager, EntitiesRanges ranges = {}) noexcept requires (Ranged) : mRanges(std::move(ranges)) {
			initArrays<ComponentTypes..., T>(manager);
			auto sectorsArray = mArrays[getIndex<T>()];

			for (auto& range : mRanges.ranges) {
				range.first = static_cast<EntityId>(sectorsArray->findRightNearestSectorIndex(range.first));
				range.second = static_cast<EntityId>(sectorsArray->findRightNearestSectorIndex(range.second));
			}
			mRanges.mergeIntersections();

			if constexpr (ThreadSafe) {
				mPins = manager->template pinContainers<T, ComponentTypes...>(mRanges.empty() ? INVALID_ID : mRanges.back().second);
			}
			mLast = mRanges.empty() ? 0 : mRanges.back().second;
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
		std::array<Memory::SectorsArray<ThreadSafe, Allocator>*, sizeof...(ComponentTypes) + 1> mArrays;
		std::array<Memory::PinnedSector, sizeof...(ComponentTypes) + 1> mPins;
		size_t mLast = 0;
		EntitiesRanges mRanges;
	};
}
