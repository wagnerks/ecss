#pragma once

// ecss - Entity Component System with Sectors
// "Sectors" refers to the logic of storing components.
// Multiple components of different types can be stored in one memory location, which I've named a "sector."

#include <algorithm>
#include <deque>
#include <set>
#include <array>
#include <shared_mutex>

#include "memory/SectorsArray.h"

namespace ecss {
	template <typename T, typename ...ComponentTypes>
	class ComponentArraysIterator;

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
		}

		EntityId take();
		void insert(EntityId id);
		void erase(EntityId id);
		void clear() { ranges.clear(); }
		size_t size() { return ranges.size(); }
		range& front() { return ranges.front(); }
		void pop_front() { ranges.pop_front(); }
		bool empty() { return !size(); }
		bool contains(EntityId id) const;
		std::vector<EntityId> getAll() const;
	};

	class Registry final {
		template <typename T, typename ...ComponentTypes>
		friend class ComponentArraysIterator;

		Registry(const Registry& other) = delete;
		Registry& operator=(const Registry& other) = delete;
		Registry(Registry&& other) noexcept = delete;
		Registry& operator=(Registry&& other) noexcept = delete;

	public:
		Registry() = default;
		~Registry();

		

		template <class T>
		T* getComponent(EntityId entity) {
			auto lock = containersReadLock<T>();
			return getComponentNotSafe<T>(entity);
		}

		template <class T>
		T* getComponentNotSafe(EntityId entity) {
			return getComponentContainer<T>()->getComponent<T>(entity, mReflectionHelper.getTypeId<T>());
		}

		template <class T, class ...Args>
		T* addComponent(EntityId entity, Args&&... args) {
			if (auto comp = getComponent<T>(entity)) {
				return comp;
			}

			auto container = getComponentContainer<T>();
			auto lock = containerWriteLock<T>();
			return static_cast<T*>(new(container->acquireSector(mReflectionHelper.getTypeId<T>(), entity))T(std::forward<Args>(args)...));
		}

		template<typename T>
		void copyComponentsArrayToRegistry(Memory::SectorsArray* array) {
			auto cont = getComponentContainer<T>();
			//auto lock = containerWriteLock<T>();
			*cont = *array;
		}

		//you can create component somewhere in another thread and move it into container here
		template <class T>
		void moveComponentToEntity(EntityId entity, T* component) {
			getComponentContainer<T>()->move<T>(entity, component, mReflectionHelper.getTypeId<T>());
		}

		template <class T>
		void copyComponentToEntity(EntityId entity, T* component) {
			getComponentContainer<T>()->insert<T>(entity, component, mReflectionHelper.getTypeId<T>());
		}

		template <class T>
		void removeComponent(EntityId entity) {
			auto componentTypeId = mReflectionHelper.getTypeId<T>();
			if (auto container = getComponentContainer(componentTypeId)) {
				container->destroyMember(componentTypeId, entity);
			}
		}

		template <class T>
		void removeComponent(std::vector<EntityId>& entities) {
			auto componentTypeId = mReflectionHelper.getTypeId<T>();
			if (auto container = getComponentContainer(componentTypeId)) {
				container->destroyMembers(componentTypeId, entities);
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
			std::unique_lock lock(mutex);
			bool added = false;

			((added |= prepareForContainer<Components>()), ...);
			assert(!added);

			auto container = Memory::SectorsArray::createSectorsArray<Components...>(mReflectionHelper);

			auto containerMutex = new std::shared_mutex();

			((mComponentsArraysMap[mReflectionHelper.getTypeId<Components>()] = container), ...);
			((mComponentsArraysMutexes[mReflectionHelper.getTypeId<Components>()] = containerMutex), ...);
		}

		template<typename... Components, typename Func>
		inline void forEachAsync(const std::vector<EntityId>& entities, Func&& func) {
			if (entities.empty()) {
				return;
			}

			for (auto entity : entities) {
				auto lock = containersReadLock<Components...>();
				func(entity, getComponentNotSafe<Components>(entity)...);
			}
		}

		template<typename... Components>
		inline ComponentArraysIterator<Components...> forEach(EntitiesRanges ranges = {}, bool lock = true) { return ComponentArraysIterator<Components...>(this, std::move(ranges), lock); }

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
		Memory::SectorsArray* getComponentContainer() {
			const ECSType compId = mReflectionHelper.getTypeId<T>();

			{
				auto lock = std::shared_lock(mutex);
				if (auto sectorsArray = mComponentsArraysMap.size() > compId ? mComponentsArraysMap[compId] : nullptr) {
					return sectorsArray;
				}
			}

			auto lock = std::unique_lock(mutex);
	
			if (!prepareForContainer(compId)) {
				auto container = Memory::SectorsArray::createSectorsArray<T>(mReflectionHelper);
				mComponentsArraysMap[compId] = container;
				mComponentsArraysMutexes[compId] = new std::shared_mutex();
			}

			return mComponentsArraysMap[compId];
		}

		Memory::SectorsArray* getComponentContainer(ECSType componentTypeId) {
			auto lock = std::shared_lock(mutex);
			if (mComponentsArraysMap.size() >= componentTypeId) {
				return nullptr;
			}

			return mComponentsArraysMap[componentTypeId];
		}
		
		template <class T>
		std::shared_mutex* getComponentMutex() {
			const ECSType compId = mReflectionHelper.getTypeId<T>();

			std::shared_lock lock(mutex);
			if (mComponentsArraysMutexes.size() <= compId || !mComponentsArraysMutexes[compId]) {
				lock.unlock();
				initCustomComponentsContainer<T>();
				lock.lock();
			}
			return mComponentsArraysMutexes[compId];
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
		template<typename T, typename LockType>
		void containersLockHelper(std::vector<LockType>& res) {
			auto mutex = getComponentMutex<T>();
			if (std::find_if(res.begin(), res.end(), [&mutex](const LockType& a) {
				return a.mutex() == mutex;
			}) == res.end()) {
				res.emplace_back(*mutex);
			}
		}

		template <typename T>
		bool prepareForContainer() {
			return prepareForContainer(mReflectionHelper.getTypeId<T>());
		}

		bool prepareForContainer(ECSType typeId) {
			if (mComponentsArraysMap.size() <= typeId) {
				mComponentsArraysMap.resize(typeId + 1, nullptr);
				mComponentsArraysMutexes.resize(typeId + 1, nullptr);
				return false;
			}

			return mComponentsArraysMap[typeId];
		}

	private:
		Memory::ReflectionHelper mReflectionHelper;

		EntitiesRanges mEntities;

		std::vector<Memory::SectorsArray*> mComponentsArraysMap;

		//non copyable
		std::vector<std::shared_mutex*> mComponentsArraysMutexes;
		mutable std::shared_mutex mEntitiesMutex;
		std::shared_mutex mutex;
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
	template <typename T, typename ...ComponentTypes>
	class ComponentArraysIterator final {
	public:
		explicit ComponentArraysIterator(Registry* manager, EntitiesRanges&& ranges = {}, bool lock = true) {
			((mArrays[types::getIndex<ComponentTypes, ComponentTypes...>()] = manager->getComponentContainer<ComponentTypes>()), ...);
			mArrays[sizeof...(ComponentTypes)] = manager->getComponentContainer<T>();
			if (lock) {
				mLocks = manager->containersReadLock<T, ComponentTypes...>();
			}

			mRanges = std::move(ranges);

			mReflectionHelper = &manager->mReflectionHelper;
		}

		inline bool valid() const { return mArrays[sizeof...(ComponentTypes)]->size(); }

		class Iterator {
		public:
			inline Iterator(const std::array<Memory::SectorsArray*, sizeof...(ComponentTypes) + 1>& arrays, size_t idx, const EntitiesRanges& ranges, Memory::ReflectionHelper* reflectionHelper) : mRanges(ranges), mCurIdx(idx) {
				if (!arrays[sizeof...(ComponentTypes)]->size()) {
					return;
				}

				while (mRanges.size()) {
					for (auto i = mRanges.front().first; i < mRanges.front().second; i++) {
						mCurIdx = arrays[sizeof...(ComponentTypes)]->tryGetSectorIdx(mRanges.front().first);
						if (mCurIdx < arrays[sizeof...(ComponentTypes)]->size()) {
							break;
						}
						mRanges.front().first++;
					}

					if (mCurIdx < arrays[sizeof...(ComponentTypes)]->size()) {
						break;
					}
					mRanges.pop_front();
				}

				mCurrentSector = mCurIdx >= arrays[sizeof...(ComponentTypes)]->size() ? nullptr : (*arrays[sizeof...(ComponentTypes)])[mCurIdx];

				if (!mCurrentSector) {
					return;
				}

				constexpr auto mainIdx = sizeof...(ComponentTypes);
				mGetInfo[mainIdx].array = arrays[mainIdx];
				mGetInfo[mainIdx].offset = arrays[mainIdx]->getTypeOffset(reflectionHelper->getTypeId<T>());
				mGetInfo[mainIdx].isMain = true;
				mGetInfo[mainIdx].size = arrays[mainIdx]->size();

				((
					mGetInfo[types::getIndex<ComponentTypes, ComponentTypes...>()].array = arrays[types::getIndex<ComponentTypes, ComponentTypes...>()]
					,
					mGetInfo[types::getIndex<ComponentTypes, ComponentTypes...>()].offset = arrays[types::getIndex<ComponentTypes, ComponentTypes...>()]->getTypeOffset(reflectionHelper->getTypeId<ComponentTypes>())
					,
					mGetInfo[types::getIndex<ComponentTypes, ComponentTypes...>()].isMain = arrays[mainIdx] == arrays[types::getIndex<ComponentTypes, ComponentTypes...>()]
					,
					mGetInfo[types::getIndex<ComponentTypes, ComponentTypes...>()].size = arrays[types::getIndex<ComponentTypes, ComponentTypes...>()]->size()
					)
					,
					...);
			}

			template<typename ComponentType>
			inline ComponentType* getComponent(const EntityId sectorId) {
				return mGetInfo[types::getIndex<ComponentType, ComponentTypes...>()].isMain ? mCurrentSector->getMember<ComponentType>(mGetInfo[types::getIndex<ComponentType, ComponentTypes...>()].offset) : mGetInfo[types::getIndex<ComponentType, ComponentTypes...>()].array->template getComponentByOffset<ComponentType>(sectorId, mGetInfo[types::getIndex<ComponentType, ComponentTypes...>()].offset);
			}

			inline std::tuple<EntityId, T*, ComponentTypes*...> operator*() {
				return std::forward_as_tuple(mCurrentSector->id, (mCurrentSector->getMember<T>(mGetInfo[sizeof...(ComponentTypes)].offset)), getComponent<ComponentTypes>(mCurrentSector->id)...);
			}

			inline Iterator& operator++() {//todo bug - if ids 1 5 7 but its idxs in array 0 1 2 it will skip it
				mCurrentSector = (++mCurIdx >= mGetInfo[sizeof...(ComponentTypes)].size ? nullptr : (*(mGetInfo[sizeof...(ComponentTypes)].array))[mCurIdx]);
				if (mCurrentSector && !mRanges.empty()) {
					auto& front = mRanges.front();
					if (mCurrentSector->id >= front.first && mCurrentSector->id < front.second) {
						return *this;
					}

					if (mCurrentSector->id < front.first) {
						auto sectorsArray = mGetInfo[sizeof...(ComponentTypes)].array;
						mCurIdx = sectorsArray->tryGetSectorIdx(front.first);
						mCurrentSector = sectorsArray->getSectorByIdx(mCurIdx);
						return *this;
					}

					if (mCurrentSector->id >= front.second) {
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
			struct ObjectGetterMeta {
				bool isMain = false;
				uint16_t offset = 0;
				size_t size = 0;
				Memory::SectorsArray* array = nullptr;
			};

			std::array<ObjectGetterMeta, sizeof...(ComponentTypes) + 1> mGetInfo;

			EntitiesRanges mRanges;

			size_t mCurIdx = 0;
			Memory::Sector* mCurrentSector = nullptr;
		};

		inline Iterator begin() { return { mArrays, 0, mRanges, mReflectionHelper }; }
		inline Iterator end() { return { mArrays, mArrays[sizeof...(ComponentTypes)]->size(), {}, mReflectionHelper }; }

	private:
		std::array<Memory::SectorsArray*, sizeof...(ComponentTypes) + 1> mArrays;
		std::vector<std::shared_lock<std::shared_mutex>> mLocks;

		EntitiesRanges mRanges;

		Memory::ReflectionHelper* mReflectionHelper = nullptr;
	};
}