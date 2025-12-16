

# File Sector.h

[**File List**](files.md) **>** [**ecss**](dir_194708e763cf312315c6b23555bce86f.md) **>** [**memory**](dir_3333283e221f8a8f53c5923bc4c386e0.md) **>** [**Sector.h**](Sector_8h.md)

[Go to the documentation of this file](Sector_8h.md)


```C++
#pragma once

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

        FORCE_INLINE void setAlive(uint32_t& isAliveData, uint32_t mask, bool value) noexcept {
            value ? isAliveData |= mask : isAliveData &= mask;
        }

        FORCE_INLINE void markAlive(uint32_t& isAliveData, uint32_t mask) noexcept {
            isAliveData |= mask;
        }

        FORCE_INLINE void markNotAlive(uint32_t& isAliveData, uint32_t mask) noexcept {
            isAliveData &= mask;
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

        template<typename T, class ...Args>
        T* emplaceMember(std::byte* data, uint32_t& isAliveData, const LayoutData& layout, Args&&... args) {
            void* memberPtr = getMemberPtr(data, layout.offset);
            if constexpr (!std::is_trivially_destructible_v<T>) {
                if (isAlive(isAliveData, layout.isAliveMask)) {
                    layout.functionTable.destructor(memberPtr);
                }
            }

            markAlive(isAliveData, layout.isAliveMask);

            return new(memberPtr) T(std::forward<Args>(args)...);
        }

        inline void destroyMember(std::byte* data, uint32_t& isAliveData, const LayoutData& layout) {
            if (!layout.isTrivial) {
                if (isAlive(isAliveData, layout.isAliveMask)) {
                    layout.functionTable.destructor(getMemberPtr(data, layout.offset));
                }
            }
            setAlive(isAliveData, layout.isNotAliveMask, false);
        }

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
            isAliveData = 0;
        }

        template<typename T>
        T* copyMember(const T& from, std::byte* toData, uint32_t& toIsAlive, const LayoutData& layout) {
            assert(toData);
            Sector::destroyMember(toData, toIsAlive, layout);
            setAlive(toIsAlive, layout.isAliveMask, true);
            return new(getMemberPtr(toData, layout.offset)) T(from);
        }

        template<typename T>
        T* moveMember(T&& from, std::byte* toData, uint32_t& toIsAlive, const LayoutData& layout) {
            assert(toData);
            Sector::destroyMember(toData, toIsAlive, layout);
            setAlive(toIsAlive, layout.isAliveMask, true);
            return new(getMemberPtr(toData, layout.offset)) T(std::forward<T>(from));
        }

        inline void* copyMember(const void* from, std::byte* toData, uint32_t& toIsAlive, const LayoutData& layout) {
            assert(toData);
            Sector::destroyMember(toData, toIsAlive, layout);

            auto ptr = getMemberPtr(toData, layout.offset);
            if (!from) {
                return ptr;
            }
            layout.functionTable.copy(ptr, from);
            setAlive(toIsAlive, from ? layout.isAliveMask : layout.isNotAliveMask, from != nullptr);
            return ptr;
        }

        inline void* moveMember(void* from, std::byte* toData, uint32_t& toIsAlive, const LayoutData& layout) {
            assert(toData);
            Sector::destroyMember(toData, toIsAlive, layout);
            auto ptr = getMemberPtr(toData, layout.offset);
            if (!from) {
                return ptr;
            }
            layout.functionTable.move(ptr, from);
            setAlive(toIsAlive, from ? layout.isAliveMask : layout.isNotAliveMask, from != nullptr);
            layout.functionTable.destructor(from);
            return ptr;
        }

        inline void copySectorData(const std::byte* fromData, uint32_t fromIsAlive,
                                   std::byte* toData, uint32_t& toIsAlive,
                                   const SectorLayoutMeta* layouts) {
            assert(fromData != toData);
            assert(fromData && toData);
            Sector::destroySectorData(toData, toIsAlive, layouts);

            toIsAlive = fromIsAlive;

            for (const auto& layout : *layouts) {
                if (!isAlive(fromIsAlive, layout.isAliveMask)) {
                    continue;
                }
                layout.functionTable.copy(getMemberPtr(toData, layout.offset),
                                          getMemberPtr(const_cast<std::byte*>(fromData), layout.offset));
            }
        }

        inline void moveSectorData(std::byte* fromData, uint32_t& fromIsAlive,
                                   std::byte* toData, uint32_t& toIsAlive,
                                   const SectorLayoutMeta* layouts) {
            assert(fromData != toData);
            assert(fromData && toData);
            Sector::destroySectorData(toData, toIsAlive, layouts);

            toIsAlive = fromIsAlive;

            for (const auto& layout : *layouts) {
                if (!isAlive(fromIsAlive, layout.isAliveMask)) {
                    continue;
                }
                layout.functionTable.move(getMemberPtr(toData, layout.offset),
                                          getMemberPtr(fromData, layout.offset));
            }

            Sector::destroySectorData(fromData, fromIsAlive, layouts);
        }

    } // namespace Sector

} // namespace ecss::Memory
```


