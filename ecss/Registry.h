#pragma once

// ecss - Entity Component System with Sectors
// "Sectors" refers to the logic of storing components.
// Multiple components of different types can be stored in one memory location, which I've named a "sector."

#include <algorithm>
#include <array>
#include <shared_mutex>
#include <thread>
#include <tuple>
#include <vector>

#include <ecss/EntitiesRanges.h>
#include <ecss/memory/SectorsArray.h>
#include <ecss/memory/Sector.h>
#include <ecss/threads/SyncManager.h>

namespace ecss {

	template <bool ThreadSafe, typename Allocator, bool Ranged, typename T, typename ...ComponentTypes>
	class ComponentArraysEntry;

	template<bool ThreadSafe = true, typename Allocator = Memory::ChunksAllocator<8192>>
	class Registry final {
		template <bool TS, typename Alloc, bool Ranged, typename T, typename ...ComponentTypes>
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
		void registerArray(uint32_t capacity = 0, Allocator allocator = {}) noexcept {
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

			auto sectorArray = Memory::SectorsArray<ThreadSafe, Allocator>::template create<ComponentTypes...>(capacity, std::move(allocator));
			mComponentsArrays.push_back(sectorArray);
			((mComponentsArraysMap[componentTypeId<ComponentTypes>()] = sectorArray), ...);
		}

		

		template <class T>
		bool hasComponent(EntityId entity, bool sync = ThreadSafe) noexcept {
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
			PinnedComponent(typename Memory::SectorsArray<ThreadSafe, Allocator>::PinnedSector&& pinnedSector, T* ptr) : sec(std::move(pinnedSector)), ptr(ptr) {}

			T* get() const noexcept { return ptr; }

			T* operator->() const noexcept { return ptr; }
			T& operator* () const noexcept { return *ptr; }
			explicit operator bool() const noexcept { return ptr != nullptr; }
			void release() { sec.release(); ptr = nullptr; }

		private:
			typename Memory::SectorsArray<ThreadSafe, Allocator>::PinnedSector sec; // RAII pin сектора
			T* ptr = nullptr;
		};

		template<class T>
		[[nodiscard]] PinnedComponent<T> getPinnedComponent(EntityId entity, bool sync = ThreadSafe) noexcept {
			auto* container = getComponentContainer<T>();
			auto pinnedSector = container->pinSector(entity, sync);
			if (!pinnedSector) { return {}; }

			auto component = Memory::Sector::getComponentFromSector<T>(pinnedSector.get(), container->getLayout());
			if (!component) { return {}; }

			return { std::move(pinnedSector), component };
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
		T* addComponent(EntityId entity, Args&&... args) noexcept {	return addComponent<T>(entity, ThreadSafe, std::forward<Args>(args)...); }

		template<typename T, bool TS, typename Alloc>
		void copyComponentsArrayToRegistry(const Memory::SectorsArray<TS, Alloc>& array) noexcept { *getComponentContainer<T>() = array; }

		template <class T>
		void destroyComponent(EntityId entity) noexcept { if (auto container = getComponentContainer<T>()) container->template destroyMember<T>(entity); }

		template <class T>
		void destroyComponent(std::vector<EntityId>& entities) noexcept { if (auto container = getComponentContainer<T>()) container->template destroyMembers<T>(entities, true, true); }

		void destroyComponents(EntityId entityId) const noexcept { for (auto array : mComponentsArrays) Memory::Sector::destroyMembers(array->findSector(entityId), array->getLayout()); }

	public:
		template<typename... Components>
		auto forEach(const EntitiesRanges& ranges) noexcept { return ComponentArraysEntry<ThreadSafe, Allocator, true, Components...>{ this, ranges }; }

		template<typename... Components>
		auto forEach() noexcept { return ComponentArraysEntry<ThreadSafe, Allocator, false, Components...>{this}; }

		template<typename... Components, typename Func>
		inline void forEachAsync(const std::vector<EntityId>& entities, const Func& func) noexcept { for (auto entity : entities) withPinned<Components...>(entity, func, true); }

	public:
		// container handles
		template <class... Components>
		void reserve(uint32_t newCapacity, bool sync = ThreadSafe) noexcept { (getComponentContainer<Components>()->reserve(newCapacity, sync), ...); }

		void clear(bool sync = ThreadSafe) noexcept {
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

		void defragment(bool sync = ThreadSafe) noexcept {
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
		Memory::SectorsArray<ThreadSafe, Allocator>* getComponentContainer() noexcept {
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
		bool contains(EntityId entityId, bool sync = ThreadSafe) const noexcept { auto lock = ecss::Threads::sharedLock(mEntitiesMutex, sync); return mEntities.contains(entityId); }

		EntityId takeEntity(bool sync = ThreadSafe) noexcept { auto lock = ecss::Threads::uniqueLock(mEntitiesMutex, sync); return mEntities.take(); }

		std::vector<EntityId> getAllEntities(bool sync = ThreadSafe) const noexcept { auto lock = ecss::Threads::sharedLock(mEntitiesMutex, sync); return mEntities.getAll(); }

		void destroyEntity(EntityId entityId, bool sync = ThreadSafe) noexcept {
			if (!contains(entityId, sync)) {
				return;
			}

			{
				auto lock = ecss::Threads::uniqueLock(mEntitiesMutex, sync);
				mEntities.erase(entityId);
			}

			destroyComponents(entityId);
		}

		void destroyEntities(std::vector<EntityId>& entities, bool sync = ThreadSafe) noexcept {
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
		template<typename T>
		void defragment(bool sync = ThreadSafe) noexcept { if (auto container = getComponentContainer<T>()) container->defragment(sync); }

	private:
		template<class... Ts, class F>
		void withPinned(EntityId entity, F&& f, bool sync = ThreadSafe) noexcept { auto pins = std::tuple{ getPinnedComponent<Ts>(entity, sync)... }; f(entity, (std::get<PinnedComponent<Ts>>(pins).get())...); }

		template <class... T>
		std::array<typename Memory::SectorsArray<ThreadSafe, Allocator>::PinnedSector, sizeof...(T)> pinContainers(SectorId pinId) noexcept {
			std::array<typename Memory::SectorsArray<ThreadSafe, Allocator>::PinnedSector, sizeof...(T)> pins;
			if (pinId == INVALID_ID) {
				return pins;
			}
			size_t i = 0;
			((pins[i++] = std::move(Memory::SectorsArray<ThreadSafe, Allocator>::PinnedSector(getComponentContainer<T>(), nullptr, pinId))), ...);

			return pins;
		}

	private:
		Memory::ReflectionHelper mReflectionHelper;

		EntitiesRanges mEntities;

		std::vector<Memory::SectorsArray<ThreadSafe, Allocator>*> mComponentsArraysMap;
		std::vector<Memory::SectorsArray<ThreadSafe, Allocator>*> mComponentsArrays;

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

	template<bool ThreadSafe, typename Allocator>
	struct TypeAccessInfo {
		Memory::SectorsArray<ThreadSafe, Allocator>* array = nullptr;
		uint32_t typeAliveMask = 0;
		uint16_t typeOffsetInSector = 0;
		bool isMain = false;
	};

	template <bool ThreadSafe, typename Allocator, bool Ranged, typename T, typename ...ComponentTypes>
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
			inline ComponentType* getComponent(EntityId sectorId) noexcept { return Iterator::getComponent<ComponentType>((getTypeAccessInfo<ComponentType>().isMain ? *mIterator : getTypeAccessInfo<ComponentType>().array->findSector(sectorId, false)), getTypeAccessInfo<ComponentType>()); }

			template<typename ComponentType>
			inline static ComponentType* getComponent(Memory::Sector* sector, const TypeAccessInfo<ThreadSafe, Allocator>& meta) noexcept { return sector && (sector->isAliveData & meta.typeAliveMask) ? reinterpret_cast<ComponentType*>(reinterpret_cast<char*>(sector) + meta.typeOffsetInSector) : nullptr; }

		private:
			std::conditional_t<Ranged, typename Memory::SectorsArray<ThreadSafe, Allocator>::RangedIteratorAlive, typename Memory::SectorsArray<ThreadSafe, Allocator>::IteratorAlive> mIterator;

			std::tuple<TypeAccessInfo<ThreadSafe, Allocator>, decltype((void)sizeof(ComponentTypes), TypeAccessInfo<ThreadSafe, Allocator>{})...> mTypeAccessInfo;
		};

		inline Iterator begin() noexcept {
			if constexpr (Ranged) {
				return Iterator{ mArrays, Memory::SectorsArray<ThreadSafe, Allocator>::RangedIteratorAlive(mArrays[getIndex<T>()], mRanges, mArrays[getIndex<T>()]->template getLayoutData<T>().isAliveMask) };
			}
			else {
				auto id = mPins[getIndex<T>()].getId();
				auto idx = id == INVALID_ID ? 0 : mArrays[getIndex<T>()]->getSectorIndex(id, false) + 1;
				return Iterator{ mArrays, Memory::SectorsArray<ThreadSafe, Allocator>::IteratorAlive(mArrays[getIndex<T>()], 0, idx, mArrays[getIndex<T>()]->template getLayoutData<T>().isAliveMask) };
			}
		}

		inline Iterator end() noexcept {
			if constexpr (Ranged) {
				return Iterator{ mArrays, Memory::SectorsArray<ThreadSafe, Allocator>::RangedIteratorAlive(mArrays[getIndex<T>()], mRanges.empty() ? 0 : mRanges.back().second) };
			}
			else {
				auto id = mPins[getIndex<T>()].getId();
				auto idx = id == INVALID_ID ? 0 : mArrays[getIndex<T>()]->getSectorIndex(id, false) + 1;
				return Iterator{ mArrays, Memory::SectorsArray<ThreadSafe, Allocator>::IteratorAlive(mArrays[getIndex<T>()], idx) };
			}
		}

	public:
		explicit ComponentArraysEntry(Registry<ThreadSafe, Allocator>* manager) noexcept requires (!Ranged) {
			initArrays<ComponentTypes..., T>(manager);

			auto pinnedBack = mArrays[getIndex<T>()]->pinBackSector();
			auto index = pinnedBack.getId();
			mPins = manager->template pinContainers<T, ComponentTypes...>(index);
		}

		explicit ComponentArraysEntry(Registry<ThreadSafe, Allocator>* manager, EntitiesRanges ranges = {}) noexcept requires (Ranged) : mRanges(std::move(ranges)) {
			initArrays<ComponentTypes..., T>(manager);

			auto sectorsArray = mArrays[getIndex<T>()];
			for (auto& range : mRanges.ranges) {
				range.first = static_cast<EntityId>(sectorsArray->findRightNearestSectorIndex(range.first));
				range.second = static_cast<EntityId>(sectorsArray->findRightNearestSectorIndex(range.second));
			}

			mRanges.mergeIntersections();

			mPins = manager->template pinContainers<T, ComponentTypes...>(mRanges.empty() ? INVALID_ID : mRanges.back().second);
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
		void initArrays(Registry<ThreadSafe, Allocator>* registry) noexcept {
			static_assert(types::areUnique<Types...>(), "Duplicates detected in types");
			((mArrays[getIndex<Types>()] = registry->template getComponentContainer<Types>()), ...);
		}

	private:
		std::array<Memory::SectorsArray<ThreadSafe, Allocator>*, sizeof...(ComponentTypes) + 1> mArrays;

		std::array<typename ecss::Memory::SectorsArray<ThreadSafe, Allocator>::PinnedSector, sizeof...(ComponentTypes) + 1> mPins;

		EntitiesRanges mRanges;
	};
}
