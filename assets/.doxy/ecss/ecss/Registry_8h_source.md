

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
#include <atomic>
#include <shared_mutex>
#include <tuple>
#include <vector>

#include <ecss/Access.h>
#include <ecss/AccessTracker.h>
#include <ecss/IdSet.h>
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

        template <class... ComponentTypes>
        void warnPartialGrouping() const noexcept {
            #ifndef ECSS_NO_GROUPING_WARNINGS
            std::fprintf(stderr, "ecss: registerArray<");
            bool first = true;
            ((std::fprintf(stderr, "%s%.*s", first ? "" : ", ",
                static_cast<int>(detail::typeName<ComponentTypes>().size()),
                detail::typeName<ComponentTypes>().data()), first = false), ...);
            std::fprintf(stderr,
                ">() was ignored: some of those types already have arrays and some do\n"
                "      not, so the group cannot be formed. Register the whole group before\n"
                "      anything adds one of its members.\n");
            std::fflush(stderr);
            #endif
        }

        template <class... ComponentTypes>
        void warnIfGroupingIgnored() const noexcept {
            if constexpr (sizeof...(ComponentTypes) > 1) {
                const Memory::SectorsArray<ThreadSafe, Allocator>* const found[] = {
                    mComponentsArraysMap[componentTypeId<ComponentTypes>()]...
                };
                bool together = true;
                for (const auto* a : found) { together = together && a == found[0]; }
                if (together) { return; }

                // A warning rather than the assert its two neighbours in registerArray() use. Those
                // guard a state that is actually broken -- a half-registered group. This is a missed
                // optimisation: everything still works, just in separate arrays. Aborting a debug
                // build over it would be out of proportion, and it would make the case untestable
                // in exactly the configuration CI runs.
                #ifndef ECSS_NO_GROUPING_WARNINGS
                std::fprintf(stderr, "ecss: registerArray<");
                bool first = true;
                ((std::fprintf(stderr, "%s%.*s", first ? "" : ", ",
                    static_cast<int>(detail::typeName<ComponentTypes>().size()),
                    detail::typeName<ComponentTypes>().data()), first = false), ...);
                std::fprintf(stderr,
                    ">() was ignored: those types already have separate arrays, and a\n"
                    "      component's layout is fixed when its array is created. Group them\n"
                    "      before the first addComponent() or getComponentContainer() that\n"
                    "      names them. Define ECSS_NO_GROUPING_WARNINGS to silence this.\n");
                std::fflush(stderr);
                #endif
            }
        }

    public:
        Registry(const Registry& other) noexcept = delete;
        Registry& operator=(const Registry& other) noexcept = delete;
        Registry(Registry&& other) noexcept = delete;
        Registry& operator=(Registry&& other) noexcept = delete;

    public:
        Registry() noexcept {
            if constexpr (!ThreadSafe) {
                // No lock-free readers here, so a superseded snapshot has nothing to outlive.
                mNodeBin.setGracePeriod(0);
            }
        }

        ~Registry() noexcept {
            for (auto array : mComponentsArrays) delete array;
            // Superseded snapshots are in the bin, which frees them on destruction; the one
            // still published is not, so it is freed here.
            std::free(mRegistered.load(std::memory_order_relaxed));
        }

        void update(bool withDefragment = true) noexcept requires(ThreadSafe) {
            mNodeBin.tick();    // superseded snapshots past their grace period
            const auto [begin, end] = registeredArrays();
            for (auto it = begin; it != end; ++it) {
                (*it)->tick();  // Free retired memory older than grace period
                (*it)->processPendingErases(withDefragment);
            }

            // The other half of clearAsync(): the ids it withheld are released here, once no
            // array is still carrying the clear it asked for. Until then an id handed back
            // would name a sector whose components are still there.
            if (mPendingEntityClear.load(std::memory_order_acquire)) {
                bool stillPending = false;
                for (auto it = begin; it != end; ++it) {
                    if ((*it)->hasPendingClear()) { stillPending = true; break; }
                }
                if (!stillPending) {
                    std::unique_lock lock(mEntitiesMutex);
                    mEntities.clear();
                    mPendingEntityClear.store(false, std::memory_order_release);
                }
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

        size_t tick() noexcept {
            size_t freed = mNodeBin.tick();
            const auto [begin, end] = registeredArrays();
            for (auto it = begin; it != end; ++it) {
                freed += (*it)->tick();
            }
            return freed;
        }

        void setRetireGracePeriod(uint32_t ticks) noexcept {
            if constexpr (ThreadSafe) {
                // Zero means "free on release, nothing lock-free is reading" -- true of
                // Registry<false> and of nothing else. Honouring it here would hand a reader
                // that is mid-walk a chunk that has already been freed, so it is clamped
                // rather than obeyed.
                assert(ticks != 0
                    && "a thread-safe registry cannot retire with no grace period; "
                       "lock-free readers need the blocks to outlive them");
                if (ticks == 0) { ticks = 1; }
            }

            // Remembered as well as applied. Arrays registered later used to be built with the
            // default and never told, so the setting quietly covered only whatever happened to
            // exist when it was called.
            mRetireGracePeriod.store(ticks, std::memory_order_seq_cst);

            const auto [begin, end] = registeredArrays();
            for (auto it = begin; it != end; ++it) {
                (*it)->setRetireGracePeriod(ticks);
            }
        }

    public:
        template <class T>
        FORCE_INLINE bool hasComponent(EntityId entity) noexcept {
            // lookupComponentAccess, not getComponentAccess: a question must not register the
            // type it is asking about. The latter creates the array on first use, so
            // hasComponent<T>() on a type nothing had ever added allocated one to answer false
            // -- and left T registered on its own, which quietly put a later
            // registerArray<T, U>() beyond reach.
            const auto access = lookupComponentAccess<T>();
            if (!access) { return false; }
            auto container = access.array;
            // No pin and no lock: this hands out no pointer, so there is nothing to keep
            // alive. The slot lookup and the alive word both go through the lock-free
            // snapshots (SparseMap seqlock + DenseArrays seqlock), which is exactly what
            // ArraysView::getComponent already does. Pinning here cost two seq_cst RMWs
            // plus a potential wake syscall, and the shared lock serialised every reader,
            // for an answer that is inherently a point-in-time sample either way.
            // findLinearIdx, not findSlot: the latter also resolves the sector's data pointer
            // through the chunk table, and this answer never looks at the data. That address
            // computation was the whole difference against a hand-rolled check.
            const auto idx = container->template findLinearIdx<false>(entity);
            if (idx == INVALID_IDX) {
                return false;
            }
            return Memory::Sector::isAlive(container->template loadAliveWord<ThreadSafe>(idx),
                                           access.layout->isAliveMask);
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
            const auto guard = detail::writeScope<T>(componentTypeId<T>());
            return getComponentContainer<T>()->template push<T>(entity, std::forward<Args>(args)...);
        }

        template <class T, typename Func>
        void addComponents(Func&& func) {
            // Drain the generator first. Holding the write lock across it saved the per-element
            // lock traffic but still inserted one id at a time, so a generator that emitted ids
            // out of order paid O(M*N): each middle insert shifts the tail and rewrites the
            // sparse entry of every sector it passes. Sorting the batch once and merging it is
            // linear instead -- see SectorsArray::insertBulk.
            //
            // The generator therefore runs outside the lock now. Its side effects are no longer
            // serialised with the insertion, but the insertion itself is still one atomic batch,
            // and a generator that touched this same container under the lock would have
            // deadlocked before.
            std::vector<std::pair<EntityId, T>> batch;
            auto f = std::forward<Func>(func);
            for (auto res = f(); res.first != INVALID_ID; res = f()) {
                batch.emplace_back(res.first, std::move(res.second));
            }
            if (batch.empty()) { return; }
            const auto guard = detail::writeScope<T>(componentTypeId<T>());
            getComponentContainer<T>()->template insertBulk<T>(batch.begin(), batch.end());
        }

        template <class T, class It>
        void insertBulk(It first, It last) {
            const auto guard = detail::writeScope<T>(componentTypeId<T>());
            getComponentContainer<T>()->template insertBulk<T>(first, last);
        }

        template <class T>
        void destroyComponent(EntityId entity) noexcept {
            const auto guard = detail::writeScope<T>(componentTypeId<T>());
            if (auto container = getComponentContainer<T>()) {
                if constexpr (ThreadSafe) {
                    // Lock-free presence check before locking (see destroySector).
                    if (!container->template containsSector<false>(entity)) {
                        return;
                    }
                    // Destroys one member in place -- only that sector must be unpinned.
                    container->exclusiveWhenUnpinned(entity, [&] {
                        auto idx = container->template findLinearIdx<false>(entity);
                        if (idx != INVALID_IDX) {
                            auto& isAlive = container->template getIsAliveRef<false>(idx);
                            auto before = isAlive;
                            Memory::Sector::destroyMember<ThreadSafe>(container->mAllocator.at(idx), isAlive, container->template getLayoutData<T>());
                            if (before != isAlive && !Memory::Sector::isSectorAlive(isAlive)) {
                                container->incDefragmentSize();
                            }
                        }
                    });
                }
                else {
                    auto idx = container->template findLinearIdx<false>(entity);
                    if (idx != INVALID_IDX) {
                        auto& isAlive = container->template getIsAliveRef<false>(idx);
                        auto before = isAlive;
                        Memory::Sector::destroyMember<ThreadSafe>(container->mAllocator.at(idx), isAlive, container->template getLayoutData<T>());
                        if (before != isAlive && !Memory::Sector::isSectorAlive(isAlive)) {
                            container->incDefragmentSize();
                        }
                    }
                }
            }
        }

        template <class T>
        void destroyComponent(std::vector<EntityId>& entities) noexcept {
            if (entities.empty()) {return;}
            const auto guard = detail::writeScope<T>(componentTypeId<T>());

            if (auto container = getComponentContainer<T>()) {
                const auto& layout = container->template getLayoutData<T>();
                if constexpr (ThreadSafe) {
                    // A whole batch of sectors is touched, so require quiescence rather than
                    // testing them one by one; the wait happens outside the write lock.
                    container->exclusiveWhenQuiescent([&] {
                        prepareEntities(entities, container->template sparseCapacity<false>());
                        if (entities.empty()) {
                            return;
                        }

                        for (const auto sectorId : entities) {
                            // Use findSlot for single lookup
                            auto slotInfo = container->template findSlot<false>(sectorId);
                            if (slotInfo) {
                                auto& isAlive = container->template getIsAliveRef<false>(slotInfo.linearIdx);
                                auto before = isAlive;
                                Memory::Sector::destroyMember<ThreadSafe>(slotInfo.data, isAlive, layout);
                                if (before != isAlive && !Memory::Sector::isSectorAlive(isAlive)) {
                                    container->incDefragmentSize();
                                }
                            }
                        }
                    });
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
                            if (before != isAlive && !Memory::Sector::isSectorAlive(isAlive)) {
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
        void setAutoMaintenance(bool enabled) noexcept { storeAutoMaintenance(enabled); }

        template <typename... Claims>
        [[nodiscard]] detail::AccessGuard access() {
            static_assert(sizeof...(Claims) > 0, "name at least one component type");
            std::array<detail::AccessGuard::Entry, sizeof...(Claims)> claims{
                detail::AccessGuard::Entry{
                    &typeMutex(componentTypeId<typename Claims::Component>()),
                    componentTypeId<typename Claims::Component>(),
                    Claims::kWrites,
                    false }...
            };
            return detail::AccessGuard(claims);
        }

        void setAccessTracking(bool enabled) noexcept {
#ifndef NDEBUG
            detail::AccessTracker::setEnabled(enabled);
#else
            (void)enabled;
#endif
        }

        [[nodiscard]] bool autoMaintenance() const noexcept { return loadAutoMaintenance(); }

        template<typename... Components>
        FORCE_INLINE auto view(const Ranges<EntityId>& ranges) noexcept {
            maintainFor<Components...>();
            return ArraysView<ThreadSafe, Allocator, true, Components...>{ this, ranges };
        }

        template<typename... Components>
        FORCE_INLINE auto view() noexcept {
            maintainFor<Components...>();
            return ArraysView<ThreadSafe, Allocator, false, Components...>{this};
        }

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
                    const auto [begin, end] = registeredArrays();
                    for (auto it = begin; it != end; ++it) {
                        (*it)->clear();
                    }
                }

                std::unique_lock lock(mEntitiesMutex);
                mEntities.clear();
                mPendingEntityClear.store(false, std::memory_order_release);
            }
            else {
                for (auto* array : mComponentsArrays) {
                    array->clear();
                }

                mEntities.clear();
            }
        }

        void clearAsync() noexcept {
            if constexpr (ThreadSafe) {
                const auto [begin, end] = registeredArrays();
                for (auto it = begin; it != end; ++it) { (*it)->clearAsync(); }
                mPendingEntityClear.store(true, std::memory_order_release);
            }
            else {
                clear();
            }
        }

        void defragment() noexcept {
            if constexpr(ThreadSafe) {
                const auto [begin, end] = registeredArrays();
                for (auto it = begin; it != end; ++it) {
                    (*it)->defragment();
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
                        warnPartialGrouping<ComponentTypes...>();
                        assert(false && "Partial registerArray across mixed components is not allowed");
                        return;
                    }

                    bool isCreated = true;
                    ((isCreated = isCreated && mComponentsArraysMap.size() > componentTypeId<ComponentTypes>() && mComponentsArraysMap[componentTypeId<ComponentTypes>()]), ...);
                    if (isCreated) {
                        warnIfGroupingIgnored<ComponentTypes...>();
                        return;
                    }

                    ECSType maxId = 0;
                    ((maxId = std::max(maxId, componentTypeId<ComponentTypes>())), ...);
                    if (maxId >= mComponentsArraysMap.size()) {
                        mComponentsArraysMap.resize(maxId + 1);
                    }

                    sectorsArray = Memory::SectorsArray<ThreadSafe, Allocator>::template create<ComponentTypes...>(std::move(allocator));
                    // Whatever setRetireGracePeriod() last asked for applies to this one too.
                    // Only in the thread-safe build: the plain one deliberately runs at zero,
                    // having no lock-free readers for the blocks to outlive.
                    if constexpr (ThreadSafe) {
                        sectorsArray->setRetireGracePeriod(
                            mRetireGracePeriod.load(std::memory_order_seq_cst));
                    }
                    mComponentsArrays.push_back(sectorsArray);
                    ((mComponentsArraysMap[componentTypeId<ComponentTypes>()] = sectorsArray), ...);
                    // Resolve each component's LayoutData once, here, where the pack is still a
                    // compile-time thing. Every later lookup would otherwise re-derive it by
                    // scanning the layout's type tokens, which is what made a grouped array
                    // cost more per query the more types it held.
                    recordLayouts<ComponentTypes...>(sectorsArray);
                    publishRegistered();
                }

                sectorsArray->reserve(capacity);
            }
            else {
                Memory::SectorsArray<ThreadSafe, Allocator>* sectorsArray;
                bool anyPresent = ((mComponentsArraysMap.size() > componentTypeId<ComponentTypes>() && mComponentsArraysMap[componentTypeId<ComponentTypes>()] != nullptr) || ...);
                bool allPresent = ((mComponentsArraysMap.size() > componentTypeId<ComponentTypes>() && mComponentsArraysMap[componentTypeId<ComponentTypes>()] != nullptr) && ...);
                if (anyPresent && !allPresent) {
                    warnPartialGrouping<ComponentTypes...>();
                    assert(false && "Partial registerArray across mixed components is not allowed");
                    return;
                }

                bool isCreated = true;
                ((isCreated = isCreated && mComponentsArraysMap.size() > componentTypeId<ComponentTypes>() && mComponentsArraysMap[componentTypeId<ComponentTypes>()]), ...);
                if (isCreated) {
                    warnIfGroupingIgnored<ComponentTypes...>();
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
                recordLayouts<ComponentTypes...>(sectorsArray);
                publishRegistered();

                sectorsArray->reserve(capacity);
            }
        }

        template <class T>
        [[nodiscard]] Memory::SectorsArray<ThreadSafe, Allocator>* getComponentContainer() noexcept {
            // Lock-free: one acquire load of the published snapshot. This is on the entry of
            // every single registry call, and taking a registry-global shared_lock here made
            // all of them contend on one cache line (measured ~300x per-op latency at 32
            // threads). The snapshot is safe because registration is append-only -- an entry,
            // once set, is never cleared or repointed before destruction.
            const auto componentType = componentTypeId<T>();
            if (const auto* node = mRegistered.load(std::memory_order_acquire)) [[likely]] {
                if (componentType < node->mapCount) {
                    if (auto* array = node->map[componentType]) [[likely]] {
                        return array;
                    }
                }
            }

            return registerArray<T>(), getComponentContainer<T>();
        }

        struct ComponentAccess {
            Memory::SectorsArray<ThreadSafe, Allocator>* array = nullptr;
            const Memory::LayoutData* layout = nullptr;
            explicit operator bool() const noexcept { return array != nullptr; }
        };

        template <class T>
        [[nodiscard]] FORCE_INLINE ComponentAccess getComponentAccess() noexcept {
            if (const auto access = lookupComponentAccess<T>()) [[likely]] {
                return access;
            }
            // First use of T: register it, then look again. Written as a second lookup rather
            // than a recursive call because GCC rejects always_inline on a function that calls
            // itself, even down a branch taken once per component type.
            registerArray<T>();
            return lookupComponentAccess<T>();
        }

        template <class T>
        [[nodiscard]] FORCE_INLINE ComponentAccess lookupComponentAccess() noexcept {
            const auto componentType = componentTypeId<T>();
            if (const auto* node = mRegistered.load(std::memory_order_acquire)) [[likely]] {
                if (componentType < node->mapCount) {
                    if (auto* array = node->map[componentType]) [[likely]] {
                        const auto* layout = node->layout[componentType];
                        if (layout && node->meta[componentType] == array->getLayout()) [[likely]] {
                            return { array, layout };
                        }
                        // Repointed since registration: fall back to asking the array itself.
                        return { array, &array->template getLayoutData<T>() };
                    }
                }
            }
            return {};
        }

    public:
        // ===== Entities API ===================================================

        FORCE_INLINE bool contains(EntityId entityId) const noexcept { return mEntities.contains(entityId); }

        FORCE_INLINE EntityId takeEntity() noexcept { return mEntities.take(); }

        FORCE_INLINE void takeEntities(size_t count, std::vector<EntityId>& out) noexcept {
            mEntities.take(count, out);
        }

        FORCE_INLINE std::vector<EntityId> getAllEntities() const noexcept
        {
            if constexpr (ThreadSafe) {
                // Shared here only to exclude clear(), which is the one operation the id set
                // cannot absorb concurrently. take/erase run lock-free alongside.
                auto lock = std::shared_lock(mEntitiesMutex); return mEntities.getAll();
            }
            else {
                return mEntities.getAll();
            }
        }

        void destroyEntity(EntityId entityId) noexcept {
            // Presence check: cheap lock-free early-out.
            if (!mEntities.contains(entityId)) return;
            // Destroy components FIRST, under each array's write lock. If a concurrent
            // destroyEntity(same id) races us, per-array destroy is idempotent and safe.
            destroySector(entityId);
            // Release the id only AFTER the components are gone. This ordering is what
            // closes the window where a concurrent takeEntity() could recycle the id while
            // destroySector was still walking the component arrays and would then destroy
            // the freshly emplaced components. It is program order, not the mutex, that
            // provided it -- so it survives the id set becoming lock-free.
            mEntities.erase(entityId);
        }

        void destroyEntities(std::vector<EntityId>& entities) noexcept {
            if (entities.empty()) {
                return;
            }

            // Checking costs 0.18 ns per entity, sorting 3.4 on an already-ordered list and 47
            // on a shuffled one. Callers usually have order for free -- ids gathered by walking
            // a view or getAllEntities() come out ascending -- and were paying for a pass that
            // had nothing to do.
            if (!std::is_sorted(entities.begin(), entities.end())) {
                std::sort(entities.begin(), entities.end());
            }

            auto destroyInArray = [&](auto* array, const EntityId* begin, const EntityId* end) {
                const auto layout = array->getLayout();
                // The list is sorted, so from the first id past this array's sparse capacity
                // onwards nothing can be in it. Read under the caller's lock.
                const auto cap = static_cast<EntityId>(array->template sparseCapacity<false>());
                const EntityId* const trimmedEnd = std::lower_bound(begin, end, cap);
                if (trimmedEnd == begin) { return; }

                for (auto p = begin; p != trimmedEnd; ++p) {
                    auto slotInfo = array->template findSlot<false>(*p);
                    if (slotInfo) {
                        Memory::Sector::destroySectorData<ThreadSafe>(slotInfo.data, array->template getIsAliveRef<false>(slotInfo.linearIdx), layout);
                        array->incDefragmentSize();
                    }
                }
            };

            if constexpr (ThreadSafe) {
                const auto [begin, end] = registeredArrays();
                for (auto it = begin; it != end; ++it) {
                    auto* array = *it;
                    // Destroying in place moves no sector, so only the sectors being destroyed
                    // need to be unpinned. exclusiveWhenQuiescent waited for every pin and hold
                    // on the array instead, and one unrelated holder is enough to stall it: a
                    // camera pinning a Transform stopped a world unload that never touched that
                    // entity. This matches what destroySector() already did for a single id.
                    //
                    // The wait covers the whole list while destroyInArray trims it to the
                    // array's capacity, and that direction is the safe one. Trimming the wait to
                    // match would mean reading the capacity out here, before the lock; if it grew
                    // in between, the destroy would reach an id nothing had waited for -- a
                    // pinned sector destroyed underneath its holder.
                    array->exclusiveWhenUnpinned(entities.data(), entities.data() + entities.size(), [&] {
                        destroyInArray(array, entities.data(), entities.data() + entities.size());
                    });
                }
            }
            else {
                for (auto* array : mComponentsArrays) {
                    destroyInArray(array, entities.data(), entities.data() + entities.size());
                }
            }

            // One operation per word rather than per id: the list is sorted by now, so runs of
            // ids share a word and clear together.
            mEntities.erase(entities.data(), entities.data() + entities.size());
        }

        template<typename T>
        FORCE_INLINE void defragment() noexcept { if (auto container = getComponentContainer<T>()) { container->defragment();} }

        template<typename T>
        FORCE_INLINE void setDefragmentThreshold(float threshold) { if (auto container = getComponentContainer<T>()) { container->setDefragmentThreshold(threshold); } }

    private:
        void destroySector(EntityId entityId) noexcept {
            if constexpr (ThreadSafe) {
                // Snapshot walk: this runs once per destroyEntity, and the old form took a
                // registry-global shared_lock and heap-copied the array list every time.
                const auto [begin, end] = registeredArrays();

                for (auto it = begin; it != end; ++it) {
                    auto* array = *it;
                    // Lock-free presence check first. Taking the write lock and only then asking
                    // whether the entity is even in this array made destroyEntity cost one lock
                    // acquisition per *registered* array rather than per array the entity is
                    // actually in -- 20.7 ns with one array, 116 ns with nine.
                    //
                    // A concurrent addComponent for this entity could land just after the check,
                    // but destroying an entity while another thread adds components to it is
                    // already unordered; within one thread program order settles it.
                    if (!array->template containsSector<false>(entityId)) {
                        continue;
                    }
                    // In-place destroy of one sector: only that sector must be unpinned, and
                    // the wait must not happen under the write lock (it would deadlock any
                    // pin holder that still needs the shared lock).
                    array->exclusiveWhenUnpinned(entityId, [&] {
                        auto idx = array->template findLinearIdx<false>(entityId);
                        if (idx != INVALID_IDX) {
                            Memory::Sector::destroySectorData<ThreadSafe>(array->mAllocator.at(idx), array->template getIsAliveRef<false>(idx), array->getLayout());
                            array->incDefragmentSize();
                        }
                    });
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

        static void prepareEntities(std::vector<EntityId>& entities, size_t sparseCapacity) {
            if (entities.empty()) { return; }
            std::sort(entities.begin(), entities.end());

            if (entities.front() >= sparseCapacity) {
                entities.clear();
                return;
            }

            if (entities.back() >= sparseCapacity) {
                int distance = static_cast<int>(entities.size());
                for (auto it = entities.rbegin(); it != entities.rend(); ++it) {
                    if (*it < sparseCapacity) {
                        break;
                    }
                    distance--;
                }

                entities.erase(entities.begin() + distance, entities.end());
            }
        }

    private:
        struct Registered {
            size_t mapCount = 0;
            Memory::SectorsArray<ThreadSafe, Allocator>** map = nullptr;
            const Memory::LayoutData** layout = nullptr;
            const Memory::SectorLayoutMeta** meta = nullptr;
            size_t listCount = 0;
            Memory::SectorsArray<ThreadSafe, Allocator>** list = nullptr;
        };

        std::shared_mutex& typeMutex(ECSType type) {
            {
                auto shared = std::shared_lock(mTypeMutexesGrowth);
                if (type < mTypeMutexes.size() && mTypeMutexes[type]) { return *mTypeMutexes[type]; }
            }
            auto unique = std::unique_lock(mTypeMutexesGrowth);
            if (type >= mTypeMutexes.size()) { mTypeMutexes.resize(static_cast<size_t>(type) + 1); }
            if (!mTypeMutexes[type]) { mTypeMutexes[type] = std::make_unique<std::shared_mutex>(); }
            return *mTypeMutexes[type];
        }

        template <class... ComponentTypes>
        void recordLayouts(Memory::SectorsArray<ThreadSafe, Allocator>* array) {
            ECSType maxId = 0;
            ((maxId = std::max(maxId, componentTypeId<ComponentTypes>())), ...);
            if (maxId >= mComponentsLayoutsMap.size()) {
                mComponentsLayoutsMap.resize(maxId + 1, nullptr);
                mComponentsMetaMap.resize(maxId + 1, nullptr);
            }
            ((mComponentsLayoutsMap[componentTypeId<ComponentTypes>()] =
                &array->template getLayoutData<ComponentTypes>()), ...);
            ((mComponentsMetaMap[componentTypeId<ComponentTypes>()] = array->getLayout()), ...);
        }

        template<typename... Components>
        FORCE_INLINE void maintainFor() noexcept {
            if (!loadAutoMaintenance()) [[likely]] { return; }
            (maintainArrayFor<Components>(), ...);
            maintainOneInRotation();
        }

        template<typename T>
        void maintainArrayFor() noexcept {
            if (auto* array = getComponentContainer<T>()) { maintainArray(array); }
        }

        void maintainOneInRotation() noexcept {
            // Not on every view. The rotation exists so an array nobody iterates is still
            // reached eventually, and once every kRotationStride views is enough for that.
            // Charging every view was measurable at +36 ns, a quarter of what opening a view
            // costs; the curve flattens by a stride of four (+23 ns) and gains nothing after,
            // so four it is -- four times the coverage of sixteen for the same price.
            // Racy on purpose: the ticket only decides whose turn it is.
            const auto ticket = mMaintainCursor.fetch_add(1, std::memory_order_relaxed);
            if ((ticket % kRotationStride) != 0) [[likely]] { return; }

            const auto [begin, end] = registeredArrays();
            const auto count = static_cast<size_t>(end - begin);
            if (count == 0) { return; }
            maintainArray(begin[(ticket / kRotationStride) % count]);
        }

        static constexpr size_t kRotationStride = 4;

        void maintainArray(Memory::SectorsArray<ThreadSafe, Allocator>* array) noexcept {
            if constexpr (ThreadSafe) {
                // Neither bin is ticked here, and for the same reason. A grace period counts how
                // long a lock-free reader might still be walking a buffer that was replaced --
                // it is a claim about readers, not about how many views have opened since. This
                // runs per array per view, so ticking from here made the window collapse under
                // exactly the load it exists to survive: a loop that opens views quickly spends
                // the whole period in microseconds, and a program that opens none never spends
                // it at all. update() ticks both, at the rate a frame actually passes.
                array->processPendingErases(true);
            }
            else {
                // No holds exist here to make compaction unsafe, but a view that is already
                // open would still be reading the array this is about to move sectors in, and
                // the plain build has no way to know. Callers who nest views this way should
                // leave the switch off; the same rule as calling update() mid-iteration.
                if (array->needDefragment()) { array->defragment(); }
            }
        }

        void publishRegistered() {
            using ArrayPtr = Memory::SectorsArray<ThreadSafe, Allocator>*;

            const size_t mapCount = mComponentsArraysMap.size();
            const size_t listCount = mComponentsArrays.size();

            // Every member is pointer-sized, and sizeof(Registered) is a multiple of that, so
            // laying the arrays out back to back keeps each of them aligned.
            static_assert(sizeof(Registered) % alignof(void*) == 0, "array suffix would be misaligned");
            const size_t bytes = sizeof(Registered)
                + mapCount * sizeof(ArrayPtr)
                + mapCount * sizeof(const Memory::LayoutData*)
                + mapCount * sizeof(const Memory::SectorLayoutMeta*)
                + listCount * sizeof(ArrayPtr);

            auto* raw = static_cast<std::byte*>(std::malloc(bytes));
            if (!raw) { return; }                     // out of memory: keep serving the old one
            auto* node = new (raw) Registered{};

            std::byte* cursor = raw + sizeof(Registered);
            const auto carve = [&cursor](size_t count, size_t size) {
                std::byte* p = cursor;
                cursor += count * size;
                return p;
            };
            node->mapCount = mapCount;
            node->map = reinterpret_cast<ArrayPtr*>(carve(mapCount, sizeof(ArrayPtr)));
            node->layout = reinterpret_cast<const Memory::LayoutData**>(carve(mapCount, sizeof(const Memory::LayoutData*)));
            node->meta = reinterpret_cast<const Memory::SectorLayoutMeta**>(carve(mapCount, sizeof(const Memory::SectorLayoutMeta*)));
            node->listCount = listCount;
            node->list = reinterpret_cast<ArrayPtr*>(carve(listCount, sizeof(ArrayPtr)));

            for (size_t i = 0; i < mapCount; ++i) {
                node->map[i] = mComponentsArraysMap[i];
                node->layout[i] = i < mComponentsLayoutsMap.size() ? mComponentsLayoutsMap[i] : nullptr;
                node->meta[i] = i < mComponentsMetaMap.size() ? mComponentsMetaMap[i] : nullptr;
            }
            for (size_t i = 0; i < listCount; ++i) { node->list[i] = mComponentsArrays[i]; }

            auto* superseded = mRegistered.exchange(node, std::memory_order_release);
            if (superseded) { mNodeBin.retire(superseded); }
        }

        std::pair<Memory::SectorsArray<ThreadSafe, Allocator>* const*, Memory::SectorsArray<ThreadSafe, Allocator>* const*>
        registeredArrays() const noexcept {
            if (const auto* node = mRegistered.load(std::memory_order_acquire)) {
                return { node->list, node->list + node->listCount };
            }
            return { nullptr, nullptr };
        }

    private:
        static_assert(types::isLockFreeAtomic<Registered*>, "the registered-arrays snapshot must be lock-free");
        std::vector<const Memory::LayoutData*>       mComponentsLayoutsMap;
        std::vector<const Memory::SectorLayoutMeta*> mComponentsMetaMap;

        std::vector<std::unique_ptr<std::shared_mutex>> mTypeMutexes;
        mutable std::shared_mutex mTypeMutexesGrowth;

        FORCE_INLINE bool loadAutoMaintenance() const noexcept {
            if constexpr (ThreadSafe) {
                return std::atomic_ref<bool>(const_cast<bool&>(mAutoMaintenance))
                    .load(std::memory_order_relaxed);
            }
            else {
                return mAutoMaintenance;
            }
        }

        FORCE_INLINE void storeAutoMaintenance(bool value) noexcept {
            if constexpr (ThreadSafe) {
                std::atomic_ref<bool>(mAutoMaintenance).store(value, std::memory_order_relaxed);
            }
            else {
                mAutoMaintenance = value;
            }
        }

        std::atomic<uint32_t> mRetireGracePeriod{ Memory::RetireBin::DEFAULT_GRACE_PERIOD };

        bool mAutoMaintenance = false;

        mutable std::atomic<size_t> mMaintainCursor{ 0 };

        std::atomic<Registered*> mRegistered{ nullptr }; 
        mutable Memory::RetireBin mNodeBin;


        IdSet<EntityId, ThreadSafe> mEntities;

        std::vector<Memory::SectorsArray<ThreadSafe, Allocator>*> mComponentsArraysMap;

        std::vector<Memory::SectorsArray<ThreadSafe, Allocator>*> mComponentsArrays;

        struct Dummy{};
        std::atomic<bool> mPendingEntityClear{ false };
        mutable std::conditional_t<ThreadSafe, std::shared_mutex, Dummy> mEntitiesMutex;          
        mutable std::conditional_t<ThreadSafe, std::shared_mutex, Dummy> componentsArrayMapMutex; 
    };

    struct TypeAccessInfo {
        static constexpr uint8_t kMainIteratorIdx = 255; 

        uint32_t typeAliveMask      = 0; 
        uint16_t typeOffsetInSector = 0; 
        uint8_t  iteratorIdx        = kMainIteratorIdx; 
    };

    namespace detail {
        template<bool ThreadSafe, class Callback>
        FORCE_INLINE void forEachAliveSlot(
            void* const* chunks, size_t numChunks, size_t size,
            const uint32_t* isAliveData, uint32_t aliveMask,
            size_t stride, size_t chunkCapacity,
            Callback&& callback)
        {
            size_t idx = 0;
            for (size_t chunkIdx = 0; chunkIdx < numChunks && idx < size; ++chunkIdx) {
                auto* base = static_cast<std::byte*>(chunks[chunkIdx]);
                const size_t chunkEnd = std::min(idx + chunkCapacity, size);
                if constexpr (ThreadSafe) {
                    // Read a block of liveness words, then walk the block. Each word is still read
                    // once and still atomically -- another thread may be destroying a component in
                    // place -- but the atomic reads are no longer interleaved with the callback.
                    //
                    // That interleaving was the whole cost. std::atomic_ref with relaxed ordering
                    // compiles to a plain move on its own (checked: identical instructions to a
                    // direct read), but sitting in the same loop body as an inlined callback it
                    // stopped MSVC optimizing the body at all, and the thread-safe build ran at
                    // twice the plain one over a grouped array. Split, the two are level again.
                    constexpr size_t kAliveBlock = 64;
                    uint32_t alive[kAliveBlock];
                    size_t localIdx = 0;
                    while (idx < chunkEnd) {
                        const size_t n = std::min(kAliveBlock, chunkEnd - idx);
                        for (size_t k = 0; k < n; ++k) {
                            alive[k] = Memory::detail::loadRelaxed<ThreadSafe>(isAliveData, idx + k);
                        }
                        for (size_t k = 0; k < n; ++k) {
                            if ((alive[k] & aliveMask) == aliveMask) {
                                callback(base + (localIdx + k) * stride);
                            }
                        }
                        idx += n;
                        localIdx += n;
                    }
                }
                else {
                    // Nothing to order against here, so the straight loop stays: blocking it would
                    // only add a buffer the plain build has no use for, and it measured slower.
                    for (size_t localIdx = 0; idx < chunkEnd; ++idx, ++localIdx) {
                        if ((isAliveData[idx] & aliveMask) == aliveMask) {
                            callback(base + localIdx * stride);
                        }
                    }
                }
            }
        }
    }

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
            // Lock-free: one sparse load for the linear index, one alive-word load through the
            // dense seqlock, then the address from the chunk snapshot cached at construction.
            // Resolving through findSlot() instead would re-read the chunk seqlock per element.
            auto* arr = mSecondaryArrays[info.iteratorIdx];
            const auto linearIdx = arr->template findLinearIdx<false>(slot.id);
            if (linearIdx == INVALID_IDX) [[unlikely]] {
                return nullptr;
            }
            // The alive word goes through the seqlock snapshot (loadView) so we don't race
            // with a concurrent push_back reallocating the isAlive vector; RetireAllocator
            // keeps the old buffer valid, so the snapshot is always safe.
            const auto isAlive = arr->loadAliveWord(linearIdx);
            if (!(isAlive & info.typeAliveMask)) [[unlikely]] {
                return nullptr;
            }
            auto* data = arr->dataAt(mSecondaryChunks[info.iteratorIdx], linearIdx);
            return data ? reinterpret_cast<ComponentType*>(data + info.typeOffsetInSector) : nullptr;
            }

            template<typename... Types>
            void initTypeAccessInfo(const SectorArrays& arrays, const std::vector<std::pair<Sectors*, SectorsRangeIt>>& secondary) noexcept {
                uint8_t arrayIndexes[TypesCount];
                std::fill_n(arrayIndexes, TypesCount, TypeInfo::kMainIteratorIdx);

                for (const auto& entry : secondary) {
                    auto* arr = entry.first;
                    mSecondaryArrays[mSecondaryCount] = arr;
                    mSecondaryChunks[mSecondaryCount] = arr->loadChunks();
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
                    EntityId next{};
                    if (!mRangeFilter->nextStartAfter(slot.id, next)) {
                        mIterator.becomeEnd();
                        return;
                    }
                    mIterator.advanceToId(next);
                }
            }

            TypeAccessTuple mTypeAccessInfo;
            Sectors*        mSecondaryArrays[CTCount ? CTCount : 1] = {};
            typename Allocator::ChunksView mSecondaryChunks[CTCount ? CTCount : 1] = {};
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

        ArraysView(const ArraysView&) = delete;
        ArraysView& operator=(const ArraysView&) = delete;
        ArraysView(ArraysView&&) = delete;
        ArraysView& operator=(ArraysView&&) = delete;

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
        template<typename Func>
        FORCE_INLINE void eachSingle(Func&& func) const requires (sizeof...(CompTypes) == 0 && !Ranged) {
            if (!mMainArray || mSize == 0) return;
            const auto& layout = mMainArray->template getLayoutData<T>();

            const auto offset = layout.offset;
            auto view = mMainArray->mDenseArrays.loadView();
            
            // One component per sector, no padding around it, and nothing dead in the array:
            // each chunk is then a plain T[] and every slot is live. Saying that outright is
            // worth doing because the general loop takes the stride as a runtime value, so it
            // cannot turn base + i * stride into typed indexing and never vectorizes. Measured
            // over a million sectors under clang: 0.97 ms against 0.46, and the checksum then
            // matches a raw loop over a std::vector exactly -- same summation order, so the
            // same vectorization. MSVC gains little from it but loses nothing.
            //
            // Liveness is not skipped so much as already answered: a single-type sector stops
            // being alive the moment its only component goes, and that is exactly when
            // destroyComponent bumps the dead count this checks.
            if (mMainArray->getLayout()->getTypesCount() == 1
                && mMainArray->mAllocator.mSectorSize == sizeof(T)
                && offset == 0
                && mMainArray->template getDefragmentationSize<false>() == 0) {
                constexpr size_t cap =
                    std::remove_reference_t<decltype(mMainArray->mAllocator)>::mChunkCapacity;
                size_t idx = 0;
                for (size_t c = 0; c < mChunksCount && idx < mSize; ++c) {
                    T* p = reinterpret_cast<T*>(mChunksSnapshot[c]);
                    const size_t cnt = std::min(cap, mSize - idx);
                    for (size_t i = 0; i < cnt; ++i) { func(p[i]); }
                    idx += cnt;
                }
                return;
            }
            detail::forEachAliveSlot<ThreadSafe>(
                mChunksSnapshot, mChunksCount, mSize,
                view.isAlive, layout.isAliveMask,
                mMainArray->mAllocator.mSectorSize,
                std::remove_reference_t<decltype(mMainArray->mAllocator)>::mChunkCapacity,
                [&](std::byte* slot) { func(*reinterpret_cast<T*>(slot + offset)); }
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

            const uint16_t mainOff = mMainArray->template getLayoutData<T>().offset;
            const std::array<uint16_t, CTCount> compOffs{
                mMainArray->template getLayoutData<CompTypes>().offset...
            };

            uint32_t combinedMask = 0;
            combinedMask |= mMainArray->template getLayoutData<T>().isAliveMask;
            ((combinedMask |= mMainArray->template getLayoutData<CompTypes>().isAliveMask), ...);

            auto view = mMainArray->mDenseArrays.loadView();
            detail::forEachAliveSlot<ThreadSafe>(
                mChunksSnapshot, mChunksCount, mSize,
                view.isAlive, combinedMask,
                mMainArray->mAllocator.mSectorSize,
                std::remove_reference_t<decltype(mMainArray->mAllocator)>::mChunkCapacity,
                [&](std::byte* slot) {
                    func(*reinterpret_cast<T*>(slot + mainOff),
                         *reinterpret_cast<CompTypes*>(slot + compOffs[types::typeIndex<CompTypes, CompTypes...>])...);
                }
            );
        }

    private:
        void init(Registry<ThreadSafe, Allocator>* manager, const Ranges<EntityId>& ranges = {}) {
            auto arrays = initArrays<CompTypes..., T>(manager);
            SectorsIt it;

            {
                auto mainArr = arrays[getIndex<T>()];
                mMainArray = mainArr;
                // No lock: the size, the chunk table and the dense view all come from
                // lock-free snapshots, and pinning validates itself against the structural
                // epoch rather than relying on the shared lock.
                mSize = mainArr->template size<false>();
                const auto chunks = mainArr->mAllocator.loadChunks();
                mChunksSnapshot = chunks.chunks;
                mChunksCount = chunks.count;
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
                    // A hold, not a pin: what iteration needs is that the array is not
                    // compacted, and a hold expresses exactly that without every thread
                    // piling onto one sector counter.
                    if (!ranges.empty() && sectorsArray->template size<false>() != 0) {
                        mHolds[i] = sectorsArray->holdStructure();
                    }
                }
            }
            else {
                size_t last;
                if constexpr (ThreadSafe) {
                    mHolds[i] = sectorsArray->holdStructure();
                    last = sectorsArray->template size<false>();
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
                    // Translate the id ranges into this array's linear index range. Nothing is
                    // pinned here -- the view's structural hold is what keeps the bounds valid.
                    // The secondary RangedIterator is never read (component lookups go through
                    // findSlot), so we do not build it.
                    initRange(arr, ranges, i);
                }
                // Non-ThreadSafe uses direct lookup; iterator is unused either way.
                secondary.emplace_back(arr, SectorsRangeIt{});
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
        std::conditional_t<ThreadSafe, std::array<Memory::StructuralHold, TypesCount>, Dummy> mHolds;

#ifndef NDEBUG
        std::array<detail::AccessScope, TypesCount> mReadScopes{
            detail::readScope<T>(Registry<ThreadSafe, Allocator>::template componentTypeId<T>()),
            detail::readScope<CompTypes>(Registry<ThreadSafe, Allocator>::template componentTypeId<CompTypes>())...
        };
#endif
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


