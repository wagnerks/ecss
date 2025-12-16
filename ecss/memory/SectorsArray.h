#pragma once
/**
 * @file SectorsArray.h
 * @brief SoA-based sparse storage for ECS components with optional thread-safety.
 *
 * Architecture (Sorted Dense + Sparse Index):
 *   - mIds[linearIdx]        -> SectorId (sorted, dense)
 *   - mIsAliveData[linearIdx] -> uint32_t bitfield of alive components
 *   - mSparse[sectorId]      -> linearIdx (sparse map for O(1) lookup)
 *   - ChunksAllocator        -> raw component data at linearIdx
 *
 * Core responsibilities:
 *  - O(1) random access by SectorId through sparse map
 *  - O(1) iteration with excellent cache locality (SoA layout)
 *  - Supports insertion / emplacement / overwrite of component members
 *  - Supports conditional & ranged erasure, deferred erase, and defragmentation
 *  - Exposes multiple iterator flavours: linear, alive-only, ranged
 *
 * Thread safety model (when ThreadSafe=true):
 *  - Read-only APIs acquire a shared (reader) lock
 *  - Mutating APIs acquire a unique (writer) lock
 *  - Structural operations wait on PinCounters before relocating memory
 *
 * @see ecss::Registry (higher-level orchestration)
 */

#include <algorithm>
#include <cassert>
#include <shared_mutex>
#include <utility>
#include <vector>

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
	/// @brief Slot info for fast sparse lookup - stores data pointer and linear index
	/// This enables O(1) component access with a single sparse map lookup
	struct SlotInfo {
		std::byte* data = nullptr;       ///< Direct pointer to sector data (nullptr = not present)
		uint32_t linearIdx = INVALID_IDX; ///< Linear index for isAlive access via dense arrays
		
		FORCE_INLINE bool isValid() const { return data != nullptr; }
		FORCE_INLINE explicit operator bool() const { return isValid(); }
	};

	/// @brief Invalid slot info constant
	inline constexpr SlotInfo INVALID_SLOT{ nullptr, INVALID_IDX };

	template<bool TS>
	struct SparseMap;

	/// @brief Thread-safe sparse map with atomic view for lock-free reads
	template<>
	struct SparseMap<true> {
		FORCE_INLINE SlotInfo find(SectorId id) const {
			auto map = sparseMapView.load(std::memory_order_acquire);
			return id < map.size ? map.data[id] : INVALID_SLOT;
		}

		FORCE_INLINE SlotInfo get(SectorId id) const {
			assert(id < sparseMapView.load().size);
			return sparseMapView.load(std::memory_order_acquire).data[id];
		}

		FORCE_INLINE uint32_t findLinearIdx(SectorId id) const {
			auto map = sparseMapView.load(std::memory_order_acquire);
			return id < map.size ? map.data[id].linearIdx : INVALID_IDX;
		}

		FORCE_INLINE size_t capacity() const { return sparseMapView.load(std::memory_order_acquire).size; }
		
		FORCE_INLINE void storeView() {
			sparseMapView.store(SparseView { sparse.data(), sparse.size() }, std::memory_order_release);
			// Note: drainAll() NOT called here - must be called under unique lock at safe point
		}

		FORCE_INLINE void drainRetired() { bin.drainAll(); }

		FORCE_INLINE void resize(size_t newSize) {
			sparse.resize(newSize, INVALID_SLOT);
			storeView();
		}

		FORCE_INLINE SlotInfo& operator[](SectorId id) { return sparse[id]; }
		FORCE_INLINE const SlotInfo& operator[](SectorId id) const { return sparse[id]; }

	private:
		mutable Memory::RetireBin bin;

	public:
		std::vector<SlotInfo, Memory::RetireAllocator<SlotInfo>> sparse{ Memory::RetireAllocator<SlotInfo>{&bin} };

	private:
		struct SparseView {
			const SlotInfo* data;
			size_t size;
		};
		mutable std::atomic<SparseView> sparseMapView{ SparseView{nullptr, 0} };
	};

	/// @brief Non-thread-safe sparse map (simple vector)
	template<>
	struct SparseMap<false> {
		FORCE_INLINE SlotInfo find(SectorId id) const { return id < sparse.size() ? sparse[id] : INVALID_SLOT; }
		FORCE_INLINE SlotInfo get(SectorId id) const { assert(id < sparse.size()); return sparse[id]; }
		FORCE_INLINE uint32_t findLinearIdx(SectorId id) const { return id < sparse.size() ? sparse[id].linearIdx : INVALID_IDX; }
		FORCE_INLINE size_t capacity() const { return sparse.size(); }
		FORCE_INLINE void storeView() {} // dummy
		FORCE_INLINE void drainRetired() {} // dummy
		FORCE_INLINE void resize(size_t newSize) { sparse.resize(newSize, INVALID_SLOT); }

		FORCE_INLINE SlotInfo& operator[](SectorId id) { return sparse[id]; }
		FORCE_INLINE const SlotInfo& operator[](SectorId id) const { return sparse[id]; }

		std::vector<SlotInfo> sparse;
	};

	/// @brief Atomic view for dense arrays (ids + isAlive) for thread-safe iteration
	template<bool TS>
	struct DenseArrays;

	/// @brief Thread-safe dense arrays with atomic view for lock-free reads
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
			auto otherView = other.view.load(std::memory_order_acquire);
			storeView(otherView.size);
		}
		
		DenseArrays& operator=(const DenseArrays& other) {
			if (this != &other) {
				ids.assign(other.ids.begin(), other.ids.end());
				isAlive.assign(other.isAlive.begin(), other.isAlive.end());
				auto otherView = other.view.load(std::memory_order_acquire);
				storeView(otherView.size);
			}
			return *this;
		}
		
		// Move constructor - create new vectors, move data, bind to OUR bin
		DenseArrays(DenseArrays&& other) noexcept 
			: ids(Memory::RetireAllocator<SectorId>{&bin})
			, isAlive(Memory::RetireAllocator<uint32_t>{&bin}) {
			auto otherView = other.view.load(std::memory_order_acquire);
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
				auto otherView = other.view.load(std::memory_order_acquire);
				storeView(otherView.size);
				other.ids.clear();
				other.isAlive.clear();
				other.storeView(0);
			}
			return *this;
		}

		FORCE_INLINE View loadView() const {
			return view.load(std::memory_order_acquire);
		}

		FORCE_INLINE void storeView(size_t size) {
			view.store(View{ ids.data(), isAlive.data(), size }, std::memory_order_release);
			// Note: drainAll() NOT called here - must be called under unique lock at safe point
		}

		FORCE_INLINE void drainRetired() { bin.drainAll(); }

		FORCE_INLINE void resize(size_t newSize, size_t actualSize) {
			ids.resize(newSize);
			isAlive.resize(newSize, 0);
			storeView(actualSize);
		}

		FORCE_INLINE void reserve(size_t newCapacity) {
			ids.reserve(newCapacity);
			isAlive.reserve(newCapacity);
		}

		FORCE_INLINE void clear(size_t actualSize) {
			ids.clear();
			isAlive.clear();
			storeView(actualSize);
		}

		FORCE_INLINE void shrinkToFit() {
			ids.shrink_to_fit();
			isAlive.shrink_to_fit();
		}

		FORCE_INLINE SectorId& idAt(size_t idx) { return ids[idx]; }
		FORCE_INLINE uint32_t& isAliveAt(size_t idx) { return isAlive[idx]; }
		FORCE_INLINE const SectorId& idAt(size_t idx) const { return ids[idx]; }
		FORCE_INLINE const uint32_t& isAliveAt(size_t idx) const { return isAlive[idx]; }

		mutable Memory::RetireBin bin;
		std::vector<SectorId, Memory::RetireAllocator<SectorId>> ids{ Memory::RetireAllocator<SectorId>{&bin} };
		std::vector<uint32_t, Memory::RetireAllocator<uint32_t>> isAlive{ Memory::RetireAllocator<uint32_t>{&bin} };

	private:
		std::atomic<View> view{ View{nullptr, nullptr, 0} };
	};

	/// @brief Non-thread-safe dense arrays (simple vectors)
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

		FORCE_INLINE void resize(size_t newSize, size_t) {
			ids.resize(newSize);
			isAlive.resize(newSize, 0);
		}

		FORCE_INLINE void reserve(size_t newCapacity) {
			ids.reserve(newCapacity);
			isAlive.reserve(newCapacity);
		}

		FORCE_INLINE void clear(size_t) {
			ids.clear();
			isAlive.clear();
		}

		FORCE_INLINE void shrinkToFit() {
			ids.shrink_to_fit();
			isAlive.shrink_to_fit();
		}

		FORCE_INLINE SectorId& idAt(size_t idx) { return ids[idx]; }
		FORCE_INLINE uint32_t& isAliveAt(size_t idx) { return isAlive[idx]; }
		FORCE_INLINE const SectorId& idAt(size_t idx) const { return ids[idx]; }
		FORCE_INLINE const uint32_t& isAliveAt(size_t idx) const { return isAlive[idx]; }

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

/**
 * @brief RAII pin for a sector to prevent relocation / destruction while in use.
 */
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

/**
 * @brief SoA-based container managing sector data with external id/isAlive arrays.
 *
 * @tparam ThreadSafe If true, operations are synchronized & relocation waits on pins.
 * @tparam Allocator  Allocation policy (e.g. ChunksAllocator).
 */
template<bool ThreadSafe = true, typename Allocator = ChunksAllocator<8192>>
class SectorsArray final {
	template<bool, typename>
	friend class SectorsArray;

	template<bool, typename>
	friend class ecss::Registry;

	template <bool TS, typename Alloc, bool Ranged, typename T, typename ...ComponentTypes>
	friend class ecss::ArraysView;

public:
	/// @brief Slot info returned by iterators
	struct SlotInfo {
		SectorId id;
		uint32_t isAlive;
		std::byte* data;
		size_t linearIdx;

		explicit operator bool() const { return data != nullptr; }
	};

	// ==================== Iterators ====================

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

	/**
	 * @brief Forward iterator over all slots (alive or dead).
	 * Optimized: uses chunk-aware pointer increment for O(1) per-element access.
	 * Uses atomic view snapshots for thread-safe iteration.
	 */
	class Iterator {
	public:
		ITERATOR_COMMON_USING(Iterator)

		Iterator(const SectorsArray* array, size_t idx) 
			: mChunks(array->mAllocator.mChunks.data())
			, mChunksCount(array->mAllocator.mChunks.size())
			, mStride(array->mAllocator.mSectorSize) {
			// Load atomic view snapshot for thread-safe access
			auto view = array->mDenseArrays.loadView();
			mIds = view.ids;
			mIsAlive = view.isAlive;
			mSize = view.size;
			mIdx = std::min(idx, mSize);
			initChunkState();
		}

		FORCE_INLINE value_type operator*() const {
			return SlotInfo{
				mIds[mIdx],
				mIsAlive[mIdx],
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

	template<bool TS = ThreadSafe> Iterator begin() const { TS_GUARD(TS, SHARED, return Iterator(this, 0);); }
	template<bool TS = ThreadSafe> Iterator end()   const { TS_GUARD(TS, SHARED, return Iterator(this, sizeImpl());); }

	/**
	 * @brief Forward iterator skipping slots where component is not alive.
	 * Optimized: uses chunk-aware pointer increment for O(1) per-element access.
	 * When isPacked=true (defragmentSize==0), skipDead is bypassed for maximum speed.
	 * Uses atomic view snapshots for thread-safe iteration.
	 */
	class IteratorAlive {
	public:
		ITERATOR_COMMON_USING(IteratorAlive)

		IteratorAlive(const SectorsArray* array, size_t idx, size_t sz, uint32_t aliveMask, bool isPacked = false)
			: mChunks(array->mAllocator.mChunks.data())
			, mChunksCount(array->mAllocator.mChunks.size())
			, mIdx(idx)
			, mStride(array->mAllocator.mSectorSize)
			, mAliveMask(aliveMask)
			, mIsPacked(isPacked) {
			// Load atomic view snapshot for thread-safe access
			auto view = array->mDenseArrays.loadView();
			mIds = view.ids;
			mIsAlive = view.isAlive;
			mSize = std::min(sz, view.size);
			if (mIsPacked) {
				initChunkState();
			} else {
				skipDeadFast(); // This also syncs data pointer
			}
		}

		FORCE_INLINE value_type operator*() const {
			return SlotInfo{
				mIds[mIdx],
				mIsAlive[mIdx],
				mDataPtr,
				mIdx
			};
		}

		FORCE_INLINE IteratorAlive& operator++() noexcept {
			++mIdx;
			if (mIsPacked) [[likely]] {
				// Fast path: no dead slots, just advance pointer
				advanceDataPtr();
			} else {
				// Slow path: scan isAliveData for next alive, then sync pointer
				skipDeadFast();
			}
			return *this;
		}

		FORCE_INLINE bool operator==(const IteratorAlive& other) const noexcept { return mIdx == other.mIdx; }
		FORCE_INLINE size_t linearIndex() const noexcept { return mIdx; }
		FORCE_INLINE std::byte* rawPtr() const noexcept { return mDataPtr; }
		FORCE_INLINE explicit operator bool() const noexcept { return mIdx < mSize; }

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

		/// @brief Fast skip: only scan isAliveData, update pointer once at the end
		FORCE_INLINE void skipDeadFast() {
			const uint32_t mask = mAliveMask;
			// Batch check 4 elements at a time (cache-friendly)
			while (mIdx + 4 <= mSize) {
				if ((mIsAlive[mIdx] & mask) | (mIsAlive[mIdx+1] & mask) | 
				    (mIsAlive[mIdx+2] & mask) | (mIsAlive[mIdx+3] & mask)) {
					break; // At least one alive in this batch
				}
				mIdx += 4;
			}
			// Fine-grained search for exact position
			while (mIdx < mSize && !(mIsAlive[mIdx] & mask)) {
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

	/// @brief Check if array has no dead slots (defragmentSize == 0)
	template<bool TS = ThreadSafe>
	bool isPacked() const { TS_GUARD(TS, SHARED, return mDefragmentSize == 0;); }

	template<class T, bool TS = ThreadSafe>
	IteratorAlive beginAlive() const { 
		// Note: isPacked=false because we're filtering by a specific component's alive mask,
		// not just checking if any component is alive. mDefragmentSize==0 only means no dead
		// sectors, not that all sectors have this specific component.
		TS_GUARD(TS, SHARED, return IteratorAlive(this, 0, sizeImpl(), getLayoutData<T>().isAliveMask, false);); 
	}
	template<bool TS = ThreadSafe> 
	IteratorAlive endAlive() const { 
		TS_GUARD(TS, SHARED, return IteratorAlive(this, sizeImpl(), sizeImpl(), 0, true);); 
	}

	/**
	 * @brief Iterator over sectors whose IDs fall within specified SectorId ranges.
	 * Converts SectorId ranges to linear index ranges using binary search.
	 * Optimized: chunk-aware pointer access.
	 * Uses atomic view snapshots for thread-safe iteration.
	 */
	class RangedIterator {
	public:
		ITERATOR_COMMON_USING(RangedIterator)

		RangedIterator(const SectorsArray* array, const Ranges<SectorId>& ranges)
			: mChunks(array->mAllocator.mChunks.data())
			, mChunksCount(array->mAllocator.mChunks.size())
			, mStride(array->mAllocator.mSectorSize) {
			// Load atomic view snapshot for thread-safe access
			auto view = array->mDenseArrays.loadView();
			mIds = view.ids;
			mIsAlive = view.isAlive;
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
			return SlotInfo{
				mIds[mIdx],
				mIsAlive[mIdx],
				mDataPtr,
				mIdx
			};
		}

		FORCE_INLINE RangedIterator& operator++() noexcept {
			++mIdx;
			++mInChunkIdx;
			mDataPtr += mStride;
			if (mInChunkIdx >= Allocator::mChunkCapacity) [[unlikely]] {
				mInChunkIdx = 0;
				++mChunkIdx;
				if (mChunkIdx < mChunksCount) {
					mDataPtr = static_cast<std::byte*>(mChunks[mChunkIdx]);
				}
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
		/// @brief Binary search to find first linear index where mIds[idx] >= sectorId
		FORCE_INLINE size_t lowerBound(SectorId sectorId) const {
			size_t left = 0, right = mSize;
			while (left < right) {
				size_t mid = left + (right - left) / 2;
				if (mIds[mid] < sectorId) {
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
		std::vector<std::pair<size_t, size_t>> mLinearRanges;  ///< Linear index ranges [begin, end)
		size_t mIdx = 0;
		size_t mSize = 0;
		size_t mRangeIdx = 0;
		uint16_t mStride = 0;
	};

	template<bool TS = ThreadSafe> 
	RangedIterator beginRanged(const Ranges<SectorId>& ranges) const { 
		TS_GUARD(TS, SHARED, return RangedIterator(this, ranges);); 
	}
	template<bool TS = ThreadSafe> 
	RangedIterator endRanged() const { 
		TS_GUARD(TS, SHARED, return RangedIterator(this, Ranges<SectorId>{});); 
	}

	// ==================== Copy / Move ====================

	template<bool T, typename Alloc>
	SectorsArray(const SectorsArray<T, Alloc>& other) { *this = other; }
	SectorsArray(const SectorsArray& other) { *this = other; }

	template<bool T, typename Alloc>
	SectorsArray& operator=(const SectorsArray<T, Alloc>& other) { if (!isSameAdr(this, &other)) { copy(other); } return *this; }
	SectorsArray& operator=(const SectorsArray& other) { if (this != &other) { copy(other); } return *this; }

	template<bool T, typename Alloc>
	SectorsArray(SectorsArray<T, Alloc>&& other) noexcept { *this = std::move(other); }
	SectorsArray(SectorsArray&& other) noexcept { *this = std::move(other); }

	template<bool T, typename Alloc>
	SectorsArray& operator=(SectorsArray<T, Alloc>&& other) noexcept { if (!isSameAdr(this, &other)) { move(std::move(other)); } return *this; }
	SectorsArray& operator=(SectorsArray&& other) noexcept { if (this != &other) { move(std::move(other)); } return *this; }

private:
	SectorsArray(SectorLayoutMeta* meta, Allocator&& allocator = {}) : mAllocator(std::move(allocator)) {
		mAllocator.init(meta);
		mSparseMap.storeView();
	}

public:
	~SectorsArray() { clear(); shrinkToFit(); }

	template <typename... Types>
	static SectorsArray* create(Allocator&& allocator = {}) {
		static_assert(types::areUnique<Types...>, "Duplicates detected in SectorsArray types!");
		static SectorLayoutMeta* meta = SectorLayoutMeta::create<Types...>();
		return new SectorsArray(meta, std::move(allocator));
	}

	// ==================== Layout helpers ====================

	template<typename T>
	FORCE_INLINE const LayoutData& getLayoutData() const { return getLayout()->template getLayoutData<T>(); }

	FORCE_INLINE SectorLayoutMeta* getLayout() const { return mAllocator.getSectorLayout(); }

	// ==================== Pin API (ThreadSafe builds) ====================

	template<bool TS = true>
	[[nodiscard]] PinnedSector pinSector(SectorId id) const requires(ThreadSafe) {
		TS_GUARD(TS, SHARED, return pinSectorImpl(id););
	}

	template<bool TS = true>
	[[nodiscard]] PinnedSector pinSectorAt(size_t idx) const requires(ThreadSafe) {
		if constexpr (TS) {
			SHARED_LOCK();
			return pinSectorAtImpl(idx);
		} else {
			return pinSectorAtImpl(idx);
		}
	}

	template<bool TS = true>
	[[nodiscard]] PinnedSector pinBackSector() const requires(ThreadSafe) {
		if constexpr (TS) {
			SHARED_LOCK();
			return pinBackSectorImpl();
		} else {
			return pinBackSectorImpl();
		}
	}

	// ==================== Erase & maintenance ====================

	template<bool TS = ThreadSafe>
	void erase(size_t beginIdx, size_t count = 1, bool defragment = false) {
		if constexpr(TS && ThreadSafe) {
			UNIQUE_LOCK();
			if (beginIdx >= sizeImpl()) return;
			mPinsCounter.waitUntilChangeable(mDenseArrays.idAt(beginIdx));
			eraseRangeImpl(beginIdx, count, defragment);
		} else {
			eraseRangeImpl(beginIdx, count, defragment);
		}
	}

	template<bool TS = ThreadSafe>
	Iterator erase(Iterator it, bool defragment = false) noexcept {
		auto idx = it.linearIndex();
		if (idx >= sizeImpl()) return it;
		TS_GUARD_S(TS && ThreadSafe, UNIQUE, mPinsCounter.waitUntilChangeable(mDenseArrays.idAt(idx));, 
			eraseRangeImpl(idx, 1, defragment);
			return Iterator(this, idx);
		);
	}

	void eraseAsync(SectorId id, size_t count = 1) requires(ThreadSafe) {
		// Note: Uses shared_lock for initial lookup, unique_lock for actual modification.
		// Early exit optimization if sector not found. Element deletion may be deferred.
		for (auto i = id; i < id + count; ++i) {
			eraseAsyncImpl(i);
		}
	}

	// ==================== Lookup ====================

	template<bool TS = ThreadSafe> 
	size_t findLinearIdx(SectorId sectorId) const { 
		TS_GUARD(TS && ThreadSafe, SHARED, return findLinearIdxImpl(sectorId)); 
	}

	template<bool TS = ThreadSafe> 
	bool containsSector(SectorId id) const { 
		TS_GUARD(TS && ThreadSafe, SHARED, return containsSectorImpl(id)); 
	}

	template<bool TS = ThreadSafe> 
	std::byte* findSectorData(SectorId id) const { 
		TS_GUARD(TS && ThreadSafe, SHARED, return findSectorDataImpl(id)); 
	}

	/// @brief Find slot info (data pointer + linearIdx) for fast sparse lookup
	/// @return SlotInfo with data pointer and linear index, or INVALID_SLOT if not found
	template<bool TS = ThreadSafe>
	detail::SlotInfo findSlot(SectorId id) const {
		TS_GUARD(TS && ThreadSafe, SHARED, return findSlotImpl(id));
	}

	template<bool TS = ThreadSafe>
	uint32_t getIsAlive(SectorId id) const {
		TS_GUARD(TS && ThreadSafe, SHARED, 
			auto idx = findLinearIdxImpl(id);
			return idx != INVALID_IDX ? mDenseArrays.isAliveAt(idx) : 0;
		);
	}

	template<bool TS = ThreadSafe>
	uint32_t& getIsAliveRef(size_t linearIdx) {
		return mDenseArrays.isAliveAt(linearIdx);
	}

	template<bool TS = ThreadSafe>
	SectorId getId(size_t linearIdx) const {
		return mDenseArrays.idAt(linearIdx);
	}

	// ==================== Capacity ====================

	template<bool TS = ThreadSafe> size_t sparseCapacity() const { TS_GUARD(TS && ThreadSafe, SHARED, return mSparseMap.capacity()); }
	template<bool TS = ThreadSafe> size_t capacity() const { TS_GUARD(TS && ThreadSafe, SHARED, return mAllocator.capacity()); }
	template<bool TS = ThreadSafe> size_t size() const { TS_GUARD(TS && ThreadSafe, SHARED, return sizeImpl()); }
	template<bool TS = ThreadSafe> bool empty() const { TS_GUARD(TS && ThreadSafe, SHARED, return sizeImpl() == 0); }
	template<bool TS = ThreadSafe> void shrinkToFit() { TS_GUARD(TS && ThreadSafe, UNIQUE, shrinkToFitImpl()); }

	template<bool TS = ThreadSafe> void reserve(uint32_t newCapacity) { TS_GUARD(TS && ThreadSafe, UNIQUE, reserveImpl(newCapacity)); }
	template<bool TS = ThreadSafe> void clear() { TS_GUARD_S(TS && ThreadSafe, UNIQUE, mPinsCounter.waitUntilChangeable();, clearImpl();); }

	// ==================== Defragmentation ====================

	template<bool TS = ThreadSafe>
	void defragment() { TS_GUARD_S(TS && ThreadSafe, UNIQUE, mPinsCounter.waitUntilChangeable();, defragmentImpl();); }

	template<bool TS = ThreadSafe>
	void tryDefragment() { TS_GUARD_S(TS && ThreadSafe, UNIQUE, if (mPinsCounter.isArrayLocked()) return;, defragmentImpl();); }

	void incDefragmentSize(uint32_t count = 1) { mDefragmentSize += count; }

	template<bool TS = ThreadSafe> auto getDefragmentationSize() const { TS_GUARD(TS && ThreadSafe, SHARED, return mDefragmentSize;); }
	template<bool TS = ThreadSafe> auto getDefragmentationRatio() const { 
		TS_GUARD(TS && ThreadSafe, SHARED, return mSize ? (static_cast<float>(mDefragmentSize) / static_cast<float>(mSize)) : 0.f;); 
	}
	template<bool TS = ThreadSafe> bool needDefragment() const { 
		TS_GUARD(TS && ThreadSafe, SHARED, return getDefragmentationRatio<false>() > mDefragThreshold;); 
	}
	template<bool TS = ThreadSafe> void setDefragmentThreshold(float threshold) { 
		TS_GUARD(TS && ThreadSafe, UNIQUE, mDefragThreshold = std::max(0.f, std::min(threshold, 1.f));); 
	}

	// ==================== Insert / Emplace ====================

	template<typename T, bool TS = ThreadSafe>
	std::remove_cvref_t<T>* insert(SectorId sectorId, T&& data) {
		TS_GUARD_S(TS && ThreadSafe, UNIQUE, mPinsCounter.waitUntilChangeable(sectorId);, 
			return insertImpl(sectorId, std::forward<T>(data));
		);
	}

	template<typename T, bool TS = ThreadSafe, class... Args>
	T* emplace(SectorId sectorId, Args&&... args) {
		TS_GUARD_S(TS && ThreadSafe, UNIQUE, mPinsCounter.waitUntilChangeable(sectorId);, 
			return emplaceImpl<T>(sectorId, std::forward<Args>(args)...);
		);
	}

	template<typename T, bool TS = ThreadSafe, class... Args>
	T* push(SectorId sectorId, Args&&... args) {
		if constexpr (sizeof...(Args) == 1 && (std::is_same_v<std::remove_cvref_t<Args>, T> && ...)) {
			return insert<Args..., TS>(sectorId, std::forward<Args>(args)...);
		} else {
			return emplace<T, TS>(sectorId, std::forward<Args>(args)...);
		}
	}

	template<bool Lock = true>
	void processPendingErases(bool withDefragment = true) requires(ThreadSafe) {
		if constexpr(Lock) {
			auto lock = std::unique_lock(mtx);
			processPendingErasesImpl(withDefragment);
		} else {
			processPendingErasesImpl(withDefragment);
		}
	}

	// ==================== Lock access (for Registry) ====================
	auto readLock() const requires(ThreadSafe) { return std::shared_lock(mtx); }
	auto writeLock() const requires(ThreadSafe) { return std::unique_lock(mtx); }

private:
	struct Dummy{};
	auto readLock() const requires(!ThreadSafe) { return Dummy{}; }
	auto writeLock() const requires(!ThreadSafe) { return Dummy{}; }

	// ==================== Implementation ====================

	/// @brief Drain all retired memory from allocators (safe point - call under unique lock)
	void drainAllRetired() {
		mDenseArrays.drainRetired();
		mSparseMap.drainRetired();
		mAllocator.mBin.drainAll();
	}

	FORCE_INLINE size_t sizeImpl() const { return mSize; }

	/// @brief Find slot info by sector id (returns data pointer + linearIdx)
	FORCE_INLINE detail::SlotInfo findSlotImpl(SectorId id) const {
		return mSparseMap.find(id);
	}

	FORCE_INLINE uint32_t findLinearIdxImpl(SectorId id) const {
		return mSparseMap.findLinearIdx(id);
	}

	FORCE_INLINE bool containsSectorImpl(SectorId id) const {
		return mSparseMap.find(id).isValid();
	}

	FORCE_INLINE std::byte* findSectorDataImpl(SectorId id) const {
		return mSparseMap.find(id).data;  // Direct pointer access - no second lookup!
	}

	[[nodiscard]] PinnedSector pinSectorImpl(SectorId id) const requires(ThreadSafe) {
		auto slot = mSparseMap.find(id);
		if (!slot) return PinnedSector{};
		return PinnedSector(mPinsCounter, id, slot.data, mDenseArrays.isAliveAt(slot.linearIdx));
	}

	[[nodiscard]] PinnedSector pinSectorAtImpl(size_t idx) const requires(ThreadSafe) {
		if (idx >= sizeImpl()) return PinnedSector{};
		return PinnedSector(mPinsCounter, mDenseArrays.idAt(idx), mAllocator.at(idx), mDenseArrays.isAliveAt(idx));
	}

	[[nodiscard]] PinnedSector pinBackSectorImpl() const requires(ThreadSafe) {
		auto sz = sizeImpl();
		if (sz == 0) return PinnedSector{};
		return PinnedSector(mPinsCounter, mDenseArrays.idAt(sz-1), mAllocator.at(sz-1), mDenseArrays.isAliveAt(sz-1));
	}

	void shrinkToFitImpl() {
		mAllocator.deallocate(mSize, mAllocator.capacity());
		mDenseArrays.shrinkToFit();
	}

	void clearImpl() {
		if (mSize) {
			if (!getLayout()->isTrivial()) {
				for (size_t i = 0; i < mSize; ++i) {
					Sector::destroySectorData(mAllocator.at(i), mDenseArrays.isAliveAt(i), getLayout());
				}
			}
			// Clear sparse map
			std::fill(mSparseMap.sparse.begin(), mSparseMap.sparse.end(), detail::INVALID_SLOT);
			mDenseArrays.clear(0);
			mPendingErase.clear();
			mSize = 0;
			mDefragmentSize = 0;
			// Note: retired memory is drained on destruction, not here
			// to avoid freeing memory while readers might still hold view pointers
		}
	}

	void reserveImpl(uint32_t newCapacity) {
		if (mAllocator.capacity() < newCapacity) {
			mAllocator.allocate(newCapacity);
			mDenseArrays.reserve(newCapacity);
		}
	}

	size_t findInsertPositionImpl(SectorId sectorId, size_t validSize) const {
		if (validSize == 0) return 0;
		if (mDenseArrays.idAt(validSize - 1) < sectorId) return validSize;
		if (mDenseArrays.idAt(0) > sectorId) return 0;

		// Binary search
		size_t left = 0, right = validSize;
		while (right - left > 1) {
			size_t mid = left + (right - left) / 2;
			if (mDenseArrays.idAt(mid) < sectorId) left = mid;
			else right = mid;
		}
		return right;
	}

	size_t acquireSlotImpl(SectorId sectorId) {
		// Expand sparse map if needed
		if (sectorId >= mSparseMap.capacity()) [[unlikely]] {
			mSparseMap.resize(static_cast<size_t>(sectorId) + 1);
		}

		// Check if already exists
		auto& existingSlot = mSparseMap[sectorId];
		if (existingSlot.isValid()) [[unlikely]] {
			return existingSlot.linearIdx;
		}

		// Allocate storage - resize vectors but keep view at old size until data is populated
		mAllocator.allocate(mSize + 1);
		mDenseArrays.resize(mSize + 1, mSize);

		const size_t oldSize = mSize;
		size_t pos = mSize++;

		// Check if we need to insert in the middle (maintain sorted order)
		if (!((pos == 0) || (sectorId > mDenseArrays.idAt(pos - 1)))) [[unlikely]] {
			pos = findInsertPositionImpl(sectorId, oldSize);
			if (pos != mSize - 1) [[unlikely]] {
				shiftRightImpl(pos, 1);
			}
		}

		mDenseArrays.idAt(pos) = sectorId;
		mDenseArrays.isAliveAt(pos) = 0;
		// Store data pointer, isAlive pointer, and linear index for O(1) lookup
		mSparseMap[sectorId] = detail::SlotInfo{ mAllocator.at(pos), static_cast<uint32_t>(pos) };
		
		// Update atomic view to show new size now that data is valid
		mDenseArrays.storeView(mSize);

		return pos;
	}

	void shiftRightImpl(size_t from, size_t count) {
		// Shift data right to make room at 'from'
		const size_t oldSize = mSize - count;
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

		// Shift metadata and update sparse map with new data pointers
		for (size_t i = oldSize + count - 1; i >= from + count; --i) {
			mDenseArrays.idAt(i) = mDenseArrays.idAt(i - count);
			if (getLayout()->isTrivial()) {
				mDenseArrays.isAliveAt(i) = mDenseArrays.isAliveAt(i - count);
			}
			mSparseMap[mDenseArrays.idAt(i)] = detail::SlotInfo{ mAllocator.at(i), static_cast<uint32_t>(i) };
		}
	}

	void shiftLeftImpl(size_t from, size_t count) {
		// Shift data left to fill gap
		if (from < count) return;
		const size_t tail = from > mSize ? 0 : mSize - from;
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

		// Shift metadata and update sparse map with new data pointers
		for (size_t i = from - count; i < from - count + tail; ++i) {
			mDenseArrays.idAt(i) = mDenseArrays.idAt(i + count);
			if (getLayout()->isTrivial()) {
				mDenseArrays.isAliveAt(i) = mDenseArrays.isAliveAt(i + count);
			}
			mSparseMap[mDenseArrays.idAt(i)] = detail::SlotInfo{ mAllocator.at(i), static_cast<uint32_t>(i) };
		}
	}

	template<typename T>
	std::remove_reference_t<T>* insertImpl(SectorId sectorId, T&& data) {
		using U = std::remove_cvref_t<T>;

		size_t pos = acquireSlotImpl(sectorId);
		std::byte* slotData = mAllocator.at(pos);

		const auto& layout = getLayoutData<U>();
		if constexpr (std::is_lvalue_reference_v<T>) {
			return Sector::copyMember<U>(data, slotData, mDenseArrays.isAliveAt(pos), layout);
		} else {
			return Sector::moveMember<U>(std::forward<T>(data), slotData, mDenseArrays.isAliveAt(pos), layout);
		}
	}

	template<typename T, class... Args>
	T* emplaceImpl(SectorId sectorId, Args&&... args) {
		size_t pos = acquireSlotImpl(sectorId);
		std::byte* slotData = mAllocator.at(pos);
		return Sector::emplaceMember<T>(slotData, mDenseArrays.isAliveAt(pos), getLayoutData<T>(), std::forward<Args>(args)...);
	}

	void eraseRangeImpl(size_t beginIdx, size_t count, bool defragment) {
		count = std::min(count, mSize - beginIdx);
		for (size_t i = beginIdx; i < beginIdx + count; ++i) {
			auto id = mDenseArrays.idAt(i);
			if (id < mSparseMap.capacity()) {
				mSparseMap[id] = detail::INVALID_SLOT;
			}
			Sector::destroySectorData(mAllocator.at(i), mDenseArrays.isAliveAt(i), getLayout());
		}

		if (defragment) {
			shiftLeftImpl(beginIdx + count, count);
			mSize -= count;
			mDenseArrays.resize(mSize, mSize);
			mDenseArrays.storeView(mSize);
		} else {
			incDefragmentSize(static_cast<uint32_t>(count));
		}
	}

	void eraseAsyncImpl(SectorId id) requires(ThreadSafe) {
		// Initial check with shared lock - early exit optimization
		{
			SHARED_LOCK();
			if (findLinearIdxImpl(id) == INVALID_IDX) return;
		}

		if (!mPinsCounter.isPinned(id)) {
			UNIQUE_LOCK();
			// Re-read under unique lock to ensure consistency
			auto idx = findLinearIdxImpl(id);
			if (idx == INVALID_IDX) return;
			
			if (mPinsCounter.canMoveSector(id)) {
				mSparseMap[id] = detail::INVALID_SLOT;
				Sector::destroySectorData(mAllocator.at(idx), mDenseArrays.isAliveAt(idx), getLayout());
				incDefragmentSize();
			} else {
				mPendingErase.push_back(id);
			}
		} else {
			UNIQUE_LOCK();
			mPendingErase.push_back(id);
		}
	}

	void processPendingErasesImpl(bool withDefragment) requires(ThreadSafe) {
		if (mPendingErase.empty()) {
			if (needDefragment<false>() && withDefragment) {
				mPinsCounter.waitUntilChangeable();
				defragmentImpl();
			}
			return;
		}

		auto tmp = std::move(mPendingErase);
		std::ranges::sort(tmp);
		tmp.erase(std::ranges::unique(tmp).begin(), tmp.end());

		for (auto id : tmp) {
			auto idx = findLinearIdxImpl(id);
			if (idx == INVALID_IDX) continue;

			if (mPinsCounter.canMoveSector(id)) {
				mSparseMap[id] = detail::INVALID_SLOT;
				Sector::destroySectorData(mAllocator.at(idx), mDenseArrays.isAliveAt(idx), getLayout());
				incDefragmentSize();
			} else {
				mPendingErase.push_back(id);
			}
		}

		if (needDefragment<false>() && withDefragment) {
			mPinsCounter.waitUntilChangeable();
			defragmentImpl();
		}
	}

	void defragmentImpl() {
		if constexpr (ThreadSafe) {
			if (mPinsCounter.isArrayLocked()) return;
		}

		size_t read = 0, write = 0, deleted = 0;
		const size_t n = mSize;
		const bool isTrivial = getLayout()->isTrivial();

		while (read < n) {
			// Skip dead slots
			while (read < n && !Sector::isSectorAlive(mDenseArrays.isAliveAt(read))) {
				mSparseMap[mDenseArrays.idAt(read)] = detail::INVALID_SLOT;
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
						mDenseArrays.idAt(write + i) = mDenseArrays.idAt(runBeg + i);
						mDenseArrays.isAliveAt(write + i) = mDenseArrays.isAliveAt(runBeg + i);
						mSparseMap[mDenseArrays.idAt(write + i)] = detail::SlotInfo{ mAllocator.at(write + i), static_cast<uint32_t>(write + i) };
					}
				} else {
					// Non-trivial types: proper move semantics for each sector
					// moveSectorData handles isAlive state, we just need to update id and sparse map
					for (size_t i = 0; i < runLen; ++i) {
						Sector::moveSectorData(
							mAllocator.at(runBeg + i), mDenseArrays.isAliveAt(runBeg + i),
							mAllocator.at(write + i), mDenseArrays.isAliveAt(write + i),
							getLayout());
						mDenseArrays.idAt(write + i) = mDenseArrays.idAt(runBeg + i);
						mSparseMap[mDenseArrays.idAt(write + i)] = detail::SlotInfo{ mAllocator.at(write + i), static_cast<uint32_t>(write + i) };
					}
				}
			}
			write += runLen;
		}

		mSize -= deleted;
		mDefragmentSize -= std::min(mDefragmentSize, static_cast<uint32_t>(deleted));
		mDenseArrays.resize(mSize, mSize);
		mDenseArrays.storeView(mSize);
		shrinkToFitImpl();
	}

	template<bool T, typename Alloc>
	void copy(const SectorsArray<T, Alloc>& other) {
		if constexpr (ThreadSafe || T) {
			auto lock = writeLock();
			auto otherLock = other.readLock();
			mPinsCounter.waitUntilChangeable();
			copyImpl(other);
		} else {
			copyImpl(other);
		}
	}

	template<bool T, typename Alloc>
	void copyImpl(const SectorsArray<T, Alloc>& other) {
		clearImpl();
		shrinkToFitImpl();

		mSize = other.mSize;
		
		// Copy layout metadata first - required before we can check isTrivial()
		mAllocator.copyCommonData(other.mAllocator);
		
		// For non-trivial types, we must NOT use ChunksAllocator's memcpy-based copy!
		// Instead: allocate chunks, then properly copy-construct each member via layout.
		if (getLayout()->isTrivial()) {
			mAllocator = other.mAllocator; // Trivial: memcpy is safe
		} else {
			// Non-trivial: just allocate storage, copy will be done via copySectorData below
			mAllocator.allocate(mSize);
		}
		
		// Copy dense arrays data
		mDenseArrays.resize(mSize, mSize);
		for (size_t i = 0; i < mSize; ++i) {
			mDenseArrays.idAt(i) = other.mDenseArrays.idAt(i);
			mDenseArrays.isAliveAt(i) = other.mDenseArrays.isAliveAt(i);
		}
		mDenseArrays.storeView(mSize);
		
		// For non-trivial types, properly copy-construct each sector's members
		if (!getLayout()->isTrivial()) {
			for (size_t i = 0; i < mSize; ++i) {
				uint32_t srcIsAlive = other.mDenseArrays.isAliveAt(i);
				uint32_t dstIsAlive = 0;
				Sector::copySectorData(
					other.mAllocator.at(i), srcIsAlive,
					mAllocator.at(i), dstIsAlive,
					getLayout());
				mDenseArrays.isAliveAt(i) = dstIsAlive;
			}
			mDenseArrays.storeView(mSize);
		}
		
		mSparseMap.resize(other.mSparseMap.capacity());
		for (size_t i = 0; i < mSize; ++i) {
			mSparseMap[mDenseArrays.idAt(i)] = detail::SlotInfo{ mAllocator.at(i), static_cast<uint32_t>(i) };
		}

		mDefragmentSize = other.mDefragmentSize;
		mDefragThreshold = other.mDefragThreshold;
	}

	template<bool T, typename Alloc>
	void move(SectorsArray<T, Alloc>&& other) {
		if constexpr (ThreadSafe || T) {
			auto lock = writeLock();
			auto otherLock = other.writeLock();
			mPinsCounter.waitUntilChangeable();
			other.mPinsCounter.waitUntilChangeable();
			moveImpl(std::move(other));
		} else {
			moveImpl(std::move(other));
		}
	}

	template<bool T, typename Alloc>
	void moveImpl(SectorsArray<T, Alloc>&& other) {
		clearImpl();
		shrinkToFitImpl();

		mSize = other.mSize;
		mAllocator = std::move(other.mAllocator);
		
		// Move dense arrays data (copy for cross-template moves)
		mDenseArrays.resize(mSize, mSize);
		for (size_t i = 0; i < mSize; ++i) {
			mDenseArrays.idAt(i) = other.mDenseArrays.idAt(i);
			mDenseArrays.isAliveAt(i) = other.mDenseArrays.isAliveAt(i);
		}
		mDenseArrays.storeView(mSize);
		
		mSparseMap.resize(other.mSparseMap.capacity());
		for (size_t i = 0; i < mSize; ++i) {
			mSparseMap[mDenseArrays.idAt(i)] = detail::SlotInfo{ mAllocator.at(i), static_cast<uint32_t>(i) };
		}

		mDefragmentSize = other.mDefragmentSize;
		mDefragThreshold = other.mDefragThreshold;
		
		other.mSize = 0;
		other.mDenseArrays.clear(0);
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

	mutable std::conditional_t<ThreadSafe, std::shared_mutex, Dummy> mtx;
	mutable std::conditional_t<ThreadSafe, Threads::PinCounters, Dummy> mPinsCounter;

	std::vector<SectorId> mPendingErase;

	size_t mSize = 0;
	float mDefragThreshold = 0.2f;
	uint32_t mDefragmentSize = 0;
};

#undef SHARED_LOCK
#undef UNIQUE_LOCK
#undef TS_GUARD
#undef TS_GUARD_S
#undef ITERATOR_COMMON_USING

} // namespace ecss::Memory
