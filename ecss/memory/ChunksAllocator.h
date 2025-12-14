#pragma once

#include <vector>
#include <cstring>

#include <ecss/Types.h>
#include <ecss/memory/SectorLayoutMeta.h>
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
	 * @brief Chunked memory allocator for sector data.
	 * 
	 * Stores raw sector data (component payloads only) in fixed-size chunks.
	 * Sector metadata (id, isAliveData) is stored externally in SectorsArray.
	 * 
	 * @tparam ChunkCapacity Number of sectors per chunk (rounded up to power of 2).
	 */
	template<uint32_t ChunkCapacity = 8192>
	struct ChunksAllocator {
		template<uint32_t>
		friend struct ChunksAllocator;

		/**
		 * @brief Cursor for linear iteration over sector data.
		 * 
		 * Provides access to raw sector data via linearIndex.
		 * Does NOT have access to sector id/isAlive - those come from external arrays.
		 */
		struct Cursor {
			Cursor() = default;

			Cursor(const ChunksAllocator* allocator, size_t index = 0) noexcept : shift(allocator->mSectorSize)
			{
				chunks.assign(allocator->mChunks.begin(), allocator->mChunks.end());
				chunksCount = chunks.size();
				setLinear(index);
			}

			FORCE_INLINE Cursor& operator++() noexcept { return step(), *this; }
			FORCE_INLINE Cursor& operator+(size_t value) noexcept { return setLinear(linIdx + value), *this; }

			/// @return Raw pointer to sector data (not a Sector*, just component data)
			FORCE_INLINE std::byte* operator*()  const noexcept { return curB; }
			FORCE_INLINE std::byte* operator->() const noexcept { return curB; }

			FORCE_INLINE bool operator==(const Cursor& other) const noexcept { return linIdx == other.linIdx; }
			FORCE_INLINE bool operator!=(const Cursor& other) const noexcept { return !(*this == other); }

			FORCE_INLINE explicit operator bool() const noexcept { return curB != nullptr; }

			FORCE_INLINE size_t linearIndex() const noexcept { return linIdx; }
			FORCE_INLINE std::byte* rawPtr()  const noexcept { return curB; }

		private:
			std::vector<void*> chunks;
			size_t chunksCount = 0;

			size_t chunkIdx = 0;
			size_t linIdx   = 0;

			std::byte* curB      = nullptr;
			std::byte* chunkBase = nullptr;
			std::byte* chunkEnd  = nullptr;

			size_t shift = 0;

			FORCE_INLINE void step() noexcept {
				++linIdx;
				curB += shift;
				if (curB != chunkEnd) [[likely]] return;
				nextChunk();
			}

			FORCE_INLINE void nextChunk() noexcept {
				++chunkIdx;
				if (chunkIdx >= chunksCount) [[unlikely]] {
					curB = chunkBase = chunkEnd = nullptr;
					return;
				}
				chunkBase = static_cast<std::byte*>(chunks[chunkIdx]);
				curB      = chunkBase;
				chunkEnd  = chunkBase + (shift << ChunksAllocator<ChunkCapacity>::mChunkShift);
			}

			FORCE_INLINE void setLinear(size_t newIdx) noexcept {
				linIdx = newIdx;
				if (chunksCount == 0) [[unlikely]] { curB = chunkBase = chunkEnd = nullptr; return; }

				chunkIdx = newIdx >> ChunksAllocator<ChunkCapacity>::mChunkShift;
				if (chunkIdx >= chunksCount) [[unlikely]] { curB = chunkBase = chunkEnd = nullptr; return; }

				const size_t in = newIdx & (ChunksAllocator<ChunkCapacity>::mChunkCapacity - 1);
				chunkBase = static_cast<std::byte*>(chunks[chunkIdx]);
				curB      = chunkBase + in * shift;
				chunkEnd  = chunkBase + (shift << ChunksAllocator<ChunkCapacity>::mChunkShift);
			}
		};

		FORCE_INLINE Cursor getCursor(size_t index = 0) const { return { this, index }; }

		/**
		 * @brief Cursor for ranged iteration over sector data.
		 * 
		 * Iterates over pre-computed spans of sector data.
		 * Does NOT have access to sector id/isAlive - those come from external arrays.
		 */
		struct RangesCursor {
			RangesCursor() = default;
			RangesCursor(const ChunksAllocator* alloc, const Ranges<SectorId>& ranges, size_t size) : shift(alloc->mSectorSize) {
				std::vector<void*> chunks;
				chunks.assign(alloc->mChunks.begin(), alloc->mChunks.end());
				if (chunks.empty()) {
					return;
				}
				size = std::min(size, alloc->capacity());

				// precompute all spans as [beginPtr, endPtr) with linearIdx
				spans.reserve(ranges.ranges.size() + chunks.size());

				// split each logical [first,last) into per-chunk [b,e)
				for (auto [first, last] : ranges.ranges) {
					first = std::min(first, static_cast<EntityId>(size));
					last  = std::min(last,  static_cast<EntityId>(size));

					while (first < last) {
						const auto chunkIndex = ChunksAllocator<ChunkCapacity>::calcChunkIndex(first);
						std::byte* base = chunkIndex >= chunks.size() ? nullptr : static_cast<std::byte*>(chunks[chunkIndex]);
						if (!base) break;

						const auto chunkEndIndex = (chunkIndex + 1) << ChunksAllocator<ChunkCapacity>::mChunkShift;
						const auto upto = (last < chunkEndIndex) ? last : chunkEndIndex;
						const auto count = upto - first;

						auto beginPtr = base + ChunksAllocator<ChunkCapacity>::calcInChunkShift(first, shift);
						auto endPtr = beginPtr + count * shift;
						if (beginPtr != endPtr) { spans.emplace_back(SpanInfo{beginPtr, endPtr, first}); }

						first = static_cast<decltype(first)>(upto);
					}
				}
				spans.shrink_to_fit();

				spansCount = spans.size();
				if (spansCount) {
					spanIdx = 0;
					ptr = spans[0].beginPtr;
					end = spans[0].endPtr;
					linIdx = spans[0].startLinearIdx;
				}
			}

			FORCE_INLINE void nextSpan() noexcept {
				if (++spanIdx < spansCount) [[likely]] {
					ptr = spans[spanIdx].beginPtr;
					end = spans[spanIdx].endPtr;
					linIdx = spans[spanIdx].startLinearIdx;
				}
				else {
					ptr = end = nullptr;
				}
			}

			FORCE_INLINE void step() noexcept {
				ptr += shift;
				++linIdx;
				if (ptr != end) [[likely]] return;
				nextSpan();
			}

			/// @brief Advance to a specific linear index (binary search if needed).
			FORCE_INLINE void advanceToLinearIdx(size_t targetIdx) noexcept {
				if (!ptr || linIdx >= targetIdx) [[likely]] return;
				step(); if (!ptr || linIdx >= targetIdx) [[likely]] return;
				step(); if (!ptr || linIdx >= targetIdx) [[likely]] return;
				jumpToSpanWithLinearIdx(targetIdx);
				if (!ptr || linIdx >= targetIdx) [[likely]] return;
				jumpToPositionInSpan(targetIdx);
			}

			FORCE_INLINE std::byte* rawPtr() const noexcept { return ptr; }
			FORCE_INLINE size_t linearIndex() const noexcept { return linIdx; }
			FORCE_INLINE explicit operator bool() const noexcept { return ptr != nullptr; }

			/// @return Raw pointer to sector data
			FORCE_INLINE std::byte* operator*()  const noexcept { return ptr; }
			FORCE_INLINE std::byte* operator->() const noexcept { return ptr; }

			FORCE_INLINE bool operator==(const RangesCursor& other) const noexcept { return ptr == other.ptr; }
			FORCE_INLINE bool operator!=(const RangesCursor& other) const noexcept { return !(*this == other); }

		private:
			struct SpanInfo {
				std::byte* beginPtr;
				std::byte* endPtr;
				size_t startLinearIdx;
			};

			FORCE_INLINE void jumpToSpanWithLinearIdx(size_t targetIdx) noexcept {
				// Binary search for span containing targetIdx
				size_t spanEndIdx = linIdx + (end - ptr) / shift;
				if (spanEndIdx <= targetIdx) [[unlikely]] {
					size_t lo = spanIdx + 1;
					size_t hi = spansCount;
					while (lo < hi) [[likely]] {
						size_t mid = (lo + hi) >> 1;
						size_t midEndIdx = spans[mid].startLinearIdx + (spans[mid].endPtr - spans[mid].beginPtr) / shift;
						if (midEndIdx <= targetIdx) { lo = mid + 1; } else { hi = mid; }
					}

					if (lo == spansCount) [[unlikely]] {  ptr = end = nullptr;	return;}

					spanIdx = lo;
					ptr = spans[lo].beginPtr;
					end = spans[lo].endPtr;
					linIdx = spans[lo].startLinearIdx;
				}
			}

			FORCE_INLINE void jumpToPositionInSpan(size_t targetIdx) noexcept {
				// Binary search within span
				size_t offset = targetIdx - linIdx;
				ptr += offset * shift;
				linIdx = targetIdx;
			}

			std::vector<SpanInfo> spans{}; // all ranges as [beginPtr, endPtr) with start linear index
			size_t spansCount{0};
			size_t spanIdx{0};
			size_t shift{0};
			size_t linIdx{0};

			std::byte* ptr{nullptr};
			std::byte* end{nullptr};
		};

		FORCE_INLINE RangesCursor getRangesCursor(const Ranges<SectorId>& ranges, size_t size) const { return {this, ranges, size}; }

	public:
		// copy
		template<uint32_t OC>
		ChunksAllocator(const ChunksAllocator<OC>& other) { *this = other; }
		ChunksAllocator(const ChunksAllocator& other) { *this = other; }

		template<uint32_t OC>
		ChunksAllocator& operator=(const ChunksAllocator<OC>& other) { if (!isSameAdr(this, &other)) { copy(other); } return *this; }
		ChunksAllocator& operator=(const ChunksAllocator& other) { if (this != &other) { copy(other); } return *this; }

		// move
		template<uint32_t OC>
		ChunksAllocator(ChunksAllocator<OC>&& other) noexcept { *this = std::move(other); }
		ChunksAllocator(ChunksAllocator&& other) noexcept { *this = std::move(other); }

		template<uint32_t OC>
		ChunksAllocator& operator=(ChunksAllocator<OC>&& other) noexcept { if (!isSameAdr(this, &other)) { move(std::move(other)); } return *this; }
		ChunksAllocator& operator=(ChunksAllocator&& other) noexcept { if (this != &other) { move(std::move(other)); } return *this; }

	public:
		ChunksAllocator() = default;
		~ChunksAllocator() {
			deallocate(0, capacity());
		}

		FORCE_INLINE SectorLayoutMeta* getSectorLayout() const { return mSectorLayout; }

		FORCE_INLINE void init(SectorLayoutMeta* layoutMeta) { assert(layoutMeta);
			mSectorLayout = layoutMeta;

			mSectorSize = mSectorLayout->getTotalSize();
			mIsSectorTrivial = mSectorLayout->isTrivial();
		}

	public:
		/// @return Raw pointer to sector data at given linear index
		FORCE_INLINE std::byte* operator[](size_t index) const { 
			return static_cast<std::byte*>(mChunks[calcChunkIndex(index)]) + calcInChunkShift(index, mSectorSize); 
		}
		FORCE_INLINE std::byte* at(size_t index) const { 
			return static_cast<std::byte*>(mChunks[calcChunkIndex(index)]) + calcInChunkShift(index, mSectorSize); 
		}


		/// @brief Simple memmove for trivial sector data (no alive array needed)
		void moveSectorsDataTrivial(size_t dst, size_t src, size_t n) const {
			if (!n || dst == src) return;

			if (dst < src) {
				// forward
				while (n) {
					const size_t srcRoom = mChunkCapacity - (src & (mChunkCapacity - 1));
					const size_t dstRoom = mChunkCapacity - (dst & (mChunkCapacity - 1));
					const size_t run = std::min({ n, srcRoom, dstRoom });
					std::memmove(at(dst), at(src), run * static_cast<size_t>(mSectorSize));
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
					std::memmove(at(dstBeg), at(srcBeg), run * static_cast<size_t>(mSectorSize));

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
			const auto count = (need + mChunkCapacity - 1) >> mChunkShift;

			mChunks.reserve(mChunks.size() + count);
			for (auto i = 0u; i < count; i++) {
				void* ptr = calloc(mChunkCapacity, mSectorSize); assert(ptr);
				mChunks.emplace_back(ptr);
			}
		}

		FORCE_INLINE size_t capacity() const { return mChunks.size() << mChunkShift; }

		size_t find(const std::byte* dataPtr) const {
			if (!dataPtr || mChunks.empty()) return capacity();

			const size_t stride = static_cast<size_t>(mChunkCapacity) * static_cast<size_t>(mSectorSize);

			{
				const std::byte* base0 = static_cast<const std::byte*>(mChunks.front());
				if (dataPtr >= base0 && dataPtr < base0 + stride) {
					const size_t local = (dataPtr - base0) / static_cast<size_t>(mSectorSize);
					return local;
				}
				const std::byte* baseL = static_cast<const std::byte*>(mChunks.back());
				if (dataPtr >= baseL && dataPtr < baseL + stride) {
					const size_t local = (dataPtr - baseL) / static_cast<size_t>(mSectorSize);
					return (mChunks.size() - 1) * static_cast<size_t>(mChunkCapacity) + local;
				}
			}

			for (size_t ci = 1; ci + 1 < mChunks.size(); ++ci) {
				const std::byte* base = static_cast<const std::byte*>(mChunks[ci]);
				if (dataPtr >= base && dataPtr < base + stride) {
					const size_t local = (dataPtr - base) / static_cast<size_t>(mSectorSize);
					return ci * static_cast<size_t>(mChunkCapacity) + local;
				}
			}

			return capacity();
		}

	public:
		// Exposed for SectorsArray to handle non-trivial copy separately
		template<uint32_t OC>
		void copyCommonData(const ChunksAllocator<OC>& other)  {
			mSectorLayout = other.mSectorLayout;
			mSectorSize = other.mSectorSize;
			mIsSectorTrivial = other.mIsSectorTrivial;
		}

	private:
		FORCE_INLINE static constexpr size_t calcChunkIndex(size_t sectorIdx) { return sectorIdx >> mChunkShift; }
		FORCE_INLINE static constexpr size_t calcInChunkShift(size_t sectorIdx, size_t sectorSize) { return (sectorIdx & (mChunkCapacity - 1)) * sectorSize; }

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
				// Note: for non-trivial copy, caller must handle isAlive arrays separately
				auto from = other.getCursor();
				auto to = getCursor();
				for (auto i = 0u; i < capacity(); i++) {
					// Copy raw bytes only - isAlive handling is external
					std::memcpy(*to, *from, mSectorSize);
					++from;
					++to;
				}
			}
		}

		template<uint32_t OC>
		void move(ChunksAllocator<OC>&& other)  {
			copyCommonData(other);
			if constexpr (OC == ChunkCapacity) {
				// Can't just std::move the vector - allocator points to other.mBin!
				// Transfer chunk pointers manually to our vector (with our allocator/bin)
				mChunks.clear();
				mChunks.reserve(other.mChunks.size());
				for (void* chunk : other.mChunks) {
					mChunks.push_back(chunk);
				}
				other.mChunks.clear();
			}
			else {
				allocate(other.mChunks.size() * mChunkCapacity);
				auto from = other.getCursor();
				auto to = getCursor();
				for (auto i = 0u; i < capacity(); i++) {
					std::memcpy(*to, *from, mSectorSize);
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

		bool mIsSectorTrivial = true;
	};
}
