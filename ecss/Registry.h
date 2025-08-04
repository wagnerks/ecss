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
#include <stdbool.h>
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

			auto sectorArray = Memory::SectorsArray::create<ComponentTypes...>(mReflectionHelper, capacity, chunkSize);
			mComponentsArrays.push_back(sectorArray);
			((mComponentsArraysMap[componentTypeId<ComponentTypes>()] = sectorArray), ...);
		}

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

	public:
		// iterator helpers
		template<typename... Components, typename Func>
		inline void forEachAsync(const std::vector<EntityId>& entities, const Func& func) {
			for (auto entity : entities) {
				auto lock = containersLock<Components...>();
				func(entity, getComponentNotSafe<Components>(entity)...);
			}
		}

		template<typename... Components>
		ComponentArraysEntry<true, Components...> forEach(const EntitiesRanges& ranges, bool lock = true) { return ComponentArraysEntry<true, Components...>{ this, ranges, lock }; }

		template<typename... Components>
		ComponentArraysEntry<false, Components...> forEach(bool lock = true) { return ComponentArraysEntry<false, Components...>{this, lock}; }

	public:
		// container handles
		template <class... Components>
		void reserve(uint32_t newCapacity) { (getComponentContainer<Components>()->reserveSafe(newCapacity), ...); }

		void clear() {
			{
				std::shared_lock lock(componentsArrayMapMutex);
				for (auto* array : mComponentsArrays) {
					array->clear();
				}
			}

			std::unique_lock lock(mEntitiesMutex);
			mEntities.clear();
		}

		void defragment() {
			std::shared_lock lock(componentsArrayMapMutex);
			for (auto* array : mComponentsArrays) {
				array->defragment();
			}
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

	public:
		// entities
		bool contains(EntityId entityId) const { std::shared_lock lock(mEntitiesMutex); return mEntities.contains(entityId); }

		EntityId takeEntity() {
			std::unique_lock lock(mEntitiesMutex);
			return mEntities.take();
		}

		std::vector<EntityId> getAllEntities() const {
			std::shared_lock lock(mEntitiesMutex);
			return mEntities.getAll();
		}

		void destroyEntity(EntityId entityId) {
			if (!contains(entityId)) {
				return;
			}

			{
				std::unique_lock lock(mEntitiesMutex);
				mEntities.erase(entityId);
			}

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
					destroyThreads.emplace_back(std::thread([&, array = array]() { Memory::SectorArrayUtils::destroyAllMembers(*array, entities, false); }));
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
		std::shared_mutex* getSectorsArrayMutex() { return &getComponentContainer<T>()->getMutex(); }

	
		struct Lock {
			Lock(std::shared_mutex* mutex)	{
				sharedLock = new std::shared_lock(*mutex);
			}

			~Lock() {
				delete sharedLock;
			}

			std::shared_lock<std::shared_mutex>* sharedLock = nullptr;
		};


		template<typename T>
		void addMutex(std::unordered_map<std::shared_mutex*, bool>& mutexes) {
			mutexes[getSectorsArrayMutex<T>()] |= std::is_const_v<T>;
		}

		template <class... T>
		std::vector<Lock> containersLock() {
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
		Lock containerLock() {
			return {getSectorsArrayMutex<T>()/*, std::is_const_v<T>*/ };
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

	template <typename T0, typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7, typename T8, typename T9>
	struct ComponentResult10{ EntityId id; T0* c0; T1* c1; T2* c2; T3* c3; T4* c4; T5* c5; T6* c6; T7* c7; T8* c8; T9* c9; };

	template <typename T0, typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7, typename T8>
	struct ComponentResult9 { EntityId id; T0* c0; T1* c1; T2* c2; T3* c3; T4* c4; T5* c5; T6* c6; T7* c7; T8* c8; };

	template <typename T0, typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7>
	struct ComponentResult8 { EntityId id; T0* c0; T1* c1; T2* c2; T3* c3; T4* c4; T5* c5; T6* c6; T7* c7; };

	template <typename T0, typename T1, typename T2, typename T3, typename T4, typename T5, typename T6>
	struct ComponentResult7 { EntityId id; T0* c0; T1* c1; T2* c2; T3* c3; T4* c4; T5* c5; T6* c6; };

	template <typename T0, typename T1, typename T2, typename T3, typename T4, typename T5>
	struct ComponentResult6 { EntityId id; T0* c0; T1* c1; T2* c2; T3* c3; T4* c4; T5* c5; };

	template <typename T0, typename T1, typename T2, typename T3, typename T4>
	struct ComponentResult5 { EntityId id; T0* c0; T1* c1; T2* c2; T3* c3; T4* c4; };

	template <typename T0, typename T1, typename T2, typename T3>
	struct ComponentResult4 { EntityId id; T0* c0; T1* c1; T2* c2; T3* c3; };

	template <typename T0, typename T1, typename T2>
	struct ComponentResult3 { EntityId id; T0* c0; T1* c1; T2* c2; };

	template <typename T0, typename T1>
	struct ComponentResult2 { EntityId id; T0* c0; T1* c1; };

	template <typename T0>
	struct ComponentResult1 { EntityId id; T0* c0; };

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
			uint32_t typeAliveMask = 0;
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

			Iterator(const SectorArrays& arrays, size_t index, size_t endIndex) noexcept requires (!Ranged) {
				initTypeAccessInfo<T, ComponentTypes...>(arrays);
				
				mCurrentIt = Memory::SectorsArray::Iterator(getTypeAccessInfo<T>().array, index);
				mEndIterator = Memory::SectorsArray::Iterator(getTypeAccessInfo<T>().array, endIndex);
				advanceToAlive(mCurrentIt, mEndIterator, getTypeAccessInfo<T>().typeAliveMask);
			}

			Iterator(const SectorArrays& arrays, EntitiesRanges ranges) noexcept requires (Ranged) {
				initTypeAccessInfo<T, ComponentTypes...>(arrays);
				
				initIterators(ranges);
			}

			inline auto operator*() {
				auto sector = *mCurrentIt;
				if constexpr (sizeof...(ComponentTypes) + 1 == 1) {
					return ComponentResult1{ sector->id, reinterpret_cast<T*>(reinterpret_cast<char*>(sector) + getTypeAccessInfo<T>().typeOffsetInSector)};
				}
				else {
					EntityId id = sector->id;
					if constexpr (sizeof...(ComponentTypes) + 1 == 2) {
						return ComponentResult2{ id, Iterator::getComponent<T, true>(sector, getTypeAccessInfo<T>()), getComponent<ComponentTypes>(id)... };
					}
					else if constexpr (sizeof...(ComponentTypes) + 1 == 3) {
						return ComponentResult3{ id, Iterator::getComponent<T, true>(sector, getTypeAccessInfo<T>()), getComponent<ComponentTypes>(id)... };
					}
					else if constexpr (sizeof...(ComponentTypes) + 1 == 4) {
						return ComponentResult4{ id, Iterator::getComponent<T, true>(sector, getTypeAccessInfo<T>()), getComponent<ComponentTypes>(id)... };
					}
					else if constexpr (sizeof...(ComponentTypes) + 1 == 5) {
						return ComponentResult5{ id, Iterator::getComponent<T, true>(sector, getTypeAccessInfo<T>()), getComponent<ComponentTypes>(id)... };
					}
					else if constexpr (sizeof...(ComponentTypes) + 1 == 6) {
						return ComponentResult6{ id, Iterator::getComponent<T, true>(sector, getTypeAccessInfo<T>()), getComponent<ComponentTypes>(id)... };
					}
					else if constexpr (sizeof...(ComponentTypes) + 1 == 7) {
						return ComponentResult7{ id, Iterator::getComponent<T, true>(sector, getTypeAccessInfo<T>()), getComponent<ComponentTypes>(id)... };
					}
					else if constexpr (sizeof...(ComponentTypes) + 1 == 8) {
						return ComponentResult8{ id, Iterator::getComponent<T, true>(sector, getTypeAccessInfo<T>()), getComponent<ComponentTypes>(id)... };
					}
					else if constexpr (sizeof...(ComponentTypes) + 1 == 9) {
						return ComponentResult9{ id, Iterator::getComponent<T, true>(sector, getTypeAccessInfo<T>()), getComponent<ComponentTypes>(id)... };
					}
					else if constexpr (sizeof...(ComponentTypes) + 1 == 10) {
						return ComponentResult10{ id, Iterator::getComponent<T, true>(sector, getTypeAccessInfo<T>()), getComponent<ComponentTypes>(id)... };
					}
					else {
						return std::tuple{ id, Iterator::getComponent<T, true>(sector, getTypeAccessInfo<T>()), getComponent<ComponentTypes>(id)... };
					}
				}
			}

			inline Iterator& operator++() {
				++mCurrentIt;
				auto aliveMask = std::get<getIndex<T>()>(mTypeAccessInfo).typeAliveMask;
				if constexpr(Ranged) {
					if (mIterators.empty()) {
						return *this;
					}

					while (mCurrentIt != mIterators.back().second) {
						auto sector = *mCurrentIt;
						if (sector->isAliveData & aliveMask) {
							return *this;
						}

						++mCurrentIt;
					}

					mIterators.pop_back();
					while (!mIterators.empty()) {
						mCurrentIt = std::move(mIterators.back().first);
						while (mCurrentIt != mIterators.back().second) {
							auto sector = *mCurrentIt;
							if (sector->isAliveData & aliveMask) {
								return *this;
							}

							++mCurrentIt;
						}

						mIterators.pop_back();
					}
				}
				else {
					while (mCurrentIt != mEndIterator) {
						auto sector = *mCurrentIt;
						if (sector->isAliveData & aliveMask) {
							break;
						}

						++mCurrentIt;
					}
				}

				return *this;
			}
			bool operator!=(const Iterator& other) const{ return mCurrentIt != other.mCurrentIt; }

		private:
			template<typename ComponentType>
			inline void initTypeAccessInfoImpl(Memory::SectorsArray* sectorArray) {
				auto& info = std::get<getIndex<ComponentType>()>(mTypeAccessInfo);
				info.array = sectorArray;
				info.typeIndexInSector = static_cast<uint8_t>(sectorArray->getLayoutData<ComponentType>().index);
				info.typeAliveMask = 1 << info.typeIndexInSector;
				info.typeOffsetInSector = sectorArray->getLayoutData<ComponentType>().offset;
				info.isMain = sectorArray == std::get<getIndex<ComponentType>()>(mTypeAccessInfo).array;

			}

			template<typename... Types>
			inline void initTypeAccessInfo(const SectorArrays& arrays) {
				(initTypeAccessInfoImpl<Types>(arrays[getIndex<Types>()]), ...);
			}

			template <typename ComponentType>
			inline constexpr TypeAccessInfo& getTypeAccessInfo() { return std::get<getIndex<ComponentType>()>(mTypeAccessInfo); }

			template<typename ComponentType>
			inline ComponentType* getComponent(EntityId sectorId) {
				const auto& info = getTypeAccessInfo<ComponentType>();

				return info.isMain ? Iterator::getComponent<ComponentType, true, true>(*mCurrentIt, info) : Iterator::getComponent<ComponentType>(info.array->findSector(sectorId), info);
			}

			inline void initIterators(EntitiesRanges& ranges) requires(Ranged) {
				assert(!ranges.ranges.empty());
				
				auto sectorArray = std::get<getIndex<T>()>(mTypeAccessInfo).array;

				mIterators.reserve(ranges.size());
				auto aliveMask = std::get<getIndex<T>()>(mTypeAccessInfo).typeAliveMask;
				while (!ranges.empty()) {
					mCurrentIt = { sectorArray, ranges.back().first };
					mEndIterator = { sectorArray, ranges.back().second };
					ranges.pop_back();

					advanceToAlive(mCurrentIt, mEndIterator, aliveMask);

					mIterators.emplace_back(mCurrentIt, mEndIterator);
				}
				mIterators.shrink_to_fit();
			}

			inline bool advanceToAlive(Memory::SectorsArray::Iterator& iterator, const Memory::SectorsArray::Iterator& endIterator, size_t aliveMask) {
				bool res = false;
				while (iterator != endIterator) {
					auto sector = *iterator;
					if (sector->isAliveData & aliveMask) {
						res = true;
						break;
					}

					++iterator;
				}

				return res;
			}

			template<typename ComponentType, bool Main = false, bool CheckIsAlive = false>
			static inline ComponentType* getComponent(Memory::Sector* sector, const TypeAccessInfo& meta) {
				if constexpr (Main) {
					if constexpr (CheckIsAlive) {
						return sector->isAliveData & meta.typeAliveMask ? reinterpret_cast<ComponentType*>(reinterpret_cast<char*>(sector) + meta.typeOffsetInSector) : nullptr;
					}
					else {
						return reinterpret_cast<ComponentType*>(reinterpret_cast<char*>(sector) + meta.typeOffsetInSector);
					}
				}
				else {
					return sector && (sector->isAliveData & meta.typeAliveMask) ? reinterpret_cast<ComponentType*>(reinterpret_cast<char*>(sector) + meta.typeOffsetInSector) : nullptr;
				}
			}

		private:
			Memory::SectorsArray::Iterator mCurrentIt;

			std::tuple<TypeAccessInfo, decltype((void)sizeof(ComponentTypes), TypeAccessInfo{})... > mTypeAccessInfo;

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
		static inline size_t constexpr getIndex() {
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