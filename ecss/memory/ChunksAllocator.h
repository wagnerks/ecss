#pragma once

#include <vector>

#include <ecss/memory/Sector.h>

namespace ecss::Memory {
    struct ChunksAllocator {
        class Iterator {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type = Sector*;
            using difference_type = std::ptrdiff_t;
            using pointer = value_type*;
            using reference = value_type&;

            Iterator() = default;
            Iterator(size_t idx, const ChunksAllocator* chunks) : mSectorPtr(idx < chunks->capacity() ? chunks->at(std::min(idx, chunks->size())) : nullptr), shift(chunks->mSectorSize) {
                if (mSectorPtr) {
                    auto chunkIdx = chunks->calcChunkIndex(idx);
                    pointersList.reserve((chunks->mChunks.size() - chunkIdx) * 2 - 1);

                    const size_t stride = static_cast<size_t>(chunks->mSectorSize) * chunks->mChunkCapacity;
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
                if (mSectorPtr == pointersList.back()) [[unlikely]] {
                    pointersList.pop_back();
                    if (pointersList.empty()) [[unlikely]] {
                        mSectorPtr = nullptr;
                        return *this;
                    }
                    mSectorPtr = pointersList.back();
                    pointersList.pop_back();
                }
                return *this;
            }

            inline Iterator operator++(int) noexcept { Iterator tmp = *this; ++(*this); return tmp; }

            inline bool operator==(const Iterator& other) const noexcept { return mSectorPtr == other.mSectorPtr; }
            inline bool operator!=(const Iterator& other) const noexcept { return !(*this == other); }

            inline value_type operator*() const noexcept { return mSectorPtr; }
            inline value_type operator->() const noexcept { return mSectorPtr; }

        private:
            std::vector<Sector*> pointersList;

            Sector* mSectorPtr = nullptr;
            uint32_t shift = 0;
        };

        Iterator begin() noexcept { return { 0, this }; }
        Iterator getIterator(size_t index) noexcept { return { index, this }; }
        Iterator end() noexcept { return { size(), this }; }

    public:
        ChunksAllocator(const ChunksAllocator& other) noexcept {
            copyCommonData(other);
            copyChunksData(other);
        }

        ChunksAllocator& operator=(const ChunksAllocator& other) noexcept {
            if (this == &other) { return *this; }

            copyCommonData(other);
            copyChunksData(other);

            return *this;
        }

        ChunksAllocator(ChunksAllocator&& other) noexcept {
            copyCommonData(other);
            moveChunksData(std::move(other));
        }

        ChunksAllocator& operator=(ChunksAllocator&& other) noexcept {
            if (this == &other) { return *this; }

            copyCommonData(other);
            moveChunksData(std::move(other));

            return *this;
        }

    public:
        ~ChunksAllocator() noexcept { free(); }
        ChunksAllocator() noexcept = default;
        ChunksAllocator(uint32_t chunkSize) noexcept : mChunkCapacity(nextPowerOfTwo(chunkSize)), mChunkShift(std::countr_zero(mChunkCapacity)) {}

    public:
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
        	if (!((pos == 0) || (sectorId > at(pos - 1)->id))) [[unlikely]] {
                pos = findInsertPosition(sectorId, pos);

                if (pos != mSize - 1) [[unlikely]] {
                    shiftDataRight(pos);
                }
            }

            sector = at(pos);
            return new (sector)Sector(sectorId, 0);
        }

        void freeSector(SectorId id, bool shift) noexcept {
            erase(calcSectorIndex(id), 1, shift);
        }

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
            mSize = 0;
        }

        Sector* getSector(size_t sectorId) const noexcept { assert(sectorId < mSectorsMap.size()); return mSectorsMap[sectorId]; }
        Sector* tryGetSector(size_t sectorId) const noexcept { return sectorId < mSectorsMap.size() ? mSectorsMap[sectorId] : nullptr; }
        
        Sector* at(size_t index) const noexcept {
            auto chunkIndex = calcChunkIndex(index);
            auto inChunkIndex = calcInChunkIndex(index);

            return reinterpret_cast<Sector*>(static_cast<char*>(getChunk(chunkIndex)) + inChunkIndex);
        }

        void reserve(size_t newCapacity) noexcept {
            const size_t need = newCapacity > capacity() ? (newCapacity - capacity()) : 0;
            const size_t add = (need + mChunkCapacity - 1) / mChunkCapacity; // ceil
            if (add) [[unlikely]] {
            	allocateChunks(add);
                if (capacity() > sectorsCapacity()) [[unlikely]] {
                    mSectorsMap.resize(capacity(), nullptr);
                }
            }
        }

        size_t capacity() const noexcept { return static_cast<size_t>(mChunkCapacity) * mChunks.size(); }
        size_t sectorsCapacity() const noexcept { return mSectorsMap.size(); }
        size_t size() const noexcept { return mSize; }
        bool empty() const noexcept { return !mSize; }

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
                    mDefragmentationCoef += count;
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

        size_t calcSectorIndex(SectorId id) const noexcept {
            auto sectorPtr = tryGetSector(id);
            if (!sectorPtr || mSize == 0) [[unlikely]] {
                return static_cast<size_t>(INVALID_ID);
            }

        	// Number of bytes per chunk (chunk capacity × sector size)
            const size_t stride = size_t(mChunkCapacity) * size_t(mSectorSize);
            const char* const p = reinterpret_cast<const char*>(sectorPtr);

            // --- FAST PATH: Check first chunk ---
            {
                const char* const base0 = reinterpret_cast<const char*>(mChunks.front());
                if (p >= base0 && p < base0 + stride) {
                    const size_t local = size_t(p - base0) / size_t(mSectorSize);
                    return local; // First chunk → global index == local index
                }
            }

            // --- FAST PATH: Check last chunk ---
            {
                const char* const baseL = reinterpret_cast<const char*>(mChunks.back());
                if (p >= baseL && p < baseL + stride) {
                    const size_t local = size_t(p - baseL) / size_t(mSectorSize);
                    return (mChunks.size() - 1) * size_t(mChunkCapacity) + local;
                }
            }

            // Get IDs of the first and last sector for quick range check
            const Sector* const firstSec = at(0);
            const Sector* const lastSec = at(mSize - 1);

            const SectorId firstId = firstSec->id;
            const SectorId lastId = lastSec->id;

            // If the given sectorPtr's ID is outside the valid range → invalid index
            const SectorId sid = sectorPtr->id;
            if (sid < firstId || sid > lastId) [[unlikely]] {
                return static_cast<size_t>(INVALID_ID);
            }

            // --- HEURISTIC: Estimate the chunk index from ID ---
            // This assumes IDs are mostly contiguous, so we can jump near the target chunk.
            size_t approx = 0;
            {
                const uint64_t dist = uint64_t(sid - firstId);
                approx = size_t(dist / mChunkCapacity);
                if (approx >= mChunks.size()) approx = mChunks.size() - 1;
            }

            // Lambda to check if address p is inside a chunk with given index
            auto inChunk = [&](size_t ci) -> bool {
                const char* const base = reinterpret_cast<const char*>(mChunks[ci]);
                return (p >= base) && (p < base + stride);
            };

            // --- Try the estimated chunk first ---
            if (inChunk(approx)) {
                const char* const base = reinterpret_cast<const char*>(mChunks[approx]);
                const size_t local = size_t(p - base) / size_t(mSectorSize);
                return approx * size_t(mChunkCapacity) + local;
            }

            // --- Expand search left/right from approx ---
            // Usually finds the chunk within 1-2 steps, worst case scans all chunks.
            size_t left = approx;
            size_t right = approx + 1;

            while (left > 0 || right < mChunks.size()) {
                if (right < mChunks.size()) {
                    if (inChunk(right)) {
                        const char* const base = reinterpret_cast<const char*>(mChunks[right]);
                        const size_t local = size_t(p - base) / size_t(mSectorSize);
                        return right * size_t(mChunkCapacity) + local;
                    }
                    ++right;
                }
                if (left > 0) {
                    --left;
                    if (inChunk(left)) {
                        const char* const base = reinterpret_cast<const char*>(mChunks[left]);
                        const size_t local = size_t(p - base) / size_t(mSectorSize);
                        return left * size_t(mChunkCapacity) + local;
                    }
                }
            }

            // Should not reach here unless the pointer is invalid
            return static_cast<size_t>(INVALID_ID);
        }

        void defragment() noexcept {
            const size_t n = size();
            if (!n) return;

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
        
    private:
        void* getChunk(size_t index) const noexcept { return mChunks[index]; }

        inline size_t calcInChunkIndex(size_t sectorIdx) const noexcept { return (sectorIdx & (mChunkCapacity - 1)) * mSectorSize; }
        inline size_t calcChunkIndex(size_t sectorIdx) const noexcept { return sectorIdx >> mChunkShift; }

        void allocateChunks(size_t count = 1) noexcept {
            if (count == 0) [[unlikely]] {
                return;
            }

            mChunks.reserve(mChunks.size() + count);
            for (auto i = 0; i < count; i++) {
                void* ptr = calloc(mChunkCapacity, mSectorSize);
				assert(ptr);
                mChunks.emplace_back(ptr);
            }
        }

        void copyCommonData(const ChunksAllocator& other) noexcept {
            mSectorLayout = other.mSectorLayout;
            mChunkCapacity = other.mChunkCapacity;
            mSectorSize = other.mSectorSize;
            mChunkShift = std::countr_zero(mChunkCapacity);
            mIsSectorTrivial = other.mIsSectorTrivial;
        }

        void copyChunksData(const ChunksAllocator& other) noexcept {
            free();
            mSize = other.mSize;
            allocateChunks(other.mChunks.size());

            mSectorsMap.resize(other.mSectorsMap.size(), nullptr);

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

    	void moveChunksData(ChunksAllocator&& other) noexcept {
            free();
            mSize = other.mSize;
            mChunks = std::move(other.mChunks);
            mSectorsMap = std::move(other.mSectorsMap);
            other.mSize = 0;
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

        static inline uint32_t nextPowerOfTwo(uint32_t x) noexcept {
            if (x == 0) return 1;
            --x;
            x |= x >> 1;
            x |= x >> 2;
            x |= x >> 4;
            x |= x >> 8;
            x |= x >> 16;
            return ++x;
        }

        __forceinline char* bytePtrAt(size_t idx) const noexcept {
            return reinterpret_cast<char*>(at(idx));
        }

        void copySectors(size_t dst, size_t src, size_t n) noexcept {
            if (!n || dst == src) return;

            if (dst < src) {
                // forward
                while (n) {
                    const size_t srcRoom = mChunkCapacity - (src & (mChunkCapacity - 1));
                    const size_t dstRoom = mChunkCapacity - (dst & (mChunkCapacity - 1));
                    const size_t run = std::min({ n, srcRoom, dstRoom });
                    std::memmove(bytePtrAt(dst), bytePtrAt(src), run * size_t(mSectorSize));
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
                    std::memmove(bytePtrAt(dstBeg), bytePtrAt(srcBeg), run * size_t(mSectorSize));

                    srcEnd -= run; dstEnd -= run; n -= run;
                }
            }
        }

        // [][][][from][][][] -> [][][] ____ [from][][][]
        void shiftDataRight(size_t from, size_t count = 1) noexcept {
            if (!count) return;

            if (mIsSectorTrivial) {
                const size_t oldSize = mSize - count;
                const size_t tail = oldSize > from ? (oldSize - from) : 0;
                if (tail) {
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

        // [][][][from][][][] -> [][][from][][][]
        void shiftDataLeft(size_t from, size_t count = 1) noexcept {
            if (!count) return;

            if (mIsSectorTrivial) {
                const size_t tail = (mSize > from + count) ? (mSize - (from + count)) : 0;
                if (tail) {
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

    private:
        std::vector<void*> mChunks;
        std::vector<Sector*> mSectorsMap;

        SectorLayoutMeta* mSectorLayout = nullptr;

        size_t mDefragmentationCoef = 0;
        size_t mSize = 0;

        uint32_t mChunkCapacity = 8192;
        uint32_t mChunkShift = 0;
        uint16_t mSectorSize = 0;

        bool mIsSectorTrivial = std::is_trivial_v<Sector>;
    };
}