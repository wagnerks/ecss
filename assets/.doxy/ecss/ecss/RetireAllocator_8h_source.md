

# File RetireAllocator.h

[**File List**](files.md) **>** [**ecss**](dir_194708e763cf312315c6b23555bce86f.md) **>** [**memory**](dir_3333283e221f8a8f53c5923bc4c386e0.md) **>** [**RetireAllocator.h**](RetireAllocator_8h.md)

[Go to the documentation of this file](RetireAllocator_8h.md)


```C++
#pragma once

#include <cstddef>
#include <vector>
#include <mutex>
#include <type_traits>

namespace ecss::Memory {
    struct RetireBin {
        RetireBin() = default;
        ~RetireBin() { drainAll(); }

        RetireBin(const RetireBin&){}
        RetireBin& operator=(const RetireBin&) { return *this; }

        RetireBin(RetireBin&& other) noexcept : mRetired(std::move(other.mRetired)){}
        RetireBin& operator=(RetireBin&& other) noexcept {
            if (this == &other) { return *this; }
                
            mRetired = std::move(other.mRetired);
            return *this;
        }

        void retire(void* p) {
            auto lock = std::lock_guard(mMtx);
            mRetired.emplace_back(p);
        }

        void drainAll() {
            std::vector<void*> tmp;
            { auto lock = std::lock_guard(mMtx); tmp.swap(mRetired); }

            for (auto b : tmp){
                std::free(b);
            }
        }

    private:
        mutable std::mutex mMtx;
        std::vector<void*> mRetired;
    };


    template<class T>
    struct RetireAllocator {
        using value_type = T;

        explicit RetireAllocator(RetireBin* bin) noexcept : bin(bin) {}

        template<class U>
        RetireAllocator(const RetireAllocator<U>& other) noexcept : bin(other.bin) {}

        T* allocate(size_t n) {
            return static_cast<T*>(std::calloc(n, sizeof(T)));
        }

        void deallocate(T* p, size_t n) noexcept {
            if (!bin) { std::free(static_cast<void*>(p)); return; }
            bin->retire(static_cast<void*>(p));
        }

        template<class U> friend struct RetireAllocator;
        template<class U>
        bool operator==(const RetireAllocator<U>& rhs) const noexcept { return bin == rhs.bin; }
        template<class U>
        bool operator!=(const RetireAllocator<U>& rhs) const noexcept { return !(*this == rhs); }

        using propagate_on_container_move_assignment = std::false_type;
        using is_always_equal = std::true_type;

        RetireBin* bin = nullptr;
    };
}
```


