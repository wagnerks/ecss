#pragma once

// ecss - Entity Component System with Sectors
// "Sectors" refers to the logic of storing components.
// Multiple components of different types can be stored in one memory location, which I've named a "sector."

#include <algorithm>
#include <deque>
#include <set>
#include <array>
#include <shared_mutex>
#include <unordered_set>

#include "memory/SectorsArray.h"

namespace ecss {
	template <bool Ranged, typename T, typename ...ComponentTypes>
	class ComponentArraysEntry;

	struct EntitiesRanges {
		using range = std::pair<EntityId, EntityId>;
		std::deque<range> ranges;

		EntitiesRanges() = default;

		EntitiesRanges(const std::vector<EntityId>& sortedEntities) {
			if (sortedEntities.empty()) {
				return;
			}

			EntityId previous = sortedEntities.front();
			EntityId begin = previous;

			for (auto i = 1u; i < sortedEntities.size(); i++) {
				const auto current = sortedEntities[i];
				if (current == previous) {
					continue;
				}
				if (current - previous > 1) {
					ranges.emplace_back(begin, previous + 1);
					begin = current;
				}

				previous = current;
			}
			ranges.emplace_back(begin, previous + 1);
		}

		EntitiesRanges(const std::deque<range>& range){
			ranges = range;
		}

		inline void mergeIntersections() {
			if (ranges.empty()) {
				return;
			}

			for (auto it = ranges.begin() + 1; it != ranges.end();) {
				auto& prev = *(it - 1);
				auto& cur = *(it);
				if (prev.second >= cur.first) {
					prev.second = std::max(prev.second, cur.second);
					it = ranges.erase(it);
				}
				else {
					++it;
				}
			}
		}

		EntityId take();
		void insert(EntityId id);
		void erase(EntityId id);
		void clear() { ranges.clear(); }
		size_t size() const { return ranges.size(); }
		range& front() { return ranges.front(); }
		range& back() { return ranges.back(); }
		void pop_front() { ranges.pop_front(); }
		void pop_back() { ranges.pop_back(); }
		bool empty() const { return !size(); }
		bool contains(EntityId id) const;
		std::vector<EntityId> getAll() const;
	};

	class Registry final {
		template <bool Ranged, typename T, typename ...ComponentTypes>
		friend class ComponentArraysEntry;

	public:
		Registry(const Registry& other) = delete;
		Registry& operator=(const Registry& other) = delete;
		Registry(Registry&& other) noexcept = delete;
		Registry& operator=(Registry&& other) noexcept = delete;

		struct SectorEntry
		{
			Memory::SectorsArray* sector = nullptr;
			std::shared_mutex* mutex = nullptr;

			explicit operator bool() const
			{
				return sector && mutex;
			}
		};

		Registry() = default;
		~Registry();

		template <class T>
		T* getComponent(EntityId entity) {
			auto lock = containersReadLock<T>();
			return getComponentNotSafe<T>(entity);
		}

		template <class T>
		T* getComponentNotSafe(EntityId entity) {
			auto container = getComponentContainer<T>();
			return Memory::Sector::getComponentFromSector<T>(container->findSector(entity), container->getSectorLayout());
		}

		template <class T, class ...Args>
		T* addComponent(EntityId entity, Args&&... args) {
			if (auto comp = getComponent<T>(entity)) {
				return comp;
			}

			auto container = getComponentContainer<T>();
			auto lock = containerWriteLock<T>();

			return Memory::Sector::emplaceMember<T>(container->acquireSector(entity), container->template getLayoutData<T>(), std::forward<Args>(args)...);
		}

		template<typename T>
		ECSType getComponentTypeId() {
			return mReflectionHelper.getTypeId<T>();
		}

		template<typename T>
		void copyComponentsArrayToRegistry(Memory::SectorsArray* array) {
			auto cont = getComponentContainer<T>();
			//auto lock = containerWriteLock<T>();
			*cont = *array;
		}

		//you can create component somewhere in another thread and move it into container here

		template <class T>
		void insert(EntityId entity, const T& component) {
			getComponentContainer<T>()->template insert<T>(entity, component);
		}

		/*template <class T>
		void insert(EntityId entity, T&& component) {
			getComponentContainer<T>()->template insert<T>(entity, std::forward<T>(component), getComponentTypeId<T>());
		}*/

		template <class T>
		void removeComponent(EntityId entity) {
			if (auto container = getComponentContainer<T>()) {
				Memory::SectorArrayUtils::destroyMember<T>(*container, entity);
			}
		}

		template <class T>
		void removeComponent(std::vector<EntityId>& entities) {
			if (auto container = getComponentContainer<T>()) {
				Memory::SectorArrayUtils::destroyMembers<T>(*container, entities);
			}
		}

		void destroyComponents(EntityId entityId) const;

		/*this function allows to init container which stores multiple components in one memory sector

		 0x..[sector info]
		 0x..[component 1]
		 0x..[component 2]
		 0x..[    ...    ]

		  should be called before any getContainer calls
		*/
		template<typename... Components>
		void initCustomComponentsContainer() {
			((prepareForContainer<Components>()), ...);

			std::unique_lock lock(componentsArrayMapMutex);

			const SectorEntry entry = { Memory::SectorsArray::createSectorsArray<Components...>(mReflectionHelper), new std::shared_mutex() };

			((mComponentsArraysMap[getComponentTypeId<Components>()] = entry), ...);
		}

		template<typename... Components, typename Func>
		inline void forEachAsync(const std::vector<EntityId>& entities, const Func& func) {
			if (entities.empty()) {
				return;
			}

			for (auto entity : entities) {
				auto lock = containersReadLock<Components...>();
				func(entity, getComponentNotSafe<Components>(entity)...);
			}
		}

		template<typename... Components>
		inline ComponentArraysEntry<true, Components...> forEach(EntitiesRanges ranges, bool lock = true) { return ComponentArraysEntry<true, Components...>(this, std::move(ranges), lock); }

		template<typename... Components>
		inline ComponentArraysEntry<false, Components...> forEach(bool lock = true) { return ComponentArraysEntry<false, Components...>(this, lock); }

		template <class... Components>
		void reserve(uint32_t newCapacity) { /*auto lock = containersWriteLock<Components...>(); */(getComponentContainer<Components>()->reserve(newCapacity), ...); }
		void clear();
		bool contains(EntityId entityId) const;

		EntityId takeEntity();

		void destroyEntity(EntityId entityId);
		void destroyEntities(std::vector<EntityId>& entities);
		void removeEmptySectors();

		const std::vector<EntityId> getAllEntities();

		template <class T>
		const SectorEntry& getSectorEntry()
		{
			const ECSType componentType = getComponentTypeId<T>();

			prepareForContainer(componentType);

			auto& entry = mComponentsArraysMap[componentType];
			if (entry) {
				return entry;
			}

			std::unique_lock writeLock(componentsArrayMapMutex);
			return entry = { Memory::SectorsArray::createSectorsArray<T>(mReflectionHelper), new std::shared_mutex() }, entry;
		}

		template <class T>
		Memory::SectorsArray* getComponentContainer() {
			return getSectorEntry<T>().sector;
		}

		Memory::SectorsArray* getComponentContainer(ECSType componentTypeId) {
			prepareForContainer(componentTypeId);

			auto lock = std::shared_lock(componentsArrayMapMutex);
			return mComponentsArraysMap[componentTypeId].sector;
		}
		
		template <class T>
		std::shared_mutex* getComponentMutex() {
			return getSectorEntry<T>().mutex;
		}
		
		template <class... T>
		std::vector<std::shared_lock<std::shared_mutex>> containersReadLock() {
			auto mutexes = containersMutexSet<T...>();
			std::vector<std::shared_lock<std::shared_mutex>> locks;
			locks.reserve(mutexes.size());
			for (auto* m : mutexes) {
				locks.emplace_back(*m);
			}
			return locks;
		}

		template <class... T>
		std::vector<std::unique_lock<std::shared_mutex>> containersWriteLock() {
			auto mutexes = containersMutexSet<T...>();
			std::vector<std::unique_lock<std::shared_mutex>> locks;
			locks.reserve(mutexes.size());
			for (auto* m : mutexes) {
				locks.emplace_back(*m);
			}
			return locks;
		}

		template <class... T>
		std::vector<std::shared_mutex*> containersMutexSet() {
			std::vector<std::shared_mutex*> res;
			res.reserve(sizeof...(T));
			auto addUnique = [&](std::shared_mutex* mtx) {
				if (std::ranges::find(res, mtx) == res.end()) {
					res.emplace_back(mtx);
				}
			};

			(addUnique(getComponentMutex<T>()), ...);
			return res;
		}

		template <class T>
		std::unique_lock<std::shared_mutex> containerWriteLock() {
			return std::unique_lock {*getComponentMutex<T>()};
		}

		template <class T>
		std::shared_lock<std::shared_mutex> containerReadLock() {
			return std::shared_lock {*getComponentMutex<T>()};
		}

		std::unique_lock<std::shared_mutex> containerWriteLock(ECSType containerType) const {
			return std::unique_lock {*mComponentsArraysMap[containerType].mutex};
		}

		std::shared_lock<std::shared_mutex> containerReadLock(ECSType containerType) const {
			return std::shared_lock {*mComponentsArraysMap[containerType].mutex};
		}

	private:
		template<typename T, typename LockType>
		void containersLockHelper(std::unordered_set<LockType>& res) {
			auto mutex = getComponentMutex<T>();
			res.insert(*mutex);
		}

		template <typename T>
		void prepareForContainer() {
			return prepareForContainer(getComponentTypeId<T>());
		}

		void prepareForContainer(ECSType typeId) {
			if (typeId < mComponentsArraysMap.size()) {
				return;
			}

			std::unique_lock writeLock{ componentsArrayMapResizeMutex };
			if (typeId >= mComponentsArraysMap.size()) {
				mComponentsArraysMap.resize(typeId + 1);
			}
		}

	private:
		Memory::ReflectionHelper mReflectionHelper;

		EntitiesRanges mEntities;

		std::vector<SectorEntry> mComponentsArraysMap;

		//non copyable
		mutable std::shared_mutex mEntitiesMutex;
		std::shared_mutex componentsArrayMapMutex;
		std::shared_mutex componentsArrayMapResizeMutex;
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
		inline void initArrays(Registry* manager) {
			static_assert(types::areUnique<Types...>(), "Duplicates detected in types");
			((mArrays[getIndex<Types>()] = manager->getComponentContainer<Types>()), ...);
		}

		explicit ComponentArraysEntry(Registry* manager, bool lock = true) requires (!Ranged) {
			initArrays<ComponentTypes..., T>(manager);

			if (lock) {
				mLocks = manager->containersReadLock<T, ComponentTypes...>();
			}
		}

		explicit ComponentArraysEntry(Registry* manager, EntitiesRanges&& ranges = {}, bool lock = true) requires (Ranged) : mRanges(std::move(ranges)) {
			initArrays<ComponentTypes..., T>(manager);

			if (lock) {
				mLocks = manager->containersReadLock<T, ComponentTypes...>();
			}
			
			if (!mRanges.empty()) {
				auto sectorsArray = mArrays[getIndex<T>()];
				for (auto& range : mRanges.ranges) {
					range.first = Memory::SectorArrayUtils::findRightNearestSectorIndex(*sectorsArray, range.first);
					range.second = Memory::SectorArrayUtils::findRightNearestSectorIndex(*sectorsArray, range.second);
				}

				mRanges.mergeIntersections();
			}
			else {
				mRanges = { {0, mArrays[getIndex<T>()]->size()} };
			}
		}

		inline bool valid() const { return mArrays[getIndex<T>()]->size(); }

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

			inline Iterator(const SectorArrays& arrays, size_t index) requires (!Ranged) {
				InitTypeAccessInfo<T, ComponentTypes...>(arrays);

				mCurrentIt = Memory::SectorsArray::Iterator(mTypeAccessInfo[getIndex<T>()].array, index);
				mCurrentIt.advanceToAlive();
			}

			inline Iterator(const SectorArrays& arrays, EntitiesRanges ranges) requires (Ranged) {
				InitTypeAccessInfo<T, ComponentTypes...>(arrays);
				InitIterators(ranges);

				if (iterators.empty()) {
					return;
				}

				mCurrentIt = iterators.back().first;
			}

			inline constexpr value_type operator*() {
				auto sector = *mCurrentIt;
				EntityId id = sector->id;
				
				return { id, ComponentArraysEntry::getComponent<T>(sector, getTypeAccesInfo<T>()), getComponent<ComponentTypes>(id)... };
			}

			inline Iterator& operator++() {
				if constexpr (Ranged) {
					++mCurrentIt;
					mCurrentIt.advanceToAlive();
					if (!iterators.empty() && mCurrentIt == iterators.back().second) {
						iterators.pop_back();
						mCurrentIt = !iterators.empty() ? iterators.back().first : mCurrentIt;
					}

					return *this;
				}
				else {
					return ++mCurrentIt, mCurrentIt.advanceToAlive(), * this;
				}
			}

			inline constexpr bool operator!=(const Iterator& other) const{ return mCurrentIt != other.mCurrentIt; }

		private:
			
			template<typename ComponentType>
			inline void InitTypeAccessInfoImpl(Memory::SectorsArray* sectorArray) {
				auto& info = mTypeAccessInfo[getIndex<ComponentType>()];
				info.array = sectorArray;
				info.typeIndexInSector = sectorArray->getLayoutData<ComponentType>().index;
				info.typeOffsetInSector = sectorArray->getLayoutData<ComponentType>().offset;
				info.isMain = sectorArray == mTypeAccessInfo[getIndex<T>()].array;
			}

			template<typename... Types>
			inline void InitTypeAccessInfo(const SectorArrays& arrays) {
				(InitTypeAccessInfoImpl<Types>(arrays[getIndex<Types>()]), ...);
			}

			template <typename ComponentType>
			inline constexpr const TypeAccessInfo& getTypeAccesInfo() {
				return mTypeAccessInfo[getIndex<ComponentType>()];
			}

			template<typename ComponentType>
			inline constexpr ComponentType* getComponent(EntityId sectorId) {
				const auto& info = getTypeAccesInfo<ComponentType>();

				return info.isMain ? ComponentArraysEntry::getComponent<ComponentType, true>(*mCurrentIt, info) : ComponentArraysEntry::getComponent<ComponentType>(info.array->findSector(sectorId), info);
			}

			inline void InitIterators(EntitiesRanges& ranges) requires(Ranged) {
				assert(!ranges.ranges.empty());

				auto sectorArray = getTypeAccesInfo<T>().array;

				iterators.reserve(ranges.size());
				while (!ranges.empty()) {
					auto range = ranges.back();
					ranges.pop_back();

					Memory::SectorsArray::Iterator begin = { sectorArray, range.first };
					begin.advanceToAlive();
					if (!begin.isValid()) {
						continue;
					}

					Memory::SectorsArray::Iterator end = { sectorArray, range.second };
					end.advanceToAlive();
					if (begin == end) {
						continue;
					}

					iterators.emplace_back(begin, end);
				}
				iterators.shrink_to_fit();
			}

		private:
			Memory::SectorsArray::Iterator mCurrentIt;
			std::array<TypeAccessInfo, sizeof...(ComponentTypes) + 1> mTypeAccessInfo;

			std::conditional_t<Ranged, std::vector<std::pair<Memory::SectorsArray::Iterator, Memory::SectorsArray::Iterator>>, Dummy> iterators;
		};

		inline Iterator begin() {
			if constexpr (Ranged) {
				return { mArrays, mRanges };
			}
			else {
				return { mArrays, 0 };
			}
		}
		
		inline Iterator end() {
			if constexpr (Ranged) {
				return { mArrays, EntitiesRanges{{mRanges.back().second, mRanges.back().second}} };
			}
			else {
				return { mArrays, mArrays[getIndex<T>()]->size() };
			}
		}

	private:
		template<typename ComponentType, bool Main = false>
		static inline ComponentType* getComponent(Memory::Sector* sector, const TypeAccessInfo& meta) {
			if constexpr (Main) {
				return sector->getMember<ComponentType>(meta.typeOffsetInSector, meta.typeIndexInSector);
			}
			else {
				return sector ? sector->getMember<ComponentType>(meta.typeOffsetInSector, meta.typeIndexInSector) : nullptr;
			}
		}

		template<typename ComponentType>
		inline static size_t constexpr getIndex() {
			if constexpr (std::is_same_v<T, ComponentType>) {
				return sizeof...(ComponentTypes);
			}
			else {
				return types::getIndex<ComponentType, ComponentTypes...>();
			}
		}

	private:
		std::array<Memory::SectorsArray*, sizeof...(ComponentTypes) + 1> mArrays;
		std::vector<std::shared_lock<std::shared_mutex>> mLocks;

		EntitiesRanges mRanges;
	};
}