

# File SectorsArray.h

[**File List**](files.md) **>** [**ecss**](dir_194708e763cf312315c6b23555bce86f.md) **>** [**memory**](dir_3333283e221f8a8f53c5923bc4c386e0.md) **>** [**SectorsArray.h**](SectorsArray_8h.md)

[Go to the documentation of this file](SectorsArray_8h.md)


```C++
#pragma once

#include <algorithm>
#include <cassert>
#include <shared_mutex>
#include <thread>
#include <utility>
#include <vector>

// CPU pause intrinsic for seqlock spin loops.
// - MSVC: _mm_pause() via <intrin.h>
// - GCC/Clang: __builtin_ia32_pause() / yield (compiler builtins, no header needed)
#if defined(_MSC_VER)
#  include <intrin.h>
#endif

#include <ecss/Ranges.h>
#include <ecss/threads/PinCounters.h>
#include <ecss/memory/ChunksAllocator.h>
#include <ecss/memory/Sector.h>

namespace ecss
{
    template <bool ThreadSafe, typename Allocator>
    class Registry;

    template <bool ThreadSafe, typename Allocator, bool Ranged, typename T, typename ...ComponentTypes>
    class ArraysView;
}

namespace ecss::Memory {

namespace detail {
    struct SlotInfo {
        std::byte* data = nullptr;        
        uint32_t linearIdx = INVALID_IDX; 
        
        FORCE_INLINE bool isValid() const { return data != nullptr; }
        FORCE_INLINE explicit operator bool() const { return isValid(); }
    };
    static_assert(std::atomic<std::byte*>::is_always_lock_free, "Pointer atomics must be lock-free for performance");

    inline constexpr SlotInfo INVALID_SLOT{ nullptr, INVALID_IDX };

    using ecss::cpuRelax;

    template<bool ThreadSafe, class T>
    FORCE_INLINE T loadRelaxed(const T* p, size_t i) noexcept {
        if constexpr (ThreadSafe) {
            return std::atomic_ref<T>(const_cast<T&>(p[i])).load(std::memory_order_relaxed);
        }
        else {
            return p[i];
        }
    }

    template<bool ThreadSafe>
    FORCE_INLINE uint32_t loadAliveAcquire(const uint32_t* p, size_t i) noexcept {
        if constexpr (ThreadSafe) {
            return std::atomic_ref<uint32_t>(const_cast<uint32_t&>(p[i])).load(std::memory_order_acquire);
        }
        else {
            return p[i];
        }
    }

    template<bool TS>
    struct SparseMap;

    template<>
    struct SparseMap<true> {
        struct SparseView {
            const uint32_t* data;
            size_t size;
        };

        FORCE_INLINE uint32_t findIdx(SectorId id) const {
            const auto view = loadView();
            if (id >= view.size) { return INVALID_IDX; }
            return std::atomic_ref<uint32_t>(const_cast<uint32_t&>(view.data[id])).load(std::memory_order_acquire);
        }

        FORCE_INLINE size_t capacity() const {
            return size_.load(std::memory_order_acquire);
        }

        FORCE_INLINE SparseView loadView() const noexcept {
            for (;;) {
                const uint64_t s1 = seq_.load(std::memory_order_acquire);
                if (s1 & 1ull) { cpuRelax(); continue; } // odd = writer in progress
                SparseView v{
                    data_.load(std::memory_order_relaxed),
                    size_.load(std::memory_order_relaxed),
                };
                std::atomic_thread_fence(std::memory_order_acquire);
                if (seq_.load(std::memory_order_relaxed) == s1) [[likely]] { return v; }
            }
        }

        FORCE_INLINE void storeView() {
            const uint64_t s = seq_.load(std::memory_order_relaxed);
            seq_.store(s + 1, std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_release);
            data_.store(sparse.data(), std::memory_order_relaxed);
            size_.store(sparse.size(), std::memory_order_relaxed);
            seq_.store(s + 2, std::memory_order_release);
        }

        FORCE_INLINE void drainRetired() { bin.drainAll(); }
        FORCE_INLINE size_t tickRetired() { return bin.tick(); }
        FORCE_INLINE void setGracePeriod(uint32_t ticks) { bin.setGracePeriod(ticks); }

        FORCE_INLINE void resize(size_t newSize) {
            sparse.resize(newSize, INVALID_IDX);
            storeView();
        }

        FORCE_INLINE void set(SectorId id, uint32_t idx) {
            std::atomic_ref<uint32_t>(sparse[id]).store(idx, std::memory_order_release);
        }

    private:
        mutable Memory::RetireBin bin;

        // Seqlock state; seq_ is even when stable, odd while a writer is publishing.
        alignas(64) std::atomic<uint64_t> seq_  { 0 };
        std::atomic<const uint32_t*>      data_ { nullptr };
        std::atomic<size_t>               size_ { 0 };

        static_assert(types::isLockFreeAtomic<uint64_t>, "seqlock counter must be lock-free");
        static_assert(types::isLockFreeAtomic<const uint32_t*>, "seqlock pointer must be lock-free");
        static_assert(types::isLockFreeAtomic<size_t>, "seqlock size must be lock-free");
        static_assert(types::isLockFreeAtomic<uint32_t>, "sparse entries must be lock-free");

    public:
        std::vector<uint32_t, Memory::RetireAllocator<uint32_t>> sparse{ Memory::RetireAllocator<uint32_t>{&bin} };
    };

    template<>
    struct SparseMap<false> {
        FORCE_INLINE uint32_t findIdx(SectorId id) const { return id < sparse.size() ? sparse[id] : INVALID_IDX; }
        FORCE_INLINE size_t capacity() const { return sparse.size(); }
        FORCE_INLINE void storeView() {} // dummy
        FORCE_INLINE void drainRetired() {} // dummy
        FORCE_INLINE size_t tickRetired() { return 0; } // dummy
        FORCE_INLINE void setGracePeriod(uint32_t) {} // dummy
        FORCE_INLINE void resize(size_t newSize) { sparse.resize(newSize, INVALID_IDX); }
        FORCE_INLINE void set(SectorId id, uint32_t idx) { sparse[id] = idx; }

        std::vector<uint32_t> sparse;
    };

    template<bool TS>
    struct DenseArrays;

    template<>
    struct DenseArrays<true> {
        struct View {
            const SectorId* ids;
            const uint32_t* isAlive;
            size_t size;
        };

        DenseArrays() = default;

        // Copy constructor - create new vectors with allocators bound to OUR bin
        DenseArrays(const DenseArrays& other)
            : ids(other.ids.begin(), other.ids.end(), Memory::RetireAllocator<SectorId>{&bin})
            , isAlive(other.isAlive.begin(), other.isAlive.end(), Memory::RetireAllocator<uint32_t>{&bin}) {
            auto otherView = other.loadView();
            storeView(otherView.size);
        }

        DenseArrays& operator=(const DenseArrays& other) {
            if (this != &other) {
                ids.assign(other.ids.begin(), other.ids.end());
                isAlive.assign(other.isAlive.begin(), other.isAlive.end());
                auto otherView = other.loadView();
                storeView(otherView.size);
            }
            return *this;
        }

        // Move constructor - create new vectors, move data, bind to OUR bin
        DenseArrays(DenseArrays&& other) noexcept
            : ids(Memory::RetireAllocator<SectorId>{&bin})
            , isAlive(Memory::RetireAllocator<uint32_t>{&bin}) {
            auto otherView = other.loadView();
            ids.reserve(other.ids.capacity());
            isAlive.reserve(other.isAlive.capacity());
            for (size_t i = 0; i < other.ids.size(); ++i) {
                ids.push_back(other.ids[i]);
                isAlive.push_back(other.isAlive[i]);
            }
            storeView(otherView.size);
            other.ids.clear();
            other.isAlive.clear();
            other.storeView(0);
        }

        DenseArrays& operator=(DenseArrays&& other) noexcept {
            if (this != &other) {
                ids.clear();
                isAlive.clear();
                ids.reserve(other.ids.capacity());
                isAlive.reserve(other.isAlive.capacity());
                for (size_t i = 0; i < other.ids.size(); ++i) {
                    ids.push_back(other.ids[i]);
                    isAlive.push_back(other.isAlive[i]);
                }
                auto otherView = other.loadView();
                storeView(otherView.size);
                other.ids.clear();
                other.isAlive.clear();
                other.storeView(0);
            }
            return *this;
        }

        FORCE_INLINE View loadView() const noexcept {
            for (;;) {
                uint64_t s1 = seq_.load(std::memory_order_acquire);
                if (s1 & 1ull) { // odd = writer in progress, spin
#if defined(_MSC_VER)
#  if defined(_M_X64) || defined(_M_IX86)
                    _mm_pause();
#  elif defined(_M_ARM64)
                    __yield();
#  endif
#else
#  if defined(__x86_64__) || defined(__i386__)
                    __builtin_ia32_pause();
#  elif defined(__aarch64__)
                    __asm__ __volatile__("yield" ::: "memory");
#  endif
#endif
                    continue;
                }
                View v{
                    ids_ptr_.load(std::memory_order_relaxed),
                    alive_ptr_.load(std::memory_order_relaxed),
                    size_.load(std::memory_order_relaxed),
                };
                std::atomic_thread_fence(std::memory_order_acquire);
                if (seq_.load(std::memory_order_relaxed) == s1) [[likely]] return v;
            }
        }

        FORCE_INLINE void storeView(size_t size) {
            uint64_t s = seq_.load(std::memory_order_relaxed);
            seq_.store(s + 1, std::memory_order_relaxed);            // enter: odd
            std::atomic_thread_fence(std::memory_order_release);
            ids_ptr_.store(ids.data(),       std::memory_order_relaxed);
            alive_ptr_.store(isAlive.data(), std::memory_order_relaxed);
            size_.store(size,                std::memory_order_relaxed);
            seq_.store(s + 2, std::memory_order_release);            // exit: even, publishes stores above
            // Note: drainAll() NOT called here - must be called under unique lock at safe point
        }

        FORCE_INLINE void drainRetired() { bin.drainAll(); }
        FORCE_INLINE size_t tickRetired() { return bin.tick(); }
        FORCE_INLINE void setGracePeriod(uint32_t ticks) { bin.setGracePeriod(ticks); }

        FORCE_INLINE void resize(size_t newSize, size_t actualSize) {
            ids.resize(newSize);
            isAlive.resize(newSize, 0);
            storeView(actualSize);
        }
        
        // Fast append - caller must call storeView() after
        FORCE_INLINE void pushBack(SectorId id, uint32_t alive) {
            ids.push_back(id);
            isAlive.push_back(alive);
        }

        FORCE_INLINE void reserve(size_t newCapacity) {
            ids.reserve(newCapacity);
            isAlive.reserve(newCapacity);
            storeView(size_.load(std::memory_order_relaxed));
        }

        FORCE_INLINE void clear(size_t actualSize) {
            ids.clear();
            isAlive.clear();
            storeView(actualSize);
        }

        FORCE_INLINE void shrinkToFit(size_t actualSize) {
            ids.shrink_to_fit();
            isAlive.shrink_to_fit();
            storeView(actualSize);
        }

        FORCE_INLINE SectorId& idAt(size_t idx) { return ids[idx]; }
        FORCE_INLINE uint32_t& isAliveAt(size_t idx) { return isAlive[idx]; }
        FORCE_INLINE const SectorId& idAt(size_t idx) const { return ids[idx]; }
        FORCE_INLINE const uint32_t& isAliveAt(size_t idx) const { return isAlive[idx]; }

        FORCE_INLINE void setIdAt(size_t idx, SectorId value) {
            std::atomic_ref<SectorId>(ids[idx]).store(value, std::memory_order_release);
        }
        FORCE_INLINE void setAliveAt(size_t idx, uint32_t value) {
            std::atomic_ref<uint32_t>(isAlive[idx]).store(value, std::memory_order_release);
        }

        mutable Memory::RetireBin bin;
        std::vector<SectorId, Memory::RetireAllocator<SectorId>> ids{ Memory::RetireAllocator<SectorId>{&bin} };
        std::vector<uint32_t, Memory::RetireAllocator<uint32_t>> isAlive{ Memory::RetireAllocator<uint32_t>{&bin} };

    private:
        // Seqlock state. seq_ is even when stable, odd while a writer is updating.
        // Aligned to a cache line so reader acquire-loads don't false-share with adjacent fields.
        // Data fields are atomic with relaxed ordering -- synchronization comes from seq_.
        alignas(64) std::atomic<uint64_t>            seq_       {0};
        std::atomic<const SectorId*>                 ids_ptr_   {nullptr};
        std::atomic<const uint32_t*>                 alive_ptr_ {nullptr};
        std::atomic<size_t>                          size_      {0};

        static_assert(std::atomic<uint64_t>::is_always_lock_free,
            "seqlock counter must be lock-free; widen or switch platform otherwise");
        static_assert(std::atomic<const SectorId*>::is_always_lock_free,
            "seqlock pointer field must be lock-free");
        static_assert(std::atomic<size_t>::is_always_lock_free,
            "seqlock size field must be lock-free");
    };

    template<>
    struct DenseArrays<false> {
        struct View {
            const SectorId* ids;
            const uint32_t* isAlive;
            size_t size;
        };

        FORCE_INLINE View loadView() const {
            return View{ ids.data(), isAlive.data(), ids.size() };
        }

        FORCE_INLINE void storeView(size_t) {} // dummy
        FORCE_INLINE void drainRetired() {} // dummy
        FORCE_INLINE size_t tickRetired() { return 0; } // dummy
        FORCE_INLINE void setGracePeriod(uint32_t) {} // dummy

        FORCE_INLINE void resize(size_t newSize, size_t) {
            ids.resize(newSize);
            isAlive.resize(newSize, 0);
        }
        
        // Fast append without size checks - caller ensures capacity
        FORCE_INLINE void pushBack(SectorId id, uint32_t alive) {
            ids.push_back(id);
            isAlive.push_back(alive);
        }

        FORCE_INLINE void reserve(size_t newCapacity) {
            ids.reserve(newCapacity);
            isAlive.reserve(newCapacity);
        }

        FORCE_INLINE void clear(size_t) {
            ids.clear();
            isAlive.clear();
        }

        FORCE_INLINE void shrinkToFit(size_t) {
            ids.shrink_to_fit();
            isAlive.shrink_to_fit();
        }

        FORCE_INLINE SectorId& idAt(size_t idx) { return ids[idx]; }
        FORCE_INLINE uint32_t& isAliveAt(size_t idx) { return isAlive[idx]; }
        FORCE_INLINE const SectorId& idAt(size_t idx) const { return ids[idx]; }
        FORCE_INLINE const uint32_t& isAliveAt(size_t idx) const { return isAlive[idx]; }

        FORCE_INLINE void setIdAt(size_t idx, SectorId value) { ids[idx] = value; }
        FORCE_INLINE void setAliveAt(size_t idx, uint32_t value) { isAlive[idx] = value; }

        std::vector<SectorId> ids;
        std::vector<uint32_t> isAlive;
    };
} // namespace detail

#define SHARED_LOCK() auto lock = readLock()
#define UNIQUE_LOCK() auto lock = writeLock()

#define TS_GUARD(TS_FLAG, LOCK_MACRO, EXPR) \
    do {enforceTSMode<TS>(); if constexpr (TS_FLAG) { LOCK_MACRO##_LOCK(); EXPR; } else { EXPR; }} while(0)

#define TS_GUARD_S(TS_FLAG, LOCK_MACRO, ADDITIONAL_SINK, EXPR) \
    do {enforceTSMode<TS>(); if constexpr (TS_FLAG) { LOCK_MACRO##_LOCK(); ADDITIONAL_SINK; EXPR; } else { EXPR; }} while(0)

struct PinnedSector {
    PinnedSector() = default;

    PinnedSector(const Threads::PinCounters& o, SectorId sid, std::byte* d, uint32_t alive)
        : owner(&o), id(sid), data(d), isAliveData(alive) {
        assert(id != INVALID_ID);
        const_cast<Threads::PinCounters*>(owner)->pin(id);
    }

    PinnedSector(const PinnedSector&) = delete;
    PinnedSector& operator=(const PinnedSector&) = delete;

    PinnedSector(PinnedSector&& other) noexcept { *this = std::move(other); }
    PinnedSector& operator=(PinnedSector&& other) noexcept {
        if (this == &other) { return *this; }
        release();
        owner = other.owner;
        id = other.id;
        data = other.data;
        isAliveData = other.isAliveData;
        other.owner = nullptr;
        other.data = nullptr;
        other.id = INVALID_ID;
        other.isAliveData = 0;
        return *this;
    }

    ~PinnedSector() { release(); }

    void release() {
        if (owner) {
            const_cast<Threads::PinCounters*>(owner)->unpin(id);
        }
        owner = nullptr;
        data = nullptr;
        id = INVALID_ID;
        isAliveData = 0;
    }

    std::byte* getData() const { return data; }
    uint32_t getIsAlive() const { return isAliveData; }
    explicit operator bool() const { return data != nullptr; }
    SectorId getId() const { return id; }

private:
    const Threads::PinCounters* owner = nullptr;
    SectorId id = INVALID_ID;
    std::byte* data = nullptr;
    uint32_t isAliveData = 0;
};

struct StructuralHold {
    StructuralHold() = default;

    explicit StructuralHold(const Threads::PinCounters& counters)
        : owner(&counters), shard(counters.acquireHold()) {}

    StructuralHold(const StructuralHold&) = delete;
    StructuralHold& operator=(const StructuralHold&) = delete;

    StructuralHold(StructuralHold&& other) noexcept { *this = std::move(other); }
    StructuralHold& operator=(StructuralHold&& other) noexcept {
        if (this != &other) {
            release();
            owner = other.owner;
            shard = other.shard;
            other.owner = nullptr;
        }
        return *this;
    }

    ~StructuralHold() { release(); }

    void release() {
        if (owner) { owner->releaseHold(shard); }
        owner = nullptr;
    }

    explicit operator bool() const { return owner != nullptr; }

private:
    const Threads::PinCounters* owner = nullptr;
    uint32_t shard = 0;
};

template<bool ThreadSafe = true, typename Allocator = ChunksAllocator<8192>>
class SectorsArray final {
    template<bool, typename>
    friend class SectorsArray;

    template<bool, typename>
    friend class ecss::Registry;

    template <bool TS, typename Alloc, bool Ranged, typename T, typename ...ComponentTypes>
    friend class ecss::ArraysView;

public:
    struct SlotInfo {
        SectorId id;
        uint32_t isAlive;
        std::byte* data;
        size_t linearIdx;

        explicit operator bool() const { return data != nullptr; }
    };

    // ==================== Iterators ====================

    static FORCE_INLINE SectorId loadId(const SectorId* p, size_t i) noexcept {
        return detail::loadRelaxed<ThreadSafe>(p, i);
    }

    static FORCE_INLINE uint32_t loadAliveRelaxed(const uint32_t* p, size_t i) noexcept {
        return detail::loadRelaxed<ThreadSafe>(p, i);
    }

    static FORCE_INLINE uint32_t loadAliveAcquire(const uint32_t* p, size_t i) noexcept {
        return detail::loadAliveAcquire<ThreadSafe>(p, i);
    }

#define ITERATOR_COMMON_USING(IteratorName)                                         \
    using iterator_concept  = std::forward_iterator_tag;                            \
    using iterator_category = std::forward_iterator_tag;                            \
    using value_type = SlotInfo;                                                    \
    using difference_type = std::ptrdiff_t;                                         \
    using pointer = SlotInfo*;                                                      \
    using reference = SlotInfo;                                                     \
    IteratorName() noexcept = default;                                              \
    FORCE_INLINE IteratorName operator++(int) noexcept { auto tmp = *this; ++(*this); return tmp; } \
    FORCE_INLINE bool operator!=(const IteratorName& other) const noexcept { return !(*this == other); }

    class Iterator {
    public:
        ITERATOR_COMMON_USING(Iterator)

        Iterator(const SectorsArray* array, size_t idx) {
            // The dense view first and the chunk table second, and the order is the point:
            // the table must be no older than the size it is indexed with. Taken the other
            // way round, an append landing between the two leaves a size that counts a
            // sector living in a chunk this table does not list, and the walk runs past its
            // end. Both snapshots are lock-free -- the chunk table has a seqlock of its own,
            // so neither needs the array's shared lock (see ChunksAllocator::loadChunks).
            auto view = array->mDenseArrays.loadView();
            mIds = view.ids;
            mIsAlive = view.isAlive;

            const auto chunks = array->mAllocator.loadChunks();
            mChunks = chunks.chunks;
            mChunksCount = chunks.count;
            mStride = chunks.sectorSize;
            mSize = view.size;
            mIdx = std::min(idx, mSize);
            initChunkState();
        }

        FORCE_INLINE value_type operator*() const {
            // Acquire-load the alive word so a set bit synchronizes-with the
            // release in Sector::markAlive<true>, making the component bytes visible.
            return SlotInfo{
                loadId(mIds, mIdx),
                loadAliveAcquire(mIsAlive, mIdx),
                mDataPtr,
                mIdx
            };
        }

        FORCE_INLINE Iterator& operator++() noexcept { 
            ++mIdx;
            ++mInChunkIdx;
            mDataPtr += mStride;
            if (mInChunkIdx >= Allocator::mChunkCapacity) [[unlikely]] {
                mInChunkIdx = 0;
                ++mChunkIdx;
                if (mChunkIdx < mChunksCount) {
                    mChunkBase = static_cast<std::byte*>(mChunks[mChunkIdx]);
                    mDataPtr = mChunkBase;
                } else {
                    mDataPtr = nullptr;
                }
            }
            return *this; 
        }
        FORCE_INLINE Iterator& operator+=(difference_type n) noexcept { 
            mIdx += n;
            mChunkIdx = mIdx >> Allocator::mChunkShift;
            mInChunkIdx = mIdx & (Allocator::mChunkCapacity - 1);
            if (mChunkIdx < mChunksCount) {
                mChunkBase = static_cast<std::byte*>(mChunks[mChunkIdx]);
                mDataPtr = mChunkBase + mInChunkIdx * mStride;
            } else {
                mDataPtr = nullptr;
            }
            return *this; 
        }
        FORCE_INLINE Iterator operator+(difference_type n) const noexcept { Iterator t(*this); t += n; return t; }
        
        FORCE_INLINE bool operator==(const Iterator& other) const noexcept { return mIdx == other.mIdx; }
        FORCE_INLINE size_t linearIndex() const noexcept { return mIdx; }
        FORCE_INLINE std::byte* rawPtr() const noexcept { return mDataPtr; }

    private:
        FORCE_INLINE void initChunkState() {
            if (mIdx < mSize && mChunksCount > 0) {
                mChunkIdx = mIdx >> Allocator::mChunkShift;
                mInChunkIdx = mIdx & (Allocator::mChunkCapacity - 1);
                if (mChunkIdx < mChunksCount) {
                    mChunkBase = static_cast<std::byte*>(mChunks[mChunkIdx]);
                    mDataPtr = mChunkBase + mInChunkIdx * mStride;
                }
            }
        }

        const SectorId* mIds = nullptr;
        const uint32_t* mIsAlive = nullptr;
        void* const* mChunks = nullptr;
        std::byte* mChunkBase = nullptr;
        std::byte* mDataPtr = nullptr;
        size_t mChunksCount = 0;
        size_t mChunkIdx = 0;
        size_t mInChunkIdx = 0;
        size_t mIdx = 0;
        size_t mSize = 0;
        uint16_t mStride = 0;
    };

    // No lock: an iterator is built entirely from lock-free snapshots (the dense arrays
    // seqlock and the chunk table seqlock), and old buffers stay readable because both are
    // retire-allocated. Taking the shared lock here only serialised readers against each
    // other on one mutex word.
    template<bool TS = ThreadSafe> Iterator begin() const { enforceTSMode<TS>(); return Iterator(this, 0); }
    template<bool TS = ThreadSafe> Iterator end()   const { enforceTSMode<TS>(); return Iterator(this, sizeImpl()); }

    class IteratorAlive {
    public:
        ITERATOR_COMMON_USING(IteratorAlive)

        IteratorAlive(const SectorsArray* array, size_t idx, size_t sz, uint32_t aliveMask, bool isPacked = false)
            : mIdx(idx)
            , mAliveMask(aliveMask)
            , mIsPacked(isPacked) {
            // The dense view first and the chunk table second, and the order is the point:
            // the table must be no older than the size it is indexed with. Taken the other
            // way round, an append landing between the two leaves a size that counts a
            // sector living in a chunk this table does not list, and the walk runs past its
            // end. Both snapshots are lock-free -- the chunk table has a seqlock of its own,
            // so neither needs the array's shared lock (see ChunksAllocator::loadChunks).
            auto view = array->mDenseArrays.loadView();
            mIds = view.ids;
            mIsAlive = view.isAlive;

            const auto chunks = array->mAllocator.loadChunks();
            mChunks = chunks.chunks;
            mChunksCount = chunks.count;
            mStride = chunks.sectorSize;
            mSize = std::min(sz, view.size);
            // Clamped against the snapshot, as Iterator does: an index past it would read
            // liveness words that are not in this view.
            mIdx = std::min(mIdx, mSize);
            if (mIsPacked) {
                initChunkState();
            } else {
                skipDeadFast(); // This also syncs data pointer
            }
        }

        FORCE_INLINE value_type operator*() const {
            // Acquire-load the alive word so a set bit synchronizes-with the
            // release in Sector::markAlive<true>, making the component bytes visible.
            return SlotInfo{
                loadId(mIds, mIdx),
                loadAliveAcquire(mIsAlive, mIdx),
                mDataPtr,
                mIdx
            };
        }

        FORCE_INLINE IteratorAlive& operator++() noexcept {
            ++mIdx;
            // Step first, scan only across a gap. The scan is not cheap -- it reads four
            // liveness words before it will even look at one, and then rebuilds the data
            // pointer from the index through the chunk table -- and it used to run on every
            // increment, including the overwhelmingly common one where the very next slot is
            // alive and a pointer bump is the whole job. Over a million single-component
            // sectors that was 1.77 ms against 1.06 for this, same elements and same result.
            advanceDataPtr();
            if (!mIsPacked
                && mIdx < mSize
                && !(loadAliveRelaxed(mIsAlive, mIdx) & mAliveMask)) [[unlikely]] {
                skipDeadFast();
            }
            return *this;
        }

        FORCE_INLINE bool operator==(const IteratorAlive& other) const noexcept { return mIdx == other.mIdx; }
        FORCE_INLINE size_t linearIndex() const noexcept { return mIdx; }
        FORCE_INLINE std::byte* rawPtr() const noexcept { return mDataPtr; }
        FORCE_INLINE explicit operator bool() const noexcept { return mIdx < mSize; }

        FORCE_INLINE void becomeEnd() noexcept {
            mIdx = mSize;
            mDataPtr = nullptr;
        }

        FORCE_INLINE void advanceToId(SectorId minId) {
            size_t lo = mIdx;
            size_t hi = mSize;
            while (lo < hi) {
                const size_t mid = lo + (hi - lo) / 2;
                if (loadId(mIds, mid) < minId) {
                    lo = mid + 1;
                }
                else {
                    hi = mid;
                }
            }
            mIdx = lo;
            if (mIsPacked) {
                syncDataPtr();
            }
            else {
                skipDeadFast();
            }
        }

    private:
        FORCE_INLINE void syncDataPtr() {
            if (mIdx < mSize && mChunksCount > 0) {
                mChunkIdx = mIdx >> Allocator::mChunkShift;
                mInChunkIdx = mIdx & (Allocator::mChunkCapacity - 1);
                if (mChunkIdx < mChunksCount) {
                    mChunkBase = static_cast<std::byte*>(mChunks[mChunkIdx]);
                    mDataPtr = mChunkBase + mInChunkIdx * mStride;
                } else {
                    mDataPtr = nullptr;
                }
            }
        }

        FORCE_INLINE void advanceDataPtr() {
            ++mInChunkIdx;
            mDataPtr += mStride;
            if (mInChunkIdx >= Allocator::mChunkCapacity) [[unlikely]] {
                mInChunkIdx = 0;
                ++mChunkIdx;
                if (mChunkIdx < mChunksCount) {
                    mChunkBase = static_cast<std::byte*>(mChunks[mChunkIdx]);
                    mDataPtr = mChunkBase;
                } else {
                    mDataPtr = nullptr;
                }
            }
        }

        FORCE_INLINE void skipDeadFast() {
            const uint32_t mask = mAliveMask;
            // Batch check 4 elements at a time (cache-friendly)
            while (mIdx + 4 <= mSize) {
                if ((loadAliveRelaxed(mIsAlive, mIdx)   & mask) |
                    (loadAliveRelaxed(mIsAlive, mIdx+1) & mask) |
                    (loadAliveRelaxed(mIsAlive, mIdx+2) & mask) |
                    (loadAliveRelaxed(mIsAlive, mIdx+3) & mask)) {
                    break; // At least one alive in this batch
                }
                mIdx += 4;
            }
            // Fine-grained search for exact position
            while (mIdx < mSize && !(loadAliveRelaxed(mIsAlive, mIdx) & mask)) {
                ++mIdx;
            }
            // Update data pointer once
            syncDataPtr();
        }

        FORCE_INLINE void initChunkState() { syncDataPtr(); }

        const SectorId* mIds = nullptr;
        const uint32_t* mIsAlive = nullptr;
        void* const* mChunks = nullptr;
        std::byte* mChunkBase = nullptr;
        std::byte* mDataPtr = nullptr;
        size_t mChunksCount = 0;
        size_t mChunkIdx = 0;
        size_t mInChunkIdx = 0;
        size_t mIdx = 0;
        size_t mSize = 0;
        uint16_t mStride = 0;
        uint32_t mAliveMask = 0;
        bool mIsPacked = false;
    };

    template<bool TS = ThreadSafe>
    bool isPacked() const { enforceTSMode<TS>(); return mDefragmentSize.load(std::memory_order_relaxed) == 0; }

    template<class T, bool TS = ThreadSafe>
    IteratorAlive beginAlive() const {
        // Note: isPacked=false because we're filtering by a specific component's alive mask,
        // not just checking if any component is alive. mDefragmentSize==0 only means no dead
        // sectors, not that all sectors have this specific component.
        enforceTSMode<TS>();
        return IteratorAlive(this, 0, sizeImpl(), getLayoutData<T>().isAliveMask, false);
    }
    template<bool TS = ThreadSafe>
    IteratorAlive endAlive() const {
        enforceTSMode<TS>();
        return IteratorAlive(this, sizeImpl(), sizeImpl(), 0, true);
    }

    class RangedIterator {
    public:
        ITERATOR_COMMON_USING(RangedIterator)

        RangedIterator(const SectorsArray* array, const Ranges<SectorId>& ranges) {
            // The dense view first and the chunk table second, and the order is the point:
            // the table must be no older than the size it is indexed with. Taken the other
            // way round, an append landing between the two leaves a size that counts a
            // sector living in a chunk this table does not list, and the walk runs past its
            // end. Both snapshots are lock-free -- the chunk table has a seqlock of its own,
            // so neither needs the array's shared lock (see ChunksAllocator::loadChunks).
            auto view = array->mDenseArrays.loadView();
            mIds = view.ids;
            mIsAlive = view.isAlive;

            const auto chunks = array->mAllocator.loadChunks();
            mChunks = chunks.chunks;
            mChunksCount = chunks.count;
            mStride = chunks.sectorSize;
            mSize = view.size;
            // Convert SectorId ranges to linear index ranges
            for (const auto& [first, last] : ranges.ranges) {
                size_t beginIdx = lowerBound(first);
                size_t endIdx = lowerBound(last);
                if (beginIdx < endIdx) {
                    mLinearRanges.push_back({beginIdx, endIdx});
                }
            }
            if (!mLinearRanges.empty()) {
                mRangeIdx = 0;
                mIdx = mLinearRanges[0].first;
                updatePtrFromIdx();
            } else {
                mIdx = mSize;
            }
        }

        FORCE_INLINE value_type operator*() const {
            // Acquire-load the alive word so a set bit synchronizes-with the
            // release in Sector::markAlive<true>, making the component bytes visible.
            return SlotInfo{
                loadId(mIds, mIdx),
                loadAliveAcquire(mIsAlive, mIdx),
                mDataPtr,
                mIdx
            };
        }

        FORCE_INLINE RangedIterator& operator++() noexcept {
            // Exhausted: mRangeIdx indexes one past the last range (or mLinearRanges is
            // empty, as for a default-constructed / end iterator). Indexing it here was an
            // out-of-bounds read on every increment past the end.
            if (mRangeIdx >= mLinearRanges.size()) [[unlikely]] {
                mIdx = mSize;
                mDataPtr = nullptr;
                return *this;
            }

            ++mIdx;
            ++mInChunkIdx;
            mDataPtr += mStride;
            if (mInChunkIdx >= Allocator::mChunkCapacity) [[unlikely]] {
                mInChunkIdx = 0;
                ++mChunkIdx;
                mDataPtr = mChunkIdx < mChunksCount ? static_cast<std::byte*>(mChunks[mChunkIdx]) : nullptr;
            }
            // Check range boundary (now using linear index ranges)
            if (mIdx >= mLinearRanges[mRangeIdx].second) [[unlikely]] {
                advanceToNextRange();
            }
            return *this;
        }

        FORCE_INLINE void advanceToLinearIdx(size_t targetIdx) {
            if (mIdx >= targetIdx) return;
            mIdx = targetIdx;
            clampToRangeAndUpdatePtr();
        }

        FORCE_INLINE bool operator==(const RangedIterator& other) const noexcept { return mIdx == other.mIdx; }
        FORCE_INLINE size_t linearIndex() const noexcept { return mIdx; }
        FORCE_INLINE std::byte* rawPtr() const noexcept { return mDataPtr; }
        FORCE_INLINE explicit operator bool() const noexcept { return mIdx < mSize && mRangeIdx < mLinearRanges.size(); }

    private:
        FORCE_INLINE size_t lowerBound(SectorId sectorId) const {
            size_t left = 0, right = mSize;
            while (left < right) {
                size_t mid = left + (right - left) / 2;
                if (loadId(mIds, mid) < sectorId) {
                    left = mid + 1;
                } else {
                    right = mid;
                }
            }
            return left;
        }

        FORCE_INLINE void advanceToNextRange() {
            ++mRangeIdx;
            if (mRangeIdx < mLinearRanges.size()) {
                mIdx = mLinearRanges[mRangeIdx].first;
                updatePtrFromIdx();
            } else {
                mIdx = mSize;
                mDataPtr = nullptr;
            }
        }

        FORCE_INLINE void clampToRangeAndUpdatePtr() {
            while (mRangeIdx < mLinearRanges.size()) {
                if (mIdx < mLinearRanges[mRangeIdx].second && mIdx >= mLinearRanges[mRangeIdx].first) {
                    updatePtrFromIdx();
                    return;
                }
                if (mIdx >= mLinearRanges[mRangeIdx].second) {
                    ++mRangeIdx;
                    if (mRangeIdx < mLinearRanges.size()) {
                        mIdx = mLinearRanges[mRangeIdx].first;
                    }
                } else {
                    mIdx = mLinearRanges[mRangeIdx].first;
                }
            }
            mIdx = mSize;
            mDataPtr = nullptr;
        }

        FORCE_INLINE void updatePtrFromIdx() {
            mChunkIdx = mIdx >> Allocator::mChunkShift;
            mInChunkIdx = mIdx & (Allocator::mChunkCapacity - 1);
            if (mChunkIdx < mChunksCount) {
                mDataPtr = static_cast<std::byte*>(mChunks[mChunkIdx]) + mInChunkIdx * mStride;
            } else {
                mDataPtr = nullptr;
            }
        }

        const SectorId* mIds = nullptr;
        const uint32_t* mIsAlive = nullptr;
        void* const* mChunks = nullptr;
        std::byte* mDataPtr = nullptr;
        size_t mChunksCount = 0;
        size_t mChunkIdx = 0;
        size_t mInChunkIdx = 0;
        std::vector<std::pair<size_t, size_t>> mLinearRanges;  
        size_t mIdx = 0;
        size_t mSize = 0;
        size_t mRangeIdx = 0;
        uint16_t mStride = 0;
    };

    template<bool TS = ThreadSafe>
    RangedIterator beginRanged(const Ranges<SectorId>& ranges) const {
        enforceTSMode<TS>();
        return RangedIterator(this, ranges);
    }
    template<bool TS = ThreadSafe>
    RangedIterator endRanged() const {
        enforceTSMode<TS>();
        return RangedIterator(this, Ranges<SectorId>{});
    }

    // ==================== Copy / Move ====================

    template<bool T, typename Alloc>
    SectorsArray(const SectorsArray<T, Alloc>& other) { configureReclamation(); *this = other; }
    SectorsArray(const SectorsArray& other) { configureReclamation(); *this = other; }

    template<bool T, typename Alloc>
    SectorsArray& operator=(const SectorsArray<T, Alloc>& other) { if (!isSameAdr(this, &other)) { copy(other); } return *this; }
    SectorsArray& operator=(const SectorsArray& other) { if (this != &other) { copy(other); } return *this; }

    template<bool T, typename Alloc>
    SectorsArray(SectorsArray<T, Alloc>&& other) noexcept { configureReclamation(); *this = std::move(other); }
    SectorsArray(SectorsArray&& other) noexcept { configureReclamation(); *this = std::move(other); }

    template<bool T, typename Alloc>
    SectorsArray& operator=(SectorsArray<T, Alloc>&& other) noexcept { if (!isSameAdr(this, &other)) { move(std::move(other)); } return *this; }
    SectorsArray& operator=(SectorsArray&& other) noexcept { if (this != &other) { move(std::move(other)); } return *this; }

private:
    SectorsArray(const SectorLayoutMeta* meta, Allocator&& allocator = {}) : mAllocator(std::move(allocator)) {
        configureReclamation();
        mAllocator.init(meta);
        mSparseMap.storeView();
    }

public:
    ~SectorsArray() { clear(); shrinkToFit(); }

    template <typename... Types>
    static SectorsArray* create(Allocator&& allocator = {}) {
        static_assert(types::areUnique<Types...>, "Duplicates detected in SectorsArray types!");
        // One per distinct type pack, for the lifetime of the process: the layout an array
        // reports never changes, and the LayoutData records keep fixed addresses.
        static const SectorLayoutMeta* meta = SectorLayoutMeta::create<Types...>();
        return new SectorsArray(meta, std::move(allocator));
    }

    // ==================== Layout helpers ====================

    template<typename T>
    FORCE_INLINE const LayoutData& getLayoutData() const { return getLayout()->template getLayoutData<T>(); }

    FORCE_INLINE const SectorLayoutMeta* getLayout() const { return mAllocator.getSectorLayout(); }

    // ==================== Pin API (ThreadSafe builds) ====================

    // No lock: pinning validates itself against the structural epoch (see pinSectorImpl).
    // The shared lock used to be what made "writer holds the unique lock => no new pins"
    // true; the epoch handshake replaces it, and every reader stops serialising on one word.

    template<bool TS = true>
    [[nodiscard]] PinnedSector pinSector(SectorId id) const requires(ThreadSafe) {
        enforceTSMode<TS>();
        return pinSectorImpl(id);
    }

    template<bool TS = true>
    [[nodiscard]] PinnedSector pinSectorAt(size_t idx) const requires(ThreadSafe) {
        enforceTSMode<TS>();
        return pinSectorAtImpl(idx);
    }

    template<bool TS = true>
    [[nodiscard]] PinnedSector pinBackSector() const requires(ThreadSafe) {
        enforceTSMode<TS>();
        return pinBackSectorImpl();
    }

    [[nodiscard]] StructuralHold holdStructure() const requires(ThreadSafe) {
        // Same courtesy the pin path pays: a few threads building views in a loop would
        // otherwise keep the array permanently held and compaction would never run.
        yieldToWriters();
        return StructuralHold(mPinsCounter);
    }

    // ==================== Erase & maintenance ====================

    template<bool TS = ThreadSafe>
    void erase(size_t beginIdx, size_t count = 1, bool defragment = false) {
        if constexpr(TS && ThreadSafe) {
            exclusiveWhenQuiescent([&] {
                if (beginIdx >= sizeImpl()) return;
                eraseRangeImpl(beginIdx, count, defragment);
            });
        } else {
            if (beginIdx >= sizeImpl()) return;
            eraseRangeImpl(beginIdx, count, defragment);
        }
    }

    template<bool TS = ThreadSafe>
    Iterator erase(Iterator it, bool defragment = false) noexcept {
        auto idx = it.linearIndex();
        if constexpr (TS && ThreadSafe) {
            return exclusiveWhenQuiescent([&] {
                if (idx >= sizeImpl()) return it;
                eraseRangeImpl(idx, 1, defragment);
                return Iterator(this, idx);
            });
        } else {
            if (idx >= sizeImpl()) return it;
            eraseRangeImpl(idx, 1, defragment);
            return Iterator(this, idx);
        }
    }

    void clearAsync() requires(ThreadSafe) {
        mHasPendingClear.store(true, std::memory_order_release);
    }

    bool hasPendingClear() const noexcept { return mHasPendingClear.load(std::memory_order_acquire); }

    bool tryClearImpl() requires(ThreadSafe) {
        // Announce, as tryDefragmentImpl() does: readers that hold nothing stand aside, so a
        // busy array is usually free by the next call rather than perpetually skipped.
        Threads::PinCounters::WriterIntent intent(mPinsCounter);
        if (mPinsCounter.hasAnyPins()) { return false; }

        auto lock = writeLock();
        StructuralEdit edit(*this);
        // Re-checked after the epoch is published, as everywhere else.
        if (mPinsCounter.hasAnyPins()) { return false; }

        clearImpl();
        mHasPendingClear.store(false, std::memory_order_release);
        return true;
    }

    void eraseAsync(SectorId id, size_t count = 1) requires(ThreadSafe) {
        // Note: Uses shared_lock for initial lookup, unique_lock for actual modification.
        // Early exit optimization if sector not found. Element deletion may be deferred.
        for (auto i = id; i < id + count; ++i) {
            eraseAsyncImpl(i);
        }
    }

    // ==================== Lookup ====================
    //
    // These take no lock. Every one of them resolves through the SparseMap seqlock and the
    // DenseArrays seqlock, both of which are lock-free and tolerate a concurrent writer by
    // construction (old buffers are held by RetireAllocator until the grace period expires).
    // Taking the array shared_mutex here bought nothing but a contended cache line: it made
    // every reader serialise on one SRWLOCK word (measured ~320x per-op latency at 32
    // threads). ArraysView::getComponent has always used exactly this unlocked path.
    //
    // Note this is a point-in-time sample either way -- the answer could be stale the moment
    // the lock was released, so holding it never made the result more authoritative.
    // Operations that must *keep* a result valid pin the sector instead (see pinSector).

    template<bool TS = ThreadSafe>
    size_t findLinearIdx(SectorId sectorId) const {
        enforceTSMode<TS>();
        return findLinearIdxImpl(sectorId);
    }

    template<bool TS = ThreadSafe>
    bool containsSector(SectorId id) const {
        enforceTSMode<TS>();
        return containsSectorImpl(id);
    }

    template<bool TS = ThreadSafe>
    std::byte* findSectorData(SectorId id) const {
        enforceTSMode<TS>();
        return findSectorDataImpl(id);
    }

    template<bool TS = ThreadSafe>
    detail::SlotInfo findSlot(SectorId id) const {
        enforceTSMode<TS>();
        return findSlotImpl(id);
    }

    template<bool TS = ThreadSafe>
    uint32_t getIsAlive(SectorId id) const {
        enforceTSMode<TS>();
        const auto idx = findLinearIdxImpl(id);
        // loadAliveWord, not isAliveAt: the latter dereferences the live vector, which a
        // concurrent push_back may be reallocating. The seqlock snapshot is bounds-checked.
        return idx != INVALID_IDX ? loadAliveWord<ThreadSafe>(idx) : 0;
    }

    template<bool TS = ThreadSafe>
    uint32_t& getIsAliveRef(size_t linearIdx) {
        return mDenseArrays.isAliveAt(linearIdx);
    }

    template<bool TS = ThreadSafe>
    FORCE_INLINE uint32_t loadAliveWord(size_t linearIdx) const noexcept {
        if constexpr (TS) {
            auto view = mDenseArrays.loadView();
            if (linearIdx >= view.size) [[unlikely]] return 0;
            return loadAliveAcquire(view.isAlive, linearIdx);
        } else {
            return mDenseArrays.isAliveAt(linearIdx);
        }
    }

    template<bool TS = ThreadSafe>
    SectorId getId(size_t linearIdx) const {
        return mDenseArrays.idAt(linearIdx);
    }

    // ==================== Capacity ====================

    // sparseCapacity/size/empty read a single atomic -- no lock needed (see Lookup note).
    // capacity() keeps the shared lock: it reads the chunk vector, which a concurrent
    // allocate() may be reallocating, and that vector is not published through a seqlock.
    template<bool TS = ThreadSafe> size_t sparseCapacity() const { enforceTSMode<TS>(); return mSparseMap.capacity(); }
    template<bool TS = ThreadSafe> size_t capacity() const { TS_GUARD(TS && ThreadSafe, SHARED, return mAllocator.capacity()); }
    template<bool TS = ThreadSafe> size_t size() const { enforceTSMode<TS>(); return sizeImpl(); }
    template<bool TS = ThreadSafe> bool empty() const { enforceTSMode<TS>(); return sizeImpl() == 0; }
    template<bool TS = ThreadSafe> void shrinkToFit() { TS_GUARD(TS && ThreadSafe, UNIQUE, shrinkToFitImpl()); }

    template<bool TS = ThreadSafe> void reserve(uint32_t newCapacity) { TS_GUARD(TS && ThreadSafe, UNIQUE, reserveImpl(newCapacity)); }
    template<bool TS = ThreadSafe> void clear() {
        if constexpr (TS && ThreadSafe) { exclusiveWhenQuiescent([&] { clearImpl(); }); }
        else { clearImpl(); }
        // A clearAsync() still waiting for a free frame has just had its wish granted.
        // Leaving the flag set sends the next maintenance pass through a write lock and a
        // published structural epoch to clear an array that is already empty.
        mHasPendingClear.store(false, std::memory_order_release);
    }

    // ==================== Defragmentation ====================

    template<bool TS = ThreadSafe>
    void defragment() {
        if constexpr (TS && ThreadSafe) { exclusiveWhenQuiescent([&] { defragmentImpl(); }); }
        else { defragmentImpl(); }
    }

    template<bool TS = ThreadSafe>
    void tryDefragment() { TS_GUARD_S(TS && ThreadSafe, UNIQUE, if (mPinsCounter.hasAnyPins()) return;, defragmentImpl();); }

    void incDefragmentSize(uint32_t count = 1) { mDefragmentSize.fetch_add(count, std::memory_order_relaxed); }

    template<bool TS = ThreadSafe> auto getDefragmentationSize() const { enforceTSMode<TS>(); return mDefragmentSize.load(std::memory_order_relaxed); }
    template<bool TS = ThreadSafe> auto getDefragmentationRatio() const {
        enforceTSMode<TS>();
        const auto sz = sizeImpl();
        return sz ? (static_cast<float>(mDefragmentSize.load(std::memory_order_relaxed)) / static_cast<float>(sz)) : 0.f;
    }
    template<bool TS = ThreadSafe> bool needDefragment() const {
        enforceTSMode<TS>();
        return getDefragmentationRatio<false>() > loadDefragThreshold();
    }
    template<bool TS = ThreadSafe> void setDefragmentThreshold(float threshold) { 
        enforceTSMode<TS>(); storeDefragThreshold(std::max(0.f, std::min(threshold, 1.f))); 
    }

    // ==================== Retired Memory Management (ThreadSafe only) ====================

    size_t tick() { return tickRetired(); }


    template<typename T, bool TS = ThreadSafe>
    std::remove_cvref_t<T>* insert(SectorId sectorId, T&& data) noexcept {
        using U = std::remove_cvref_t<T>;
        if constexpr (TS && ThreadSafe) {
            return exclusiveForInsert(sectorId, [&](size_t pos) {
                return writeMemberImpl<T>(pos, std::forward<T>(data));
            });
        } else {
            return insertImpl(sectorId, std::forward<T>(data));
        }
    }

    template<typename T, bool TS = ThreadSafe, class... Args>
    T* emplace(SectorId sectorId, Args&&... args) noexcept {
        if constexpr (TS && ThreadSafe) {
            return exclusiveForInsert(sectorId, [&](size_t pos) {
                return emplaceMemberImpl<T>(pos, std::forward<Args>(args)...);
            });
        } else {
            return emplaceImpl<T>(sectorId, std::forward<Args>(args)...);
        }
    }

    template<typename T, bool TS = ThreadSafe, class... Args>
    T* push(SectorId sectorId, Args&&... args) noexcept {
        if constexpr (sizeof...(Args) == 1 && (std::is_same_v<std::remove_cvref_t<Args>, T> && ...)) {
            return insert<Args..., TS>(sectorId, std::forward<Args>(args)...);
        } else {
            return emplace<T, TS>(sectorId, std::forward<Args>(args)...);
        }
    }

    template<typename C, typename It, bool TS = ThreadSafe>
    void insertBulk(It first, It last) noexcept {
        if constexpr (TS && ThreadSafe) { exclusiveWhenQuiescent([&] { insertBulkImpl<C>(first, last); }); }
        else { insertBulkImpl<C>(first, last); }
    }

    template<bool Lock = true>
    void processPendingErases(bool withDefragment = true) requires(ThreadSafe) {
        // Registry::update() calls this for every array every frame. Both conditions below
        // are lock-free, so an array with nothing queued and nothing to compact costs a
        // couple of atomic loads instead of a write-lock acquisition.
        if constexpr (Lock) {
            if (!mHasPendingErase.load(std::memory_order_acquire)
                && !mHasPendingClear.load(std::memory_order_acquire)
                && !needDefragment<false>()) [[likely]] {
                return;
            }
        }

        // A pending clear supersedes everything else queued: it destroys the sectors the
        // erase queue names and leaves nothing to compact. Attempted, not awaited, for the
        // same reason compaction is -- see tryDefragmentImpl().
        if constexpr (Lock) {
            if (mHasPendingClear.load(std::memory_order_acquire) && tryClearImpl()) {
                return;
            }
        }

        // Two phases on purpose: the deferred erases themselves only destroy sectors in
        // place and never block, while compaction relocates sectors and therefore needs
        // the array quiescent -- which must be awaited *outside* the write lock.
        bool wantsDefragment = false;
        if constexpr(Lock) {
            auto lock = std::unique_lock(mtx);
            StructuralEdit edit(*this);
            wantsDefragment = processPendingErasesImpl();
        } else {
            wantsDefragment = processPendingErasesImpl();
        }

        if (withDefragment && wantsDefragment) {
            if constexpr(Lock) {
                // Attempted, not awaited. Waiting here is what made this call care where it
                // was made from: compaction needs the array quiescent, and a caller that is
                // itself iterating holds the very thing being waited for, so update() from
                // inside a loop hung. The work is deferred by nature -- needDefragment stays
                // true and the next call picks it up -- so a busy array is skipped instead.
                tryDefragmentImpl();
            } else {
                defragmentImpl(); // caller owns the lock and the quiescence precondition
            }
        }
    }

    bool tryDefragmentImpl() requires(ThreadSafe) {
        // Announce the attempt even though we will not block on it: readers back off for a
        // bounded spin when a writer is waiting (see yieldToWriters), so an array that is
        // busy this time is usually free the next.
        Threads::PinCounters::WriterIntent intent(mPinsCounter);
        if (mPinsCounter.hasAnyPins()) {
            return false;
        }

        auto lock = writeLock();
        StructuralEdit edit(*this);
        // Re-checked after the epoch is published, as everywhere else: a pin taken in between
        // observes the odd epoch and retries, so it cannot be live across the relocation.
        if (mPinsCounter.hasAnyPins()) {
            return false;
        }
        defragmentImpl();
        return true;
    }

    // ==================== Lock access (for Registry) ====================
    auto readLock() const requires(ThreadSafe) { return std::shared_lock(mtx); }
    auto writeLock() const requires(ThreadSafe) { return std::unique_lock(mtx); }

    struct StructuralEdit {
        explicit StructuralEdit(const SectorsArray& owner) noexcept : arr(owner) {
            arr.mStructEpoch.fetch_add(1, std::memory_order_seq_cst); // odd: edit in progress
        }
        ~StructuralEdit() { arr.mStructEpoch.fetch_add(1, std::memory_order_release); }
        StructuralEdit(const StructuralEdit&) = delete;
        StructuralEdit& operator=(const StructuralEdit&) = delete;

        const SectorsArray& arr;
    };

    // ==================== Write-lock acquisition helpers ====================
    //
    // Invariant for every helper below: never block on pins while holding the write lock.
    // Pins are only ever taken under the shared lock, so a thread that already holds a pin
    // and then needs the shared lock again (a second view, pinComponent, hasComponent)
    // would deadlock against a writer parked on waitUntil* inside the unique lock.
    // The pattern is therefore always: wait outside -> lock -> re-verify -> retry.
    //
    // Once the write lock is held and the predicate re-checked, it cannot be invalidated:
    // acquiring a new pin requires at least the shared lock, which the writer excludes.

    template<typename Fn>
    auto exclusiveWhenUnpinned(SectorId sectorId, Fn&& fn) requires(ThreadSafe) {
        for (;;) {
            mPinsCounter.waitUntilChangeable(sectorId);
            auto lock = writeLock();
            StructuralEdit edit(*this);
            // Re-checked after publishing the epoch, never before: a pin taken in between is
            // then guaranteed to observe the odd epoch and retry.
            if (mPinsCounter.canMoveSector(sectorId)) {
                return std::forward<Fn>(fn)();
            }
        }
    }

    template<typename Fn>
    auto exclusiveWhenUnpinned(const EntityId* begin, const EntityId* end, Fn&& fn) requires(ThreadSafe) {
        for (;;) {
            // One aggregate load before the per-id walk: if nothing on the array is pinned at
            // all, none of the named sectors can be either. Unloading a region names tens of
            // thousands of ids and was paying for that walk twice over, once per array.
            if (mPinsCounter.hasAnyPinnedSector()) {
                for (auto p = begin; p != end; ++p) {
                    if (*p != INVALID_ID) {
                        mPinsCounter.waitUntilChangeable(*p);
                    }
                }
            }
            auto lock = writeLock();
            StructuralEdit edit(*this);
            // Re-checked after the epoch is published, never before: a pin taken in between
            // observes the odd epoch and retries. The aggregate is the stronger question, so
            // answering it settles every id at once.
            if (!mPinsCounter.hasAnyPinnedSector()) {
                return std::forward<Fn>(fn)();
            }
            bool blocked = false;
            for (auto p = begin; p != end; ++p) {
                if (*p != INVALID_ID && !mPinsCounter.canMoveSector(*p)) {
                    blocked = true;
                    break;
                }
            }
            if (!blocked) {
                return std::forward<Fn>(fn)();
            }
        }
    }

    template<typename Fn>
    auto exclusiveWhenQuiescent(Fn&& fn) requires(ThreadSafe) {
        // Announced for the whole attempt, not just for the wait inside waitUntilQuiescent().
        // The intent used to drop the moment that wait returned, so between there and taking
        // the lock readers saw no writer and had no reason to yield: one of them took a hold,
        // the check below failed, and the loop went round with the same gap open every time.
        Threads::PinCounters::WriterIntent intent(mPinsCounter);
        for (;;) {
            mPinsCounter.waitUntilQuiescent();
            auto lock = writeLock();
            StructuralEdit edit(*this);
            if (!mPinsCounter.hasAnyPins()) {
                return std::forward<Fn>(fn)();
            }
        }
    }

    template<typename Fn>
    auto exclusiveForInsert(SectorId sectorId, Fn&& fn) requires(ThreadSafe) {
        for (;;) {
            bool blockedByOtherSectors = false;
            {
                auto lock = writeLock();

                // An append past the end relocates no existing sector and names one that does
                // not exist yet, so no pin can refer to it and nothing has to be announced.
                // The sector becomes visible carrying isAlive == 0, and emplaceMember sets the
                // bit with a release store once the component is constructed, so a reader that
                // finds it either skips it or sees it whole -- the liveness word already does
                // what the epoch would have done here.
                //
                // Whether an insert is an append is only visible under the lock, which is why
                // this is a check here rather than a wait outside. Skipping both the pin wait
                // and the epoch takes a thread-safe append from 49.2 ns to about 34.
                if (const auto pos = tryAppendSlotImpl(sectorId); pos != kNoSlot) {
                    return fn(pos);
                }

                // Everything else either writes an existing sector in place or shifts others.
                // Publish the epoch first and read the pin state after: whichever side goes
                // second sees the other, so a pin validated in between cannot survive this.
                StructuralEdit edit(*this);
                if (mPinsCounter.canMoveSector(sectorId)) {
                    const auto pos = tryAcquireSlotImpl(sectorId);
                    if (pos != kNoSlot) {
                        return fn(pos);
                    }
                    // Declined: a middle insert with something pinned somewhere else.
                    blockedByOtherSectors = true;
                }
            }

            // Waiting is now the exception rather than the entry condition -- an uncontended
            // insert never announces writer intent at all. It still happens outside the lock:
            // blocking on a pin while holding it deadlocks every pin holder that needs the
            // shared lock again.
            if (blockedByOtherSectors) {
                mPinsCounter.waitUntilQuiescent();
            }
            else {
                mPinsCounter.waitUntilChangeable(sectorId);
            }
        }
    }

private:
    struct Dummy{};
    auto readLock() const requires(!ThreadSafe) { return Dummy{}; }
    auto writeLock() const requires(!ThreadSafe) { return Dummy{}; }

    // ==================== Implementation ====================

    void configureReclamation() {
        if constexpr (!ThreadSafe) {
            // Deferred reclamation exists to keep memory alive under lock-free readers, and
            // this build has none. The dense and sparse arrays already free on reallocation
            // here -- they are plain vectors in the non-thread-safe specialisations -- but
            // chunks went through the retire bin regardless, and nothing ever ticked it, so
            // every chunk ever released stayed resident until the array died.
            setRetireGracePeriod(0);
        }
    }

    void drainAllRetired() {
        mDenseArrays.drainRetired();
        mSparseMap.drainRetired();
        mAllocator.mBin.drainAll();
    }

    size_t tickRetired() {
        size_t freed = 0;
        freed += mDenseArrays.tickRetired();
        freed += mSparseMap.tickRetired();
        freed += mAllocator.mBin.tick();
        return freed;
    }

    void setRetireGracePeriod(uint32_t ticks) {
        mDenseArrays.setGracePeriod(ticks);
        mSparseMap.setGracePeriod(ticks);
        mAllocator.mBin.setGracePeriod(ticks);
    }

    FORCE_INLINE size_t sizeImpl() const { return mSize.load(std::memory_order_relaxed); }

public:
    FORCE_INLINE std::byte* dataAt(uint32_t linearIdx) const {
        return Allocator::atView(mAllocator.loadChunks(), linearIdx);
    }
    FORCE_INLINE std::byte* dataAt(const typename Allocator::ChunksView& chunks, uint32_t linearIdx) const {
        return Allocator::atView(chunks, linearIdx);
    }
    FORCE_INLINE auto loadChunks() const { return mAllocator.loadChunks(); }

private:

    FORCE_INLINE detail::SlotInfo findSlotImpl(SectorId id) const {
        const auto idx = mSparseMap.findIdx(id);
        if (idx == INVALID_IDX) { return detail::INVALID_SLOT; }
        return detail::SlotInfo{ dataAt(idx), idx };
    }

    FORCE_INLINE uint32_t findLinearIdxImpl(SectorId id) const {
        return mSparseMap.findIdx(id);
    }

    FORCE_INLINE bool containsSectorImpl(SectorId id) const {
        return mSparseMap.findIdx(id) != INVALID_IDX;
    }

    FORCE_INLINE std::byte* findSectorDataImpl(SectorId id) const {
        const auto idx = mSparseMap.findIdx(id);
        return idx == INVALID_IDX ? nullptr : dataAt(idx);
    }

    FORCE_INLINE void yieldToWriters() const {
        if (!mPinsCounter.writersWaiting()) [[likely]] { return; }

        if (Threads::PinCounters::threadHoldsNothing()) {
            mPinsCounter.waitForWritersToPass();
            return;
        }
        for (int spins = 0; spins < kWriterYields && mPinsCounter.writersWaiting(); ++spins) {
            std::this_thread::yield();
        }
    }

    [[nodiscard]] PinnedSector pinSectorImpl(SectorId id) const requires(ThreadSafe) {
        yieldToWriters();
        for (;;) {
            const auto epoch = mStructEpoch.load(std::memory_order_seq_cst);
            if (epoch & 1ull) { cpuRelax(); continue; } // a writer is mid-edit

            const auto idx = mSparseMap.findIdx(id);
            if (idx == INVALID_IDX) { return PinnedSector{}; }

            // loadAliveWord rather than isAliveAt: idx can be stale relative to the live dense
            // arrays, and the snapshot is bounds-checked, returning "not alive" out of range.
            PinnedSector pin(mPinsCounter, id, dataAt(idx), loadAliveWord<ThreadSafe>(idx));
            if (mStructEpoch.load(std::memory_order_seq_cst) == epoch) { return pin; }
            // otherwise the pin is dropped by its destructor and we start over
        }
    }

    [[nodiscard]] PinnedSector pinSectorAtImpl(size_t idx) const requires(ThreadSafe) {
        yieldToWriters();
        for (;;) {
            const auto epoch = mStructEpoch.load(std::memory_order_seq_cst);
            if (epoch & 1ull) { cpuRelax(); continue; }

            const auto view = mDenseArrays.loadView();
            if (idx >= view.size) { return PinnedSector{}; }

            PinnedSector pin(mPinsCounter, loadId(view.ids, idx), dataAt(static_cast<uint32_t>(idx)),
                             loadAliveAcquire(view.isAlive, idx));
            if (mStructEpoch.load(std::memory_order_seq_cst) == epoch) { return pin; }
        }
    }

    [[nodiscard]] PinnedSector pinBackSectorImpl() const requires(ThreadSafe) {
        yieldToWriters();
        for (;;) {
            const auto epoch = mStructEpoch.load(std::memory_order_seq_cst);
            if (epoch & 1ull) { cpuRelax(); continue; }

            const auto view = mDenseArrays.loadView();
            if (view.size == 0) { return PinnedSector{}; }
            const size_t idx = view.size - 1;

            PinnedSector pin(mPinsCounter, loadId(view.ids, idx), dataAt(static_cast<uint32_t>(idx)),
                             loadAliveAcquire(view.isAlive, idx));
            if (mStructEpoch.load(std::memory_order_seq_cst) == epoch) { return pin; }
        }
    }

    void shrinkToFitImpl() {
        const auto sz = sizeImpl();
        mAllocator.deallocate(sz, mAllocator.capacity());
        mDenseArrays.shrinkToFit(sz);
    }

    void clearImpl() {
        auto sz = sizeImpl();
        if (sz) {
            if (!getLayout()->isTrivial()) {
                for (size_t i = 0; i < sz; ++i) {
                    Sector::destroySectorData<ThreadSafe>(mAllocator.at(i), mDenseArrays.isAliveAt(i), getLayout());
                }
            }
            // Clear sparse map -- reset only the live slots (== the dense id set).
            // The remaining slots are already INVALID, so an O(sparseCapacity) fill
            // is wasteful when ids are sparse. Must run before mDenseArrays.clear().
            for (size_t i = 0; i < sz; ++i) {
                mSparseMap.set(mDenseArrays.idAt(i), INVALID_IDX);
            }
            mDenseArrays.clear(0);
            mPendingErase.clear();
            mHasPendingErase.store(false, std::memory_order_release);
            mSize.store(0, std::memory_order_relaxed);
            mDefragmentSize.store(0, std::memory_order_relaxed);
            // Note: retired memory is drained on destruction, not here
            // to avoid freeing memory while readers might still hold view pointers
        }
    }

    void reserveImpl(uint32_t newCapacity) {
        if (mAllocator.capacity() < newCapacity) {
            mAllocator.allocate(newCapacity);
            mDenseArrays.reserve(newCapacity);
        }
        if (mSparseMap.capacity() < newCapacity) {
            mSparseMap.resize(newCapacity);
        }
        if constexpr (ThreadSafe) {
            if (newCapacity > 0) {
                mPinsCounter.reserve(newCapacity - 1);
            }
        }
    }

    size_t findInsertPositionImpl(SectorId sectorId, size_t validSize) const {
        if (validSize == 0) return 0;
        if (mDenseArrays.idAt(validSize - 1) < sectorId) return validSize;
        if (mDenseArrays.idAt(0) >= sectorId) return 0;

        // Binary search. The previous form kept `right` as the answer and narrowed until
        // right-left == 1, which never inspected index 0 and so returned 1 instead of 0
        // when sectorId equalled idAt(0) -- landing a resurrected id *after* its own dead
        // dense entry and producing a duplicate. This is a plain lower_bound.
        size_t left = 0, right = validSize;
        while (left < right) {
            size_t mid = left + (right - left) / 2;
            if (mDenseArrays.idAt(mid) < sectorId) left = mid + 1;
            else right = mid;
        }
        return left;
    }

    static constexpr size_t kNoSlot = static_cast<size_t>(INVALID_IDX);

    size_t appendSlotImpl(SectorId sectorId, size_t sz) {
        mDenseArrays.pushBack(sectorId, 0);
        mSparseMap.set(sectorId, static_cast<uint32_t>(sz));
        mSize.store(sz + 1, std::memory_order_relaxed);
        mDenseArrays.storeView(sz + 1);
        return sz;
    }

    size_t tryAppendSlotImpl(SectorId sectorId) {
        const auto sz = sizeImpl();
        if (sz != 0 && sectorId <= mDenseArrays.idAt(sz - 1)) {
            return kNoSlot;
        }
        if (sectorId < mSparseMap.capacity() && mSparseMap.findIdx(sectorId) != INVALID_IDX) {
            return kNoSlot;
        }

        if (sectorId >= mSparseMap.capacity()) [[unlikely]] {
            mSparseMap.resize(static_cast<size_t>(sectorId) + 1);
        }
        mAllocator.allocate(sz + 1);
        return appendSlotImpl(sectorId, sz);
    }

    size_t acquireSlotImpl(SectorId sectorId) {
        const auto pos = tryAcquireSlotImpl(sectorId);
        // tryAcquireSlotImpl only declines when a middle insert would shift sectors while
        // pins are live. Reaching that here means the caller took the write lock without
        // establishing quiescence first -- see exclusiveForInsert / exclusiveWhenQuiescent.
        assert(pos != kNoSlot && "insert would relocate pinned sectors; caller must establish quiescence");
        return pos;
    }

    size_t tryAcquireSlotImpl(SectorId sectorId) {
        // Expand sparse map if needed
        if (sectorId >= mSparseMap.capacity()) [[unlikely]] {
            mSparseMap.resize(static_cast<size_t>(sectorId) + 1);
        }

        // Check if already exists (thread-safe read)
        if (const auto existing = mSparseMap.findIdx(sectorId); existing != INVALID_IDX) [[unlikely]] {
            return existing;
        }

        // Ensure chunk memory available
        auto sz = sizeImpl();
        mAllocator.allocate(sz + 1);

        const size_t pos = sz;
        const bool isAppend = (pos == 0) || (sectorId > mDenseArrays.idAt(pos - 1));

        if (isAppend) [[likely]] {
            return appendSlotImpl(sectorId, sz);
        }

        const size_t insertPos = findInsertPositionImpl(sectorId, pos);

        // A deferred erase (erase(..., defragment=false) / eraseAsync) clears the sparse
        // slot but leaves the dense entry in place. Without this check the id would be
        // inserted a second time: the dense array would hold a duplicate (breaking the
        // sorted-ids invariant every binary search relies on) and the next defragmentImpl
        // would clear the sparse entry on behalf of the *dead* twin, orphaning the live
        // sector -- present when iterating, absent from every lookup.
        if (insertPos < pos && mDenseArrays.idAt(insertPos) == sectorId) [[unlikely]] {
            if (!Sector::isSectorAlive(mDenseArrays.isAliveAt(insertPos))) {
                // The slot was counted as fragmentation when it died; it is live again.
                auto dead = mDefragmentSize.load(std::memory_order_relaxed);
                if (dead) { mDefragmentSize.store(dead - 1, std::memory_order_relaxed); }
            }
            mSparseMap.set(sectorId, static_cast<uint32_t>(insertPos));
            return insertPos;
        }

        // Genuine middle insert: shiftRightImpl relocates every sector from insertPos on,
        // so nothing at all may be pinned. Decline instead of waiting here -- we hold the
        // write lock, and blocking on pins under it deadlocks any pin holder that needs
        // the shared lock again.
        if constexpr (ThreadSafe) {
            if (mPinsCounter.hasAnyPins()) {
                return kNoSlot;
            }
        }

        mDenseArrays.resize(sz + 1, sz);
        mSize.store(sz + 1, std::memory_order_relaxed);

        if (insertPos != sz) {
            shiftRightImpl(insertPos, 1);
        }

        mDenseArrays.setIdAt(insertPos, sectorId);
        mDenseArrays.setAliveAt(insertPos, 0);
        // Store data pointer + linear index (linearIdx written first, then data atomically)
        mSparseMap.set(sectorId, static_cast<uint32_t>(insertPos));

        mDenseArrays.storeView(sizeImpl());

        return insertPos;
    }

    void shiftRightImpl(size_t from, size_t count) {
        const size_t oldSize = sizeImpl() - count;
        const size_t tail = oldSize > from ? (oldSize - from) : 0;
        if (!tail) return;

        // Move component data (iterate backwards to avoid overwriting)
        if (getLayout()->isTrivial()) {
        mAllocator.moveSectorsDataTrivial(from + count, from, tail);
        } else {
            for (size_t i = tail; i > 0; --i) {
                size_t srcIdx = from + i - 1;
                size_t dstIdx = from + count + i - 1;
                Sector::moveSectorData(
                    mAllocator.at(srcIdx), mDenseArrays.isAliveAt(srcIdx),
                    mAllocator.at(dstIdx), mDenseArrays.isAliveAt(dstIdx),
                    getLayout());
            }
        }

        // Shift metadata and update sparse map with new pointers
        for (size_t i = oldSize + count - 1; i >= from + count; --i) {
            mDenseArrays.setIdAt(i, mDenseArrays.idAt(i - count));
            if (getLayout()->isTrivial()) {
                mDenseArrays.setAliveAt(i, mDenseArrays.isAliveAt(i - count));
            }
            mSparseMap.set(mDenseArrays.idAt(i), static_cast<uint32_t>(i));
        }
    }

    void shiftLeftImpl(size_t from, size_t count) {
        if (from < count) return;
        auto sz = sizeImpl();
        const size_t tail = from > sz ? 0 : sz - from;
        if (!tail) return;

        // Move component data (iterate forwards)
        if (getLayout()->isTrivial()) {
        mAllocator.moveSectorsDataTrivial(from - count, from, tail);
        } else {
            for (size_t i = 0; i < tail; ++i) {
                size_t srcIdx = from + i;
                size_t dstIdx = from - count + i;
                Sector::moveSectorData(
                    mAllocator.at(srcIdx), mDenseArrays.isAliveAt(srcIdx),
                    mAllocator.at(dstIdx), mDenseArrays.isAliveAt(dstIdx),
                    getLayout());
            }
        }

        // Shift metadata and update sparse map with new pointers
        for (size_t i = from - count; i < from - count + tail; ++i) {
            mDenseArrays.setIdAt(i, mDenseArrays.idAt(i + count));
            if (getLayout()->isTrivial()) {
                mDenseArrays.setAliveAt(i, mDenseArrays.isAliveAt(i + count));
            }
            mSparseMap.set(mDenseArrays.idAt(i), static_cast<uint32_t>(i));
        }
    }

    template<typename T>
    std::remove_reference_t<T>* writeMemberImpl(size_t pos, T&& data) {
        using U = std::remove_cvref_t<T>;

        std::byte* slotData = mAllocator.at(pos);
        const auto& layout = getLayoutData<U>();
        if constexpr (std::is_lvalue_reference_v<T>) {
            return Sector::copyMember<U, ThreadSafe>(data, slotData, mDenseArrays.isAliveAt(pos), layout);
        } else {
            return Sector::moveMember<U, ThreadSafe>(std::forward<T>(data), slotData, mDenseArrays.isAliveAt(pos), layout);
        }
    }

    template<typename T, class... Args>
    T* emplaceMemberImpl(size_t pos, Args&&... args) {
        return Sector::emplaceMember<T, ThreadSafe>(mAllocator.at(pos), mDenseArrays.isAliveAt(pos), getLayoutData<T>(), std::forward<Args>(args)...);
    }

    template<typename T>
    std::remove_reference_t<T>* insertImpl(SectorId sectorId, T&& data) {
        return writeMemberImpl<T>(acquireSlotImpl(sectorId), std::forward<T>(data));
    }

    template<typename T, class... Args>
    T* emplaceImpl(SectorId sectorId, Args&&... args) {
        return emplaceMemberImpl<T>(acquireSlotImpl(sectorId), std::forward<Args>(args)...);
    }

    template<typename C, typename It>
    void insertBulkImpl(It first, It last) {
        if (first == last) return;

        const auto& layout = getLayoutData<C>();

        // One pass to size the reservation and to find out whether the cheap path applies.
        size_t count = 0;
        SectorId maxId = 0;
        bool ascending = true;
        for (auto it = first; it != last; ++it) {
            if (count && it->first <= maxId) { ascending = false; }
            if (it->first > maxId) { maxId = it->first; }
            ++count;
        }

        const size_t base = sizeImpl();
        mAllocator.allocate(base + count);          // reserve chunks once
        mDenseArrays.reserve(base + count);         // reserve dense arrays once
        if (static_cast<size_t>(maxId) >= mSparseMap.capacity()) {
            mSparseMap.resize(static_cast<size_t>(maxId) + 1);
        }

        // Pure append: strictly ascending and entirely above what is already stored. Nothing
        // moves, so it stays a single forward pass with no sort and no merge.
        if (ascending && (base == 0 || mDenseArrays.idAt(base - 1) < first->first)) [[likely]] {
            size_t pos = base;
            for (auto it = first; it != last; ++it, ++pos) {
                const SectorId id = it->first;
                mDenseArrays.pushBack(id, 0);
                std::byte* slot = mAllocator.at(pos);
                Sector::emplaceMember<C, ThreadSafe>(slot, mDenseArrays.isAliveAt(pos), layout, it->second);
                mSparseMap.set(id, static_cast<uint32_t>(pos));
            }
            mSize.store(base + count, std::memory_order_relaxed);
            mDenseArrays.storeView(base + count);   // publish the whole batch at once
            return;
        }

        mergeBulkImpl<C>(first, last, count, base, layout);
    }

    template<typename C, typename It, typename Layout>
    void mergeBulkImpl(It first, It last, size_t count, size_t base, const Layout& layout) {
        // (id, source) so the batch can be ordered without disturbing the caller's sequence.
        // Forward iterators suffice, which is all the append path needed either.
        std::vector<std::pair<SectorId, It>> batch;
        batch.reserve(count);
        for (auto it = first; it != last; ++it) { batch.emplace_back(it->first, it); }

        // Stable, so that when the caller names an id twice the last value wins -- the same
        // answer a loop of addComponent() would have given. Keeping the last of each run
        // rather than the first is what makes that true.
        std::stable_sort(batch.begin(), batch.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
        {
            size_t out = 0;
            for (size_t i = 0; i < batch.size(); ++i) {
                if (i + 1 < batch.size() && batch[i + 1].first == batch[i].first) { continue; }
                batch[out++] = batch[i];
            }
            batch.resize(out);
        }

        // Anything that already has a live slot, or a dead dense entry to resurrect, is
        // written in place and drops out of the merge: it does not change the array's shape.
        size_t toInsert = 0;
        for (auto& entry : batch) {
            const size_t slot = resolveExistingSlot(entry.first, base);
            if (slot != kNoSlot) {
                emplaceMemberImpl<C>(slot, entry.second->second);
                mSparseMap.set(entry.first, static_cast<uint32_t>(slot));
                entry.second = last;                  // mark as handled
            }
            else {
                ++toInsert;
            }
        }
        if (toInsert == 0) { return; }

        const size_t newSize = base + toInsert;
        mDenseArrays.resize(newSize, base);            // keep readers on the old size meanwhile
        mSize.store(newSize, std::memory_order_relaxed);

        // Merge back to front. The write cursor leads the read cursor by exactly the number of
        // batch entries still to be placed, so a sector is never overwritten before it has
        // been read and no scratch copy of the array is needed.
        size_t w = newSize;
        size_t r = base;
        for (size_t bi = batch.size(); bi > 0; --bi) {
            auto& entry = batch[bi - 1];
            if (entry.second == last) { continue; }    // written in place above
            const SectorId id = entry.first;

            while (r > 0 && mDenseArrays.idAt(r - 1) > id) {
                --r;
                --w;
                relocateSector(r, w);
            }

            --w;
            mDenseArrays.setIdAt(w, id);
            mDenseArrays.setAliveAt(w, 0);
            Sector::emplaceMember<C, ThreadSafe>(
                mAllocator.at(w), mDenseArrays.isAliveAt(w), layout, entry.second->second);
            mSparseMap.set(id, static_cast<uint32_t>(w));
        }

        mDenseArrays.storeView(newSize);               // publish the whole batch at once
    }

    size_t resolveExistingSlot(SectorId id, size_t validSize) {
        if (const auto live = mSparseMap.findIdx(id); live != INVALID_IDX) { return live; }

        // A deferred erase clears the sparse slot but leaves the dense entry behind. Inserting
        // the id again would append a second entry carrying the same id, breaking the sorted
        // invariant every binary search depends on; reuse the dead one instead.
        const size_t pos = findInsertPositionImpl(id, validSize);
        if (pos < validSize && mDenseArrays.idAt(pos) == id) {
            if (!Sector::isSectorAlive(mDenseArrays.isAliveAt(pos))) {
                auto dead = mDefragmentSize.load(std::memory_order_relaxed);
                if (dead) { mDefragmentSize.store(dead - 1, std::memory_order_relaxed); }
            }
            return pos;
        }
        return kNoSlot;
    }

    void relocateSector(size_t from, size_t to) {
        if (getLayout()->isTrivial()) {
            mAllocator.moveSectorsDataTrivial(to, from, 1);
            mDenseArrays.setAliveAt(to, mDenseArrays.isAliveAt(from));
        }
        else {
            Sector::moveSectorData(
                mAllocator.at(from), mDenseArrays.isAliveAt(from),
                mAllocator.at(to), mDenseArrays.isAliveAt(to),
                getLayout());
        }
        mDenseArrays.setIdAt(to, mDenseArrays.idAt(from));
        mSparseMap.set(mDenseArrays.idAt(to), static_cast<uint32_t>(to));
    }

    void eraseRangeImpl(size_t beginIdx, size_t count, bool defragment) {
        count = std::min(count, sizeImpl() - beginIdx);
        for (size_t i = beginIdx; i < beginIdx + count; ++i) {
            auto id = mDenseArrays.idAt(i);
            if (id < mSparseMap.capacity()) {
                mSparseMap.set(id, INVALID_IDX);
            }
            Sector::destroySectorData<ThreadSafe>(mAllocator.at(i), mDenseArrays.isAliveAt(i), getLayout());
        }

        if (defragment) {
            shiftLeftImpl(beginIdx + count, count);
            auto newSz = sizeImpl() - count;
            mSize.store(newSz, std::memory_order_relaxed);
            mDenseArrays.resize(newSz, newSz);
            mDenseArrays.storeView(newSz);
        } else {
            incDefragmentSize(static_cast<uint32_t>(count));
        }
    }

    void eraseAsyncImpl(SectorId id) requires(ThreadSafe) {
        // Early exit for an id this array does not hold. The lookup reads the sparse map's
        // published snapshot and takes no lock -- the same read every other path here does
        // lock-free -- so the shared lock this used to take bought nothing. The answer is a
        // snapshot either way, which is why the real work below re-reads under the unique
        // lock rather than trusting it.
        if (findLinearIdxImpl(id) == INVALID_IDX) { return; }

        if (!mPinsCounter.isPinned(id)) {
            UNIQUE_LOCK();
            StructuralEdit edit(*this);
            // Re-read under unique lock to ensure consistency
            auto idx = findLinearIdxImpl(id);
            if (idx == INVALID_IDX) return;
            
            if (mPinsCounter.canMoveSector(id)) {
                mSparseMap.set(id, INVALID_IDX);
                Sector::destroySectorData<ThreadSafe>(mAllocator.at(idx), mDenseArrays.isAliveAt(idx), getLayout());
                incDefragmentSize();
            } else {
                mPendingErase.push_back(id);
                mHasPendingErase.store(true, std::memory_order_release);
            }
        } else {
            UNIQUE_LOCK();
            mPendingErase.push_back(id);
            mHasPendingErase.store(true, std::memory_order_release);
        }
    }

    bool processPendingErasesImpl() requires(ThreadSafe) {
        if (!mPendingErase.empty()) {
            auto tmp = std::move(mPendingErase);
            mPendingErase.clear(); // moved-from vectors are only "valid but unspecified"
            mHasPendingErase.store(false, std::memory_order_release);
            std::sort(tmp.begin(), tmp.end());
            tmp.erase(std::unique(tmp.begin(), tmp.end()), tmp.end());

            for (auto id : tmp) {
                auto idx = findLinearIdxImpl(id);
                if (idx == INVALID_IDX) continue;

                if (mPinsCounter.canMoveSector(id)) {
                    mSparseMap.set(id, INVALID_IDX);
                    Sector::destroySectorData<ThreadSafe>(mAllocator.at(idx), mDenseArrays.isAliveAt(idx), getLayout());
                    incDefragmentSize();
                } else {
                    mPendingErase.push_back(id);
                    mHasPendingErase.store(true, std::memory_order_release);
                }
            }
        }

        return needDefragment<false>();
    }

    void defragmentImpl() {
        if constexpr (ThreadSafe) {
            if (mPinsCounter.hasAnyPins()) return;
        }

        size_t read = 0, write = 0, deleted = 0;
        const size_t n = sizeImpl();
        const bool isTrivial = getLayout()->isTrivial();

        while (read < n) {
            // Skip dead slots
            while (read < n && !Sector::isSectorAlive(mDenseArrays.isAliveAt(read))) {
                mSparseMap.set(mDenseArrays.idAt(read), INVALID_IDX);
                ++read; ++deleted;
            }
            if (read >= n) break;

            // Find run of alive slots
            size_t runBeg = read;
            while (read < n && Sector::isSectorAlive(mDenseArrays.isAliveAt(read))) {
                ++read;
            }
            const size_t runLen = read - runBeg;

            // Move run to write position
            if (write != runBeg) {
                if (isTrivial) {
                    // Trivial types: fast memmove
                mAllocator.moveSectorsDataTrivial(write, runBeg, runLen);
                for (size_t i = 0; i < runLen; ++i) {
                        mDenseArrays.setIdAt(write + i, mDenseArrays.idAt(runBeg + i));
                        mDenseArrays.setAliveAt(write + i, mDenseArrays.isAliveAt(runBeg + i));
                        mSparseMap.set(mDenseArrays.idAt(write + i), static_cast<uint32_t>(write + i));
                    }
                } else {
                    // Non-trivial types: proper move semantics for each sector
                    // moveSectorData handles isAlive state, we just need to update id and sparse map
                    for (size_t i = 0; i < runLen; ++i) {
                        Sector::moveSectorData(
                            mAllocator.at(runBeg + i), mDenseArrays.isAliveAt(runBeg + i),
                            mAllocator.at(write + i), mDenseArrays.isAliveAt(write + i),
                            getLayout());
                        mDenseArrays.setIdAt(write + i, mDenseArrays.idAt(runBeg + i));
                        mSparseMap.set(mDenseArrays.idAt(write + i), static_cast<uint32_t>(write + i));
                    }
                }
            }
            write += runLen;
        }

        auto newSz = n - deleted;
        mSize.store(newSz, std::memory_order_relaxed);
        // Compaction drops every sector with no live component, so by construction there
        // are now zero dead slots. Subtracting `deleted` instead would let the counter
        // drift upward whenever a dead slot was resurrected (destroyComponent + re-add)
        // and never compacted, leaving needDefragment() permanently true.
        mDefragmentSize.store(0, std::memory_order_relaxed);
        mDenseArrays.resize(newSz, newSz);
        mDenseArrays.storeView(newSz);
        // Note: do NOT shrinkToFit() here. Compaction reclaims dead slots, but
        // keeping capacity avoids realloc churn on add/remove workloads that
        // defragment every frame. Callers that want memory returned to the OS
        // call shrinkToFit() explicitly.
    }

    template<bool T, typename Alloc>
    void copy(const SectorsArray<T, Alloc>& other) {
        if constexpr (ThreadSafe) {
            exclusiveWhenQuiescent([&] {
                auto otherLock = other.readLock();
                copyImpl(other);
            });
        } else if constexpr (T) {
            auto lock = writeLock();
            auto otherLock = other.readLock();
            copyImpl(other);
        } else {
            copyImpl(other);
        }
    }

    template<bool T, typename Alloc>
    void copyImpl(const SectorsArray<T, Alloc>& other) {
        // Checked before anything is destroyed: on a mismatch the destination is left exactly
        // as it was, which is the only safe outcome. Copying on would reinterpret the source
        // bytes through a layout of a different sector size.
        if (!mAllocator.adoptOrMatchLayout(other.mAllocator)) { return; }

        clearImpl();
        shrinkToFitImpl();

        auto otherSz = other.sizeImpl();
        mSize.store(otherSz, std::memory_order_relaxed);
        
        // Copy layout metadata first - required before we can check isTrivial()
        mAllocator.copyCommonData(other.mAllocator);
        
        // For non-trivial types, we must NOT use ChunksAllocator's memcpy-based copy!
        // Instead: allocate chunks, then properly copy-construct each member via layout.
        if (getLayout()->isTrivial()) {
            mAllocator = other.mAllocator;
        } else {
            mAllocator.allocate(otherSz);
        }
        
        mDenseArrays.resize(otherSz, otherSz);
        for (size_t i = 0; i < otherSz; ++i) {
            mDenseArrays.setIdAt(i, other.mDenseArrays.idAt(i));
            mDenseArrays.setAliveAt(i, other.mDenseArrays.isAliveAt(i));
        }
        
        if (!getLayout()->isTrivial()) {
            for (size_t i = 0; i < otherSz; ++i) {
                uint32_t srcIsAlive = other.mDenseArrays.isAliveAt(i);
                uint32_t dstIsAlive = 0;
                Sector::copySectorData(
                    other.mAllocator.at(i), srcIsAlive,
                    mAllocator.at(i), dstIsAlive,
                    getLayout());
                mDenseArrays.setAliveAt(i, dstIsAlive);
            }
        }
        // Published once, and only here: a sector announced alive before its members are
        // constructed is one a lock-free reader can pick up and read as raw zeroes.
        mDenseArrays.storeView(otherSz);
        
        mSparseMap.resize(other.mSparseMap.capacity());
        for (size_t i = 0; i < otherSz; ++i) {
            mSparseMap.set(mDenseArrays.idAt(i), static_cast<uint32_t>(i));
        }

        mDefragmentSize.store(other.mDefragmentSize.load(std::memory_order_relaxed), std::memory_order_relaxed);
        storeDefragThreshold(other.loadDefragThreshold());
    }

    template<bool T, typename Alloc>
    void move(SectorsArray<T, Alloc>&& other) {
        // Same rule as everywhere else: wait for pins outside the locks, then take them and
        // re-verify, so no thread ever blocks on a pin while holding a write lock.
        if constexpr (ThreadSafe && T) {
            // Both sides synchronized: std::lock orders the two mutexes, so a concurrent
            // move in the opposite direction cannot deadlock against this one.
            for (;;) {
                mPinsCounter.waitUntilQuiescent();
                other.mPinsCounter.waitUntilQuiescent();

                auto lock = std::unique_lock(mtx, std::defer_lock);
                auto otherLock = std::unique_lock(other.mtx, std::defer_lock);
                std::lock(lock, otherLock);

                if (mPinsCounter.hasAnyPins() || other.mPinsCounter.hasAnyPins()) {
                    continue;
                }
                moveImpl(std::move(other));
                return;
            }
        } else if constexpr (ThreadSafe || T) {
            // Exactly one side is synchronized, so only one real mutex is involved and
            // writeLock() on the other side is the no-op Dummy.
            for (;;) {
                if constexpr (ThreadSafe) { mPinsCounter.waitUntilQuiescent(); }
                if constexpr (T)          { other.mPinsCounter.waitUntilQuiescent(); }

                auto lock = writeLock();
                auto otherLock = other.writeLock();

                if constexpr (ThreadSafe) { if (mPinsCounter.hasAnyPins()) continue; }
                if constexpr (T)          { if (other.mPinsCounter.hasAnyPins()) continue; }

                moveImpl(std::move(other));
                return;
            }
        } else {
            moveImpl(std::move(other));
        }
    }

    template<bool T, typename Alloc>
    void moveImpl(SectorsArray<T, Alloc>&& other) {
        if (!mAllocator.adoptOrMatchLayout(other.mAllocator)) { return; }

        clearImpl();
        shrinkToFitImpl();

        auto otherSz = other.sizeImpl();
        mSize.store(otherSz, std::memory_order_relaxed);
        if constexpr (Allocator::mChunkCapacity == Alloc::mChunkCapacity) {
            // Same chunk geometry, so the chunks simply change owner and no sector moves.
            mAllocator = std::move(other.mAllocator);
            
            mDenseArrays.resize(otherSz, otherSz);
            for (size_t i = 0; i < otherSz; ++i) {
                mDenseArrays.setIdAt(i, other.mDenseArrays.idAt(i));
                mDenseArrays.setAliveAt(i, other.mDenseArrays.isAliveAt(i));
            }
        }
        else {
            // Different capacities put every sector at a different address, so each one has
            // to be relocated rather than adopted. The allocator's own cross-capacity path
            // does that with memcpy, which is a lie for any member that owns a resource or
            // points into itself -- and it cannot do better, because the liveness words that
            // say which members exist are held here, not in there. So it happens at this
            // level, where they are.
            mAllocator.copyCommonData(other.mAllocator);
            mAllocator.allocate(otherSz);
            
            mDenseArrays.resize(otherSz, otherSz);
            const bool trivial = getLayout()->isTrivial();
            for (size_t i = 0; i < otherSz; ++i) {
                mDenseArrays.setIdAt(i, other.mDenseArrays.idAt(i));
                if (trivial) {
                    std::memcpy(mAllocator.at(i), other.mAllocator.at(i), mAllocator.mSectorSize);
                    mDenseArrays.setAliveAt(i, other.mDenseArrays.isAliveAt(i));
                }
                else {
                    // Destination liveness starts at zero: moveSectorData destroys whatever the
                    // target claims to hold before moving into it, and the target holds nothing.
                    uint32_t toIsAlive = 0;
                    Sector::moveSectorData(
                        other.mAllocator.at(i), other.mDenseArrays.isAliveAt(i),
                        mAllocator.at(i), toIsAlive,
                        getLayout());
                    mDenseArrays.setAliveAt(i, toIsAlive);
                }
            }
            other.mAllocator.deallocate(0, other.mAllocator.capacity());
        }
        mDenseArrays.storeView(otherSz);
        
        mSparseMap.resize(other.mSparseMap.capacity());
        for (size_t i = 0; i < otherSz; ++i) {
            mSparseMap.set(mDenseArrays.idAt(i), static_cast<uint32_t>(i));
        }

        mDefragmentSize.store(other.mDefragmentSize.load(std::memory_order_relaxed), std::memory_order_relaxed);
        storeDefragThreshold(other.loadDefragThreshold());
        
        // The source must stop advertising sectors it no longer owns: its sparse slots still
        // point into the chunks this array just took over, so containsSector()/findSectorData()
        // would hand out live pointers to another array memory.
        for (size_t i = 0; i < otherSz; ++i) {
            other.mSparseMap.set(other.mDenseArrays.idAt(i), INVALID_IDX);
        }
        other.mSize.store(0, std::memory_order_relaxed);
        other.mDenseArrays.clear(0);
        other.mPendingErase.clear();
        other.mHasPendingErase.store(false, std::memory_order_release);
        other.mHasPendingClear.store(false, std::memory_order_release);
        other.mDefragmentSize.store(0, std::memory_order_relaxed);
        other.shrinkToFitImpl();
    }

    template<bool UseLock>
    static consteval void enforceTSMode() {
        if constexpr (!ThreadSafe && UseLock) {
            static_assert(!UseLock, "Invalid use: TS=true on SectorsArray<ThreadSafe=false>");
        }
    }

private:
    Allocator mAllocator;

    // SoA data: parallel arrays indexed by linearIdx (with atomic view for thread-safe iteration)
    detail::DenseArrays<ThreadSafe> mDenseArrays;

    // Sparse map: [sectorId] -> linearIdx
    detail::SparseMap<ThreadSafe> mSparseMap;

    FORCE_INLINE float loadDefragThreshold() const noexcept {
        if constexpr (ThreadSafe) {
            return std::atomic_ref<float>(const_cast<float&>(mDefragThreshold))
                .load(std::memory_order_relaxed);
        }
        else { return mDefragThreshold; }
    }

    FORCE_INLINE void storeDefragThreshold(float value) noexcept {
        if constexpr (ThreadSafe) {
            std::atomic_ref<float>(mDefragThreshold).store(value, std::memory_order_relaxed);
        }
        else { mDefragThreshold = value; }
    }

    static_assert(types::isLockFreeAtomic<size_t>,   "mSize must be lock-free");
    static_assert(types::isLockFreeAtomic<uint32_t>, "mDefragmentSize must be lock-free");

    // Own cache lines: the mutex word and the pin aggregates are hammered by every reader,
    // while mSize is written by every structural mutation. Sharing a line between them
    // turns unrelated operations into false-sharing traffic.
    //
    // Only in the thread-safe build. With no threads there is nothing to false-share, and
    // unconditional padding cost 168 bytes per array for nothing.
    static constexpr size_t kHotAlign = ThreadSafe ? 64 : 1;
    static constexpr size_t kSizeAlign = ThreadSafe ? 64 : alignof(std::atomic<size_t>);

    alignas(kHotAlign) mutable std::conditional_t<ThreadSafe, std::shared_mutex, Dummy> mtx;
    alignas(kHotAlign) mutable std::conditional_t<ThreadSafe, Threads::PinCounters, Dummy> mPinsCounter;

    std::vector<SectorId> mPendingErase;
    std::atomic<bool> mHasPendingErase{ false };

    std::atomic<bool> mHasPendingClear{ false };

    static constexpr int kWriterYields = 64;
    static constexpr size_t kEpochAlign = ThreadSafe ? 64 : alignof(std::atomic<uint64_t>);
    alignas(kEpochAlign) mutable std::atomic<uint64_t> mStructEpoch{ 0 };
    static_assert(types::isLockFreeAtomic<uint64_t>, "the structural epoch must be lock-free");

    alignas(kSizeAlign) std::atomic<size_t> mSize{0};
    std::atomic<uint32_t> mDefragmentSize{0};
    float mDefragThreshold = 0.2f;
};

#undef SHARED_LOCK
#undef UNIQUE_LOCK
#undef TS_GUARD
#undef TS_GUARD_S
#undef ITERATOR_COMMON_USING

} // namespace ecss::Memory
```


