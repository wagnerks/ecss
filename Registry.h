#pragma once

// ecss - Entity Component System with Sectors
// "Sectors" refers to the logic of storing components.
// Multiple components of different types can be stored in one memory location, which I've named a "sector."

#include <set>

#include "Types.h"
#include "EntityHandle.h"
#include "memory/SectorsArray.h"

namespace ecss {
	class Registry final {
		template <typename T, typename ...ComponentTypes>
		friend class ComponentsArrayHandle;

		Registry(const Registry& other) = delete;
		Registry(Registry&& other) noexcept = delete;
		Registry& operator=(const Registry& other) = delete;
		Registry& operator=(Registry&& other) noexcept = delete;

	public:
		Registry();
		~Registry();

		void clear();

		template <class T>
		T* getComponent(const EntityHandle& entity) {
			return entity ? getComponentContainer<T>()->getComponent<T>(entity.getID()) : nullptr;
		}

		template <class T, class ...Args>
		T* addComponent(const EntityHandle& entity, Args&&... args) {
			if (auto comp = getComponent<T>(entity)) {
				return comp;
			}

			auto container = getComponentContainer<T>();
			auto comp = new(container->acquireSector(Memory::ReflectionHelper::getTypeId<T>(), entity.getID()))T(std::forward<Args>(args)...);

			return static_cast<T*>(comp);
		}

		//you can create component somewhere in another thread and move it into container here
		template <class T>
		void moveComponentToEntity(const EntityHandle& entity, T* component) {
			getComponentContainer<T>()->move<T>(entity.getID(), component);
		}

		template <class T>
		void copyComponentToEntity(const EntityHandle& entity, T* component) {
			getComponentContainer<T>()->insert<T>(entity.getID(), component);
		}

		template <class T>
		void removeComponent(const EntityHandle& entity) {
			getComponentContainer<T>()->destroyObject(Memory::ReflectionHelper::getTypeId<T>(), entity.getID());
		}

		template <class T>
		void removeComponent(const std::vector<SectorId>& entities) {
			getComponentContainer<T>()->destroyObjects(Memory::ReflectionHelper::getTypeId<T>(), entities);
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
			bool added = false;

			((added |= prepareForContainer<Components>()), ...);
			if (added) {
				assert(false);
				return;
			}

			auto container = Memory::SectorsArray::createSectorsArray<Components...>();

			((mComponentsArraysMap[Memory::ReflectionHelper::getTypeId<Components>()] = container), ...);
		}

		template <typename T>
		bool prepareForContainer() {
			auto type = Memory::ReflectionHelper::getTypeId<T>();
			if (mComponentsArraysMap.size() <= type) {
				mComponentsArraysMap.resize(type + 1, nullptr);
				return false;
			}
			
			return mComponentsArraysMap[type];
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
		template<typename... Components>
		ComponentsArrayHandle<Components...> getComponentsArray() { return ComponentsArrayHandle<Components...>(this); }

		template <class... Components>
		void reserve(uint32_t newCapacity) { (getComponentContainer<Components>()->reserve(newCapacity),...); }

		EntityHandle takeEntity();
		EntityHandle getEntity(SectorId entityId) const;

		void destroyEntity(const EntityHandle& entityId);

		const std::vector<SectorId>& getAllEntities();

		template <class T>
		Memory::SectorsArray* getComponentContainer() {
			const ECSType compId = Memory::ReflectionHelper::getTypeId<T>();

			if (mComponentsArraysMap.size() <= compId) {
				mComponentsArraysMap.resize(compId + 1);
			}

			if (mComponentsArraysMap[compId] == nullptr) {
				initCustomComponentsContainer<T>();
			}

			return mComponentsArraysMap[compId];
		}

	private:
		std::vector<Memory::SectorsArray*> mComponentsArraysMap;

		std::vector<SectorId> mEntities;
		std::set<SectorId> mFreeEntities;

		SectorId getNewId();
	};

	template <typename T, typename ...ComponentTypes>
	class ComponentsArrayHandle {
	public:
		explicit ComponentsArrayHandle(Registry* manager) {
			((mArrays[types::getIndex<ComponentTypes, ComponentTypes...>()] = manager->getComponentContainer<ComponentTypes>()), ...);
			mArrays[sizeof...(ComponentTypes)] = manager->getComponentContainer<T>();
		}

		inline size_t size() const { return mArrays[sizeof...(ComponentTypes)]->size(); }
		inline bool empty() const { return !size(); }

		inline std::tuple<T&, ComponentTypes&...> operator[](size_t i) const { return *Iterator(mArrays, i); }

		class Iterator {
		public:
			inline Iterator(const std::array<Memory::SectorsArray*, sizeof...(ComponentTypes) + 1>& arrays, size_t idx) : mCurIdx(idx), mCurrentSector((*arrays[sizeof...(ComponentTypes)])[idx]){
				constexpr auto mainIdx = sizeof...(ComponentTypes);
				mGetInfo[mainIdx].array = arrays[mainIdx];
				mGetInfo[mainIdx].offset = arrays[mainIdx]->getSectorData().membersLayout.at(Memory::ReflectionHelper::getTypeId<T>());
				mGetInfo[mainIdx].isMain = true;

				((
					mGetInfo[types::getIndex<ComponentTypes, ComponentTypes...>()].array = arrays[types::getIndex<ComponentTypes, ComponentTypes...>()]
					,
					mGetInfo[types::getIndex<ComponentTypes, ComponentTypes...>()].offset = arrays[types::getIndex<ComponentTypes, ComponentTypes...>()]->getSectorData().membersLayout.at(Memory::ReflectionHelper::getTypeId<ComponentTypes>())
					,
					mGetInfo[types::getIndex<ComponentTypes, ComponentTypes...>()].isMain = arrays[mainIdx] == arrays[types::getIndex<ComponentTypes, ComponentTypes...>()]
					)
				,
				...);
			}

			template<typename ComponentType>
			inline ComponentType* getComponent(const SectorId sectorId) {
				auto getterInfo = mGetInfo[types::getIndex<ComponentType, ComponentTypes...>()];
				return getterInfo.isMain ? mCurrentSector->getObject<ComponentType>(getterInfo.offset) : getterInfo.array->template getComponent<ComponentType>(sectorId, getterInfo.offset);
			}
			
			inline std::tuple<SectorId, T&, ComponentTypes&...> operator*() {
				auto id = mCurrentSector->id;
				return std::forward_as_tuple(id, *(mCurrentSector->getObject<T>(mGetInfo[sizeof...(ComponentTypes)].offset)), *getComponent<ComponentTypes>(id)...);
			}

			inline Iterator& operator++() {
				return mCurrentSector = (*(mGetInfo[sizeof...(ComponentTypes)].array))[++mCurIdx], *this;
			}

			inline bool operator!=(const Iterator& other) const { return mCurrentSector != other.mCurrentSector; }

		private:
			struct ObjectGetter {
				bool isMain = false;
				Memory::SectorsArray* array = nullptr;
				uint16_t offset = 0;
			};

			std::array<ObjectGetter, sizeof...(ComponentTypes) + 1> mGetInfo;

			size_t mCurIdx = 0;
			Memory::Sector* mCurrentSector = nullptr;
		};

		inline Iterator begin() { return {  mArrays, 0}; }
		inline Iterator end() {	return {  mArrays, mArrays[sizeof...(ComponentTypes)]->size()}; }

	private:
		std::array<Memory::SectorsArray*, sizeof...(ComponentTypes) + 1> mArrays;
	};
}