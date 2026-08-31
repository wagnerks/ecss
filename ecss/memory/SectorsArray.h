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
	/// @brief Result of a sparse lookup: the sector data address plus its linear index.
	/// This is composed on demand from the stored index and the chunk snapshot -- it is not
	/// what the sparse table holds (that is a bare uint32_t; see SparseMap<true>).
	struct SlotInfo {
		std::byte* data = nullptr;        ///< Direct pointer (nullptr = not present), atomically accessed
		uint32_t linearIdx = INVALID_IDX; ///< Linear index for isAlive access
		
		FORCE_INLINE bool isValid() const { return data != nullptr; }
		FORCE_INLINE explicit operator bool() const { return isValid(); }
	};
	static_assert(std::atomic<std::byte*>::is_always_lock_free, "Pointer atomics must be lock-free for performance");

	/// @brief Invalid slot info constant
	inline constexpr SlotInfo INVALID_SLOT{ nullptr, INVALID_IDX };

	using ecss::cpuRelax;

	template<bool TS>
	struct SparseMap;

	/// @brief Thread-safe sparse map with atomic view for lock-free reads
	/// Writer: store data (release) then store linearIdx (release) -- single consistent update.
	/// Reader: load linearIdx (acquire), load data (acquire), re-load linearIdx (acquire).
	/// If linearIdx unchanged, the pair is consistent. Otherwise retry (seqlock pattern).
	/// On the hot path (no concurrent write) this is one load + one branch, never retries.
	/// @brief Thread-safe sparse map: sector id -> linear index.
	///
	/// The entry used to be {data pointer, linearIdx} -- 16 bytes, which cannot be read
	/// atomically, so every lookup ran a per-slot seqlock (load idx, load data, re-load idx,
	/// retry) and every update wrote three times. Storing the index alone makes an entry a
	/// single 4-byte atomic: one load to read, one store to write, no retry loop. It also
	/// quarters the table, which is what actually decides the cost of a random lookup once
	/// it stops fitting in cache (measured -40% at 200k ids, -17% at 1M).
	///
	/// The data pointer is recovered from the chunk snapshot instead, which is one extra
	/// dependent load from a table that is a few hundred bytes and always hot.
	template<>
	struct SparseMap<true> {
		/// @brief Consistent {table, size} pair handed to readers by loadView().
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

		/// @brief Seqlock snapshot of {table, size}; the table itself is retire-allocated,
		/// so a snapshot stays readable after a resize.
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

		/// @brief Resize and publish (caller holds the write lock).
		FORCE_INLINE void resize(size_t newSize) {
			sparse.resize(newSize, INVALID_IDX);
			storeView();
		}

		/// @brief Point @p id at linear index @p idx, or INVALID_IDX to clear it.
		/// A single release store: the entry is one word, so there is nothing to tear.
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

	/// @brief Non-thread-safe sparse map: sector id -> linear index.
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

		/// @brief Seqlock snapshot of {ids, isAlive, size}.
		/// Hot path (no concurrent writer): 2 x 8B atomic loads (seq) + 3 relaxed loads + 1 compare.
		/// On x86-64 all loads compile to plain MOVs and the acquire fence is a no-op.
		/// Replaces the previous std::atomic<View> which was mutex-backed (View is 24 bytes,
		/// exceeds the 16-byte lock-free boundary on every mainstream platform).
		/// Data fields are std::atomic<> with relaxed ordering -- the seq counter provides all
		/// real synchronization; atomics just tell the compiler/TSan these concurrent accesses
		/// are intentional (torn reads are detected and retried via the seq comparison).
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

		/// @brief Publish a new view. Caller must hold the SectorsArray write lock so
		/// storeView() is never called concurrently with itself. loadView() is lock-free
		/// and may run concurrently with storeView().
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
 * @brief RAII structural hold: while one is alive, no sector in the array may be relocated.
 *
 * Weaker and cheaper than a pin. A pin says "leave this sector alone" and is counted per
 * sector; a hold says "do not compact the array" and is counted per thread, so holders on
 * different threads do not share a cache line. Iteration needs the second, not the first --
 * which is why views used to pin the back sector and made every thread contend on it.
 */
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

	/// @brief Relaxed atomic load of an alive-bit word. Compiles to a plain MOV on x86-64
	/// and a plain LDR on ARM64 -- identical codegen to a raw read. Exists purely to
	/// satisfy the C++ memory model when the word may be concurrently written by
	/// Sector::markAlive<true>/markNotAlive<true>.
	/// Declared static so nested iterator classes can call it without an enclosing instance.
	static FORCE_INLINE uint32_t loadAliveRelaxed(const uint32_t* p, size_t i) noexcept {
		if constexpr (ThreadSafe) {
			return std::atomic_ref<uint32_t>(const_cast<uint32_t&>(p[i]))
				.load(std::memory_order_relaxed);
		} else {
			// No concurrent writer can exist, and an atomic load -- even relaxed -- stops the
			// vectoriser from touching the surrounding scan.
			return p[i];
		}
	}

	/// @brief Acquire load of an alive-bit word. Pairs with the release fetch_or in
	/// Sector::markAlive<true>, so a reader observing a set bit is guaranteed to see
	/// the fully-constructed component bytes written before the bit was set.
	/// x86-64: plain MOV (all loads are acquire). ARM64: LDAR instead of LDR.
	static FORCE_INLINE uint32_t loadAliveAcquire(const uint32_t* p, size_t i) noexcept {
		if constexpr (ThreadSafe) {
			return std::atomic_ref<uint32_t>(const_cast<uint32_t&>(p[i]))
				.load(std::memory_order_acquire);
		} else {
			return p[i];
		}
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

	/**
	 * @brief Forward iterator over all slots (alive or dead).
	 * Optimized: uses chunk-aware pointer increment for O(1) per-element access.
	 * Uses atomic view snapshots for thread-safe iteration.
	 */
	class Iterator {
	public:
		ITERATOR_COMMON_USING(Iterator)

		Iterator(const SectorsArray* array, size_t idx) {
			// Both snapshots are lock-free; the chunk table is published by a seqlock so it
			// can be read without the array shared lock (see ChunksAllocator::loadChunks).
			const auto chunks = array->mAllocator.loadChunks();
			mChunks = chunks.chunks;
			mChunksCount = chunks.count;
			mStride = chunks.sectorSize;

			auto view = array->mDenseArrays.loadView();
			mIds = view.ids;
			mIsAlive = view.isAlive;
			mSize = view.size;
			mIdx = std::min(idx, mSize);
			initChunkState();
		}

		FORCE_INLINE value_type operator*() const {
			// Acquire-load the alive word so a set bit synchronizes-with the
			// release in Sector::markAlive<true>, making the component bytes visible.
			return SlotInfo{
				mIds[mIdx],
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
			: mIdx(idx)
			, mAliveMask(aliveMask)
			, mIsPacked(isPacked) {
			const auto chunks = array->mAllocator.loadChunks();
			mChunks = chunks.chunks;
			mChunksCount = chunks.count;
			mStride = chunks.sectorSize;

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
			// Acquire-load the alive word so a set bit synchronizes-with the
			// release in Sector::markAlive<true>, making the component bytes visible.
			return SlotInfo{
				mIds[mIdx],
				loadAliveAcquire(mIsAlive, mIdx),
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

		FORCE_INLINE void becomeEnd() noexcept {
			mIdx = mSize;
			mDataPtr = nullptr;
		}

		/// @brief Jump to the first alive sector whose id is >= @p minId (or end).
		FORCE_INLINE void advanceToId(SectorId minId) {
			size_t lo = mIdx;
			size_t hi = mSize;
			while (lo < hi) {
				const size_t mid = lo + (hi - lo) / 2;
				if (mIds[mid] < minId) {
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

		/// @brief Fast skip: only scan isAliveData, update pointer once at the end.
		/// Uses relaxed atomic loads -- same codegen as plain loads on every mainstream
		/// target, but tells the compiler (and TSan) that concurrent writers are allowed.
		/// Acquire ordering is deferred to operator*()'s yield point so the scan itself
		/// costs nothing extra.
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

	/// @brief Check if array has no dead slots (defragmentSize == 0)
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

	/**
	 * @brief Iterator over sectors whose IDs fall within specified SectorId ranges.
	 * Converts SectorId ranges to linear index ranges using binary search.
	 * Optimized: chunk-aware pointer access.
	 * Uses atomic view snapshots for thread-safe iteration.
	 */
	class RangedIterator {
	public:
		ITERATOR_COMMON_USING(RangedIterator)

		RangedIterator(const SectorsArray* array, const Ranges<SectorId>& ranges) {
			const auto chunks = array->mAllocator.loadChunks();
			mChunks = chunks.chunks;
			mChunksCount = chunks.count;
			mStride = chunks.sectorSize;

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
			// Acquire-load the alive word so a set bit synchronizes-with the
			// release in Sector::markAlive<true>, making the component bytes visible.
			return SlotInfo{
				mIds[mIdx],
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
	SectorsArray(SectorLayoutMeta* meta, Allocator&& allocator = {}) : mAllocator(std::move(allocator)) {
		configureReclamation();
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

	/// @brief Block compaction of this array for as long as the returned object lives.
	/// Use this, not a pin, when the point is "do not move things", not "leave this sector".
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

	/// @brief Find slot info (data pointer + linearIdx) for fast sparse lookup
	/// @return SlotInfo with data pointer and linear index, or INVALID_SLOT if not found
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

	/// @brief Thread-safe alive-word read that routes through the seqlock snapshot.
	/// Unlike getIsAliveRef, this never dereferences the live std::vector, so it is
	/// safe against concurrent push_back reallocation -- the vector's internal
	/// _M_start field would otherwise race with the reader's non-atomic read of it.
	/// Old isAlive buffers remain valid because RetireAllocator defers their free.
	/// Returns 0 if linearIdx is outside the snapshot (newly allocated slot not yet
	/// published), which callers treat as "not alive".
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
		return getDefragmentationRatio<false>() > mDefragThreshold;
	}
	template<bool TS = ThreadSafe> void setDefragmentThreshold(float threshold) { 
		TS_GUARD(TS && ThreadSafe, UNIQUE, mDefragThreshold = std::max(0.f, std::min(threshold, 1.f));); 
	}

	// ==================== Retired Memory Management (ThreadSafe only) ====================

	/**
	 * @brief Process one tick of the grace period for retired memory.
	 * 
	 * Call this once per frame/update cycle. Memory blocks that have waited
	 * the full grace period (default 3 ticks) will be freed.
	 * 
	 * This is safe to call while iterators may be active - only sufficiently
	 * old memory (older than grace period) will be freed.
	 * 
	 * @note In non-thread-safe mode there is no grace period, so memory is already freed
	 *       as it is released and this is a no-op returning zero. It stays callable so
	 *       that a frame loop does not have to branch on the mode.
	 * 
	 * @return Number of memory blocks freed this tick
	 */
	size_t tick() { return tickRetired(); }

	/**
	 * @brief Set the grace period (in ticks) before retired memory is freed.
	 * 
	 * Higher values = safer but more memory usage.
	 * Lower values = less memory but risk of use-after-free if iterators live long.
	 * 
	 * Default is 3 ticks, which is safe for typical game loops where
	 * iterators don't survive across frames.
	 * 
	 * @note The non-thread-safe build fixes this at zero and ignores the setter: with no
	 *       lock-free readers there is nothing for retired memory to outlive.
	 * 
	 * @param ticks Number of tick() calls before memory is freed (0 = free on release)
	 */
	void setGracePeriod(uint32_t ticks) {
		if constexpr (ThreadSafe) { setRetireGracePeriod(ticks); }
	}

	// ==================== Insert / Emplace ====================
	//
	// These are noexcept on purpose, matching Registry::addComponent: a component
	// constructor that throws terminates the process rather than propagating.
	//
	// The alternative is worse here, not better. emplaceMember() destroys the previous value
	// before constructing the new one and only marks the slot alive afterwards, so an escaping
	// exception would leave the liveness bit set over destroyed storage -- and the next read,
	// erase or defragment would run the destructor a second time. Verified: a throwing
	// constructor reached through these entry points took the live-object count to -1, i.e. a
	// double destruction, which is a double free for any component that owns heap memory.
	//
	// Terminating keeps that state unobservable at no runtime cost. Making the mutation path
	// roll back instead would mean clearing the liveness bit before destroying the old value,
	// which is pure overhead on the hot path in the normal case.

	/// @brief Fast path for overwriting a component that is already present and alive.
	///
	/// Such a write changes no structure at all: no slot acquisition, no shift, no liveness
	/// transition, no view publication -- it is a plain assignment over existing bytes. Every
	/// structural mutation in this class runs under the unique lock, so holding the *shared*
	/// lock is enough to keep the sector from being relocated or destroyed underneath the
	/// write. That lets N threads overwrite N different entities concurrently instead of
	/// serialising them all on one writer lock.
	///
	/// Deliberately restricted to trivially copyable components: for a non-trivial type an
	/// overwrite is destroy-then-construct, and two threads doing that at once would
	/// double-destroy. Those keep the structural path and its exclusive lock.
	///
	/// It also deliberately does not touch the alive bit. copyMember() clears it before
	/// constructing and sets it after, which would make a live component blink out of
	/// existence for concurrent readers; for an already-live trivial component there is no
	/// liveness transition to publish.
	///
	/// @note Two threads overwriting the *same* component now race on its value rather than
	///       being serialised. For a trivially copyable type that is the same race a caller
	///       already has when writing through a pointer obtained from a view or a pin.
	/// @return the written component, or nullptr when the fast path does not apply and the
	///         caller must fall back to the structural path.
	template<typename U, typename Write>
	U* tryOverwriteShared(SectorId sectorId, Write&& write) requires(ThreadSafe) {
		static_assert(std::is_trivially_copyable_v<U>, "fast path is only sound for trivial components");

		const auto& layout = getLayoutData<U>();
		auto lock = readLock();

		const auto idx = mSparseMap.findIdx(sectorId);
		if (idx == INVALID_IDX) {
			return nullptr;
		}
		// Must already be alive: publishing a *new* component flips a liveness bit and moves
		// the fragmentation bookkeeping, both of which belong to the structural path.
		if (!Sector::isAlive(loadAliveWord<ThreadSafe>(idx), layout.isAliveMask)) {
			return nullptr;
		}

		auto* dst = reinterpret_cast<U*>(dataAt(idx) + layout.offset);
		write(dst);
		return dst;
	}

	template<typename T, bool TS = ThreadSafe>
	std::remove_cvref_t<T>* insert(SectorId sectorId, T&& data) noexcept {
		using U = std::remove_cvref_t<T>;
		if constexpr (TS && ThreadSafe) {
			if constexpr (std::is_trivially_copyable_v<U>) {
				// Copy, never move: on a miss `data` must still be intact for the fallback.
				if (auto* fast = tryOverwriteShared<U>(sectorId, [&](U* dst) { *dst = data; })) {
					return fast;
				}
			}
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
			// Args are bound as lvalues here so a miss leaves them forwardable below.
			if constexpr (std::is_trivially_copyable_v<T> && std::is_constructible_v<T, Args&...>) {
				if (auto* fast = tryOverwriteShared<T>(sectorId, [&](T* dst) { *dst = T(args...); })) {
					return fast;
				}
			}
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

	/// @brief Append-only bulk insert. Each *it yields a pair-like {SectorId, C}.
	/// Preconditions: ids are strictly ascending AND greater than every id already stored
	/// (pure append -- no overwrite, no middle insert). Reserves once and publishes the dense
	/// view once, skipping the per-element existence check / insert-position search / view
	/// publish that addComponent() pays. In the TS build it also batches the write lock, the
	/// pin wait and the dense-view publish across the whole range.
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
			if (!mHasPendingErase.load(std::memory_order_acquire) && !needDefragment<false>()) [[likely]] {
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
				exclusiveWhenQuiescent([&] { defragmentImpl(); });
			} else {
				defragmentImpl(); // caller owns the lock and the quiescence precondition
			}
		}
	}

	// ==================== Lock access (for Registry) ====================
	auto readLock() const requires(ThreadSafe) { return std::shared_lock(mtx); }
	auto writeLock() const requires(ThreadSafe) { return std::unique_lock(mtx); }

	/// @brief RAII publication of "sector storage is changing" around a writer body.
	///
	/// Pins are taken with no lock at all, so a pin can be published just after a writer has
	/// checked that nothing is pinned. Both sides therefore publish before they check: the
	/// reader pins and then re-reads the epoch, the writer bumps the epoch and then re-reads
	/// the pin state. Whichever went second sees the other, so a validated pin can never be
	/// live across a structural change, and a writer never relocates or destroys a sector
	/// that a reader has already committed to.
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

	/// @brief Run @p fn under the write lock once sector @p sectorId carries no pins.
	///        For operations confined to that one sector (in-place destroy / overwrite).
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

	/// @brief Run @p fn under the write lock once no sector at all is pinned.
	///        Required by anything that relocates sectors (shift, defragment, clear, copy).
	template<typename Fn>
	auto exclusiveWhenQuiescent(Fn&& fn) requires(ThreadSafe) {
		for (;;) {
			mPinsCounter.waitUntilQuiescent();
			auto lock = writeLock();
			StructuralEdit edit(*this);
			if (!mPinsCounter.hasAnyPins()) {
				return std::forward<Fn>(fn)();
			}
		}
	}

	/// @brief Acquire (or reuse) the slot for @p sectorId and run @p fn(linearIdx) on it.
	///
	/// Appends and overwrites only need the target sector unpinned. A middle insert also
	/// shifts every following sector, so it additionally needs quiescence -- but that is
	/// only discoverable under the lock, hence the two-tier retry: tryAcquireSlotImpl
	/// declines with INVALID_IDX and the wait happens after the lock is released.
	template<typename Fn>
	auto exclusiveForInsert(SectorId sectorId, Fn&& fn) requires(ThreadSafe) {
		for (;;) {
			mPinsCounter.waitUntilChangeable(sectorId);
			{
				auto lock = writeLock();
				StructuralEdit edit(*this);
				if (!mPinsCounter.canMoveSector(sectorId)) {
					continue; // re-pinned between the wait and the lock
				}
				const auto pos = tryAcquireSlotImpl(sectorId);
				if (pos != kNoSlot) {
					return fn(pos);
				}
			}
			mPinsCounter.waitUntilQuiescent();
		}
	}

private:
	struct Dummy{};
	auto readLock() const requires(!ThreadSafe) { return Dummy{}; }
	auto writeLock() const requires(!ThreadSafe) { return Dummy{}; }

	// ==================== Implementation ====================

	/// @brief Set the reclamation policy for this array's bins. Every constructor calls it,
	/// including the copy and move ones: RetireBin's copy constructor does not carry the
	/// grace period across, so an array built from another would otherwise silently get the
	/// thread-safe default back.
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

	/// @brief Drain all retired memory from allocators (safe point - call under unique lock)
	void drainAllRetired() {
		mDenseArrays.drainRetired();
		mSparseMap.drainRetired();
		mAllocator.mBin.drainAll();
	}

	/**
	 * @brief Process one tick of the grace period for retired memory.
	 * 
	 * Call this once per frame/update cycle to gradually free old memory.
	 * Memory is freed only after the grace period expires (default 3 ticks),
	 * giving concurrent iterators time to finish using old buffers.
	 * 
	 * This is safe to call while iterators are active - only sufficiently
	 * old memory will be freed.
	 * 
	 * @return Total number of memory blocks freed this tick
	 */
	size_t tickRetired() {
		size_t freed = 0;
		freed += mDenseArrays.tickRetired();
		freed += mSparseMap.tickRetired();
		freed += mAllocator.mBin.tick();
		return freed;
	}

	/// @brief Set grace period (in ticks) before retired memory is freed
	void setRetireGracePeriod(uint32_t ticks) {
		mDenseArrays.setGracePeriod(ticks);
		mSparseMap.setGracePeriod(ticks);
		mAllocator.mBin.setGracePeriod(ticks);
	}

	FORCE_INLINE size_t sizeImpl() const { return mSize.load(std::memory_order_relaxed); }

public:
	/// @brief Sector data address for a linear index, read through the chunk snapshot.
	/// Loops should hoist loadChunks() and use the two-argument form instead.
	FORCE_INLINE std::byte* dataAt(uint32_t linearIdx) const {
		return Allocator::atView(mAllocator.loadChunks(), linearIdx);
	}
	FORCE_INLINE std::byte* dataAt(const typename Allocator::ChunksView& chunks, uint32_t linearIdx) const {
		return Allocator::atView(chunks, linearIdx);
	}
	FORCE_INLINE auto loadChunks() const { return mAllocator.loadChunks(); }

private:

	/// @brief Find slot info by sector id (returns data pointer + linearIdx)
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

	/// @brief Take a pin with no lock, then confirm the structure did not move under it.
	///
	/// The pin is published first and the epoch re-read after, so either the writer sees the
	/// pin and backs off, or we see its odd/changed epoch and retry. Everything read here
	/// comes from a lock-free snapshot, so a concurrent reallocation cannot be observed torn.
	/// @brief Give a waiting writer a bounded window to acquire before pinning again.
	/// Bounded on purpose: the caller may already hold a pin the writer is waiting for, so
	/// blocking here would deadlock. See PinCounters::writersWaiting.
	FORCE_INLINE void yieldToWriters() const {
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

			PinnedSector pin(mPinsCounter, view.ids[idx], dataAt(static_cast<uint32_t>(idx)),
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

			PinnedSector pin(mPinsCounter, view.ids[idx], dataAt(static_cast<uint32_t>(idx)),
			                 loadAliveAcquire(view.isAlive, idx));
			if (mStructEpoch.load(std::memory_order_seq_cst) == epoch) { return pin; }
		}
	}

	void shrinkToFitImpl() {
		mAllocator.deallocate(sizeImpl(), mAllocator.capacity());
		mDenseArrays.shrinkToFit();
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

	/// @return lower_bound: the first linear index whose id is >= @p sectorId.
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

	/// @brief Sentinel returned by tryAcquireSlotImpl when the caller must retry.
	static constexpr size_t kNoSlot = static_cast<size_t>(INVALID_IDX);

	/// @brief Blocking slot acquisition for callers that already own the quiescence
	///        precondition (non-thread-safe builds, and TS=false explicit-lock paths).
	size_t acquireSlotImpl(SectorId sectorId) {
		const auto pos = tryAcquireSlotImpl(sectorId);
		// tryAcquireSlotImpl only declines when a middle insert would shift sectors while
		// pins are live. Reaching that here means the caller took the write lock without
		// establishing quiescence first -- see exclusiveForInsert / exclusiveWhenQuiescent.
		assert(pos != kNoSlot && "insert would relocate pinned sectors; caller must establish quiescence");
		return pos;
	}

	/// @brief Acquire the linear slot for @p sectorId, or kNoSlot if the insert would have
	///        to relocate sectors while some are pinned (caller waits and retries).
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
			mDenseArrays.pushBack(sectorId, 0);
			mSparseMap.set(sectorId, static_cast<uint32_t>(pos));
			mSize.store(sz + 1, std::memory_order_relaxed);
			mDenseArrays.storeView(sz + 1);
			return pos;
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

		mDenseArrays.idAt(insertPos) = sectorId;
		mDenseArrays.isAliveAt(insertPos) = 0;
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
			mDenseArrays.idAt(i) = mDenseArrays.idAt(i - count);
			if (getLayout()->isTrivial()) {
				mDenseArrays.isAliveAt(i) = mDenseArrays.isAliveAt(i - count);
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
			mDenseArrays.idAt(i) = mDenseArrays.idAt(i + count);
			if (getLayout()->isTrivial()) {
				mDenseArrays.isAliveAt(i) = mDenseArrays.isAliveAt(i + count);
			}
			mSparseMap.set(mDenseArrays.idAt(i), static_cast<uint32_t>(i));
		}
	}

	/// @brief Store @p data into an already-acquired slot.
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

	/// @brief Construct T in an already-acquired slot.
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

		// ids are ascending -> the last one is the max; one pass to size the reservation.
		size_t count = 0;
		SectorId maxId = 0;
		for (auto it = first; it != last; ++it) { maxId = it->first; ++count; }

		const size_t base = sizeImpl();
		mAllocator.allocate(base + count);          // reserve chunks once
		mDenseArrays.reserve(base + count);         // reserve dense arrays once
		if (static_cast<size_t>(maxId) >= mSparseMap.capacity()) {
			mSparseMap.resize(static_cast<size_t>(maxId) + 1);
		}

		size_t pos = base;
		for (auto it = first; it != last; ++it, ++pos) {
			const SectorId id = it->first;
			mDenseArrays.pushBack(id, 0);
			std::byte* slot = mAllocator.at(pos);
			Sector::emplaceMember<C, ThreadSafe>(slot, mDenseArrays.isAliveAt(pos), layout, it->second);
			mSparseMap.set(id, static_cast<uint32_t>(pos));
		}
		mSize.store(base + count, std::memory_order_relaxed);
		mDenseArrays.storeView(base + count);       // publish the whole batch at once
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
		// Initial check with shared lock - early exit optimization
		{
			SHARED_LOCK();
			if (findLinearIdxImpl(id) == INVALID_IDX) return;
		}

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

	/// @brief Drain the deferred-erase queue (in place, never blocks).
	/// @return true if the array now wants compaction; the caller runs it once quiescent.
	bool processPendingErasesImpl() requires(ThreadSafe) {
		if (!mPendingErase.empty()) {
			auto tmp = std::move(mPendingErase);
			mPendingErase.clear(); // moved-from vectors are only "valid but unspecified"
			mHasPendingErase.store(false, std::memory_order_release);
			std::ranges::sort(tmp);
			tmp.erase(std::ranges::unique(tmp).begin(), tmp.end());

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
						mDenseArrays.idAt(write + i) = mDenseArrays.idAt(runBeg + i);
						mDenseArrays.isAliveAt(write + i) = mDenseArrays.isAliveAt(runBeg + i);
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
						mDenseArrays.idAt(write + i) = mDenseArrays.idAt(runBeg + i);
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
			mDenseArrays.idAt(i) = other.mDenseArrays.idAt(i);
			mDenseArrays.isAliveAt(i) = other.mDenseArrays.isAliveAt(i);
		}
		mDenseArrays.storeView(otherSz);
		
		if (!getLayout()->isTrivial()) {
			for (size_t i = 0; i < otherSz; ++i) {
				uint32_t srcIsAlive = other.mDenseArrays.isAliveAt(i);
				uint32_t dstIsAlive = 0;
				Sector::copySectorData(
					other.mAllocator.at(i), srcIsAlive,
					mAllocator.at(i), dstIsAlive,
					getLayout());
				mDenseArrays.isAliveAt(i) = dstIsAlive;
			}
			mDenseArrays.storeView(otherSz);
		}
		
		mSparseMap.resize(other.mSparseMap.capacity());
		for (size_t i = 0; i < otherSz; ++i) {
			mSparseMap.set(mDenseArrays.idAt(i), static_cast<uint32_t>(i));
		}

		mDefragmentSize.store(other.mDefragmentSize.load(std::memory_order_relaxed), std::memory_order_relaxed);
		mDefragThreshold = other.mDefragThreshold;
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
		clearImpl();
		shrinkToFitImpl();

		auto otherSz = other.sizeImpl();
		mSize.store(otherSz, std::memory_order_relaxed);
		mAllocator = std::move(other.mAllocator);
		
		mDenseArrays.resize(otherSz, otherSz);
		for (size_t i = 0; i < otherSz; ++i) {
			mDenseArrays.idAt(i) = other.mDenseArrays.idAt(i);
			mDenseArrays.isAliveAt(i) = other.mDenseArrays.isAliveAt(i);
		}
		mDenseArrays.storeView(otherSz);
		
		mSparseMap.resize(other.mSparseMap.capacity());
		for (size_t i = 0; i < otherSz; ++i) {
			mSparseMap.set(mDenseArrays.idAt(i), static_cast<uint32_t>(i));
		}

		mDefragmentSize.store(other.mDefragmentSize.load(std::memory_order_relaxed), std::memory_order_relaxed);
		mDefragThreshold = other.mDefragThreshold;
		
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
	/// Lock-free mirror of "mPendingErase is non-empty", so the per-frame maintenance pass
	/// can skip the write lock for arrays with no deferred erases.
	std::atomic<bool> mHasPendingErase{ false };

	/// @brief Structural epoch: odd while sector storage is being relocated or destroyed.
	/// Read on every pin, written only by structural mutations, so it gets its own line.
	/// How many times a reader yields to a waiting writer before pinning anyway.
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
