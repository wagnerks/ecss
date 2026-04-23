

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
        FORCE_INLINE static ECSType componentTypeId() noexcept { return Memory::DenseTypeIdGenerator::getTypeId<T>(); }

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
                array->tick();  // Free retired memory older than grace period
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

        size_t tick() noexcept requires(ThreadSafe) {
            size_t freed = 0;
            std::shared_lock lock(componentsArrayMapMutex);
            for (auto* array : mComponentsArrays) {
                freed += array->tick();
            }
            return freed;
        }

        void setRetireGracePeriod(uint32_t ticks) noexcept requires(ThreadSafe) {
            std::shared_lock lock(componentsArrayMapMutex);
            for (auto* array : mComponentsArrays) {
                array->setGracePeriod(ticks);
            }
        }

    public:
        template <class T>
        FORCE_INLINE bool hasComponent(EntityId entity) noexcept {
            if constexpr (ThreadSafe) {
                if (auto container = getComponentContainer<T>()) {
                    if (auto pin = container->pinSector(entity)) {
                        return Memory::Sector::isAlive(pin.getIsAlive(), container->template getLayoutData<T>().isAliveMask);
                    }
                }
            }
            else {
                if (auto container = getComponentContainer<T>()) {
                    auto idx = container->template findLinearIdx<false>(entity);
                    if (idx != INVALID_IDX) {
                        return Memory::Sector::isAlive(container->template getIsAliveRef<false>(idx), container->template getLayoutData<T>().isAliveMask);
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

            auto component = Memory::Sector::getComponent<T>(pinnedSector.getData(), pinnedSector.getIsAlive(), container->getLayout());
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

                    auto idx = container->template findLinearIdx<false>(entity);
                    if (idx != INVALID_IDX) {
                        auto& isAlive = container->template getIsAliveRef<false>(idx);
                        auto before = isAlive;
                        Memory::Sector::destroyMember<ThreadSafe>(container->mAllocator.at(idx), isAlive, container->template getLayoutData<T>());
                        if (before != isAlive) {
                            container->incDefragmentSize();
                        }
                    }
                }
                else {
                    auto idx = container->template findLinearIdx<false>(entity);
                    if (idx != INVALID_IDX) {
                        auto& isAlive = container->template getIsAliveRef<false>(idx);
                        auto before = isAlive;
                        Memory::Sector::destroyMember<ThreadSafe>(container->mAllocator.at(idx), isAlive, container->template getLayoutData<T>());
                        if (before != isAlive) {
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

                    prepareEntities(entities, container->template sparseCapacity<false>());
                    if (entities.empty()) {
                        return;
                    }

                    container->mPinsCounter.waitUntilChangeable(entities.front());
                    for (const auto sectorId : entities) {
                        // Use findSlot for single lookup
                        auto slotInfo = container->template findSlot<false>(sectorId);
                        if (slotInfo) {
                            auto& isAlive = container->template getIsAliveRef<false>(slotInfo.linearIdx);
                            auto before = isAlive;
                            Memory::Sector::destroyMember<ThreadSafe>(slotInfo.data, isAlive, layout);
                            if (before != isAlive) {
                                container->incDefragmentSize();
                            }
                        }
                    }
                }
                else {
                    prepareEntities(entities, container->template sparseCapacity<false>());

                    for (const auto sectorId : entities) {
                        // Use findSlot for single lookup
                        auto slotInfo = container->template findSlot<false>(sectorId);
                        if (slotInfo) {
                            auto& isAlive = container->template getIsAliveRef<false>(slotInfo.linearIdx);
                            auto before = isAlive;
                            Memory::Sector::destroyMember<ThreadSafe>(slotInfo.data, isAlive, layout);
                            if (before != isAlive) {
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
                    auto lock = std::shared_lock(componentsArrayMapMutex);
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
            if constexpr (ThreadSafe) {
                // Presence check: cheap early-out without acquiring the write lock.
                {
                    auto lock = std::shared_lock(mEntitiesMutex);
                    if (!mEntities.contains(entityId)) return;
                }
                // Destroy components FIRST, under each array's write lock. If a
                // concurrent destroyEntity(same id) races us, per-array destroy
                // is idempotent and safe.
                destroySector(entityId);
                // Release the id back to the free pool only AFTER components are gone.
                // This closes the window where a concurrent takeEntity() could recycle
                // the id while destroySector was still walking component arrays and
                // would subsequently destroy the newly-emplaced components.
                {
                    auto lock = std::unique_lock(mEntitiesMutex);
                    mEntities.erase(entityId);
                }
            }
            else {
                if (!mEntities.contains(entityId)) return;
                destroySector(entityId);
                mEntities.erase(entityId);
            }
        }

        void destroyEntities(std::vector<EntityId>& entities) noexcept {
            if (entities.empty()) {
                return;
            }

            std::ranges::sort(entities);

            auto destroyInArray = [&](auto* array, const EntityId* begin, const EntityId* end) {
                const auto layout = array->getLayout();
                auto cap = array->template sparseCapacity<false>();
                auto it = std::lower_bound(begin, end, static_cast<EntityId>(cap));
                if (it == begin) return;
                const EntityId* trimmedEnd = it;
                const EntityId* trimmedBegin = begin;

                if constexpr (ThreadSafe) {
                    array->mPinsCounter.waitUntilChangeable(*trimmedBegin);
                }

                for (auto p = trimmedBegin; p != trimmedEnd; ++p) {
                    auto slotInfo = array->template findSlot<false>(*p);
                    if (slotInfo) {
                        Memory::Sector::destroySectorData<ThreadSafe>(slotInfo.data, array->template getIsAliveRef<false>(slotInfo.linearIdx), layout);
                        array->incDefragmentSize();
                    }
                }
            };

            if constexpr (ThreadSafe) {
                decltype(mComponentsArrays) compArrays;
                {
                    auto lock = std::shared_lock(componentsArrayMapMutex);
                    compArrays = mComponentsArrays;
                }

                for (auto* array : compArrays) {
                    auto arrLock = array->writeLock();
                    destroyInArray(array, entities.data(), entities.data() + entities.size());
                }
            }
            else {
                for (auto* array : mComponentsArrays) {
                    destroyInArray(array, entities.data(), entities.data() + entities.size());
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
                    auto lock = std::shared_lock(componentsArrayMapMutex);
                    arrays = mComponentsArrays;
                }

                for (auto array : arrays) {
                    auto lock = array->writeLock();
                    array->mPinsCounter.waitUntilChangeable(entityId);

                    auto idx = array->template findLinearIdx<false>(entityId);
                    if (idx != INVALID_IDX) {
                        Memory::Sector::destroySectorData<ThreadSafe>(array->mAllocator.at(idx), array->template getIsAliveRef<false>(idx), array->getLayout());
                        array->incDefragmentSize();
                    }
                }
            }
            else {
                for (auto array : mComponentsArrays) {
                    auto idx = array->template findLinearIdx<false>(entityId);
                    if (idx != INVALID_IDX) {
                        Memory::Sector::destroySectorData<ThreadSafe>(array->mAllocator.at(idx), array->template getIsAliveRef<false>(idx), array->getLayout());
                        array->incDefragmentSize();
                    }
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

        static void prepareEntities(std::vector<EntityId>& entities, size_t sparseCapacity) {
            if (entities.empty()) { return; }
            std::ranges::sort(entities);

            if (entities.front() >= sparseCapacity) {
                entities.clear();
                return;
            }

            if (entities.back() >= sparseCapacity) {
                int distance = static_cast<int>(entities.size());
                for (auto entity : std::ranges::reverse_view(entities)) {
                    if (entity < sparseCapacity) {
                        break;
                    }
                    distance--;
                }

                entities.erase(entities.begin() + distance, entities.end());
            }
        }

    private:
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
        using SlotInfo = typename Sectors::SlotInfo;

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

            Iterator(const SectorArrays& arrays, SectorsIt iterator, const std::vector<std::pair<Sectors*, SectorsRangeIt>>& secondary, const Ranges<EntityId>* rangeFilter = nullptr)
                : mIterator(std::move(iterator)), mRangeFilter(rangeFilter) {
                initTypeAccessInfo<T, CompTypes...>(arrays, secondary);
                skipOutOfRange();
            }

            FORCE_INLINE value_type operator*() const noexcept { 
                auto slot = *mIterator;
                return { slot.id, 
                         reinterpret_cast<T*>(slot.data + mMainOffset), 
                         getComponent<CompTypes>(slot)... }; 
            }
            FORCE_INLINE Iterator& operator++() noexcept { 
                ++mIterator;
                skipOutOfRange();
                return *this; 
            }

            FORCE_INLINE bool operator!=(const Iterator& other) const noexcept { return mIterator != other.mIterator; }
            FORCE_INLINE bool operator==(const Iterator& other) const noexcept { return mIterator == other.mIterator; }

            // Alive iterator self-checks end condition by returning nullptr.
            FORCE_INLINE bool operator==(const EndIterator&) const noexcept { return !mIterator; }
            FORCE_INLINE bool operator!=(const EndIterator&) const noexcept { return static_cast<bool>(mIterator); }
            FORCE_INLINE friend bool operator==(const EndIterator endIt, const Iterator& it) noexcept { return it == endIt; }
            FORCE_INLINE friend bool operator!=(const EndIterator endIt, const Iterator& it) noexcept { return it != endIt; }

            template<typename Func>
            FORCE_INLINE bool tryInvoke(Func&& func) const noexcept {
                auto slot = *mIterator;
                T* main = reinterpret_cast<T*>(slot.data + mMainOffset);
                if (!(slot.isAlive & std::get<0>(mTypeAccessInfo).typeAliveMask)) return false;
                
                if constexpr (sizeof...(CompTypes) == 0) {
                    func(*main);
                    return true;
                } else {
                    // Recursive template expansion - no tuple allocation
                    return tryInvokeRec(std::forward<Func>(func), slot, main);
                }
            }

        private:
            template<typename Func, typename... Got>
            FORCE_INLINE bool tryInvokeRec(Func&& func, const SlotInfo& slot, T* main, Got*... got) const noexcept {
                if constexpr (sizeof...(Got) == sizeof...(CompTypes)) {
                    // All components collected - invoke
                    func(*main, (*got)...);
                    return true;
                } else {
                    // Get next component type
                    using Next = std::tuple_element_t<sizeof...(Got), std::tuple<CompTypes...>>;
                    Next* next = getComponent<Next>(slot);
                    if (!next) return false;
                    return tryInvokeRec(std::forward<Func>(func), slot, main, got..., next);
                }
            }

        private:
            template<typename ComponentType>
            FORCE_INLINE ComponentType* getComponent(const SlotInfo& slot) const noexcept {
                constexpr auto idx = getIndex<ComponentType>();
                const auto& info = std::get<idx>(mTypeAccessInfo);

                if (info.iteratorIdx == TypeInfo::kMainIteratorIdx) [[likely]] {
                    return (slot.isAlive & info.typeAliveMask) ? reinterpret_cast<ComponentType*>(slot.data + info.typeOffsetInSector) : nullptr;
                }
            // Sparse lookup using optimized findSlot - single lookup returns data ptr + linearIdx
            // O(1) sparse lookup + O(1) dense array access for isAlive
            auto* arr = mSecondaryArrays[info.iteratorIdx];
            auto slotInfo = arr->template findSlot<false>(slot.id);
            if (!slotInfo) [[unlikely]] {
                return nullptr;
            }
            // Route the alive-word read through the seqlock snapshot (loadView) so we
            // don't race with concurrent push_back reallocating the isAlive vector.
            // RetireAllocator keeps old buffers valid, so the snapshot is always safe.
            auto isAlive = arr->loadAliveWord(slotInfo.linearIdx);
            if (!(isAlive & info.typeAliveMask)) [[unlikely]] {
                return nullptr;
            }
                return reinterpret_cast<ComponentType*>(slotInfo.data + info.typeOffsetInSector);
            }

            template<typename... Types>
            void initTypeAccessInfo(const SectorArrays& arrays, const std::vector<std::pair<Sectors*, SectorsRangeIt>>& secondary) noexcept {
                uint8_t arrayIndexes[TypesCount];
                std::fill_n(arrayIndexes, TypesCount, TypeInfo::kMainIteratorIdx);

                for (const auto& [arr, it] : secondary) {
                    mSecondaryArrays[mSecondaryCount] = arr;
                    mSecondaryIterators[mSecondaryCount] = it;  // Use iterators for both TS and non-TS
                    for (size_t a = 0; a < TypesCount; ++a) {
                        if (arrays[a] == arr) {
                            arrayIndexes[a] = mSecondaryCount;
                        }
                    }
                    ++mSecondaryCount;
                }

                (initTypeAccessInfoImpl<Types>(arrays[getIndex<T>()], arrays[getIndex<Types>()], arrayIndexes), ...);
                mMainOffset = std::get<0>(mTypeAccessInfo).typeOffsetInSector;
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
            FORCE_INLINE void skipOutOfRange() {
                if (!mRangeFilter) return;
                while (mIterator) {
                    auto slot = *mIterator;
                    if (mRangeFilter->contains(slot.id)) return;
                    ++mIterator;
                }
            }

            TypeAccessTuple mTypeAccessInfo;
            Sectors*        mSecondaryArrays[CTCount ? CTCount : 1] = {};
            mutable SectorsRangeIt mSecondaryIterators[CTCount ? CTCount : 1];
            SectorsIt       mIterator;
            const Ranges<EntityId>* mRangeFilter = nullptr;
            uint16_t        mMainOffset = 0;
            uint8_t         mSecondaryCount = 0;
        };

        FORCE_INLINE Iterator begin() const noexcept { return mBeginIt; }

        FORCE_INLINE EndIterator end() const noexcept { return {}; }

    public:
        explicit ArraysView(Registry<ThreadSafe, Allocator>* manager) noexcept requires (!Ranged) { init(manager); }
        explicit ArraysView(Registry<ThreadSafe, Allocator>* manager, const Ranges<EntityId>& ranges = {}) noexcept requires (Ranged) { init(manager, ranges); }

        FORCE_INLINE bool empty() const noexcept { return mBeginIt == end(); }

        template<typename Func>
        FORCE_INLINE void each(Func&& func) const {
            if constexpr (sizeof...(CompTypes) == 0 && !Ranged) {
                // Single component fast path - direct chunk iteration
                eachSingle(std::forward<Func>(func));
            } else if constexpr (!Ranged) {
                // Try grouped multi-component fast path
                eachGrouped(std::forward<Func>(func));
            } else {
                for (auto it = mBeginIt; it != end(); ++it) {
                    auto val = *it;
                    auto* main = std::get<1>(val);
                    if (main) {
                        std::apply([&](auto, auto* m, auto*... rest) {
                            if constexpr (sizeof...(rest) == 0) {
                                func(*m);
                            } else if ((rest && ...)) {
                                func(*m, (*rest)...);
                            }
                        }, val);
                    }
                }
            }
        }

    private:
        static void forEachAliveSlot(
            void* const* chunks, size_t numChunks, size_t size,
            const uint32_t* isAliveData, uint32_t aliveMask,
            size_t stride, size_t chunkCapacity,
            void (*callback)(std::byte* slotBase, void* ctx),
            void* ctx)
        {
            size_t idx = 0;
            for (size_t chunkIdx = 0; chunkIdx < numChunks && idx < size; ++chunkIdx) {
                auto* base = static_cast<std::byte*>(chunks[chunkIdx]);
                const size_t chunkEnd = std::min(idx + chunkCapacity, size);
                for (size_t localIdx = 0; idx < chunkEnd; ++idx, ++localIdx) {
                    if ((isAliveData[idx] & aliveMask) == aliveMask) {
                        callback(base + localIdx * stride, ctx);
                    }
                }
            }
        }

        template<typename Func>
        FORCE_INLINE void eachSingle(Func&& func) const requires (sizeof...(CompTypes) == 0 && !Ranged) {
            if (!mMainArray || mSize == 0) return;
            const auto& layout = mMainArray->template getLayoutData<T>();

            struct Ctx { Func* f; uint16_t offset; };
            Ctx ctx{ &func, layout.offset };

            auto view = mMainArray->mDenseArrays.loadView();
            forEachAliveSlot(
                mChunksSnapshot, mChunksCount, mSize,
                view.isAlive, layout.isAliveMask,
                mMainArray->mAllocator.mSectorSize,
                std::remove_reference_t<decltype(mMainArray->mAllocator)>::mChunkCapacity,
                [](std::byte* slot, void* raw) {
                    auto& c = *static_cast<Ctx*>(raw);
                    (*c.f)(*reinterpret_cast<T*>(slot + c.offset));
                },
                &ctx
            );
        }

        template<typename Func>
        FORCE_INLINE void eachGrouped(Func&& func) const requires (sizeof...(CompTypes) > 0 && !Ranged) {
            if (!mMainArray || mSize == 0) return;
            
            if (!mIsGrouped) {
                for (auto it = mBeginIt; it != end(); ++it) {
                    it.tryInvoke(std::forward<Func>(func));
                }
                return;
            }

            struct Ctx {
                Func* f;
                uint16_t mainOff;
                std::array<uint16_t, CTCount> compOffs;
            };
            Ctx ctx{
                &func,
                mMainArray->template getLayoutData<T>().offset,
                { mMainArray->template getLayoutData<CompTypes>().offset... }
            };

            uint32_t combinedMask = 0;
            combinedMask |= mMainArray->template getLayoutData<T>().isAliveMask;
            ((combinedMask |= mMainArray->template getLayoutData<CompTypes>().isAliveMask), ...);

            auto view = mMainArray->mDenseArrays.loadView();
            forEachAliveSlot(
                mChunksSnapshot, mChunksCount, mSize,
                view.isAlive, combinedMask,
                mMainArray->mAllocator.mSectorSize,
                std::remove_reference_t<decltype(mMainArray->mAllocator)>::mChunkCapacity,
                [](std::byte* slot, void* raw) {
                    auto& c = *static_cast<Ctx*>(raw);
                    (*c.f)(*reinterpret_cast<T*>(slot + c.mainOff),
                           *reinterpret_cast<CompTypes*>(slot + c.compOffs[types::typeIndex<CompTypes, CompTypes...>])...);
                },
                &ctx
            );
        }

    private:
        void init(Registry<ThreadSafe, Allocator>* manager, const Ranges<EntityId>& ranges = {}) {
            auto arrays = initArrays<CompTypes..., T>(manager);
            SectorsIt it;

            {
                auto mainArr = arrays[getIndex<T>()];
                mMainArray = mainArr;
                auto lock = mainArr->readLock();
                mSize = mainArr->template size<false>();
                mChunksSnapshot = mainArr->mAllocator.mChunks.data();
                mChunksCount = mainArr->mAllocator.mChunks.size();
                auto effectiveRanges = initRange(mainArr, ranges, getIndex<T>());
                
                // Determine iteration bounds
                size_t startIdx = 0;
                size_t endIdx = mSize;
                
                if constexpr (Ranged) {
                    if (!effectiveRanges.empty()) {
                        // Convert SectorId range bounds to linear indices using binary search
                        // Load atomic view snapshot for thread-safe access
                        auto view = mainArr->mDenseArrays.loadView();
                        const auto* ids = view.ids;
                        // Find start: first linear index where mIds[idx] >= range.first
                        {
                            size_t lo = 0, hi = mSize;
                            while (lo < hi) {
                                size_t mid = lo + (hi - lo) / 2;
                                if (ids[mid] < effectiveRanges.ranges.front().first) lo = mid + 1;
                                else hi = mid;
                            }
                            startIdx = lo;
                        }
                        // Find end: first linear index where mIds[idx] >= range.last (for last range)
                        {
                            size_t lo = 0, hi = mSize;
                            while (lo < hi) {
                                size_t mid = lo + (hi - lo) / 2;
                                if (ids[mid] < effectiveRanges.ranges.back().second) lo = mid + 1;
                                else hi = mid;
                            }
                            endIdx = lo;
                        }
                    }
                }
                
                // Note: isPacked=false because we're filtering by a specific component's alive mask,
                // not just checking if any component is alive. mDefragmentSize==0 only means no dead
                // sectors, not that all sectors have this specific component.
                it = SectorsIt(mainArr, startIdx, endIdx, mainArr->template getLayoutData<T>().isAliveMask, false);
                if constexpr (Ranged) {
                    mRanges = effectiveRanges;
                }
            }

        auto secondary = collectSecondaryArrays(arrays, ranges);
        if constexpr (Ranged) {
            mBeginIt = Iterator{ arrays, it, secondary, &mRanges };
        } else {
            mBeginIt = Iterator{ arrays, it, secondary };
        }
            
            mIsGrouped = secondary.empty();
        }
        
        Ranges<EntityId> initRange(Sectors* sectorsArray, const Ranges<EntityId>& _ranges, size_t i = 0) {
            Ranges<EntityId> ranges = _ranges;

            if constexpr (Ranged) {
                // Convert entity id ranges to linear index ranges
                // For simplicity, keep ranges as-is (they will be filtered during iteration)
                ranges.mergeIntersections();

                if constexpr (ThreadSafe) {
                    auto sz = sectorsArray->template size<false>();
                    mPins[i] = ranges.empty() || sz == 0 
                        ? Memory::PinnedSector{} 
                        : sectorsArray->template pinSectorAt<false>(sz - 1);
                }
            }
            else {
                size_t last;
                if constexpr (ThreadSafe) {
                    mPins[i] = sectorsArray->template pinBackSector<false>();
                    last = mPins[i] ? sectorsArray->template size<false>() : 0;
                }
                else {
                    last = sectorsArray->size();
                }
                ranges.ranges.clear();
                ranges.ranges.emplace_back(0u, static_cast<SectorId>(last));
            }

            return ranges;
        }

        std::vector<std::pair<Sectors*, SectorsRangeIt>> collectSecondaryArrays(const std::array<Sectors*, TypesCount>& arrays, const Ranges<EntityId>& ranges) {
            std::vector<std::pair<Sectors*, SectorsRangeIt>> secondary;
            secondary.reserve(TypesCount - 1);
            auto main = arrays[0];
            for (auto i = 1u; i < arrays.size(); i++) {
                auto arr = arrays[i];
                if (arr == main || std::find_if(secondary.begin(), secondary.end(), [arr](const auto& p){ return p.first == arr; }) != secondary.end()) { continue; }
                if constexpr (ThreadSafe) {
                    // Acquire read lock to protect iterator creation from concurrent modifications
                    auto lock = arr->readLock();
                    secondary.emplace_back(arr, SectorsRangeIt(arr, initRange(arr, ranges, i)));
                } else {
                    // Non-ThreadSafe uses direct lookup, no iterator needed
                    secondary.emplace_back(arr, SectorsRangeIt{});
                }
            }
            return secondary;
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

        Sectors* mMainArray = nullptr;
        void* const* mChunksSnapshot = nullptr;
        size_t mChunksCount = 0;
        size_t mSize = 0;
        bool mIsGrouped = false;
        std::conditional_t<Ranged, Ranges<EntityId>, Dummy> mRanges;
    };
} // namespace ecss
```


