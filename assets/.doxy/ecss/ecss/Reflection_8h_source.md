

# File Reflection.h

[**File List**](files.md) **>** [**ecss**](dir_194708e763cf312315c6b23555bce86f.md) **>** [**memory**](dir_3333283e221f8a8f53c5923bc4c386e0.md) **>** [**Reflection.h**](Reflection_8h.md)

[Go to the documentation of this file](Reflection_8h.md)


```C++
#pragma once

#include <atomic>
#include <ecss/Types.h>

namespace ecss::Memory {

    template<typename T>
    FORCE_INLINE size_t GlobalTypeId() noexcept {
        static constexpr char tag = 0;
        return reinterpret_cast<size_t>(&tag);
    }

    class DenseTypeIdGenerator {
        static inline std::atomic<ECSType> sNextId{0};
        
    public:
        template<typename T>
        FORCE_INLINE static ECSType getId() noexcept {
            // Static local - initialized once per type, thread-safe in C++11+
            static const ECSType id = sNextId.fetch_add(1, std::memory_order_relaxed);
            return id;
        }

        template<typename T>
        FORCE_INLINE static ECSType getTypeId() noexcept {
            return DenseTypeIdGenerator::getId<std::remove_const_t<std::remove_pointer_t<std::remove_reference_t<T>>>>();
        }

        static ECSType getCount() noexcept {
            return sNextId.load(std::memory_order_relaxed);
        }
    };
} // namespace ecss::Memory
```


