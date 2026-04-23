

# File Sector.h

[**File List**](files.md) **>** [**ecss**](dir_194708e763cf312315c6b23555bce86f.md) **>** [**memory**](dir_3333283e221f8a8f53c5923bc4c386e0.md) **>** [**Sector.h**](Sector_8h.md)

[Go to the documentation of this file](Sector_8h.md)


```C++
#pragma once

#include <atomic>
#include <ecss/memory/SectorLayoutMeta.h>

namespace ecss::Memory {
    namespace Sector {

        template<typename T>
        FORCE_INLINE T* getMember(std::byte* data, uint16_t offset, uint32_t mask, uint32_t isAliveData) noexcept {
            return (isAliveData & mask) ? reinterpret_cast<T*>(data + offset) : nullptr;
        }

        template<typename T>
        FORCE_INLINE const T* getMember(const std::byte* data, uint16_t offset, uint32_t mask, uint32_t isAliveData) noexcept {
            return (isAliveData & mask) ? reinterpret_cast<const T*>(data + offset) : nullptr;
        }

        template<typename T>
        FORCE_INLINE T* getMember(std::byte* data, const LayoutData& layout, uint32_t isAliveData) noexcept {
            return getMember<T>(data, layout.offset, layout.isAliveMask, isAliveData);
        }

        template<typename T>
        FORCE_INLINE const T* getMember(const std::byte* data, const LayoutData& layout, uint32_t isAliveData) noexcept {
            return getMember<T>(data, layout.offset, layout.isAliveMask, isAliveData);
        }

        FORCE_INLINE void* getMemberPtr(std::byte* data, uint16_t offset) noexcept {
            return data + offset;
        }

        FORCE_INLINE const void* getMemberPtr(const std::byte* data, uint16_t offset) noexcept {
            return data + offset;
        }

        FORCE_INLINE bool isAlive(uint32_t isAliveData, uint32_t mask) noexcept {
            return isAliveData & mask;
        }

        FORCE_INLINE bool isSectorAlive(uint32_t isAliveData) noexcept {
            return isAliveData != 0;
        }

        template<bool ThreadSafe = false>
        FORCE_INLINE void setAlive(uint32_t& isAliveData, uint32_t mask, bool value) noexcept {
            if constexpr (ThreadSafe) {
                // Setting alive publishes freshly-constructed bytes -> release (pairs with
                // the acquire load in IteratorAlive::operator*). Clearing has no data to
                // publish on this path -> relaxed is sufficient.
                if (value) std::atomic_ref(isAliveData).fetch_or(mask, std::memory_order_release);
                else       std::atomic_ref(isAliveData).fetch_and(mask, std::memory_order_relaxed);
            } else {
                if (value) isAliveData |= mask;
                else       isAliveData &= mask;
            }
        }

        template<bool ThreadSafe = false>
        FORCE_INLINE void markAlive(uint32_t& isAliveData, uint32_t mask) noexcept {
            if constexpr (ThreadSafe) {
                // Release: orders the component construction in emplaceMember() before
                // the alive-bit publication so lockless readers in IteratorAlive::operator*
                // (which acquire-loads the same word) observe the fully-constructed bytes.
                // Cost: x86 unchanged (LOCK OR is already seq-cst).
                // ARM64: one-instruction release barrier -- only on the mutation path.
                std::atomic_ref(isAliveData).fetch_or(mask, std::memory_order_release);
            } else {
                isAliveData |= mask;
            }
        }

        template<bool ThreadSafe = false>
        FORCE_INLINE void markNotAlive(uint32_t& isAliveData, uint32_t mask) noexcept {
            if constexpr (ThreadSafe) {
                std::atomic_ref(isAliveData).fetch_and(mask, std::memory_order_relaxed);
            } else {
                isAliveData &= mask;
            }
        }

        template <typename T>
        const T* getComponent(const std::byte* data, uint32_t isAliveData, SectorLayoutMeta* sectorLayout) {
            if (!data) {
                return nullptr;
            }
            const auto& layout = sectorLayout->getLayoutData<T>();
            return getMember<T>(data, layout.offset, layout.isAliveMask, isAliveData);
        }

        template <typename T>
        T* getComponent(std::byte* data, uint32_t isAliveData, SectorLayoutMeta* sectorLayout) {
            if (!data) {
                return nullptr;
            }
            const auto& layout = sectorLayout->getLayoutData<T>();
            return getMember<T>(data, layout.offset, layout.isAliveMask, isAliveData);
        }

        template<typename T, bool TS = false, class ...Args>
        T* emplaceMember(std::byte* data, uint32_t& isAliveData, const LayoutData& layout, Args&&... args) {
            void* memberPtr = getMemberPtr(data, layout.offset);
            if constexpr (!std::is_trivially_destructible_v<T>) {
                if (isAlive(isAliveData, layout.isAliveMask)) {
                    layout.functionTable.destructor(memberPtr);
                }
            }

            // Construct FIRST, then mark alive - ensures readers see fully constructed data
            T* result = new(memberPtr) T(std::forward<Args>(args)...);
            markAlive<TS>(isAliveData, layout.isAliveMask);
            return result;
        }

        template<bool ThreadSafe = false>
        inline void destroyMember(std::byte* data, uint32_t& isAliveData, const LayoutData& layout) {
            if (!layout.isTrivial) {
                if (isAlive(isAliveData, layout.isAliveMask)) {
                    layout.functionTable.destructor(getMemberPtr(data, layout.offset));
                }
            }
            setAlive<ThreadSafe>(isAliveData, layout.isNotAliveMask, false);
        }

        template<bool ThreadSafe = false>
        inline void destroySectorData(std::byte* data, uint32_t& isAliveData, const SectorLayoutMeta* layouts) {
            if (!data || !isSectorAlive(isAliveData)) {
                return;
            }

            if (!layouts->isTrivial()) {
                for (const auto& layout : *layouts) {
                    if (isAlive(isAliveData, layout.isAliveMask)) {
                        layout.functionTable.destructor(getMemberPtr(data, layout.offset));
                    }
                }
            }
            if constexpr (ThreadSafe) {
                std::atomic_ref(isAliveData).store(0, std::memory_order_relaxed);
            } else {
                isAliveData = 0;
            }
        }

        template<typename T, bool TS = false>
        T* copyMember(const T& from, std::byte* toData, uint32_t& toIsAlive, const LayoutData& layout) {
            assert(toData);
            Sector::destroyMember<TS>(toData, toIsAlive, layout);
            // Construct FIRST, then mark alive - ensures readers see fully constructed data
            T* result = new(getMemberPtr(toData, layout.offset)) T(from);
            setAlive<TS>(toIsAlive, layout.isAliveMask, true);
            return result;
        }

        template<typename T, bool TS = false>
        T* moveMember(T&& from, std::byte* toData, uint32_t& toIsAlive, const LayoutData& layout) {
            assert(toData);
            Sector::destroyMember<TS>(toData, toIsAlive, layout);
            // Construct FIRST, then mark alive - ensures readers see fully constructed data
            T* result = new(getMemberPtr(toData, layout.offset)) T(std::forward<T>(from));
            setAlive<TS>(toIsAlive, layout.isAliveMask, true);
            return result;
        }

        template<bool ThreadSafe = false>
        inline void* copyMember(const void* from, std::byte* toData, uint32_t& toIsAlive, const LayoutData& layout) {
            assert(toData);
            Sector::destroyMember<ThreadSafe>(toData, toIsAlive, layout);

            auto ptr = getMemberPtr(toData, layout.offset);
            if (!from) {
                return ptr;
            }
            // Copy FIRST, then mark alive - ensures readers see fully constructed data
            layout.functionTable.copy(ptr, from);
            setAlive<ThreadSafe>(toIsAlive, layout.isAliveMask, true);
            return ptr;
        }

        template<bool ThreadSafe = false>
        inline void* moveMember(void* from, std::byte* toData, uint32_t& toIsAlive, const LayoutData& layout) {
            assert(toData);
            Sector::destroyMember<ThreadSafe>(toData, toIsAlive, layout);
            auto ptr = getMemberPtr(toData, layout.offset);
            if (!from) {
                return ptr;
            }
            // Move FIRST, then mark alive - ensures readers see fully constructed data
            layout.functionTable.move(ptr, from);
            setAlive<ThreadSafe>(toIsAlive, layout.isAliveMask, true);
            layout.functionTable.destructor(from);
            return ptr;
        }

        template<bool ThreadSafe = false>
        inline void copySectorData(const std::byte* fromData, uint32_t fromIsAlive,
                                   std::byte* toData, uint32_t& toIsAlive,
                                   const SectorLayoutMeta* layouts) {
            assert(fromData != toData);
            assert(fromData && toData);
            Sector::destroySectorData<ThreadSafe>(toData, toIsAlive, layouts);

            if constexpr (ThreadSafe) {
                std::atomic_ref(toIsAlive).store(fromIsAlive, std::memory_order_relaxed);
            } else {
                toIsAlive = fromIsAlive;
            }

            for (const auto& layout : *layouts) {
                if (!isAlive(fromIsAlive, layout.isAliveMask)) {
                    continue;
                }
                layout.functionTable.copy(getMemberPtr(toData, layout.offset),
                                          getMemberPtr(const_cast<std::byte*>(fromData), layout.offset));
            }
        }

        template<bool ThreadSafe = false>
        inline void moveSectorData(std::byte* fromData, uint32_t& fromIsAlive,
                                   std::byte* toData, uint32_t& toIsAlive,
                                   const SectorLayoutMeta* layouts) {
            assert(fromData != toData);
            assert(fromData && toData);
            Sector::destroySectorData<ThreadSafe>(toData, toIsAlive, layouts);

            if constexpr (ThreadSafe) {
                std::atomic_ref(toIsAlive).store(fromIsAlive, std::memory_order_relaxed);
            } else {
                toIsAlive = fromIsAlive;
            }

            for (const auto& layout : *layouts) {
                if (!isAlive(fromIsAlive, layout.isAliveMask)) {
                    continue;
                }
                layout.functionTable.move(getMemberPtr(toData, layout.offset),
                                          getMemberPtr(fromData, layout.offset));
            }

            Sector::destroySectorData<ThreadSafe>(fromData, fromIsAlive, layouts);
        }

    } // namespace Sector

} // namespace ecss::Memory
```


