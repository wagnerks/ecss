#pragma once

#include <vector>

#include <ecss/memory/Sector.h>
#include <ecss/memory/RetireAllocator.h>

namespace ecss::Memory {
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

        class Iterator {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type = Sector*;
            using difference_type = std::ptrdiff_t;
            using pointer = value_type*;
            using reference = value_type&;

            Iterator() = default;
            Iterator(size_t idx, const ChunksAllocator* chunks) : mIdx(std::min(idx, chunks->size())), mSectorPtr(idx < chunks->capacity() ? chunks->at(mIdx) : nullptr), shift(chunks->mSectorSize) {
                if (mSectorPtr) {
                    auto chunkIdx = ChunksAllocator<ChunkCapacity>::calcChunkIndex(idx);
                    pointersList.reserve((chunks->mChunks.size() - chunkIdx) * 2 - 1);

                    const size_t stride = static_cast<size_t>(chunks->mSectorSize) * mChunkCapacity;
                    for (auto i = chunks->mChunks.size(); --i > chunkIdx;) {
                        auto base = static_cast<char*>(chunks->mChunks[i]);
                        pointersList.emplace_back(reinterpret_cast<Sector*>(base + stride));
                        pointersList.emplace_back(reinterpret_cast<Sector*>(base));
                    }
                    pointersList.emplace_back(reinterpret_cast<Sector*>(static_cast<char*>(chunks->mChunks[chunkIdx]) + stride));
                }
            }

            inline Iterator& operator++() noexcept {
                mSectorPtr = reinterpret_cast<Sector*>(reinterpret_cast<char*>(mSectorPtr) + shift);
                mIdx++;
                if (mSectorPtr == pointersList.back()) [[unlikely]] {
                    pointersList.pop_back();
                    if (pointersList.empty()) [[unlikely]] {
                        return *this;
                    }
                    mSectorPtr = pointersList.back();
                    pointersList.pop_back();
                }
                return *this;
            }

            inline Iterator operator++(int) noexcept { Iterator tmp = *this; ++(*this); return tmp; }

            inline bool operator==(const Iterator& other) const noexcept { return mIdx == other.mIdx; }
            inline bool operator!=(const Iterator& other) const noexcept { return !(*this == other); }

            inline value_type operator*() const noexcept { return mSectorPtr; }
            inline value_type operator->() const noexcept { return mSectorPtr; }

        private:
            std::vector<Sector*> pointersList;
            size_t mIdx = 0;
            Sector* mSectorPtr = nullptr;
            uint32_t shift = 0;
        };

        Iterator begin() noexcept { return { 0, this }; }
        Iterator getIterator(size_t index) noexcept { return { index, this }; }
        Iterator end() noexcept { return { size(), this }; }

    public:
        ChunksAllocator(const ChunksAllocator& other) noexcept { *this = other; }
        ChunksAllocator& operator=(const ChunksAllocator& other) noexcept {
            if (this == &other) { return *this; }

            copyCommonData(other);
            copyChunksData(other);

            return *this;
        }

        ChunksAllocator(ChunksAllocator&& other) noexcept { *this = std::move(other); }
        ChunksAllocator& operator=(ChunksAllocator&& other) noexcept {
            if (this == &other) { return *this; }

            copyCommonData(other);
            moveChunksData(std::move(other));

            return *this;
        }

        template<uint32_t OtherCapacity>
        ChunksAllocator(const ChunksAllocator<OtherCapacity>& other) noexcept { *this = other; }
        template<uint32_t OtherCapacity>
        ChunksAllocator& operator=(const ChunksAllocator<OtherCapacity>& other) noexcept {
            if (reinterpret_cast<void*>(this) == reinterpret_cast<void*>(const_cast<ChunksAllocator<OtherCapacity>*>(&other))) { return *this; }

            copyCommonData(other);
            copyChunksData(other);

            return *this;
        }

        template<uint32_t OtherCapacity>
        ChunksAllocator(ChunksAllocator<OtherCapacity>&& other) noexcept { *this = std::move(other); }
        template<uint32_t OtherCapacity>
        ChunksAllocator& operator=(ChunksAllocator<OtherCapacity>&& other) noexcept {
            if (reinterpret_cast<void*>(this) == reinterpret_cast<void*>(const_cast<ChunksAllocator<OtherCapacity>*>(&other))) { return *this; }

            copyCommonData(other);
            moveChunksData(std::move(other));

            return *this;
        }

    public:
        ~ChunksAllocator() noexcept { free(); }
        ChunksAllocator() = default;

    public:
        /**
        * @brief Initialize allocator with a sector layout.
        *
        * Must be called before using sectors. Stores metadata about sector size
        * and whether the layout is trivial (plain old data).
        */
        void init(SectorLayoutMeta* layoutMeta) noexcept {
            assert(layoutMeta);

            mSectorLayout = layoutMeta;

            mSectorSize = mSectorLayout->getTotalSize();
            if (mIsSectorTrivial) {
                for (auto& layout : *mSectorLayout) {
                    mIsSectorTrivial = mIsSectorTrivial && layout.isTrivial;
                }
            }
        }

        SectorLayoutMeta* getSectorLayout() const { return mSectorLayout; }

        /**
         * @brief Acquire a sector by ID, creating it if necessary.
         *
         * Expands internal storage if needed and constructs a new `Sector`
         * at the appropriate position. Ensures `mSectorsMap` is updated.
         *
         * @param sectorId Unique identifier for the sector.
         * @return Pointer to the acquired sector.
         */
        Sector* acquireSector(SectorId sectorId) noexcept {
			if (sectorId >= mSectorsMap.size()) [[unlikely]] {
                mSectorsMap.resize(static_cast<size_t>(sectorId) + 1, nullptr);
            }
            reserve(mSize + 1);

            auto& sector = mSectorsMap[sectorId];
            if (sector) [[unlikely]] {
                return sector;
            }

            size_t pos = mSize++;
        	if (!((pos == 0) || (sectorId < back()->id))) [[unlikely]] {
                pos = findInsertPosition(sectorId, pos);

                if (pos != mSize - 1) [[unlikely]] {
                    shiftDataRight(pos);
                }
            }

            sector = at(pos);
            sector->id = sectorId;
            return sector;
        }

        /**
        * @brief Free a sector by ID.
        *
        * Destroys the sector and removes its mapping. Optionally shifts
        * subsequent data left to keep memory compact.
        *
        * @param id Sector ID.
        * @param shift Whether to shift subsequent sectors left.
        */
        void freeSector(SectorId id, bool shift) noexcept { erase(calcSectorIndex(id), 1, shift); }

        /**
         * @brief Destroy all sectors and release all memory.
         *
         * Calls destructors for non-trivial sectors, frees all chunks,
         * clears the sector map, and empties the retire bin.
         */
        void free() noexcept {
            if (!mIsSectorTrivial) {
                for (auto it = begin(), endIt = end(); it != endIt; ++it) {
                    Sector::destroySector(*it, mSectorLayout);
                }
            }

            for (auto chunk : mChunks) { std::free(chunk); }
            mSectorsMap.clear();
            mChunks.clear();
            mChunks.shrink_to_fit();
            emptyVectorsBin();
            mDefragmentationKoef = 0;
            mSize = 0;
        }

        Sector* getSector(size_t sectorId) const noexcept { assert(sectorId < mSectorsMap.size()); return mSectorsMap[sectorId]; }
        Sector* tryGetSector(size_t sectorId) const noexcept { return sectorId < mSectorsMap.size() ? mSectorsMap[sectorId] : nullptr; }
        
        Sector* at(size_t index) const noexcept { return reinterpret_cast<Sector*>(static_cast<char*>(getChunk(calcChunkIndex(index))) + calcInChunkIndex(index)); }
        Sector* back() const noexcept { return reinterpret_cast<Sector*>(static_cast<char*>(mChunks.back()) + calcInChunkIndex(mSize - 1)); }

        void reserve(size_t newCapacity) noexcept {
            auto oldCapacity = capacity();
            const size_t need = newCapacity > oldCapacity ? (newCapacity - oldCapacity) : 0;
            if (const size_t add = (need + mChunkCapacity - 1) / mChunkCapacity) [[unlikely]] { // ceil
            	allocateChunks(add);

                oldCapacity = capacity();
                if (oldCapacity > sectorsCapacity()) [[unlikely]] {
                    mSectorsMap.resize(oldCapacity, nullptr);
                }
            }
        }

        size_t capacity() const noexcept { return mChunkCapacity * mChunks.size(); }
        size_t sectorsCapacity() const noexcept { return mSectorsMap.size(); }
        size_t size() const noexcept { return mSize; }
        bool empty() const noexcept { return !mSize; }

        /**
         * @brief Erase one or more sectors starting at an index.
         *
         * Optionally shifts remaining sectors left or leaves holes for
         * deferred compaction. Updates the sector map accordingly.
         *
         * @param begin Index of first sector to erase.
         * @param count Number of sectors to erase.
         * @param shift If true, shifts remaining data left.
         */
        void erase(size_t begin, size_t count = 1, bool shift = true) noexcept {
            count = std::min(size() - begin, count);
            if (count == 0 || begin >= size()) {
                return;
            }

            for (auto i = begin; i < begin + count; i++) {
                auto sector = at(i);
                mSectorsMap[sector->id] = nullptr;
                Sector::destroySector(sector, mSectorLayout);
            }

            if (shift) {
                shiftDataLeft(begin, count);
                mSize -= static_cast<uint32_t>(count);
                shrinkToFit();
            }
            else {
                if (begin + count < mSize) {
                    mDefragmentationKoef += count;
                }
            }
        }

        void shrinkToFit() noexcept {
            size_t last = 0;
            if (mSize != 0) {
                last = calcChunkIndex(mSize - 1) + 1;
            }

            if (last < mChunks.size()) {
                const auto size = mChunks.size();
                for (auto i = last; i < size; i++) {
                    std::free(mChunks[i]);
                }

                mChunks.erase(mChunks.begin() + static_cast<int64_t>(last), mChunks.end());
                mChunks.shrink_to_fit();
            }
        }

        /**
         * @brief Compute linear index of a sector by its ID.
         *
         * This function attempts to quickly locate the sector's index
         * using multiple fast paths:
         *  - First chunk check (common for low IDs).
         *  - Last chunk check (common for newly allocated sectors).
         *  - Range check on ID to discard invalid sectors.
         *  - Heuristic estimate of chunk index based on ID distance,
         *    followed by local scan left/right if necessary.
         *
         * In practice this resolves the index in O(1) or O(few) steps,
         * worst case O(#chunks).
         *
         * @param id SectorId of the sector to locate.
         * @return Linear index within the allocator, or INVALID_ID if not found.
         */
        size_t calcSectorIndex(SectorId id) const noexcept {
            auto sectorPtr = tryGetSector(id);
            if (!sectorPtr || mSize == 0) [[unlikely]] {
                return static_cast<size_t>(INVALID_ID);
            }

        	// Number of bytes per chunk (chunk capacity × sector size)
            const size_t stride = mChunkCapacity * static_cast<size_t>(mSectorSize);
            const char* const p = reinterpret_cast<const char*>(sectorPtr);

            // --- FAST PATH: Check first chunk ---
            {
                const char* const base0 = static_cast<const char*>(mChunks.front());
                if (p >= base0 && p < base0 + stride) {
                    const size_t local = static_cast<size_t>(p - base0) / static_cast<size_t>(mSectorSize);
                    return local; // First chunk → global index == local index
                }
            }

            // --- FAST PATH: Check last chunk ---
            {
                const char* const baseL = static_cast<const char*>(mChunks.back());
                if (p >= baseL && p < baseL + stride) {
                    const size_t local = static_cast<size_t>(p - baseL) / static_cast<size_t>(mSectorSize);
                    return (mChunks.size() - 1) * static_cast<size_t>(mChunkCapacity) + local;
                }
            }

            // Get IDs of the first and last sector for quick range check
            const Sector* const firstSec = at(0);
            const Sector* const lastSec = at(mSize - 1);

            const SectorId firstId = firstSec->id;
            const SectorId lastId = lastSec->id;

            // If the given sector's ID is outside the valid range → invalid index
            const SectorId sid = sectorPtr->id;
            if (sid < firstId || sid > lastId) [[unlikely]] {
                return static_cast<size_t>(INVALID_ID);
            }

            // --- HEURISTIC: Estimate the chunk index from ID ---
            // This assumes IDs are mostly contiguous, so we can jump near the target chunk.
            size_t approx;
            {
                const auto dist = static_cast<uint64_t>(sid - firstId);
                approx = static_cast<size_t>(dist / mChunkCapacity);
                if (approx >= mChunks.size()) approx = mChunks.size() - 1;
            }

            // Lambda to check if address p is inside a chunk with given index
            auto inChunk = [&](size_t ci) -> bool {
                const char* const base = static_cast<const char*>(mChunks[ci]);
                return (p >= base) && (p < base + stride);
            };

            // --- Try the estimated chunk first ---
            if (inChunk(approx)) {
                const char* const base = static_cast<const char*>(mChunks[approx]);
                const size_t local = static_cast<size_t>(p - base) / static_cast<size_t>(mSectorSize);
                return approx * static_cast<size_t>(mChunkCapacity) + local;
            }

            // --- Expand search left/right from approx ---
            // Usually finds the chunk within 1-2 steps, worst case scans all chunks.
            size_t left = approx;
            size_t right = approx + 1;

            while (left > 0 || right < mChunks.size()) {
                if (right < mChunks.size()) {
                    if (inChunk(right)) {
                        const char* const base = static_cast<const char*>(mChunks[right]);
                        const size_t local = static_cast<size_t>(p - base) / static_cast<size_t>(mSectorSize);
                        return right * static_cast<size_t>(mChunkCapacity) + local;
                    }
                    ++right;
                }
                if (left > 0) {
                    --left;
                    if (inChunk(left)) {
                        const char* const base = static_cast<const char*>(mChunks[left]);
                        const size_t local = static_cast<size_t>(p - base) / static_cast<size_t>(mSectorSize);
                        return left * static_cast<size_t>(mChunkCapacity) + local;
                    }
                }
            }

            // Should not reach here unless the pointer is invalid
            return static_cast<size_t>(INVALID_ID);
        }

        /**
         * @brief Defragment allocator by compacting alive sectors.
         *
         * Moves only alive sectors leftwards until no holes remain.
         * Destroys dead sectors and shrinks unused chunks.
         *
         * Useful to restore density after many frees without frequent shifting.
         */
        void defragment() noexcept {
            const size_t n = size();
            if (!n) return;
            mDefragmentationKoef = 0;

        	//algorithm which will not shift all sectors left every time, but shift only alive sectors to left border till not found empty place
        	//OOOOxOxxxOOxxxxOOxOOOO   0 - start
        	//OOOOx<-OxxxOOxxxxOOxOOOO 0
        	//OOOOOxxxxOOxxxxOOxOOOO   1
        	//OOOOOxxxx<-OOxxxxOOxOOOO 1
        	//OOOOOOOxxxxxxxxOOxOOOO   2
        	//OOOOOOOxxxxxxxx<-OOxOOOO 2
        	//OOOOOOOOOxxxxxxxxxOOOO   3
        	//OOOOOOOOOxxxxxxxxx<-OOOO 3
        	//OOOOOOOOOOOOOxxxxxxxxx   4 - end

            size_t read = 0;
            size_t write = 0;
            size_t deleted = 0;

            while (read < n) {
                Sector* s = at(read);
                if (!s->isSectorAlive()) break;
                ++read; ++write;
            }
            if (read == n) {
                shrinkToFit();
                return;
            }

            while (read < n) {
                while (read < n) {
                    Sector* s = at(read);
                    if (s->isSectorAlive()) break;
                    mSectorsMap[s->id] = nullptr;
                    if (!mIsSectorTrivial) {
                        Sector::destroySector(s, mSectorLayout);
                    }
                    ++read; ++deleted;
                }
                if (read >= n) break;

                size_t runBeg = read;
                while (read < n) {
                    Sector* s = at(read);
                    if (!s->isSectorAlive()) break;
                    ++read;
                }
                const size_t runLen = read - runBeg;
                if (write != runBeg) {
                    if (mIsSectorTrivial) {
                        copySectors(/*dst*/ write, /*src*/ runBeg, /*n*/ runLen);

                        for (size_t i = 0; i < runLen; ++i) {
                            Sector* ns = at(write + i);
                            mSectorsMap[ns->id] = ns;
                        }
                    }
                    else {
                        for (size_t i = 0; i < runLen; ++i) {
                            Sector* ns = Sector::moveSector(at(runBeg + i), at(write + i), mSectorLayout);
                            mSectorsMap[ns->id] = ns;
                        }
                    }
                }
                else {
                    for (size_t i = 0; i < runLen; ++i) {
                        Sector* ns = at(write + i);
                        mSectorsMap[ns->id] = ns;
                    }
                }

                write += runLen;
            }

            mSize -= deleted;

            shrinkToFit();
        }

        /**
         * @brief Empty the RetireBin associated with vectors.
         *
         * Frees all retired memory blocks that were previously held
         * due to vector reallocations.
         */
        void emptyVectorsBin() const { mBin.drainAll(); }

    private:
        void* getChunk(size_t index) const noexcept { return mChunks[index]; }

        inline constexpr size_t calcInChunkIndex(size_t sectorIdx) const noexcept { return (sectorIdx & (mChunkCapacity - 1)) * mSectorSize; }
        inline static constexpr size_t calcChunkIndex(size_t sectorIdx) noexcept { return sectorIdx >> mChunkShift; }

        void allocateChunks(size_t count = 1) noexcept {
            if (count == 0) [[unlikely]] {
                return;
            }

            mChunks.reserve(mChunks.size() + count);
            for (auto i = 0u; i < count; i++) {
                void* ptr = calloc(mChunkCapacity, mSectorSize);
				assert(ptr);
                mChunks.emplace_back(ptr);
            }
        }

        template<uint32_t OtherCapacity>
        void copyCommonData(const ChunksAllocator<OtherCapacity>& other) noexcept {
            mSectorLayout = other.mSectorLayout;
            mSectorSize = other.mSectorSize;
            mIsSectorTrivial = other.mIsSectorTrivial;
        }

        template<uint32_t OtherCapacity>
        void copyChunksData(const ChunksAllocator<OtherCapacity>& other) noexcept {
            free();
            other.emptyVectorsBin();
            mSize = other.mSize;
            allocateChunks((mSize + mChunkCapacity - 1) / mChunkCapacity);

            mSectorsMap.resize(other.mSectorsMap.size(), nullptr);
            if constexpr (OtherCapacity == ChunkCapacity) {
                if (mIsSectorTrivial) {
                    for (auto i = 0u; i < other.mChunks.size(); i++) {
                        memcpy(mChunks[i], other.mChunks[i], mChunkCapacity * static_cast<size_t>(mSectorSize));
                    }

                    for (auto it = begin(), endIt = getIterator(other.mSize); it != endIt; ++it) {
                        auto sector = *it;
                        mSectorsMap[sector->id] = sector;
                    }
                }
                else {
                    for (auto i = 0u; i < mSize; i++) {
                        auto sector = Sector::copySector(other.at(i), at(i), mSectorLayout);
                        mSectorsMap[sector->id] = sector;
                    }
                }
            }
            else {
                for (auto i = 0u; i < mSize; i++) {
                    auto sector = Sector::copySector(other.at(i), at(i), mSectorLayout);
                    mSectorsMap[sector->id] = sector;
                }
            }
            mDefragmentationKoef = other.mDefragmentationKoef;
            defragment();
        }

        template<uint32_t OtherCapacity>
    	void moveChunksData(ChunksAllocator<OtherCapacity>&& other) noexcept {
            free();
            other.emptyVectorsBin();
            mSize = other.mSize;
            if constexpr (OtherCapacity == ChunkCapacity) {
                mChunks = std::move(other.mChunks);
                mSectorsMap = std::move(other.mSectorsMap);
            }
            else {
                allocateChunks((mSize + mChunkCapacity - 1) / mChunkCapacity);
                for (auto i = 0u; i < mSize; i++) {
                    auto sector = Sector::moveSector(other.at(i), at(i), mSectorLayout);
                    mSectorsMap[sector->id] = sector;
                }
            }

            other.mSize = 0;
            mDefragmentationKoef = std::move(other.mDefragmentationKoef);
            defragment();
        }

    private:
        inline size_t findInsertPosition(SectorId sectorId, size_t size) const noexcept {
            size_t right = size;
            if (right == 0) {
                return 0;
            }

            if (at(right - 1)->id < sectorId) {
                return right;
            }

            if (at(0)->id > sectorId) {
                return 0;
            }

            size_t left = 0;
            while (right - left > 1) {
                size_t mid = left + (right - left) / 2;
                auto mid_id = at(mid)->id;
                if (mid_id < sectorId) {
                    left = mid;
                }
                else {
                    right = mid;
                }
            }

            return right;
        }

        char* bytePtrAt(size_t idx) const noexcept { return reinterpret_cast<char*>(at(idx)); }

        /**
        * @brief Copy a contiguous run of sectors from src to dst.
        *
        * Works across chunk boundaries. Preserves raw memory layout,
        * does not call constructors/destructors. Used only when
        * sectors are known to be trivially copyable.
        *
        * @param dst Destination index (linear).
        * @param src Source index (linear).
        * @param n Number of sectors to copy.
        */
        void copySectors(size_t dst, size_t src, size_t n) const noexcept {
            if (!n || dst == src) return;

            if (dst < src) {
                // forward
                while (n) {
                    const size_t srcRoom = mChunkCapacity - (src & (mChunkCapacity - 1));
                    const size_t dstRoom = mChunkCapacity - (dst & (mChunkCapacity - 1));
                    const size_t run = std::min({ n, srcRoom, dstRoom });
                    std::memmove(bytePtrAt(dst), bytePtrAt(src), run * static_cast<size_t>(mSectorSize));
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
                    std::memmove(bytePtrAt(dstBeg), bytePtrAt(srcBeg), run * static_cast<size_t>(mSectorSize));

                    srcEnd -= run; dstEnd -= run; n -= run;
                }
            }
        }

        /**
         * @brief Shift a block of sectors to the right starting at 'from'.
         *
         * Used when inserting a new sector inside the sequence.
         * Ensures existing sectors and mSectorsMap are updated to
         * their new locations.
         *
         * For trivial sectors: uses raw memmove.
         * For non-trivial: uses Sector::moveSector element by element.
         *
         * @param from First index where shifting begins.
         * @param count Number of empty slots to create.
         */
        // [][][][from][][][] -> [][][] ____ [from][][][]
        void shiftDataRight(size_t from, size_t count = 1) noexcept {
            if (!count) return;

            if (mIsSectorTrivial) {
                const size_t oldSize = mSize - count;
                if (const size_t tail = oldSize > from ? (oldSize - from) : 0) {
                    copySectors(/*dst*/ from + count, /*src*/ from, /*n*/ tail);
                    for (size_t i = from + count; i < from + count + tail; ++i) {
                        Sector* s = at(i);
                        mSectorsMap[s->id] = s;
                    }
                }
            }
            else {
                for (size_t i = mSize; i-- > from + count; ) {
                    auto* s = Sector::moveSector(at(i - count), at(i), mSectorLayout);
                    mSectorsMap[s->id] = s;
                }
            }
        }

        /**
         * @brief Shift a block of sectors to the left starting at 'from'.
         *
         * Used when erasing one or more sectors and closing the gap.
         * Ensures subsequent sectors are compacted and mSectorsMap is updated.
         *
         * For trivial sectors: uses raw memmove.
         * For non-trivial: uses Sector::moveSector element by element.
         *
         * @param from First index of erased block.
         * @param count Number of erased sectors.
         */
        // [][][][from][][][] -> [][][from][][][]
        void shiftDataLeft(size_t from, size_t count = 1) noexcept {
            if (!count) return;

            if (mIsSectorTrivial) {
	            if (const size_t tail = (mSize > from + count) ? (mSize - (from + count)) : 0) {
                    copySectors(/*dst*/ from, /*src*/ from + count, /*n*/ tail);
                    for (size_t i = from; i < from + tail; ++i) {
                        Sector* s = at(i);
                        mSectorsMap[s->id] = s;
                    }
                }
            }
            else {
                for (size_t i = from; i < mSize - count; ++i) {
                    auto* s = Sector::moveSector(at(i + count), at(i), mSectorLayout);
                    mSectorsMap[s->id] = s;
                }
            }
        }

        static constexpr inline uint32_t nextPowerOfTwo(uint32_t x) noexcept {
            if (x == 0) return 1;
            --x;
            x |= x >> 1;
            x |= x >> 2;
            x |= x >> 4;
            x |= x >> 8;
            x |= x >> 16;
            return ++x;
        }

    private:
        mutable Memory::RetireBin mBin;

        std::vector<void*, Memory::RetireAllocator<void*>> mChunks { Memory::RetireAllocator<void*>{ &mBin } };
        std::vector<Sector*, Memory::RetireAllocator<Sector*>> mSectorsMap{ Memory::RetireAllocator<Sector*>{ &mBin } };

        SectorLayoutMeta* mSectorLayout = nullptr;

        size_t mDefragmentationKoef = 0; //todo
        size_t mSize = 0;

        static constexpr uint32_t mChunkCapacity = nextPowerOfTwo(ChunkCapacity);
        static constexpr uint32_t mChunkShift = std::countr_zero(mChunkCapacity);
        
        uint16_t mSectorSize = 0;

        bool mIsSectorTrivial = std::is_trivial_v<Sector>;
    };
}