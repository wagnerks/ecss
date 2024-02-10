#pragma once

// ecss - Entity Component System with Sectors
// "Sectors" refers to the logic of storing components.
// Multiple components of different types can be stored in one memory location, which I've named a "sector."

#include <deque>
#include <set>
#include <array>

#include "Types.h"
#include "EntityHandle.h"
#include "memory/SectorsArray.h"

namespace ecss {
	template <typename T, typename ...ComponentTypes>
	class ComponentsArrayHandle;

	class Registry final {
		template <typename T, typename ...ComponentTypes>
		friend class ComponentsArrayHandle;

		Registry(const Registry& other) = delete;
		Registry(Registry&& other) noexcept = delete;
		Registry& operator=(const Registry& other) = delete;
		Registry& operator=(Registry&& other) noexcept = delete;

	public:
		Registry() = default;
		~Registry();

		void clear();

		template <class T>
		T* getComponent(const EntityHandle& entity) {
			auto lock = containersReadLock<T>();
			return entity ? getComponentContainer<T>()->template getComponent<T>(entity.getID()) : nullptr;
		}
		template <class T>
		T* getComponentForce(const EntityHandle& entity) {
			return entity ? getComponentContainer<T>()->template getComponent<T>(entity.getID()) : nullptr;
		}
		template <class T, class ...Args>
		T* addComponent(const EntityHandle& entity, Args&&... args) {
			if (auto comp = getComponent<T>(entity)) {
				return comp;
			}
			
			auto container = getComponentContainer<T>();
			auto lock = containerWriteLock<T>();
			auto comp = static_cast<T*>(new(container->acquireSector(Memory::ReflectionHelper::getTypeId<T>(), entity.getID()))T(std::forward<Args>(args)...));
			
			return comp;
		}

		//if need to set some default values to component right after creation
		template <class T, class ...Args>
		T* addComponentWithInit(const EntityHandle& entity, std::function<void(T*)> initFunc, Args&&... args) {
			if (auto comp = getComponent<T>(entity)) {
				return comp;
			}

			auto container = getComponentContainer<T>();
			auto lock = containerWriteLock<T>();
			auto comp = static_cast<T*>(new(container->acquireSector(Memory::ReflectionHelper::getTypeId<T>(), entity.getID()))T(std::forward<Args>(args)...));

			if (initFunc) {
				initFunc(comp);
			}

			return comp;
		}

		//you can create component somewhere in another thread and move it into container here
		template <class T>
		void moveComponentToEntity(const EntityHandle& entity, T* component) {
			getComponentContainer<T>()->template move<T>(entity.getID(), component);
		}

		template <class T>
		void copyComponentToEntity(const EntityHandle& entity, T* component) {
			getComponentContainer<T>()->template insert<T>(entity.getID(), component);
		}

		template <class T>
		void removeComponent(const EntityHandle& entity) {
			getComponentContainer<T>()->destroyMember(Memory::ReflectionHelper::getTypeId<T>(), entity.getID());
		}

		template <class T>
		void removeComponent(std::vector<SectorId>& entities) {
			getComponentContainer<T>()->destroyMembers(Memory::ReflectionHelper::getTypeId<T>(), entities);
		}

		void destroyComponents(const EntityHandle& entityId) const;

/*this function allows to init container which stores multiple components in one memory sector
 
 0x..[sector info]
 0x..[component 1]
 0x..[component 2]
 0x..[    ...    ]
  
  should be called before any getContainer calls
*/
		template<typename... Components> 
		void initCustomComponentsContainer() {
			std::unique_lock lock(mutex);
			bool added = false;

			((added |= prepareForContainer<Components>()), ...);
			assert(!added);

			auto container = Memory::SectorsArray::createSectorsArray<Components...>();

			auto containerMutex = new std::shared_mutex();
			
			((mComponentsArraysMap[Memory::ReflectionHelper::getTypeId<Components>()] = container), ...);
			((mComponentsArraysMutexes[Memory::ReflectionHelper::getTypeId<Components>()] = containerMutex), ...);
		}

		template <typename T>
		bool prepareForContainer() {
			auto compId = Memory::ReflectionHelper::getTypeId<T>();
			if (mComponentsArraysMap.size() <= compId) {
				mComponentsArraysMap.resize(compId + 1, nullptr);
				mComponentsArraysMutexes.resize(compId + 1, nullptr);
				return false;
			}
			
			return mComponentsArraysMap[compId];
		}

/*
this function creates an object with selected components, which provided ability to iterate through entities like it is the container of tuple<component1,component2,component3>
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
		template<typename... Components, typename Func>
		inline void forEach(const std::vector<SectorId>& entities, Func&& func) {
			if (entities.empty()) {
				return;
			}

			for (auto entity : entities) {
				auto lock = containersReadLock<Components...>();
				func(entity, getComponentForce<Components>(entity)...);
			}
		}

		template<typename... Components>
		inline ComponentsArrayHandle<Components...> getComponentsArray() { return ComponentsArrayHandle<Components...>(this); }

		template<typename... Components>
		inline ComponentsArrayHandle<Components...> getComponentsArray(const std::deque<std::pair<SectorId, SectorId>>& ranges) { return ComponentsArrayHandle<Components...>(this, ranges); }

		template<typename... Components>
		inline ComponentsArrayHandle<Components...> getComponentsArray(const std::vector<SectorId>& sortedEntities) { return ComponentsArrayHandle<Components...>(this, extractRanges(sortedEntities)); }

		inline static std::deque<std::pair<SectorId, SectorId>> extractRanges(const std::vector<SectorId>& sortedEntities) {
			std::deque<std::pair<ecss::SectorId, ecss::SectorId>> ranges;

			if (sortedEntities.empty()) {
				return ranges;
			}

			SectorId previous = sortedEntities.front();
			SectorId begin = previous;

			for (auto i = 1; i < sortedEntities.size(); i++) {
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

			return ranges;
		}

		template <class... Components>
		void reserve(uint32_t newCapacity) { /*auto lock = containersWriteLock<Components...>(); */(getComponentContainer<Components>()->reserve(newCapacity), ...); }

		EntityHandle takeEntity();
		EntityHandle getEntity(SectorId entityId) const;
		bool isEntity(SectorId entityId) const;

		void destroyEntity(const EntityHandle& entityId);
		void destroyEntities(std::vector<SectorId>& entities);
		void removeEmptySectors();

		const std::vector<SectorId>& getAllEntities();

		template <class T>
		Memory::SectorsArray* getComponentContainer() {
			mutex.lock_shared();

			const ECSType compId = Memory::ReflectionHelper::getTypeId<T>();
			
			if (mComponentsArraysMap.size() <= compId) {
				mComponentsArraysMap.resize(compId + 1);
				mComponentsArraysMutexes.resize(compId + 1);
			}

			if (mComponentsArraysMap[compId] == nullptr) {
				mutex.unlock_shared();
				initCustomComponentsContainer<T>();
			}
			else {
				mutex.unlock_shared();
			}

			return mComponentsArraysMap[compId];
		}

		template <class T>
		std::shared_mutex* getComponentMutex() {
			const ECSType compId = Memory::ReflectionHelper::getTypeId<T>();

			std::shared_lock lock(mutex);
			if (mComponentsArraysMutexes.size() <= compId || !mComponentsArraysMutexes[compId]) {
				lock.unlock();
				initCustomComponentsContainer<T>();
				lock.lock();
			}
			return mComponentsArraysMutexes[compId];
		}

		template<typename T, typename LockType>
		void containersLockHelper(std::vector<LockType>& res) {
			auto mutex = getComponentMutex<T>();
			if (std::find_if(res.begin(), res.end(), [&mutex](const LockType& a) {
				return a.mutex() == mutex;
			}) == res.end()) {
				res.emplace_back(*mutex);
			}
		}

		template <class... T>
		std::vector<std::shared_lock<std::shared_mutex>> containersReadLock() {
			std::vector<std::shared_lock<std::shared_mutex>> res;
			(containersLockHelper<T, std::shared_lock<std::shared_mutex>>(res), ...);
			return res;
		}
		
		template <class... T>
		std::vector<std::unique_lock<std::shared_mutex>> containersWriteLock() {
			std::vector<std::unique_lock<std::shared_mutex>> res;

			(containersLockHelper<T, std::unique_lock<std::shared_mutex>>(res), ...);

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
			return std::unique_lock {*mComponentsArraysMutexes[containerType]};
		}

		std::shared_lock<std::shared_mutex> containerReadLock(ECSType containerType) const {
			return std::shared_lock {*mComponentsArraysMutexes[containerType]};
		}

	private:
		std::vector<Memory::SectorsArray*> mComponentsArraysMap;
		std::vector<std::shared_mutex*> mComponentsArraysMutexes;

		std::map<uint32_t, std::shared_mutex*> mutexes;
		std::vector<SectorId> mEntities;
		std::set<SectorId> mFreeEntities;
		std::shared_mutex mutex;
		mutable std::shared_mutex mEntitiesMutex;

		SectorId getNewId();
	};

	template <typename T, typename ...ComponentTypes>
	class ComponentsArrayHandle {
	public:
		void setRanges(std::deque<std::pair<SectorId, SectorId>> ranges) {
			mRanges = std::move(ranges);
		}

		~ComponentsArrayHandle(){}

		explicit ComponentsArrayHandle(Registry* manager, std::deque<std::pair<SectorId, SectorId>> ranges = {}) {
			((mArrays[types::getIndex<ComponentTypes, ComponentTypes...>()] = manager->getComponentContainer<ComponentTypes>()), ...);
			mArrays[sizeof...(ComponentTypes)] = manager->getComponentContainer<T>();

			mLocks = manager->containersReadLock<T, ComponentTypes...>();

			mRanges = std::move(ranges);
		}

		inline size_t size() const { return mArrays[sizeof...(ComponentTypes)]->size(); }
		inline bool empty() const { return !size(); }

		inline std::tuple<T&, ComponentTypes&...> operator[](size_t i) const { return *Iterator(mArrays, i); }

		class Iterator {
		public:
			inline Iterator(const std::array<Memory::SectorsArray*, sizeof...(ComponentTypes) + 1>& arrays, size_t idx, const std::deque<std::pair<SectorId, SectorId>>& ranges) : mRanges(ranges), mCurIdx(idx) {
				if (!mRanges.empty()) {
					mCurIdx = arrays[sizeof...(ComponentTypes)]->getSectorIdx(mRanges.front().first);
				}
				
				mCurrentSector = mCurIdx >= arrays[sizeof...(ComponentTypes)]->size() ? nullptr : (*arrays[sizeof...(ComponentTypes)])[mCurIdx];

				if (!mCurrentSector) {
					return;
				}

				constexpr auto mainIdx = sizeof...(ComponentTypes);
				mGetInfo[mainIdx].array = arrays[mainIdx];
				mGetInfo[mainIdx].offset = arrays[mainIdx]->template getTypeOffset<T>();
				mGetInfo[mainIdx].isMain = true;
				mGetInfo[mainIdx].size = arrays[mainIdx]->size();

				((
					mGetInfo[types::getIndex<ComponentTypes, ComponentTypes...>()].array = arrays[types::getIndex<ComponentTypes, ComponentTypes...>()]
					,
					mGetInfo[types::getIndex<ComponentTypes, ComponentTypes...>()].offset = arrays[types::getIndex<ComponentTypes, ComponentTypes...>()]->template getTypeOffset<ComponentTypes>()
					,
					mGetInfo[types::getIndex<ComponentTypes, ComponentTypes...>()].isMain = arrays[mainIdx] == arrays[types::getIndex<ComponentTypes, ComponentTypes...>()]
					,
					mGetInfo[types::getIndex<ComponentTypes, ComponentTypes...>()].size = arrays[types::getIndex<ComponentTypes, ComponentTypes...>()]->size()
					)
				,
				...);
			}

			template<typename ComponentType>
			inline ComponentType* getComponent(const SectorId sectorId) {
				auto& getterInfo = mGetInfo[types::getIndex<ComponentType, ComponentTypes...>()];
				return getterInfo.isMain ? mCurrentSector->getMember<ComponentType>(getterInfo.offset) : getterInfo.array->template getComponent<ComponentType>(sectorId, getterInfo.offset);
			}
			
			inline std::tuple<SectorId, T*, ComponentTypes*...> operator*() {
				auto id = mCurrentSector->id;
				return std::forward_as_tuple(id, (mCurrentSector->getMember<T>(mGetInfo[sizeof...(ComponentTypes)].offset)), getComponent<ComponentTypes>(id)...);
			}

			inline Iterator& operator++() {//todo bug - if ids 1 5 7 but its idxs in array 0 1 2 it will skip it
				mCurrentSector = (++mCurIdx >= mGetInfo[sizeof...(ComponentTypes)].size ? nullptr : (*(mGetInfo[sizeof...(ComponentTypes)].array))[mCurIdx]);
				if (mCurrentSector && !mRanges.empty()) {
					if (mCurrentSector->id >= mRanges.front().first && mCurrentSector->id < mRanges.front().second) {
						return *this;
					}

					if (mCurrentSector->id < mRanges.front().first) {
						mCurrentSector = mGetInfo[sizeof...(ComponentTypes)].array->getSector(mRanges.front().first);
						mCurIdx = mGetInfo[sizeof...(ComponentTypes)].array->getSectorIdx(mRanges.front().first);
						return *this;
					}

					if (mCurrentSector->id >= mRanges.front().second) {
						if (mRanges.size() == 1) {
							mCurrentSector = nullptr;
							mRanges.pop_front();
							return *this;
						}
						else {
							mRanges.pop_front();
							if (mCurrentSector->id == mRanges.front().first) {
								return *this;
							}
							return operator++();
						}
					}
				}

				return *this;
			}

			inline bool operator!=(const Iterator& other) const { return mCurrentSector != other.mCurrentSector; }

		private:
			struct ObjectGetter {
				bool isMain = false;
				uint16_t offset = 0;
				size_t size = 0;
				Memory::SectorsArray* array = nullptr;
			};

			std::array<ObjectGetter, sizeof...(ComponentTypes) + 1> mGetInfo;

			std::deque<std::pair<SectorId, SectorId>> mRanges;

			size_t mCurIdx = 0;
			Memory::Sector* mCurrentSector = nullptr;
		};

		inline Iterator begin() { return {  mArrays, 0, mRanges }; }
		inline Iterator end() {	return {  mArrays, mArrays[sizeof...(ComponentTypes)]->size(), {} }; }

	private:
		std::array<Memory::SectorsArray*, sizeof...(ComponentTypes) + 1> mArrays;
		std::vector<std::shared_lock<std::shared_mutex>> mLocks;

		std::deque<std::pair<SectorId, SectorId>> mRanges;
	};
}