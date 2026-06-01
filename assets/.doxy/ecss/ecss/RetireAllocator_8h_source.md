

# File RetireAllocator.h

[**File List**](files.md) **>** [**ecss**](dir_194708e763cf312315c6b23555bce86f.md) **>** [**memory**](dir_3333283e221f8a8f53c5923bc4c386e0.md) **>** [**RetireAllocator.h**](RetireAllocator_8h.md)

[Go to the documentation of this file](RetireAllocator_8h.md)


```C++
#pragma once

#include <atomic>
#include <cstddef>
#include <vector>
#include <mutex>
#include <type_traits>
#include <cstdint>

namespace ecss::Memory {
    struct RetireBin {
        static constexpr uint32_t DEFAULT_GRACE_PERIOD = 3;

        RetireBin() = default;
        explicit RetireBin(uint32_t gracePeriod) : mGracePeriod(gracePeriod) {}
        ~RetireBin() { drainAll(); }

        RetireBin(const RetireBin&) {}
        RetireBin& operator=(const RetireBin&) { return *this; }

        RetireBin(RetireBin&& other) noexcept 
            : mRetired(std::move(other.mRetired))
            , mGracePeriod(other.mGracePeriod.load(std::memory_order_relaxed)) {}
        
        RetireBin& operator=(RetireBin&& other) noexcept {
            if (this == &other) { return *this; }
            mRetired = std::move(other.mRetired);
            mGracePeriod.store(other.mGracePeriod.load(std::memory_order_relaxed), std::memory_order_relaxed);
            return *this;
        }

        void retire(void* p) {
            if (!p) return;
            auto lock = std::lock_guard(mMtx);
            mRetired.push_back({p, mGracePeriod.load(std::memory_order_relaxed)});
        }

        size_t tick() {
            std::vector<void*> toFree;
            {
                auto lock = std::lock_guard(mMtx);
                auto it = mRetired.begin();
                while (it != mRetired.end()) {
                    if (it->countdown == 0 || --it->countdown == 0) {
                        toFree.push_back(it->ptr);
                        it = mRetired.erase(it);
                    }
                    else {
                        ++it;
                    }
                }
            }
            
            for (auto p : toFree) {
                std::free(p);
            }
            return toFree.size();
        }

        void drainAll() {
            std::vector<void*> tmp;
            {
                auto lock = std::lock_guard(mMtx);
                tmp.reserve(mRetired.size());
                for (auto& block : mRetired) {
                    tmp.push_back(block.ptr);
                }
                mRetired.clear();
            }

            for (auto b : tmp) {
                std::free(b);
            }
        }

        void setGracePeriod(uint32_t ticks) { mGracePeriod.store(ticks, std::memory_order_relaxed); }
        uint32_t getGracePeriod() const { return mGracePeriod.load(std::memory_order_relaxed); }
        
        size_t pendingCount() const {
            auto lock = std::lock_guard(mMtx);
            return mRetired.size();
        }

    private:
        struct RetiredBlock {
            void* ptr;
            uint32_t countdown;
        };

        mutable std::mutex mMtx;
        std::vector<RetiredBlock> mRetired;
        std::atomic<uint32_t> mGracePeriod{DEFAULT_GRACE_PERIOD};
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
        using is_always_equal = std::false_type;

        RetireBin* bin = nullptr;
    };
}
```


