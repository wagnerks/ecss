

# File Registry.h

[**File List**](files.md) **>** [**ecss**](dir_194708e763cf312315c6b23555bce86f.md) **>** [**Registry.h**](Registry_8h.md)

[Go to the documentation of this file](Registry_8h.md)


```C++
#pragma once

 // ecss - Entity Component System with Sectors
 // "Sectors" refers to the logic of storing components.
 // Multiple components of different types can be stored in one memory location, which I've named a "sector."

#include <algorithm>
#include <array>
#include <ranges>
#include <shared_mutex>
#include <tuple>
#include <vector>

#include <ecss/Ranges.h>
#include <ecss/memory/Reflection.h>
#include <ecss/memory/SectorsArray.h>
#include <ecss/memory/Sector.h>

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
        Memory::PinnedSector sec;   
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

        void update(bool withDefragment = true) noexcept requires(!ThreadSafe) {
            for (auto* array : mComponentsArrays) {
                if (withDefragment) {
                    if (array->needDefragment()) {
                        array->defragment();
                    }
                }
            }
        }

    public:
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

        template <class T, class ...Args>
        FORCE_INLINE T* addComponent(EntityId entity, Args&&... args) noexcept {
            return getComponentContainer<T>()->template push<T>(entity, std::forward<Args>(args)...);
        }

        template <class T, typename Func>
        void addComponents(Func&& func) requires(ThreadSafe) {
            auto container = getComponentContainer<T>();
            auto lock = container->writeLock();
            auto f = std::forward<Func>(func);
            auto res = f();
            while (res.first != INVALID_ID) {
                container->template push<T, false>(res.first, res.second);
                res = f();
            }
        }

        template <class T>
        void destroyComponent(EntityId entity) noexcept {
            if (auto container = getComponentContainer<T>()) {
                if constexpr (ThreadSafe) {
                    auto lock = container->writeLock();
                    container->mPinsCounter.waitUntilChangeable(entity);

                    if (auto sector = container->template findSector<false>(entity)) {
                        auto before = sector->isAliveData;
                        sector->destroyMember(container->template getLayoutData<T>());
                        if (before != sector->isAliveData) {
                            container->incDefragmentSize();
                        }
                    }
                }
                else {
                    if (auto sector = container->template findSector<false>(entity)) {
                        auto before = sector->isAliveData;
                        sector->destroyMember(container->template getLayoutData<T>());
                        if (before != sector->isAliveData) {
                            container->incDefragmentSize();
                        }
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
                    if (entities.empty()) {
                        return;
                    }

                    container->mPinsCounter.waitUntilChangeable(entities.front());
                    for (const auto sectorId : entities) {

                        if (auto sector = container->template findSector<false>(sectorId)) {
                            auto before = sector->isAliveData;
                            sector->destroyMember(layout);
                            if (before != sector->isAliveData) {
                                container->incDefragmentSize();
                            }
                        }
                    }
                }
                else {
                    prepareEntities(entities, container->template sectorsMapCapacity<false>());

                    for (const auto sectorId : entities) {
                        if (auto sector = container->template findSector<false>(sectorId)) {
                            auto before = sector->isAliveData;
                            sector->destroyMember(layout);
                            if (before != sector->isAliveData) {
                                container->incDefragmentSize();
                            }
                        }
                    }
                }
            }
        }

        template<typename T, bool TS, typename Alloc>
        FORCE_INLINE void insert(const Memory::SectorsArray<TS, Alloc>& array) noexcept { *getComponentContainer<T>() = array; }

        template<typename T, bool TS, typename Alloc>
        FORCE_INLINE void insert(Memory::SectorsArray<TS, Alloc>&& array) noexcept { *getComponentContainer<T>() = std::move(array); }

    public:
        template<typename... Components>
        FORCE_INLINE auto view(const Ranges<EntityId>& ranges) noexcept { return ArraysView<ThreadSafe, Allocator, true, Components...>{ this, ranges }; }

        template<typename... Components>
        FORCE_INLINE auto view() noexcept { return ArraysView<ThreadSafe, Allocator, false, Components...>{this}; }

        template<typename... Components, typename Func>
        inline void forEachAsync(const std::vector<EntityId>& entities, Func&& func) noexcept requires(ThreadSafe)
        {
            if (entities.empty()) { return; }
            auto f = std::forward<Func>(func);
            for (const auto& entity : entities) { withPinned<Components...>(entity, f); }
        }

    public:
        // ===== Container management ==========================================

        template <class... Components>
        FORCE_INLINE void reserve(uint32_t newCapacity) noexcept { (getComponentContainer<Components>()->reserve(newCapacity), ...); }

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
                    auto lock = std::unique_lock(componentsArrayMapMutex);
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

        template<typename... ComponentTypes>
        void registerArray(uint32_t capacity = 0, Allocator allocator = {}) noexcept {
            if constexpr (ThreadSafe) {
                Memory::SectorsArray<ThreadSafe, Allocator>* sectorsArray;
                {
                    auto lock = std::unique_lock(componentsArrayMapMutex);

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

                    sectorsArray = Memory::SectorsArray<ThreadSafe, Allocator>::template create<ComponentTypes...>(std::move(allocator));
                    mComponentsArrays.push_back(sectorsArray);
                    ((mComponentsArraysMap[componentTypeId<ComponentTypes>()] = sectorsArray), ...);
                }

                sectorsArray->reserve(capacity);
            }
            else {
                Memory::SectorsArray<ThreadSafe, Allocator>* sectorsArray;
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

                sectorsArray = Memory::SectorsArray<ThreadSafe, Allocator>::template create<ComponentTypes...>(std::move(allocator));
                mComponentsArrays.push_back(sectorsArray);
                ((mComponentsArraysMap[componentTypeId<ComponentTypes>()] = sectorsArray), ...);

                sectorsArray->reserve(capacity);
            }
        }

        template <class T>
        [[nodiscard]] Memory::SectorsArray<ThreadSafe, Allocator>* getComponentContainer() noexcept {
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
        // ===== Entities API ===================================================

        FORCE_INLINE bool contains(EntityId entityId) const noexcept {
            if constexpr (ThreadSafe) {
                auto lock = std::shared_lock(mEntitiesMutex);
                return mEntities.contains(entityId);
            }
            else {
                return mEntities.contains(entityId);
            }
        }

        FORCE_INLINE EntityId takeEntity() noexcept
        {
            if constexpr (ThreadSafe) {
                auto lock = std::unique_lock(mEntitiesMutex);
                return mEntities.take();
            }
            else {
                return mEntities.take();
            }
        }

        FORCE_INLINE std::vector<EntityId> getAllEntities() const noexcept
        {
            if constexpr (ThreadSafe) {
                auto lock = std::unique_lock(mEntitiesMutex); return mEntities.getAll();
            }
            else {
                return mEntities.getAll();
            }
        }

        void destroyEntity(EntityId entityId) noexcept {
            if (!contains(entityId)) {
                return;
            }

            if constexpr (ThreadSafe) {
                auto lock = std::unique_lock(mEntitiesMutex);
                mEntities.erase(entityId);
            }
            else {
                mEntities.erase(entityId);
            }

            destroySector(entityId);
        }

        void destroyEntities(std::vector<EntityId>& entities) noexcept {
            if (entities.empty()) {
                return;
            }

            if constexpr (ThreadSafe) {
                decltype(mComponentsArrays) compArrays;
                {
                    auto lock = std::shared_lock(componentsArrayMapMutex);
                    compArrays = mComponentsArrays;
                }

                for (auto* array : compArrays) {
                    auto arrLock = array->writeLock();
                    const auto layout = array->getLayout();

                    auto localEntities = entities;
                    prepareEntities(localEntities, array->template sectorsMapCapacity<false>());
                    if (localEntities.empty()) {
                        continue;
                    }

                    array->mPinsCounter.waitUntilChangeable(localEntities.front());
                    array->incDefragmentSize(static_cast<uint32_t>(localEntities.size()));

                    for (const auto sectorId : localEntities) {
                        Memory::Sector::destroySector(array->template findSector<false>(sectorId), layout);
                    }
                }
            }
            else {
                for (auto* array : mComponentsArrays) {
                    auto localEntities = entities;
                    prepareEntities(localEntities, array->template sectorsMapCapacity<false>());
                    const auto layout = array->getLayout();
                    for (const auto sectorId : localEntities) {
                        Memory::Sector::destroySector(array->template findSector<false>(sectorId), layout);
                        array->incDefragmentSize();
                    }
                }
            }

            if constexpr(ThreadSafe) {
                auto lock = std::unique_lock(mEntitiesMutex);
                for (auto id : entities) {
                    mEntities.erase(id);
                }
            }
            else {
                for (auto id : entities) {
                    mEntities.erase(id);
                }
            }
        }

        template<typename T>
        FORCE_INLINE void defragment() noexcept { if (auto container = getComponentContainer<T>()) { container->defragment();} }

        template<typename T>
        FORCE_INLINE void setDefragmentThreshold(float threshold) { if (auto container = getComponentContainer<T>()) { container->setDefragmentThreshold(threshold); } }

    private:
        void destroySector(EntityId entityId) noexcept {
            if constexpr (ThreadSafe) {
                decltype(mComponentsArrays) arrays;
                {
                    auto lock = std::unique_lock(componentsArrayMapMutex);
                    arrays = mComponentsArrays;
                }

                for (auto array : arrays) {
                    auto lock = array->writeLock();
                    array->mPinsCounter.waitUntilChangeable(entityId);

                    Memory::Sector::destroySector(array->template findSector<false>(entityId), array->getLayout());
                    array->incDefragmentSize();
                }
            }
            else {
                for (auto array : mComponentsArrays) {
                    Memory::Sector::destroySector(array->findSector(entityId), array->getLayout());
                    array->incDefragmentSize();
                }
            }
        }

        template<class... Ts, class F>
        void withPinned(EntityId entity, F&& f) noexcept requires(ThreadSafe) {
            auto pins = std::make_tuple(pinComponent<Ts>(entity)...);
            std::apply([&](auto&... pc) { std::forward<F>(f)(entity, pc.get()...); }, pins);
        }

        template<typename T, class ArraysArr>
        static Memory::PinnedSector pinSector(SectorId pinId, const ArraysArr& arrays, Memory::SectorsArray<ThreadSafe, Allocator>* lockedArr, size_t index) requires(ThreadSafe) {
            auto container = arrays[index];
            if (lockedArr == container) {
                return Memory::PinnedSector(container->mPinsCounter, nullptr, pinId);
            }

            auto lock = container->readLock();
            return Memory::PinnedSector(container->mPinsCounter, nullptr, pinId);
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

        Ranges<EntityId> mEntities; 

        std::vector<Memory::SectorsArray<ThreadSafe, Allocator>*> mComponentsArraysMap;

        std::vector<Memory::SectorsArray<ThreadSafe, Allocator>*> mComponentsArrays;

        struct Dummy{};
        mutable std::conditional_t<ThreadSafe, std::shared_mutex, Dummy> mEntitiesMutex;          
        mutable std::conditional_t<ThreadSafe, std::shared_mutex, Dummy> componentsArrayMapMutex; 
    };

    struct TypeAccessInfo {
        static constexpr uint8_t kMainIteratorIdx = 255; 

        uint32_t typeAliveMask      = 0; 
        uint16_t typeOffsetInSector = 0; 
        uint8_t  iteratorIdx        = kMainIteratorIdx; 
    };

    template <bool ThreadSafe, typename Allocator, bool Ranged, typename T, typename ...CompTypes>
    class ArraysView final {
        using Sectors = Memory::SectorsArray<ThreadSafe, Allocator>;
        using SectorsIt = Sectors::IteratorAlive;
        using SectorsRangeIt = Sectors::RangedIterator;
        using TypeInfo = TypeAccessInfo;

        constexpr static size_t CTCount = sizeof...(CompTypes);
        constexpr static size_t TypesCount = sizeof...(CompTypes) + 1;
        static_assert(TypesCount <= TypeInfo::kMainIteratorIdx - 1, "Too many component types for int8_t iteratorIdx");

    public:
        struct EndIterator {};

        class Iterator {
        public:
            using SectorArrays = std::array<Sectors*, TypesCount>;
            using TypeAccessTuple = std::tuple<TypeInfo, decltype((void)sizeof(CompTypes), TypeInfo{})...>;

            using iterator_category = std::forward_iterator_tag;
            using value_type = std::tuple<EntityId, T*, CompTypes*...>;
            using difference_type = std::ptrdiff_t;
            using pointer = value_type*;
            using reference = value_type&;

        public:
            Iterator() noexcept = default;

            Iterator(const SectorArrays& arrays, SectorsIt iterator, const std::vector<std::pair<Sectors*, SectorsRangeIt>>& otherIterators) : mIterator(std::move(iterator)) {
                initTypeAccessInfo<T, CompTypes...>(arrays, otherIterators);
                advanceAllIteratorsToMainId();
            }

            FORCE_INLINE value_type operator*() const noexcept { auto id = mIterator->id; return { id, getComponent<T>(id), getComponent<CompTypes>(id)... }; }
            FORCE_INLINE Iterator& operator++() noexcept { ++mIterator; advanceAllIteratorsToMainId(); return *this; }

            FORCE_INLINE bool operator!=(const Iterator& other) const noexcept { return mIterator != other.mIterator; }
            FORCE_INLINE bool operator==(const Iterator& other) const noexcept { return mIterator == other.mIterator; }

            // Alive iterator self-checks end condition by returning nullptr.
            FORCE_INLINE bool operator==(const EndIterator&) const noexcept { return *mIterator == nullptr; }
            FORCE_INLINE bool operator!=(const EndIterator&) const noexcept { return *mIterator != nullptr; }
            FORCE_INLINE friend bool operator==(const EndIterator endIt, const Iterator& it) noexcept { return it == endIt; }
            FORCE_INLINE friend bool operator!=(const EndIterator endIt, const Iterator& it) noexcept { return it != endIt; }

        private:
            FORCE_INLINE void advanceAllIteratorsToMainId() noexcept {
                if constexpr (CTCount > 0) {
                    if (mIteratorsSize) {
                        if (auto sector = *mIterator) {
                            const auto id = sector->id;
                            for (size_t i = 0; i < mIteratorsSize; i++) {
                                mArraysIterators[i].advanceToId(id);
                            }
                        }
                    }
                }
            }

            template<typename ComponentType>
            FORCE_INLINE ComponentType* getComponent(EntityId sectorId) const noexcept {
                const auto& info = std::get<getIndex<ComponentType>()>(mTypeAccessInfo);
                if constexpr (std::is_same_v<ComponentType, T>) {
                    return reinterpret_cast<ComponentType*>(mIterator.rawPtr() + info.typeOffsetInSector);
                }
                else {
                    if (info.iteratorIdx == TypeInfo::kMainIteratorIdx) {
                        return mIterator->template getMember<ComponentType>(info.typeOffsetInSector, info.typeAliveMask);
                    }

                    auto sector = *mArraysIterators[info.iteratorIdx];
                    return sector && sector->id == sectorId ? sector->template getMember<ComponentType>(info.typeOffsetInSector, info.typeAliveMask) : nullptr;
                }
            }

            template<typename... Types>
            void initTypeAccessInfo(const SectorArrays& arrays, const std::vector<std::pair<Sectors*, SectorsRangeIt>>& otherIterators) noexcept {
                uint8_t iteratorIndexes[TypesCount];
                std::fill_n(iteratorIndexes, TypesCount, TypeInfo::kMainIteratorIdx);

                for (const auto& [arr, it ] : otherIterators) {
                    mArraysIterators[mIteratorsSize] = it;
                    for (size_t a = 0; a < TypesCount; ++a) {
                        if (arrays[a] == arr) {
                            iteratorIndexes[a] = mIteratorsSize;
                        }
                    }

                    ++mIteratorsSize;
                }

                (initTypeAccessInfoImpl<Types>(arrays[getIndex<T>()], arrays[getIndex<Types>()], iteratorIndexes), ...);
            }

            template<typename ComponentType>
            FORCE_INLINE void initTypeAccessInfoImpl(Sectors* main, Sectors* sectorArray, uint8_t* iteratorIndexes) noexcept {
                constexpr auto idx = getIndex<ComponentType>();
                auto& info = std::get<idx>(mTypeAccessInfo);
                const auto& layout = sectorArray->template getLayoutData<ComponentType>();
                info.typeAliveMask = layout.isAliveMask;
                info.typeOffsetInSector = layout.offset;
                if (sectorArray != main) {
                    info.iteratorIdx = iteratorIndexes[idx];
                }
            }

        private:
            TypeAccessTuple mTypeAccessInfo;
            SectorsRangeIt  mArraysIterators[CTCount ? CTCount : 1];
            SectorsIt       mIterator;
            uint8_t         mIteratorsSize = 0;
        };

        FORCE_INLINE Iterator begin() const noexcept { return mBeginIt; }

        FORCE_INLINE EndIterator end() const noexcept { return {}; }

    public:
        explicit ArraysView(Registry<ThreadSafe, Allocator>* manager) noexcept requires (!Ranged) { init(manager); }

        explicit ArraysView(Registry<ThreadSafe, Allocator>* manager, const Ranges<EntityId>& ranges = {}) noexcept requires (Ranged) { init(manager, ranges); }

        FORCE_INLINE bool empty() const noexcept { return mBeginIt == end(); }

    private:
        void init(Registry<ThreadSafe, Allocator>* manager, const Ranges<EntityId>& ranges = {}) {
            auto arrays = initArrays<CompTypes..., T>(manager);
            SectorsIt it;

            {
                auto mainArr = arrays[getIndex<T>()];
                auto lock = mainArr->readLock();
                it = SectorsIt(mainArr, initRange(mainArr, ranges, getIndex<T>()), mainArr->template getLayoutData<T>().isAliveMask);
            }

            mBeginIt = Iterator{ arrays, it, fillOtherIterators(arrays, ranges) };
        }
        
        Ranges<EntityId> initRange(Sectors* sectorsArray, const Ranges<EntityId>& _ranges, size_t i = 0) {
            Ranges<EntityId> ranges = _ranges;

            if constexpr (Ranged) {
                for (auto& range : ranges.ranges) {
                    range.first = static_cast<EntityId>(sectorsArray->template findRightNearestSectorIndex<false>(range.first));
                    range.second = static_cast<EntityId>(sectorsArray->template findRightNearestSectorIndex<false>(range.second));
                }
                ranges.mergeIntersections();

                if constexpr (ThreadSafe) {
                    mPins[i] = ranges.empty() ? Memory::PinnedSector{} : Memory::PinnedSector(sectorsArray->mPinsCounter, nullptr, ranges.back().second);
                }
            }
            else {
                size_t last;
                if constexpr (ThreadSafe) {
                    mPins[i] = sectorsArray->template pinBackSector<false>();
                    last = mPins[i].getId();
                    last = last == INVALID_ID ? 0 : sectorsArray->template getSectorIndex<false>(static_cast<SectorId>(last)) + 1;
                }
                else {
                    last = sectorsArray->size();
                }
                ranges.ranges.emplace_back(0u, static_cast<SectorId>(last));
            }

            return ranges;
        }

        std::vector<std::pair<Sectors*, SectorsRangeIt>> fillOtherIterators(const std::array<Sectors*, TypesCount>& arrays, const Ranges<EntityId>& ranges) {
            std::vector<std::pair<Sectors*, SectorsRangeIt>> iterators;
            iterators.reserve(TypesCount - 1);
            auto main = arrays[0];
            for (auto i = 1u; i < arrays.size(); i++) {
                auto arr = arrays[i];
                if (arr == main || std::find_if(iterators.begin(), iterators.end(), [arr](const auto& a){ return a.first == arr; }) != iterators.end()) { continue; }

                auto lock = arr->readLock();
                iterators.emplace_back(arr, SectorsRangeIt(arr, initRange(arr, ranges, i)));
            }

            return iterators;
        }

        template<typename ComponentType>
        FORCE_INLINE static size_t consteval getIndex() noexcept {
            if constexpr (std::is_same_v<T, ComponentType>) { return 0; }
            else { return types::typeIndex<ComponentType, CompTypes...> + 1; }
        }

        template<typename... Types>
        FORCE_INLINE std::array<Sectors*, TypesCount> initArrays(Registry<ThreadSafe, Allocator>* registry) noexcept {
            std::array<Sectors*, TypesCount> arrays;

            static_assert(types::areUnique<Types...>, "Duplicates detected in types");
            ((arrays[getIndex<Types>()] = registry->template getComponentContainer<Types>()), ...);

            return arrays;
        }

    private:
        struct Dummy{};
        std::conditional_t<ThreadSafe, std::array<Memory::PinnedSector, TypesCount>, Dummy> mPins;  
        Iterator mBeginIt;                                   
    };
} // namespace ecss
```


