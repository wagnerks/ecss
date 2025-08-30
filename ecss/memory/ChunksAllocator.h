#pragma once

#include <vector>

#include <ecss/memory/Sector.h>
#include <ecss/memory/RetireAllocator.h>

namespace ecss::Memory {

    template<typename T>
    static void* toAdr(T* ptr) {
        if constexpr (std::is_const_v<T>) {
            return const_cast<void*>(reinterpret_cast<const void*>(ptr));
        }
        else {
            return reinterpret_cast<void*>(ptr);
        }
    }

	template<typename T1, typename T2>
	static bool isSameAdr(T1* ptr1, T2* ptr2) {
        return toAdr(ptr1) == toAdr(ptr2);
    }

    static constexpr uint32_t nextPowerOfTwo(uint32_t x) {
        if (x == 0) return 1;
        --x;
        x |= x >> 1;
        x |= x >> 2;
        x |= x >> 4;
        x |= x >> 8;
        x |= x >> 16;
        return ++x;
    }

 /**
 * @brief Chunked sector allocator with deferred memory reclamation.
 *
 * ChunksAllocator is responsible for managing memory in fixed-size chunks of
 * `Sector` objects. It provides random access to sectors by index or ID and
 * supports acquiring, freeing, shifting, and defragmenting sectors.
 *
 * - Memory model:
 *   * Sectors are stored in contiguous "chunks", each chunk contains
 *     `ChunkCapacity` sectors.
 *   * Chunks grow dynamically as needed. When more capacity is required,
 *     new chunks are allocated and appended.
 *   * A secondary map (`mSectorsMap`) provides direct lookup by SectorId.
 *
 * - Iterator:
 *   * A custom forward iterator is provided to traverse across chunks as if
 *     they form one linear array of `Sector*`.
 *
 * - Memory safety:
 *   * Uses RetireAllocator internally for both `mChunks` and `mSectorsMap`.
 *     This avoids immediate deallocation of old buffers when std::vector
 *     reallocates, preventing use-after-free in concurrent readers.
 *   * Old buffers are collected into a RetireBin and freed only when
 *     `emptyVectorsBin()` is called.
 *
 * - Operations:
 *   * `acquireSector(SectorId)` — ensures a sector exists for the given ID,
 *     allocating or constructing if needed.
 *   * `freeSector(SectorId, shift)` — destroys and removes a sector.
 *   * `erase()` / `defragment()` — remove sectors and optionally compact
 *     storage.
 *   * `reserve()` / `shrinkToFit()` — grow or release memory in chunk units.
 *   * `calcSectorIndex()` — compute linear index from a SectorId or pointer,
 *     with fast paths for first/last chunks.
 *
 * Usage:
 *   This allocator is designed for high-performance ECS-style storage,
 *   where sectors are densely packed and frequently accessed by ID.
 *   It avoids costly per-element allocations by working with whole chunks,
 *   and reduces concurrency hazards by deferring vector buffer frees.
 *
 * @tparam ChunkCapacity Number of sectors per chunk (rounded to power-of-two).
 */
    template<uint32_t ChunkCapacity = 8192>
    struct ChunksAllocator {
        template<uint32_t>
        friend struct ChunksAllocator;

        struct Cursor {
            Cursor() = default;

            Cursor(const ChunksAllocator* allocator, size_t index = 0) : shift(allocator->mSectorSize), alloc(allocator) {
                chunkIdx = ChunksAllocator<ChunkCapacity>::calcChunkIndex(index);
                inChunkIdx = index - chunkIdx * ChunksAllocator<ChunkCapacity>::mChunkCapacity;

                cur = reinterpret_cast<Sector*>(alloc->mChunks.size() > chunkIdx ? static_cast<std::byte*>(alloc->mChunks[chunkIdx]) + inChunkIdx * shift : nullptr);
            }

            Sector* step() {
                inChunkIdx++;
                if (inChunkIdx == ChunksAllocator<ChunkCapacity>::mChunkCapacity) {
                    inChunkIdx = 0;
                    chunkIdx++;
                    return cur = reinterpret_cast<Sector*>(alloc->mChunks.size() > chunkIdx ? static_cast<std::byte*>(alloc->mChunks[chunkIdx]) : nullptr), cur;
                }

                return cur = reinterpret_cast<Sector*>(reinterpret_cast<std::byte*>(cur) + shift), cur;
            }

            Cursor& operator++() { return step(), *this; }

            Cursor& operator+(size_t value) {
                if (value == 1) {
                    return ++(*this);
                }

                auto newIndex = linearIndex() + value;
                chunkIdx = ChunksAllocator<ChunkCapacity>::calcChunkIndex(newIndex);
                inChunkIdx = newIndex - chunkIdx * ChunksAllocator<ChunkCapacity>::mChunkCapacity;

                cur = reinterpret_cast<Sector*>(alloc->mChunks.size() > chunkIdx ? static_cast<std::byte*>(alloc->mChunks[chunkIdx]) + inChunkIdx * shift : nullptr);

                return *this;
            }

            Sector* operator*() const { return cur; }
            Sector* operator->() const { return cur; }

            inline bool operator==(const Cursor& other) const { return inChunkIdx == other.inChunkIdx && chunkIdx == other.chunkIdx; }
            inline bool operator!=(const Cursor& other) const { return !(*this == other); }

        	operator bool() const { return cur; }

            size_t linearIndex() const { return chunkIdx * ChunksAllocator<ChunkCapacity>::mChunkCapacity + inChunkIdx; }

        private:
            size_t shift = 0;

            const ChunksAllocator* alloc = nullptr;
            Sector* cur = nullptr;
            size_t chunkIdx = 0;
            size_t inChunkIdx = 0;            
        };

        Cursor getCursor(size_t index = 0) const { return { this, index }; }

    public:
        // copy
        template<uint32_t OC>
        ChunksAllocator(const ChunksAllocator<OC>& other) { *this = other; }
        ChunksAllocator(const ChunksAllocator& other) { *this = other; }

        template<uint32_t OC>
        ChunksAllocator& operator=(const ChunksAllocator<OC>& other) { if (isSameAdr(this, &other)) { copy(other); } return *this; }
        ChunksAllocator& operator=(const ChunksAllocator& other) { if (this != &other) { copy(other); } return *this; }

        // move
        template<uint32_t OC>
        ChunksAllocator(ChunksAllocator<OC>&& other) noexcept { *this = std::move(other); }
        ChunksAllocator(ChunksAllocator&& other) noexcept { *this = std::move(other); }

        template<uint32_t OC>
        ChunksAllocator& operator=(ChunksAllocator<OC>&& other) noexcept { if (isSameAdr(this, &other)) { move(std::move(other)); } return *this; }
        ChunksAllocator& operator=(ChunksAllocator&& other) noexcept { if (this != &other) { move(std::move(other)); } return *this; }

    public:
        ChunksAllocator() = default;
        ~ChunksAllocator() {
        	deallocate(0, capacity());
            mBin.drainAll();
        }

        SectorLayoutMeta* getSectorLayout() const { return mSectorLayout; }

        void init(SectorLayoutMeta* layoutMeta) { assert(layoutMeta);
            mSectorLayout = layoutMeta;

            mSectorSize = mSectorLayout->getTotalSize();
            mIsSectorTrivial = mSectorLayout->isTrivial();
        }

    public:
        Sector* operator[](size_t index) const { return reinterpret_cast<Sector*>(static_cast<std::byte*>(mChunks[calcChunkIndex(index)]) + calcInChunkShift(index)); }
        Sector* at(size_t index)         const { return reinterpret_cast<Sector*>(static_cast<std::byte*>(mChunks[calcChunkIndex(index)]) + calcInChunkShift(index)); }

        void moveSectors(size_t dst, size_t src, size_t n) const {
            if (!n || dst == src) return;

            if (dst < src) {
                // forward
                while (n) {
                    const size_t srcRoom = mChunkCapacity - (src & (mChunkCapacity - 1));
                    const size_t dstRoom = mChunkCapacity - (dst & (mChunkCapacity - 1));
                    const size_t run = std::min({ n, srcRoom, dstRoom });
                    if (mIsSectorTrivial) {
                        std::memmove(at(dst), at(src), run * static_cast<size_t>(mSectorSize));
                    }
                    else {
                        for (size_t i = 0; i < run; ++i) {
                            Sector::moveSector(at(src + i), at(dst + i), mSectorLayout);
                        }
                    }
                    dst += run; src += run; n -= run;
                }
            }
            else {
                // back
                size_t srcEnd = src + n;
                size_t dstEnd = dst + n;
                while (n) {
                    const size_t srcIn = srcEnd & (mChunkCapacity - 1);
                    const size_t dstIn = dstEnd & (mChunkCapacity - 1);
                    const size_t srcRoom = srcIn ? srcIn : mChunkCapacity;
                    const size_t dstRoom = dstIn ? dstIn : mChunkCapacity;
                    const size_t run = std::min({ n, srcRoom, dstRoom });

                    const size_t srcBeg = srcEnd - run;
                    const size_t dstBeg = dstEnd - run;
                    if (mIsSectorTrivial) {
                        std::memmove(at(dstBeg), at(srcBeg), run * static_cast<size_t>(mSectorSize));
                    }
                    else {
                        for (size_t i = run; i-- > 0; ) {
                            Sector::moveSector(at(srcBeg + i), at(dstBeg + i), mSectorLayout);
                        }
                    }

                    srcEnd -= run; dstEnd -= run; n -= run;
                }
            }
        }

        void deallocate(size_t from, size_t to) {
            from = std::min(mChunks.size(), calcChunkIndex(from) + static_cast<size_t>((from & (mChunkCapacity - 1)) > 0)); // chunkIndex returns chunk in which "from" exists, we need to delete next chunk if it is not the chunk start
            to = std::min(mChunks.size(), calcChunkIndex(to));
            if (from < to) {
                for (auto i = from; i < to; i++) { std::free(mChunks[i]); }

                mChunks.erase(mChunks.begin() + static_cast<int64_t>(from), mChunks.begin() + static_cast<int64_t>(to));
                mChunks.shrink_to_fit();
                mBin.drainAll();
            }
        }

        void allocate(size_t newCapacity) {
            const auto oldCapacity = capacity();
            const auto need = newCapacity > oldCapacity ? (newCapacity - oldCapacity) : 0;
            const auto count = (need + mChunkCapacity - 1) / mChunkCapacity;

            mChunks.reserve(mChunks.size() + count);
            for (auto i = 0u; i < count; i++) {
                void* ptr = calloc(mChunkCapacity, mSectorSize); assert(ptr);
                mChunks.emplace_back(ptr);
            }
        }

        size_t capacity() const { return mChunkCapacity * mChunks.size(); }

        size_t find(const Sector* sectorPtr) const {
            if (!sectorPtr || mChunks.empty()) return static_cast<size_t>(INVALID_ID);

            const size_t stride = static_cast<size_t>(mChunkCapacity) * static_cast<size_t>(mSectorSize);
            const std::byte* p = reinterpret_cast<const std::byte*>(sectorPtr);

            {
                const std::byte* base0 = static_cast<const std::byte*>(mChunks.front());
                if (p >= base0 && p < base0 + stride) {
                    const size_t local = (p - base0) / static_cast<size_t>(mSectorSize);
                    return local;
                }
                const std::byte* baseL = static_cast<const std::byte*>(mChunks.back());
                if (p >= baseL && p < baseL + stride) {
                    const size_t local = (p - baseL) / static_cast<size_t>(mSectorSize);
                    return (mChunks.size() - 1) * static_cast<size_t>(mChunkCapacity) + local;
                }
            }

            for (size_t ci = 1; ci + 1 < mChunks.size(); ++ci) {
                const std::byte* base = static_cast<const std::byte*>(mChunks[ci]);
                if (p >= base && p < base + stride) {
                    const size_t local = (p - base) / static_cast<size_t>(mSectorSize);
                    return ci * static_cast<size_t>(mChunkCapacity) + local;
                }
            }
            return static_cast<size_t>(INVALID_ID);
        }

    private:
        inline static constexpr size_t calcChunkIndex(size_t sectorIdx) { return sectorIdx >> mChunkShift; }
        inline constexpr size_t calcInChunkShift(size_t sectorIdx) const { return (sectorIdx & (mChunkCapacity - 1)) * mSectorSize; }

        template<uint32_t OC>
        void copyCommonData(const ChunksAllocator<OC>& other)  {
            mSectorLayout = other.mSectorLayout;
            mSectorSize = other.mSectorSize;
            mIsSectorTrivial = other.mIsSectorTrivial;
        }

        template<uint32_t OC>
        void copy(const ChunksAllocator<OC>& other)  {
            copyCommonData(other);
            allocate(other.mChunks.size() * mChunkCapacity);
            if (mIsSectorTrivial) {
                if constexpr (OC == ChunkCapacity) {
                    for (auto i = 0u; i < other.mChunks.size(); i++) {
                        std::memcpy(mChunks[i], other.mChunks[i], mChunkCapacity * static_cast<size_t>(mSectorSize));
                    }
                }
                else {
                    const size_t srcTotalSectors = other.mChunks.size() * static_cast<size_t>(other.mChunkCapacity);
                    const size_t dstTotalSectors = mChunks.size() * static_cast<size_t>(mChunkCapacity);
                    const size_t total = (srcTotalSectors < dstTotalSectors) ? srcTotalSectors : dstTotalSectors;

                    size_t src = 0;
                    size_t dst = 0;
                    while (dst < total) {
                        const size_t srcIn = src & (other.mChunkCapacity - 1);
                        const size_t dstIn = dst & (mChunkCapacity - 1);
                        const size_t srcRoom = other.mChunkCapacity - srcIn;
                        const size_t dstRoom = mChunkCapacity - dstIn;
                        const size_t left = total - dst;
                        const size_t run = (srcRoom < dstRoom ? (srcRoom < left ? srcRoom : left) : (dstRoom < left ? dstRoom : left));

                        auto* sChunk = static_cast<std::byte*>(other.mChunks[src / other.mChunkCapacity]);
                        auto* dChunk = static_cast<std::byte*>(mChunks[dst / mChunkCapacity]);

                        std::memcpy(dChunk + dstIn * mSectorSize, sChunk + srcIn * mSectorSize, run * mSectorSize);

                        src += run;
                        dst += run;
                    }
                }
            }
            else {
                auto from = other.getCursor();
                auto to = getCursor();
                for (auto i = 0u; i < capacity(); i++) {
                    auto sector = Sector::copySector(*from, *to, mSectorLayout);
                    ++from;
                    ++to;
                }
            }
        }

        template<uint32_t OC>
    	void move(ChunksAllocator<OC>&& other)  {
            copyCommonData(other);
            if constexpr (OC == ChunkCapacity) {
                mChunks = std::move(other.mChunks);
                other.mBin.drainAll();
            }
            else {
                allocate(other.mChunks.size() * mChunkCapacity);
                auto from = other.getCursor();
                auto to = getCursor();
                for (auto i = 0u; i < capacity(); i++) {
                    auto sector = Sector::moveSector(*from, *to, mSectorLayout);
                    ++from;
                    ++to;
                }

                other.deallocate(0, other.capacity());
            }
        }

    public:
        static constexpr uint32_t mChunkCapacity = nextPowerOfTwo(ChunkCapacity);
        static constexpr uint32_t mChunkShift = std::countr_zero(mChunkCapacity);

        mutable Memory::RetireBin mBin;
        std::vector<void*, Memory::RetireAllocator<void*>> mChunks { Memory::RetireAllocator<void*>{ &mBin } };

        SectorLayoutMeta* mSectorLayout = nullptr;
        uint16_t mSectorSize = 0;

        bool mIsSectorTrivial = std::is_trivial_v<Sector>;
    };
}