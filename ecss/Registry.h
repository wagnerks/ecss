#pragma once

// ecss - Entity Component System with Sectors
// "Sectors" refers to the logic of storing components.
// Multiple components of different types can be stored in one memory location, which I've named a "sector."

#include <algorithm>
#include <array>
#include <shared_mutex>
#include <unordered_set>
#include <thread>
#include <tuple>
#include <vector>

#include <ecss/EntitiesRanges.h>
#include <ecss/memory/SectorsArray.h>
#include <ecss/memory/Sector.h>
#include <ecss/threads/SyncManager.h>

namespace ecss {

	template <bool Ranged, typename T, typename ...ComponentTypes>
	class ComponentArraysEntry;

	class Registry final {
		template <bool Ranged, typename T, typename ...ComponentTypes>
		friend class ComponentArraysEntry;
		friend class SectorsArray;
	public:
		template<typename T>
		ECSType componentTypeId() noexcept { return mReflectionHelper.getTypeId<T>(); }

	public:
		Registry(const Registry& other) noexcept = delete;
		Registry& operator=(const Registry& other) noexcept = delete;
		Registry(Registry&& other) noexcept = delete;
		Registry& operator=(Registry&& other) noexcept = delete;

	public:
		Registry() noexcept = default;
		~Registry() noexcept { for (auto array : mComponentsArrays) delete array; }
		void update() noexcept { std::shared_lock lock(componentsArrayMapMutex); for (auto array : mComponentsArrays) array->processPendingErases(); }

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
		void registerArray(uint32_t capacity = 0) noexcept {
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

			auto sectorArray = Memory::SectorsArray<>::create<ComponentTypes...>(capacity);
			mComponentsArrays.push_back(sectorArray);
			((mComponentsArraysMap[componentTypeId<ComponentTypes>()] = sectorArray), ...);
		}

		

		template <class T>
		bool hasComponent(EntityId entity, bool sync = true) noexcept {
			if (auto container = getComponentContainer<T>()) {
				if (auto sector = container->pinSector(entity, sync)) {
					return sector->isAlive(container->template getLayoutData<T>().isAliveMask);
				}
			}
			return false;
		}

		template<class T>
		struct PinnedComponent {
			PinnedComponent() = default;
			PinnedComponent(Memory::SectorsArray<>::PinnedSector&& pinnedSector, T* ptr) : sec(std::move(pinnedSector)), ptr(ptr) {}

			T* get() const noexcept { return ptr; }

			T* operator->() const noexcept { return ptr; }
			T& operator* () const noexcept { return *ptr; }
			explicit operator bool() const noexcept { return ptr != nullptr; }
			void release() { sec.release(); ptr = nullptr; }

		private:
			Memory::SectorsArray<>::PinnedSector sec; // RAII pin сектора
			T* ptr = nullptr;
		};

		template<class T>
		[[nodiscard]] PinnedComponent<T> getPinnedComponent(EntityId entity, bool sync = true) noexcept {
			auto* container = getComponentContainer<T>();
			auto pinnedSector = container->pinSector(entity, sync);
			if (!pinnedSector) { return {}; }

			auto component = Memory::Sector::getComponentFromSector<T>(pinnedSector.get(), container->getLayout());
			if (!component) { return {}; }

			return { std::move(pinnedSector), component };
		}

		template<class... Ts, class F>
		void withPinned(EntityId entity, F&& f, bool sync = true) noexcept {
			auto pins = std::tuple{ getPinnedComponent<Ts>(entity, sync)... };
			f(entity, (std::get<PinnedComponent<Ts>>(pins).get())...);
		}

		// you can emplace component into sector using initializer list
		template <class T, class ...Args>
		T* addComponent(EntityId entity, bool sync, Args&&... args) noexcept {
			auto container = getComponentContainer<T>();
			if constexpr (sizeof...(Args) == 1 && (std::is_same_v<std::remove_cvref_t<Args>, T> && ...)) {
				return container->insert(entity, std::forward<Args>(args)..., sync);
			}
			else {
				return container->template emplace<T>(entity, sync, std::forward<Args>(args)...);
			}
		}

		// you can emplace component into sector using initializer list
		template <class T, class ...Args>
		T* addComponent(EntityId entity, Args&&... args) noexcept {
			return addComponent<T>(entity, true, std::forward<Args>(args)...);
		}

		template<typename T>
		void copyComponentsArrayToRegistry(const Memory::SectorsArray<>& array) noexcept { *getComponentContainer<T>() = array; }

		template <class T>
		void destroyComponent(EntityId entity) noexcept {
			if (auto container = getComponentContainer<T>()) {
				container->template destroyMember<T>(entity);
			}
		}

		template <class T>
		void destroyComponent(std::vector<EntityId>& entities) noexcept {
			if (auto container = getComponentContainer<T>()) {
				container->template destroyMembers<T>(entities, true, true);
			}
		}

		void destroyComponents(EntityId entityId) const noexcept {
			for (auto array : mComponentsArrays) {
				Memory::Sector::destroyMembers(array->findSector(entityId), array->getLayout());
			}
		}

	public:
		template<typename... Components>
		ComponentArraysEntry<true, Components...> forEach(const EntitiesRanges& ranges, bool lock = true) noexcept { return ComponentArraysEntry<true, Components...>{ this, ranges, lock }; }

		template<typename... Components>
		ComponentArraysEntry<false, Components...> forEach(bool lock = true) noexcept { return ComponentArraysEntry<false, Components...>{this, lock}; }

		template<typename... Components, typename Func>
		inline void forEachAsync(const std::vector<EntityId>& entities, const Func& func) noexcept {
			for (auto entity : entities) {
				withPinned<Components...>(entity, func, true);
			}
		}

	public:
		// container handles
		template <class... Components>
		void reserve(uint32_t newCapacity, bool sync = true) noexcept { (getComponentContainer<Components>()->reserve(newCapacity, sync), ...); }

		void clear(bool sync = true) noexcept {
			{
				decltype(mComponentsArrays) arrays;
				{
					std::shared_lock lock(componentsArrayMapMutex);
					arrays = mComponentsArrays;
				}

				for (auto* array : arrays) {
					array->clear(sync);
				}
			}

			std::unique_lock lock(mEntitiesMutex);
			mEntities.clear();
		}

		void defragment(bool sync = true) noexcept {
			decltype(mComponentsArrays) arrays;
			{
				auto lock = ecss::Threads::uniqueLock(componentsArrayMapMutex, sync);
				arrays = mComponentsArrays;
			}

			for (auto* array : arrays) {
				array->defragment(sync);
			}
		}

		template <class T>
		Memory::SectorsArray<>* getComponentContainer() noexcept {
			const auto componentType = componentTypeId<T>();
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
		bool contains(EntityId entityId, bool sync = true) const noexcept { auto lock = ecss::Threads::sharedLock(mEntitiesMutex, sync); return mEntities.contains(entityId); }

		EntityId takeEntity(bool sync = true) noexcept { auto lock = ecss::Threads::uniqueLock(mEntitiesMutex, sync); return mEntities.take(); }

		std::vector<EntityId> getAllEntities(bool sync = true) const noexcept { auto lock = ecss::Threads::sharedLock(mEntitiesMutex, sync); return mEntities.getAll(); }

		void destroyEntity(EntityId entityId, bool sync = true) noexcept {
			if (!contains(entityId, sync)) {
				return;
			}

			{
				auto lock = ecss::Threads::uniqueLock(mEntitiesMutex, sync);
				mEntities.erase(entityId);
			}

			destroyComponents(entityId);
		}

		void destroyEntities(std::vector<EntityId>& entities, bool sync = true) noexcept {
			if (entities.empty()) {
				return;
			}
			std::ranges::sort(entities);
			std::vector<std::thread> destroyThreads;
			{
				auto lock = ecss::Threads::sharedLock(componentsArrayMapMutex, sync);
				for (auto* array : mComponentsArrays) {
					destroyThreads.emplace_back([&, array = array]() { array->clearSectors(entities, false, true); });
				}
			}

			{
				auto lock = ecss::Threads::uniqueLock(mEntitiesMutex, sync);
				for (auto id : entities) {
					mEntities.erase(id);
				}
			}

			for (auto& thread : destroyThreads) {
				thread.join();
			}
		}

	private:
		template <class... T>
		std::array<Memory::SectorsArray<>::PinnedSector, sizeof...(T)> pinContainers(SectorId pinId) noexcept {
			std::array<Memory::SectorsArray<>::PinnedSector, sizeof...(T)> pins;
			if (pinId == INVALID_ID) {
				return pins;
			}
			size_t i = 0;
			((pins[i++] = std::move(Memory::SectorsArray<>::PinnedSector(getComponentContainer<T>(), nullptr, pinId))), ...);

			return pins;
		}

	private:
		Memory::ReflectionHelper mReflectionHelper;

		EntitiesRanges mEntities;

		std::vector<Memory::SectorsArray<>*> mComponentsArraysMap;
		std::vector<Memory::SectorsArray<>*> mComponentsArrays;

		//non copyable
		mutable std::shared_mutex mEntitiesMutex;
		mutable std::shared_mutex componentsArrayMapMutex;
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
			inline ComponentType* getComponent(EntityId sectorId) noexcept { return Iterator::getComponent<ComponentType>((getTypeAccessInfo<ComponentType>().isMain ? *mIterator : getTypeAccessInfo<ComponentType>().array->findSector(sectorId, false)), getTypeAccessInfo<ComponentType>()); }

			template<typename ComponentType>
			inline static ComponentType* getComponent(Memory::Sector* sector, const TypeAccessInfo& meta) noexcept { return sector && (sector->isAliveData & meta.typeAliveMask) ? reinterpret_cast<ComponentType*>(reinterpret_cast<char*>(sector) + meta.typeOffsetInSector) : nullptr; }

		private:
			std::conditional_t<Ranged, Memory::SectorsArray<>::RangedIteratorAlive, Memory::SectorsArray<>::IteratorAlive> mIterator;

			std::tuple<TypeAccessInfo, decltype((void)sizeof(ComponentTypes), TypeAccessInfo{})...> mTypeAccessInfo;
		};

		inline Iterator begin() noexcept {
			if constexpr (Ranged) {
				return Iterator{ mArrays, Memory::SectorsArray<>::RangedIteratorAlive(mArrays[getIndex<T>()], mRanges, mArrays[getIndex<T>()]->template getLayoutData<T>().isAliveMask) };
			}
			else {
				return Iterator{ mArrays, Memory::SectorsArray<>::IteratorAlive(mArrays[getIndex<T>()], 0, mArrays[getIndex<T>()]->getSectorIndex(mPins[getIndex<T>()].getId(), false) + 1, mArrays[getIndex<T>()]->template getLayoutData<T>().isAliveMask) };
			}
		}

		inline Iterator end() noexcept {
			if constexpr (Ranged) {
				return Iterator{ mArrays, Memory::SectorsArray<>::RangedIteratorAlive(mArrays[getIndex<T>()], mRanges.empty() ? 0 : mRanges.back().second) };
			}
			else {
				return Iterator{ mArrays, Memory::SectorsArray<>::IteratorAlive(mArrays[getIndex<T>()], mArrays[getIndex<T>()]->getSectorIndex(mPins[getIndex<T>()].getId(), false) + 1) };
			}
		}

	public:
		explicit ComponentArraysEntry(Registry* manager, bool lock = true) noexcept requires (!Ranged) {
			initArrays<ComponentTypes..., T>(manager);
			if (lock) {
				auto pinnedBack = mArrays[getIndex<T>()]->pinBackSector();
				auto index = pinnedBack.getId();
				mPins = manager->pinContainers<T, ComponentTypes...>(index);
			}
		}

		explicit ComponentArraysEntry(Registry* manager, EntitiesRanges ranges = {}, bool lock = true) noexcept requires (Ranged) : mRanges(std::move(ranges)) {
			initArrays<ComponentTypes..., T>(manager);

			auto sectorsArray = mArrays[getIndex<T>()];
			for (auto& range : mRanges.ranges) {
				range.first = static_cast<EntityId>(sectorsArray->findRightNearestSectorIndex(range.first));
				range.second = static_cast<EntityId>(sectorsArray->findRightNearestSectorIndex(range.second));
			}

			mRanges.mergeIntersections();

			mPins = lock ? manager->pinContainers<T, ComponentTypes...>(mRanges.back().second) : decltype(mPins){};
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

		std::array<ecss::Memory::SectorsArray<>::PinnedSector, sizeof...(ComponentTypes) + 1> mPins;

		EntitiesRanges mRanges;
	};
}
