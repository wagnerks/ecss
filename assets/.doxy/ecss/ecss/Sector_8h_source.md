

# File Sector.h

[**File List**](files.md) **>** [**ecss**](dir_194708e763cf312315c6b23555bce86f.md) **>** [**memory**](dir_3333283e221f8a8f53c5923bc4c386e0.md) **>** [**Sector.h**](Sector_8h.md)

[Go to the documentation of this file](Sector_8h.md)


```C++
#pragma once

#include <ecss/memory/SectorLayoutMeta.h>

namespace ecss::Memory {
    struct alignas(8) Sector final {
        SectorId id;
        uint32_t isAliveData;

        FORCE_INLINE constexpr void setAlive(size_t mask, bool value) { value ? isAliveData |= mask : isAliveData &= mask; }
        FORCE_INLINE constexpr void markAlive(size_t mask) { isAliveData |= mask; }
        FORCE_INLINE constexpr void markNotAlive(size_t mask) { isAliveData &= mask; }
        FORCE_INLINE constexpr bool isAlive(size_t mask) const { return isAliveData & mask; }
        FORCE_INLINE constexpr bool isSectorAlive() const { return isAliveData != 0; }

    public:
        template<typename T>
        FORCE_INLINE constexpr T* getMember(uint16_t offset, uint32_t mask) const { return isAliveData & mask ? static_cast<T*>(getMemberPtr(this, offset)) : nullptr; }
        template<typename T>
        FORCE_INLINE constexpr T* getMember(uint16_t offset, uint32_t mask) { return isAliveData & mask ? static_cast<T*>(getMemberPtr(this, offset)) : nullptr; }

        template<typename T>
        FORCE_INLINE constexpr T* getMember(const LayoutData& layout) const { return getMember<T>(layout.offset, layout.isAliveMask); }
        template<typename T>
        FORCE_INLINE constexpr T* getMember(const LayoutData& layout) { return getMember<T>(layout.offset, layout.isAliveMask); }

        FORCE_INLINE static void* getMemberPtr(const Sector* sectorAdr, uint16_t offset) { return const_cast<char*>(reinterpret_cast<const char*>(sectorAdr) + offset); }
        FORCE_INLINE static void* getMemberPtr(Sector* sectorAdr, uint16_t offset) { return reinterpret_cast<char*>(sectorAdr) + offset; }
        FORCE_INLINE static void* getMemberPtr(std::byte* sectorAdr, uint16_t offset) { return sectorAdr + offset; }

        template <typename T>
        static const T* getComponentFromSector(const Sector* sector, SectorLayoutMeta* sectorLayout) {
            if (!sector) {
                return nullptr;
            }
            const auto& layout = sectorLayout->getLayoutData<T>();
            return sector->getMember<T>(layout.offset, layout.isAliveMask);
        }
        template <typename T>
        static T* getComponentFromSector(Sector* sector, SectorLayoutMeta* sectorLayout) {
            if (!sector) {
                return nullptr;
            }
            const auto& layout = sectorLayout->getLayoutData<T>();
            return sector->getMember<T>(layout.offset, layout.isAliveMask);
        }

        template<typename T, class ...Args>
        T* emplaceMember(const LayoutData& layout, Args&&... args) {
            void* memberPtr = getMemberPtr(this, layout.offset);
            if constexpr (!std::is_trivially_destructible_v<T>) {
                if (isAlive(layout.isAliveMask)) {
                    layout.functionTable.destructor(memberPtr);
                }
            }

            markAlive(layout.isAliveMask);

            return new(memberPtr)T(std::forward<Args>(args)... );
        }

        template<typename T, class ...Args>
        static T* emplaceMember(Sector* sector, const LayoutData& layout, Args&&... args) {
            assert(sector);

            void* memberPtr = getMemberPtr(sector, layout.offset);
            if constexpr (!std::is_trivially_destructible_v<T>) {
                if (sector->isAlive(layout.isAliveMask)) {
                    layout.functionTable.destructor(memberPtr);
                }
            }

            sector->markAlive(layout.isAliveMask);

            return new(memberPtr)T{ std::forward<Args>(args)... };
        }

        template<typename T>
        static T* copyMember(const T& from, Sector* to, const LayoutData& layout) {
            assert(to);
            to->destroyMember(layout);
            to->setAlive(layout.isAliveMask, true);
            return new(getMemberPtr(to, layout.offset))T(from);
        }
        template<typename T>
        static T* moveMember(T&& from, Sector* to, const LayoutData& layout) {
            assert(to);
            to->destroyMember(layout);
            to->setAlive(layout.isAliveMask, true);
            return new(getMemberPtr(to, layout.offset))T(std::forward<T>(from));
        }

        static void* copyMember(const void* from, Sector* to, const LayoutData& layout) {
            assert(to);
            to->destroyMember(layout);
            
            auto ptr = getMemberPtr(to, layout.offset);
            if (!from) {
                return ptr;
            }
            layout.functionTable.copy(ptr, const_cast<void*>(from));
            to->setAlive(!!from ? layout.isAliveMask : layout.isNotAliveMask, !!from);
            return ptr;
        }

        static void* moveMember(void* from, Sector* to, const LayoutData& layout) {
            assert(to);
            to->destroyMember(layout);
            auto ptr = getMemberPtr(to, layout.offset);
            if (!from) {
                return ptr;
            }
            layout.functionTable.move(ptr, from);
            to->setAlive(!!from ? layout.isAliveMask : layout.isNotAliveMask, !!from);
            layout.functionTable.destructor(from);
            return ptr;
        }

        static Sector* copySector(Sector* from, Sector* to, const SectorLayoutMeta* layouts) {
            assert(from != to);
            assert(from && to);
            assert(to);
            destroySector(to, layouts);

            if constexpr (std::is_trivially_copyable_v<Sector>) {
                memcpy(to, from, sizeof(Sector));
            }
            else {
                new (to)Sector(*from);
            }

            for (const auto& layout : *layouts) {
                if (!from->isAlive(layout.isAliveMask)) {
                    continue;
                }

                layout.functionTable.copy(getMemberPtr(to, layout.offset), getMemberPtr(from, layout.offset));
            }

            return to;
        }

        static Sector* moveSector(Sector* from, Sector* to, const SectorLayoutMeta* layouts) {
            assert(from != to);
            assert(from && to);
            assert(to);
            destroySector(to, layouts);

            new (to)Sector(std::move(*from));
            for (const auto& layout : *layouts) {
                if (!from->isAlive(layout.isAliveMask)) {
                    continue;
                }

                layout.functionTable.move(getMemberPtr(to, layout.offset), getMemberPtr(from, layout.offset));

                to->setAlive(layout.isAliveMask, true);
            }
            destroySector(from, layouts);
            return to;
        }

        static void destroySector(Sector* sector, const SectorLayoutMeta* layouts) {
            if (!sector || !sector->isSectorAlive()) {
                return;
            }

            if (!layouts->isTrivial()) {
                for (const auto& layout : (*layouts)) {
                    if (sector->isAlive(layout.isAliveMask)) {
                        layout.functionTable.destructor(ecss::Memory::Sector::getMemberPtr(sector, layout.offset));
                    }
                }
            }
            sector->isAliveData = 0;
        }

        void destroyMember(const LayoutData& layout) {
            if (!layout.isTrivial) {
                if (isAlive(layout.isAliveMask)) {
                    layout.functionTable.destructor(getMemberPtr(this, layout.offset));
                }
            }

            setAlive(layout.isNotAliveMask, false);
        }
    };

    static_assert(sizeof(Dummy::Sector) == sizeof(Sector), "Dummy and real Sector differ!");
    static_assert(std::is_trivial_v<Sector>); //Sector should be always trivial - to enable trivially copy trivial types inside
}
```


