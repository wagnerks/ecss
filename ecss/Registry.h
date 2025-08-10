#pragma once

// ecss - Entity Component System with Sectors
// "Sectors" refers to the logic of storing components.
// Multiple components of different types can be stored in one memory location, which I've named a "sector."

#include <algorithm>
#include <array>
#include <deque>
#include <map>
#include <set>
#include <shared_mutex>
#include <stdbool.h>
#include <unordered_set>

#include <ecss/EntitiesRanges.h>
#include <ecss/memory/SectorsArray.h>
#include <ecss/threads/SyncManager.h>

namespace ecss {
	template <bool Ranged, typename T, typename ...ComponentTypes>
	class ComponentArraysEntry;
	class Registry final {
		template <bool Ranged, typename T, typename ...ComponentTypes>
		friend class ComponentArraysEntry;

	public:
		Registry(const Registry& other) noexcept = delete;
		Registry& operator=(const Registry& other) noexcept = delete;
		Registry(Registry&& other) noexcept = delete;
		Registry& operator=(Registry&& other) noexcept = delete;

	public:
		Registry() noexcept = default;
		~Registry() noexcept { for (auto array : mComponentsArrays) delete array; }

	public:
		/*
		 * this function allows to preinit container
		 * if you want container which stores multiple components in one memory sector
		 * 0x..[sector info]
		 * 0x..[component 1]
		 * 0x..[component 2]
		 * 0x..[    ...    ]
		 * it should be called before any getContainer calls
		*/
		template<typename... ComponentTypes>
		void registerArray(uint32_t capacity = 0, uint32_t chunkSize = 8192) noexcept {
			std::unique_lock lock(componentsArrayMapMutex);
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

			auto sectorArray = Memory::SectorsArray<>::create<ComponentTypes...>(mReflectionHelper, capacity, chunkSize);
			mComponentsArrays.push_back(sectorArray);
			((mComponentsArraysMap[componentTypeId<ComponentTypes>()] = sectorArray), ...);
		}

		template<typename T>
		ECSType componentTypeId() noexcept {	return mReflectionHelper.getTypeId<T>(); }

		template <class T>
		bool hasComponent(EntityId entity) noexcept {
			if (auto container = getComponentContainer<T>()) {
				auto lock = container->readLock();
				if (auto sector = container->findSector(entity, ecss::SyncType::NONE)) {
					return sector->isAlive(container->getLayoutData<T>().isAliveMask);
				}
			}
			return false;
		}

		template <class T>
		T* getComponent(EntityId entity) noexcept {
			auto container = getComponentContainer<T>();
			auto lock = container->readLock();
			return Memory::Sector::getComponentFromSector<T>(container->findSector(entity, ecss::SyncType::NONE), container->getLayout());
		}

		template <class T>
		T* getComponentNotSafe(EntityId entity) noexcept {
			auto container = getComponentContainer<T>();
			return Memory::Sector::getComponentFromSector<T>(container->findSector(entity, ecss::SyncType::NONE), container->getLayout());
		}

		// you can emplace component into sector using initializer list
		template <class T, class ...Args>
		T* addComponent(EntityId entity, ecss::SyncType syncType, Args&&... args) noexcept {
			auto container = getComponentContainer<T>();
			if constexpr (sizeof...(Args) == 1 && (std::is_same_v<std::remove_cvref_t<Args>, T> && ...)) {
				return const_cast<T*>(container->insert(entity, std::forward<Args>(args)...), syncType);
			}
			else {
				return container->emplace<T>(entity, syncType, std::forward<Args>(args)...);
			}
		}

		// you can emplace component into sector using initializer list
		template <class T, class ...Args>
		T* addComponent(EntityId entity, Args&&... args) noexcept {
			auto container = getComponentContainer<T>();
			if constexpr (sizeof...(Args) == 1 && (std::is_same_v<std::remove_cvref_t<Args>, T> && ...)) {
				return const_cast<T*>(container->insert(entity, std::forward<Args>(args)...));
			}
			else {
				return container->emplace<T>(entity, ecss::SyncType::UNIQUE, std::forward<Args>(args)...);
			}			
		}

		// you can emplace component into sector using initializer list
		template <class T>
		T* addComponent(EntityId entity, T&& component, ecss::SyncType syncType = ecss::SyncType::NONE) noexcept {
			auto container = getComponentContainer<T>();
			return container->insert(entity, std::forward<T>(component), syncType);
		}

		template<typename T>
		void copyComponentsArrayToRegistry(const Memory::SectorsArray<>& array) noexcept {
			auto cont = getComponentContainer<T>();
			*cont = array;
		}

		template <class T>
		void destroyComponent(EntityId entity) noexcept {
			if (auto container = getComponentContainer<T>()) {
				Memory::SectorArrayUtils::destroyMember<T>(container, entity);
			}
		}

		template <class T>
		void destroyComponent(std::vector<EntityId>& entities) noexcept {
			if (auto container = getComponentContainer<T>()) {
				Memory::SectorArrayUtils::destroyMembers<T>(container, entities);
			}
		}

		void destroyComponents(EntityId entityId) const noexcept {
			for (auto array : mComponentsArrays) {
				Memory::Sector::destroyMembers(array->findSector(entityId), array->getLayout());
			}
		}

	public:
		// iterator helpers
		template<typename... Components, typename Func>
		inline void forEachAsync(const std::vector<EntityId>& entities, const Func& func) noexcept {
			for (auto entity : entities) {
				auto lock = containersLock<Components...>();
				func(entity, getComponentNotSafe<Components>(entity)...);
			}
		}

		template<typename... Components>
		ComponentArraysEntry<true, Components...> forEach(const EntitiesRanges& ranges, bool lock = true) noexcept { return ComponentArraysEntry<true, Components...>{ this, ranges, lock }; }

		template<typename... Components>
		ComponentArraysEntry<false, Components...> forEach(bool lock = true) noexcept { return ComponentArraysEntry<false, Components...>{this, lock}; }

	public:
		// container handles
		template <class... Components>
		void reserve(uint32_t newCapacity) noexcept { (getComponentContainer<Components>()->reserve(newCapacity), ...); }

		void clear() noexcept {
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

		void defragment() noexcept {
			decltype(mComponentsArrays) arrays;
			{
				std::shared_lock lock(componentsArrayMapMutex);
				arrays = mComponentsArrays;
			}
			
			for (auto* array : arrays) {
				array->defragment();
			}
		}

		template <class T>
		Memory::SectorsArray<>* getComponentContainer() noexcept {
			const ECSType componentType = componentTypeId<T>();
			{
				std::shared_lock readLock(componentsArrayMapMutex);
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
		bool contains(EntityId entityId) const noexcept { std::shared_lock lock(mEntitiesMutex); return mEntities.contains(entityId); }

		EntityId takeEntity() noexcept { std::unique_lock lock(mEntitiesMutex); return mEntities.take(); }

		std::vector<EntityId> getAllEntities() const noexcept { std::shared_lock lock(mEntitiesMutex); return mEntities.getAll(); }

		void destroyEntity(EntityId entityId) noexcept {
			if (!contains(entityId)) {
				return;
			}

			{
				std::unique_lock lock(mEntitiesMutex);
				mEntities.erase(entityId);
			}

			destroyComponents(entityId);
		}

		void destroyEntities(std::vector<EntityId>& entities) noexcept {
			if (entities.empty()) {
				return;
			}
			std::ranges::sort(entities);
			std::vector<std::thread> destroyThreads;
			{
				std::shared_lock lock(componentsArrayMapMutex);
				for (auto* array : mComponentsArrays) {
					destroyThreads.emplace_back(std::thread([&, array = array]() { Memory::SectorArrayUtils::destroyAllMembers(array, entities, false); }));
				}
			}

			{
				std::unique_lock lock(mEntitiesMutex);
				for (auto id : entities) {
					mEntities.erase(id);
				}
			}

			for (auto& thread : destroyThreads) {
				thread.join();
			}
		}

	public:
		// thread safety helpers

		template <class T>
		std::shared_mutex* getSectorsArrayMutex() noexcept { return &getComponentContainer<T>()->getMutex(); }

	
		struct Lock {
			Lock(std::shared_mutex* mutex)	noexcept {
				sharedLock = new std::shared_lock(*mutex);
			}

			~Lock() noexcept {
				delete sharedLock;
			}

			std::shared_lock<std::shared_mutex>* sharedLock = nullptr;
		};


		template<typename T>
		void addMutex(std::unordered_map<std::shared_mutex*, bool>& mutexes) noexcept {
			mutexes[getSectorsArrayMutex<T>()] |= std::is_const_v<T>;
		}

		template <class... T>
		std::vector<Lock> containersLock() noexcept {
			std::unordered_map<std::shared_mutex*, bool> mutexes;
			std::vector<Lock> locks;

			(addMutex<T>(mutexes), ...);

			locks.reserve(mutexes.size());
			for (auto& [mtxPtr, constness] : mutexes) {
				locks.emplace_back(mtxPtr);
			}

			return locks;
		}

		//todo separate locks for components, container lock provided safety of container structure, not component data
		template <class T>
		Lock containerLock() noexcept {
			return {getSectorsArrayMutex<T>()};
		}

	private:
		Memory::ReflectionHelper mReflectionHelper;

		EntitiesRanges mEntities;

		std::vector<Memory::SectorsArray<>*> mComponentsArraysMap;
		std::vector<Memory::SectorsArray<>*> mComponentsArrays;

		//non copyable
		mutable std::shared_mutex mEntitiesMutex;
		std::shared_mutex componentsArrayMapMutex;
	};

	/*
		an object with selected components, which provided ability to iterate through entities like it is the container of tuple<component1,component2,component3>
		first component type in template is the "main" one, because components stores in separate containers, the first component parent container chosen for iterating

		component1Cont		component2Cont		component3Cont
		0 [	data	]	[	data	]	[	data	]
		- [	null	]	[	data	]	[	data	]
		1 [	data	]	[	data	]	[	data	]
		2 [	data	]	[	data	]	[	null	]
		- [	null	]	[	null	]	[	data	]
		3 [	data	]	[	null	]	[	data	]

		it will iterate through first 0,1,2,3... container elements

		ATTENTION

		if componentContainer has multiple components in it, it will iterate through sectors, and may return nullptr for "main" component type
		so better, if you want to merge multiple types in one sector, always create all components for sector
	*/

	struct TypeAccessInfo {
		Memory::SectorsArray<>* array = nullptr;
		uint32_t typeAliveMask = 0;
		uint16_t typeOffsetInSector = 0;
		bool isMain = false;
	};

	template <bool Ranged, typename T, typename ...ComponentTypes>
	class ComponentArraysEntry final {
	public:
		class Iterator {
		public:
			using iterator_category = std::forward_iterator_tag;
			using value_type = std::tuple<EntityId, T*, ComponentTypes*...>;
			using difference_type = std::ptrdiff_t;
			using pointer = value_type*;
			using reference = value_type&;

		public:
			using SectorArrays = std::array<Memory::SectorsArray<>*, sizeof...(ComponentTypes) + 1>;

			Iterator(const SectorArrays& arrays, Memory::SectorsArray<>::RangedIteratorAlive iterator) noexcept requires (Ranged) : mIterator(iterator) { initTypeAccessInfo<T, ComponentTypes...>(arrays); }
			Iterator(const SectorArrays& arrays, Memory::SectorsArray<>::IteratorAlive iterator) noexcept requires (!Ranged) : mIterator(iterator) { initTypeAccessInfo<T, ComponentTypes...>(arrays); }

			inline value_type operator*() noexcept { return { (*mIterator)->id, reinterpret_cast<T*>(reinterpret_cast<char*>(*mIterator) + getTypeAccessInfo<T>().typeOffsetInSector), getComponent<ComponentTypes>((*mIterator)->id)... }; }

			inline Iterator& operator++() noexcept { return ++mIterator, * this; }

			bool operator!=(const Iterator& other) const noexcept { return mIterator != other.mIterator; }
			bool operator==(const Iterator& other) const noexcept { return mIterator == other.mIterator; }

		private:
			template <typename ComponentType>
			inline constexpr TypeAccessInfo& getTypeAccessInfo() noexcept { return std::get<getIndex<ComponentType>()>(mTypeAccessInfo); }

			template<typename... Types>
			inline void initTypeAccessInfo(const SectorArrays& arrays) noexcept { (initTypeAccessInfoImpl<Types>(arrays[getIndex<Types>()]), ...); }

			template<typename ComponentType>
			inline void initTypeAccessInfoImpl(Memory::SectorsArray<>* sectorArray) noexcept {
				auto& info = getTypeAccessInfo<ComponentType>();
				info.array = sectorArray;
				info.typeAliveMask = sectorArray->getLayoutData<ComponentType>().isAliveMask;
				info.typeOffsetInSector = sectorArray->getLayoutData<ComponentType>().offset;
				info.isMain = sectorArray == getTypeAccessInfo<T>().array;
			}

			template<typename ComponentType>
			inline ComponentType* getComponent(EntityId sectorId) noexcept { return Iterator::getComponent<ComponentType>((getTypeAccessInfo<ComponentType>().isMain ? *mIterator : getTypeAccessInfo<ComponentType>().array->findSector(sectorId, ecss::SyncType::NONE)), getTypeAccessInfo<ComponentType>()); }

			template<typename ComponentType>
			inline ComponentType* getComponent(Memory::Sector* sector, const TypeAccessInfo& meta) noexcept { return sector && (sector->isAliveData & meta.typeAliveMask) ? reinterpret_cast<ComponentType*>(reinterpret_cast<char*>(sector) + meta.typeOffsetInSector) : nullptr; }

		private:
			std::conditional_t<Ranged, Memory::SectorsArray<>::RangedIteratorAlive, Memory::SectorsArray<>::IteratorAlive> mIterator;

			std::tuple<TypeAccessInfo, decltype((void)sizeof(ComponentTypes), TypeAccessInfo{})...> mTypeAccessInfo;
		};

		inline Iterator begin() noexcept {
			if constexpr (Ranged) {
				return Iterator{ mArrays, Memory::SectorsArray<>::RangedIteratorAlive(mArrays[getIndex<T>()], mRanges, mArrays[getIndex<T>()]->getLayoutData<T>().isAliveMask) };
			}
			else {
				return Iterator{ mArrays, Memory::SectorsArray<>::IteratorAlive(mArrays[getIndex<T>()], 0, mArrays[getIndex<T>()]->size(ecss::SyncType::NONE), mArrays[getIndex<T>()]->getLayoutData<T>().isAliveMask) };
			}
		}

		inline Iterator end() noexcept {
			if constexpr (Ranged) {
				return Iterator{ mArrays, Memory::SectorsArray<>::RangedIteratorAlive(mArrays[getIndex<T>()], mRanges.back().second) };
			}
			else {
				return Iterator{ mArrays, Memory::SectorsArray<>::IteratorAlive(mArrays[getIndex<T>()], mArrays[getIndex<T>()]->size(ecss::SyncType::NONE)) };
			}
		}

	public:
		explicit ComponentArraysEntry(Registry* manager, bool lock = true) noexcept requires (!Ranged) {
			initArrays<ComponentTypes..., T>(manager);

			mLocks = lock ? manager->containersLock<T, ComponentTypes...>() : std::vector<Registry::Lock>{};
		}

		explicit ComponentArraysEntry(Registry* manager, const EntitiesRanges& ranges = {}, bool lock = true) noexcept requires (Ranged) : mRanges(ranges) {
			initArrays<ComponentTypes..., T>(manager);

			mLocks = lock ? manager->containersLock<T, ComponentTypes...>() : std::vector<Registry::Lock>{};

			auto sectorsArray = mArrays[getIndex<T>()];
			for (auto& range : mRanges.ranges) {
				range.first = static_cast<EntityId>(Memory::SectorArrayUtils::findRightNearestSectorIndex(sectorsArray, range.first));
				range.second = static_cast<EntityId>(Memory::SectorArrayUtils::findRightNearestSectorIndex(sectorsArray, range.second));
			}

			mRanges.mergeIntersections();
		}

		bool valid() const noexcept {
			if constexpr (Ranged) {
				return !mRanges.empty() || !mArrays[getIndex<T>()]->empty();
			}
			else {
				return !mArrays[getIndex<T>()]->empty();
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
		void initArrays(Registry* registry) noexcept {
			static_assert(types::areUnique<Types...>(), "Duplicates detected in types");
			((mArrays[getIndex<Types>()] = registry->getComponentContainer<Types>()), ...);
		}

	private:
		std::array<Memory::SectorsArray<>*, sizeof...(ComponentTypes) + 1> mArrays;
		std::vector<Registry::Lock> mLocks;

		EntitiesRanges mRanges;
	};
}