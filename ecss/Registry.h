#pragma once

// ecss - Entity Component System with Sectors
// "Sectors" refers to the logic of storing components.
// Multiple components of different types can be stored in one memory location, which I've named a "sector."

#include <algorithm>
#include <deque>
#include <array>
#include <map>
#include <shared_mutex>
#include <set>
#include <unordered_set>

#include "EntitiesRanges.h"
#include "memory/SectorsArray.h"

namespace ecss {
	template <bool Ranged, typename T, typename ...ComponentTypes>
	class ComponentArraysEntry;
	class Registry final {
		template <bool Ranged, typename T, typename ...ComponentTypes>
		friend class ComponentArraysEntry;

	public:
		Registry(const Registry& other) = delete;
		Registry& operator=(const Registry& other) = delete;
		Registry(Registry&& other) noexcept = delete;
		Registry& operator=(Registry&& other) noexcept = delete;

		Registry() = default;
		~Registry() {
			clear();

			for (auto array : mComponentsArrays) {
				delete array;
			}
		}

	public:
		template<typename T>
		ECSType componentTypeId() {	return mReflectionHelper.getTypeId<T>(); }

		template <class T>
		bool hasComponent(EntityId entity) {
			if (auto container = getComponentContainer<T>()) {
				auto lock = container->readLock();
				if (auto sector = container->findSector(entity)) {
					return sector->isAlive(container->getLayoutData<T>().index);
				}
			}
			return false;
		}

		template <class T>
		T* getComponent(EntityId entity) {
			auto container = getComponentContainer<T>();
			auto lock = container->readLock();
			return Memory::Sector::getComponentFromSector<T>(container->findSector(entity), container->getLayout());
		}

		template <class T>
		T* getComponentNotSafe(EntityId entity) {
			auto container = getComponentContainer<T>();
			return Memory::Sector::getComponentFromSector<T>(container->findSector(entity), container->getLayout());
		}

		// you can emplace component into sector using initializer list
		template <class T, class ...Args>
		T* addComponent(EntityId entity, Args&&... args) {
			auto container = getComponentContainer<T>();
			if constexpr (sizeof...(Args) == 1 && (std::is_same_v<std::remove_cvref_t<Args>, T> && ...)) {
				return const_cast<T*>(container->insertSafe(entity, std::forward<Args>(args)...));
			}
			else {
				return container->emplaceSafe<T>(entity, std::forward<Args>(args)...);
			}			
		}

		// you can emplace component into sector using initializer list
		template <class T>
		T* addComponent(EntityId entity, T&& component) {
			auto container = getComponentContainer<T>();
			return container->insertSafe(entity, std::forward<T>(component));
		}

		template<typename T>
		void copyComponentsArrayToRegistry(const Memory::SectorsArray& array) {
			auto cont = getComponentContainer<T>();
			*cont = array;
		}

		template <class T>
		void destroyComponent(EntityId entity) {
			if (auto container = getComponentContainer<T>()) {
				Memory::SectorArrayUtils::destroyMember<T>(*container, entity);
			}
		}

		template <class T>
		void destroyComponent(std::vector<EntityId>& entities) {
			if (auto container = getComponentContainer<T>()) {
				Memory::SectorArrayUtils::destroyMembers<T>(*container, entities);
			}
		}

		void destroyComponents(EntityId entityId) const {
			for (auto array : mComponentsArrays) {
				Memory::Sector::destroyMembers(array->findSector(entityId), array->getLayout());
			}
		}

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
		void registerArray(uint32_t capacity = 0, uint32_t chunkSize = 8192) {
			std::unique_lock lock(componentsArrayMapMutex);
			ECSType maxId = 0;
			((maxId = std::max(maxId, componentTypeId<ComponentTypes>())), ...);
			if (maxId >= mComponentsArraysMap.size()) {
				mComponentsArraysMap.resize(maxId + 1);
			}

			auto sectorArray = Memory::SectorsArray::create<ComponentTypes...>(mReflectionHelper, capacity, chunkSize);
			mComponentsArrays.push_back(sectorArray);
			((mComponentsArraysMap[componentTypeId<ComponentTypes>()] = sectorArray), ...);
		}

		template<typename... Components, typename Func>
		inline void forEachAsync(const std::vector<EntityId>& entities, const Func& func) {
			if (entities.empty()) {
				return;
			}

			for (auto entity : entities) {
				auto lock = containersLock<Components...>();
				func(entity, getComponentNotSafe<Components>(entity)...);
			}
		}

		template<typename... Components>
		ComponentArraysEntry<true, Components...> forEach(const EntitiesRanges& ranges, bool lock = true) { return ComponentArraysEntry<true, Components...>{ this, ranges, lock }; }

		template<typename... Components>
		ComponentArraysEntry<false, Components...> forEach(bool lock = true) { return ComponentArraysEntry<false, Components...>{this, lock}; }

		template <class... Components>
		void reserve(uint32_t newCapacity) { (getComponentContainer<Components>()->reserveSafe(newCapacity), ...); }

		void clear() {
			for (auto* array : mComponentsArrays) {
				array->clear();
			}

			std::unique_lock lock(mEntitiesMutex);
			mEntities.clear();
		}

		bool contains(EntityId entityId) const { return mEntities.contains(entityId); }

		EntityId takeEntity() {
			std::unique_lock lock(mEntitiesMutex);
			return mEntities.take();
		}

		void destroyEntity(EntityId entityId) {
			std::unique_lock lock(mEntitiesMutex);
			if (!contains(entityId)) {
				return;
			}

			mEntities.erase(entityId);
			destroyComponents(entityId);
		}

		void destroyEntities(std::vector<EntityId>& entities) {
			if (entities.empty()) {
				return;
			}
			std::ranges::sort(entities);
			std::vector<std::thread> destroyThreads;
			{
				std::shared_lock lock(componentsArrayMapMutex);
				for (auto* array : mComponentsArrays) {
					destroyThreads.emplace_back(std::thread([&, array = array](){ Memory::SectorArrayUtils::destroyAllMembers(*array, entities, false); }));
				}
			}

			std::unique_lock lock(mEntitiesMutex);
			for (auto id : entities) {
				mEntities.erase(id);
			}

			for (auto& thread : destroyThreads) {
				thread.join();
			}
		}

		void defragment() {
			std::shared_lock lock(componentsArrayMapMutex);
			for (auto* array : mComponentsArrays) {
				array->defragment();
			}
		}

		std::vector<EntityId> getAllEntities() const {
			std::shared_lock lock(mEntitiesMutex);
			return mEntities.getAll();
		}

		template <class T>
		Memory::SectorsArray* getComponentContainer() {
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

		template <class T>
		std::shared_mutex* getComponentMutex() { return &getComponentContainer<T>()->getMutex(); }

		struct Lock
		{
			Lock(std::shared_mutex* mutex, bool shared)
			{
				if (shared) {
					sharedLock = new std::shared_lock(*mutex);
				}
				else {
					uniqueLock = new std::unique_lock(*mutex);
				}
			}

			~Lock()
			{
				delete sharedLock;
				delete uniqueLock;
			}

			std::shared_lock<std::shared_mutex>* sharedLock = nullptr;
			std::unique_lock<std::shared_mutex>* uniqueLock = nullptr;
		};


		template<typename T>
		void addMutex(std::unordered_map<std::shared_mutex*, bool>& mutexes) {
			mutexes[getComponentMutex<T>()] |= std::is_const_v<T>;
		}

		template <class... T>
		std::vector<Lock> containersLock() {
			std::unordered_map<std::shared_mutex*, bool> mutexes;
			std::vector<Lock> locks;

			(addMutex<T>(mutexes), ...);

			locks.reserve(mutexes.size());
			for (auto& [mtxPtr, constness] : mutexes) {
				locks.emplace_back(mtxPtr, constness);
			}

			return locks;
		}

		template <class T>
		Lock containerLock() {
			return {getComponentMutex<T>(), std::is_const_v<T> };
		}

	private:
		Memory::ReflectionHelper mReflectionHelper;

		EntitiesRanges mEntities;

		std::vector<Memory::SectorsArray*> mComponentsArraysMap;
		std::vector<Memory::SectorsArray*> mComponentsArrays;

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
	template <bool Ranged, typename T, typename ...ComponentTypes>
	class ComponentArraysEntry final {
		struct TypeAccessInfo {
			Memory::SectorsArray* array = nullptr;
			uint8_t typeIndexInSector = 0;
			size_t typeOffsetInSector = 0;
			bool isMain = false;
		};

	public:
		template<typename... Types>
		void initArrays(Registry* manager) {
			static_assert(types::areUnique<Types...>(), "Duplicates detected in types");
			((mArrays[getIndex<Types>()] = manager->getComponentContainer<Types>()), ...);
		}

		explicit ComponentArraysEntry(Registry* manager, bool lock = true) requires (!Ranged) {
			initArrays<ComponentTypes..., T>(manager);

			if (lock) {
				mLocks = manager->containersLock<T, ComponentTypes...>();
			}
		}

		explicit ComponentArraysEntry(Registry* manager, const EntitiesRanges& ranges = {}, bool lock = true) requires (Ranged) : mRanges(ranges) {
			initArrays<ComponentTypes..., T>(manager);

			if (lock) {
				mLocks = manager->containersLock<T, ComponentTypes...>();
			}

			if (!mRanges.empty()) {
				auto sectorsArray = mArrays[getIndex<T>()];
				for (auto& range : mRanges.ranges) {
					range.first = Memory::SectorArrayUtils::findRightNearestSectorIndex(*sectorsArray, range.first);
					range.second = Memory::SectorArrayUtils::findRightNearestSectorIndex(*sectorsArray, range.second);
				}

				mRanges.mergeIntersections();
			}
		}

		bool valid() const {
			if constexpr (Ranged) {
				return !mRanges.empty();
			}
			else {
				return !mArrays[getIndex<T>()]->empty();
			}
			
		}

		struct Dummy{};
		class Iterator {
		public:
			using iterator_category = std::forward_iterator_tag;
			using value_type = std::tuple<EntityId, T*, ComponentTypes*...>;
			using difference_type = std::ptrdiff_t;
			using pointer = value_type*;
			using reference = value_type&;

		public:
			using SectorArrays = std::array<Memory::SectorsArray*, sizeof...(ComponentTypes) + 1>;

			Iterator(const SectorArrays& arrays, size_t index, size_t endIndex) requires (!Ranged) {
				initTypeAccessInfo<T, ComponentTypes...>(arrays);

				mCurrentIt = Memory::SectorsArray::Iterator(mTypeAccessInfo[getIndex<T>()].array, index);
				mEndIterator = Memory::SectorsArray::Iterator(mTypeAccessInfo[getIndex<T>()].array, endIndex);
				advanceToAlive(mCurrentIt, mEndIterator);
			}

			Iterator(const SectorArrays& arrays, EntitiesRanges ranges) requires (Ranged) {
				initTypeAccessInfo<T, ComponentTypes...>(arrays);
				
				initIterators(ranges);
			}

			value_type operator*() {
				auto sector = *mCurrentIt;
				EntityId id = sector->id;
				
				return { id, Iterator::getComponent<T, true>(sector, getTypeAccessInfo<T>()), getComponent<ComponentTypes>(id)... };
			}

			Iterator& operator++() {
				++mCurrentIt;
				if constexpr(Ranged) {
					if (advanceToAlive(mCurrentIt, mIterators.back().second)) {
						return *this;
					}

					mIterators.pop_back();
					while (!mIterators.empty()) {
						mCurrentIt = std::move(mIterators.back().first);
						if (advanceToAlive(mCurrentIt, mIterators.back().second)) {
							return *this;
						}
						mIterators.pop_back();
					}
				}
				else {
					advanceToAlive(mCurrentIt, mEndIterator);
				}

				return *this;
			}
			bool operator!=(const Iterator& other) const{ return mCurrentIt != other.mCurrentIt; }

		private:
			template<typename ComponentType>
			inline void initTypeAccessInfoImpl(Memory::SectorsArray* sectorArray) {
				auto& info = mTypeAccessInfo[getIndex<ComponentType>()];
				info.array = sectorArray;
				info.typeIndexInSector = static_cast<uint8_t>(sectorArray->getLayoutData<ComponentType>().index);
				info.typeOffsetInSector = sectorArray->getLayoutData<ComponentType>().offset;
				info.isMain = sectorArray == mTypeAccessInfo[getIndex<T>()].array;
			}

			template<typename... Types>
			inline void initTypeAccessInfo(const SectorArrays& arrays) {
				(initTypeAccessInfoImpl<Types>(arrays[getIndex<Types>()]), ...);
			}

			template <typename ComponentType>
			inline const TypeAccessInfo& getTypeAccessInfo() {
				return mTypeAccessInfo[getIndex<ComponentType>()];
			}

			template<typename ComponentType>
			inline ComponentType* getComponent(EntityId sectorId) {
				const auto& info = getTypeAccessInfo<ComponentType>();

				return info.isMain ? Iterator::getComponent<ComponentType, true>(*mCurrentIt, info) : Iterator::getComponent<ComponentType>(info.array->findSector(sectorId), info);
			}

			inline void initIterators(EntitiesRanges& ranges) requires(Ranged) {
				assert(!ranges.ranges.empty());

				auto sectorArray = getTypeAccessInfo<T>().array;

				mIterators.reserve(ranges.size());
				while (!ranges.empty()) {
					mCurrentIt = { sectorArray, ranges.back().first };
					mEndIterator = { sectorArray, ranges.back().second };
					ranges.pop_back();

					advanceToAlive(mCurrentIt, mEndIterator);

					mIterators.emplace_back(mCurrentIt, mEndIterator);
				}
				mIterators.shrink_to_fit();
			}

			inline bool advanceToAlive(Memory::SectorsArray::Iterator& iterator, const Memory::SectorsArray::Iterator& endIterator) {
				while (iterator != endIterator) {
					if (auto sector = *iterator; sector->isSectorAlive() && sector->isAlive(getTypeAccessInfo<T>().typeIndexInSector)) {
						return true;
					}

					++iterator;
				}

				return false;
			}

			template<typename ComponentType, bool Main = false>
			static inline ComponentType* getComponent(Memory::Sector* sector, const TypeAccessInfo& meta) {
				if constexpr (Main) {
					return sector->getMember<ComponentType>(meta.typeOffsetInSector, meta.typeIndexInSector);
				}
				else {
					return sector ? sector->getMember<ComponentType>(meta.typeOffsetInSector, meta.typeIndexInSector) : nullptr;
				}
			}

		private:
			Memory::SectorsArray::Iterator mCurrentIt;
			std::array<TypeAccessInfo, sizeof...(ComponentTypes) + 1> mTypeAccessInfo;

			std::conditional_t<Ranged, std::vector<std::pair<Memory::SectorsArray::Iterator, Memory::SectorsArray::Iterator>>, Dummy> mIterators;
			Memory::SectorsArray::Iterator mEndIterator;
		};

		inline Iterator begin() {
			if constexpr (Ranged) {
				return { mArrays, mRanges };
			}
			else {
				return { mArrays, 0, mArrays[getIndex<T>()]->size() };
			}
		}
		
		inline Iterator end() {
			if constexpr (Ranged) {
				return { mArrays, EntitiesRanges{{mRanges.back().second, mRanges.back().second}} };
			}
			else {
				return { mArrays, mArrays[getIndex<T>()]->size(), mArrays[getIndex<T>()]->size() };
			}
		}

	private:

		template<typename ComponentType>
		static size_t constexpr getIndex() {
			if constexpr (std::is_same_v<T, ComponentType>) {
				return sizeof...(ComponentTypes);
			}
			else {
				return types::getIndex<ComponentType, ComponentTypes...>();
			}
		}

	private:
		std::array<Memory::SectorsArray*, sizeof...(ComponentTypes) + 1> mArrays;
		std::vector<Registry::Lock> mLocks;

		EntitiesRanges mRanges;
	};
}