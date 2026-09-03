

# File SectorLayoutMeta.h

[**File List**](files.md) **>** [**ecss**](dir_194708e763cf312315c6b23555bce86f.md) **>** [**memory**](dir_3333283e221f8a8f53c5923bc4c386e0.md) **>** [**SectorLayoutMeta.h**](SectorLayoutMeta_8h.md)

[Go to the documentation of this file](SectorLayoutMeta_8h.md)


```C++
#pragma once
#include <limits>

#include <cassert>
#include <cstdint>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

#include <ecss/Types.h>

namespace ecss::Memory {

    struct LayoutData {
        struct FunctionTable {
            void (*move)(void* dest, void* src);
            void (*copy)(void* dest, const void* src);
            void (*destructor)(void* src);
        };

        FunctionTable functionTable; // Type-erased operations for this component.
        size_t typeHash = 0;         // Optional: stable hash/ID of the component type.
        uint32_t isAliveMask = 0;    // Bit(s) set when the component is alive/present.
        uint32_t isNotAliveMask = 0; // Bit mask used to clear liveness (often ~isAliveMask & mask_width).
        uint16_t offset = 0;         // Byte offset of the component within the sector data (starts from 0).
        uint16_t index = 0;          // Index of this component within the sector layout.
        bool isTrivial = false;      // True if the component is trivially destructible/copiable/movable.
    };

    struct SectorLayoutMeta {
        // Non-copyable / non-movable: exactly one instance per type pack, shared by every
        // array built from it, so copying one would silently fork a shared invariant.
        SectorLayoutMeta(const SectorLayoutMeta& other) = delete;
        SectorLayoutMeta(SectorLayoutMeta&& other) noexcept = delete;
        SectorLayoutMeta& operator=(const SectorLayoutMeta& other) = delete;
        SectorLayoutMeta& operator=(SectorLayoutMeta&& other) noexcept = delete;
        ~SectorLayoutMeta() = default;

    private:
        template<typename U>
        inline void initLayoutData(LayoutData& data, uint8_t& index, uint16_t offset) const noexcept {
            static_assert(std::is_move_constructible_v<U>, "Type must be move-constructible for use in SectorsArray");

            data.typeHash = SectorLayoutMeta::TypeId<U>();
            data.offset = offset;
            data.index = index++;
            data.isAliveMask = static_cast<uint32_t>(1u << data.index);
            data.isNotAliveMask = ~(data.isAliveMask);
            data.isTrivial = std::is_trivially_copyable_v<U>;
            ecss::detail::checkTriviality<U>();

            data.functionTable.move = [](void* dest, void* src) { new(dest) U(std::move(*static_cast<U*>(src))); };

            data.functionTable.copy = [](void* dest, const void* src)
            {
                if constexpr (std::is_copy_constructible_v<U>) {
                    new(dest) U(*static_cast<const U*>(src));
                }
                else { assert(false && "Attempt to copy a move-only type!"); }
            };

            data.functionTable.destructor = [](void* src) { std::destroy_at(static_cast<U*>(src)); };
        }

        template<typename... U>
        inline void initLayoutData() {
            static_assert(sizeof...(U) <= maxComponentsPerSector, "Too many component types per sector (max 32)");
            uint8_t counter = 0;
            // Use EmptyBase for offset calculation - offsets start from 0
            (initLayoutData<std::remove_const_t<std::remove_pointer_t<std::remove_reference_t<U>>>>(
                layout[counter], counter, 
                static_cast<uint16_t>(types::OffsetArray<types::EmptyBase, U...>::offsets[counter])
            ), ...);
        }

    public:
        class Iterator {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type = const LayoutData;
            using difference_type = std::ptrdiff_t;
            using pointer = value_type*;
            using reference = value_type&;

            Iterator(const SectorLayoutMeta* layoutMeta, uint8_t idx) : layoutsArray(layoutMeta->getLayouts() + idx) {}

            Iterator& operator++() { ++layoutsArray; return *this; }
            Iterator operator++(int) { Iterator tmp = *this; ++(*this); return tmp; }

            bool operator==(const Iterator& other) const { return layoutsArray == other.layoutsArray; }
            bool operator!=(const Iterator& other) const { return !(*this == other); }

            reference& operator*() const { return *layoutsArray; }
            reference& operator->() const { return *layoutsArray; }

        private:
            const LayoutData* layoutsArray;
        };

        Iterator begin() const { return { this, 0 }; }
        Iterator end() const { return { this, getTypesCount() }; }

    public:
        template<typename... Types>
        static inline SectorLayoutMeta* create()
        {
            auto meta = new SectorLayoutMeta();
            meta->initData<Types...>();
            return meta;
        }

    private:
        template<typename... Types>
        void initData() {
            // T, T*, T& and const T all resolve to the same type id (DenseTypeIdGenerator::
            // getTypeId strips all three), and one id means one layout record. For const that
            // is exactly right -- view<const T> asks for read-only access to the same object,
            // same size, same offsets. A pointer or a reference is a different size, so it
            // would file sizeof(void*) under the record of the type it names and overwrite it.
            static_assert((!std::is_pointer_v<std::remove_cv_t<Types>> && ...)
                && (!std::is_reference_v<Types> && ...),
                "a component is stored by value: name A or const A, not A* or A& -- a pointer "
                "shares the type id of what it points to but not its size, and would overwrite "
                "that type's layout record");
            
            // Chunks come from calloc, which promises alignof(std::max_align_t) and no more,
            // and sectors are placed at a stride inside them. An alignas(32) or alignas(64)
            // member would be handed an address that merely looks right in testing.
            static_assert(((alignof(Types) <= alignof(std::max_align_t)) && ...),
                "over-aligned component types are not supported: chunk memory comes from "
                "calloc, which guarantees only max_align_t");
            
            // Offsets and the stride are stored as uint16_t. Past 64 KB the cast below wraps
            // and every sector address after the first is wrong, with nothing to say so.
            static_assert(types::OffsetArray<types::EmptyBase, Types...>::totalSize
                <= std::numeric_limits<uint16_t>::max(),
                "sector larger than 64 KB: component offsets and the stride are uint16_t");
            
            count = types::OffsetArray<types::EmptyBase, Types...>::count;
            totalSize = static_cast<uint16_t>(types::OffsetArray<types::EmptyBase, Types...>::totalSize);
            size_t idx = 0;
            ((typeIds[idx++] = TypeId<Types>()), ...);

            initLayoutData<Types...>();
            for (size_t i = 0; i < count; i++) {
                mIsTrivial = mIsTrivial && layout[i].isTrivial;
                if (!mIsTrivial) {
                    break;
                }
            }
        }

    public:
        bool isCompatibleWith(const SectorLayoutMeta& other) const noexcept {
            if (count != other.count || totalSize != other.totalSize) {
                return false;
            }
            for (uint8_t i = 0; i < count; ++i) {
                if (typeIds[i] != other.typeIds[i]) { return false; }
                if (layout[i].offset != other.layout[i].offset) { return false; }
                if (layout[i].isAliveMask != other.layout[i].isAliveMask) { return false; }
            }
            return true;
        }

        uint16_t getTotalSize() const { return totalSize; }

        bool isTrivial() const { return mIsTrivial; }

        template<typename T>
        inline const LayoutData& getLayoutData() const { return layout[getIndex<T>()]; }

        const LayoutData& getLayoutData(uint8_t idx) const { return layout[idx]; }

    public:
        template<typename T>
        inline static size_t TypeId() { return TypeIdImpl<std::remove_const_t<std::remove_pointer_t<std::remove_reference_t<T>>>>(); }

    private:
        SectorLayoutMeta() = default;

        template<typename T>
        inline static size_t TypeIdImpl() { static char tag; return reinterpret_cast<size_t>(&tag); }

        template<typename T>
        inline uint8_t getIndex() const { return getIndexByType(TypeId<T>()); }

        const LayoutData* getLayouts() const { return layout; }

        uint8_t getIndexByType(size_t hash) const {
            for (uint8_t i = 0; i < count; ++i) {
                if (typeIds[i] == hash) {
                    return i;
                }
            }

            assert(false);
            return count;
        }

    public:
        // Iteration needs to know whether a sector holds exactly one component, which is
        // what makes a chunk a plain array of it. Reads data fixed at construction.
        uint8_t getTypesCount() const { return count; }
    private:

    private:
        inline static constexpr size_t maxComponentsPerSector = 32; // Arbitrary limit for sanity checks.

        LayoutData  layout[maxComponentsPerSector];
        size_t      typeIds[maxComponentsPerSector];

        // Overall layout properties.
        uint16_t totalSize = 0; 
        uint8_t  count     = 0; 
        bool mIsTrivial = true; 
    };
}
```


