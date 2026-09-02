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

	/// @brief Relaxed load of a word a lock-free reader may catch mid-write.
	///
	/// Compiles to a plain MOV on x86-64 and a plain LDR on ARM64 -- identical codegen to
	/// a raw read. It exists to satisfy the memory model where the word is concurrently
	/// written by Sector::markAlive<true>/markNotAlive<true>, and, in the single-threaded
	/// build, to stop the vectoriser from reordering the surrounding scan.
	///
	/// Parameterised on ThreadSafe alone rather than living in SectorsArray, so that code
	/// which needs the load without naming an allocator can share the one definition.
	template<bool ThreadSafe, class T>
	FORCE_INLINE T loadRelaxed(const T* p, size_t i) noexcept {
		if constexpr (ThreadSafe) {
			return std::atomic_ref<T>(const_cast<T&>(p[i])).load(std::memory_order_relaxed);
		}
		else {
			return p[i];
		}
	}

	/// @brief Acquire load of an alive-bit word, pairing with the release fetch_or in
	/// Sector::markAlive<true>: a reader seeing a set bit is guaranteed the component
	/// bytes written before it. x86-64: plain MOV. ARM64: LDAR instead of LDR.
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

		/// @brief Grow the buffers, republishing because they may have moved.
		///
		/// reserve() changes no element and no size, so it looks like it cannot affect
		/// readers -- but it reallocates, and the old buffers go to the retire bin while the
		/// published view still names them. Readers then keep reading memory that is correct
		/// only until the next write, and freed once the grace period expires.
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

		/// @brief Give back spare capacity, republishing because the buffers move.
		///
		/// The hazard reserve() documents, in the other direction: shrink_to_fit reallocates,
		/// the old buffers go to the retire bin, and a view still naming them reads memory
		/// that is correct only until the next write and freed once the grace period ends.
		FORCE_INLINE void shrinkToFit(size_t actualSize) {
			ids.shrink_to_fit();
			isAlive.shrink_to_fit();
			storeView(actualSize);
		}

		FORCE_INLINE SectorId& idAt(size_t idx) { return ids[idx]; }
		FORCE_INLINE uint32_t& isAliveAt(size_t idx) { return isAlive[idx]; }
		FORCE_INLINE const SectorId& idAt(size_t idx) const { return ids[idx]; }
		FORCE_INLINE const uint32_t& isAliveAt(size_t idx) const { return isAlive[idx]; }

		/// @brief Publish an id or a liveness word to lock-free readers.
		///
		/// Readers walk these two arrays without any lock, so a writer that assigns to them
		/// plainly races even while holding the write lock -- the lock excludes other writers,
		/// not readers. Release stores here pair with the acquire and relaxed loads on the
		/// read side. On x86-64 both are a plain MOV; the atomic_ref only constrains the
		/// compiler, not the hardware.
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

		FORCE_INLINE void shrinkToFit(size_t) {
			ids.shrink_to_fit();
			isAlive.shrink_to_fit();
		}

		FORCE_INLINE SectorId& idAt(size_t idx) { return ids[idx]; }
		FORCE_INLINE uint32_t& isAliveAt(size_t idx) { return isAlive[idx]; }
		FORCE_INLINE const SectorId& idAt(size_t idx) const { return ids[idx]; }
		FORCE_INLINE const uint32_t& isAliveAt(size_t idx) const { return isAlive[idx]; }

		/// @copydoc DenseArrays<true>::setIdAt  No readers to publish to here.
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

/**
 * @brief RAII pin for a sector to prevent relocation / destruction while in use.
 *
 * @thread_safety Thread-confined -- the handle, not the array. One
 *                of these belongs to one thread; its accessors are plain member reads. Any
 *                number of threads may each hold their own pin, on the same sector or not.
 *
 *                What it buys: while it lives, that sector will not be moved, destroyed or
 *                reused, so the pointer stays good. What it costs others: a thread destroying
 *                or overwriting this sector waits until it is released. Never pin a sector and
 *                then destroy it from the same thread -- that waits on yourself.
 *
 *                It says nothing about the component's *value*: another thread may be writing
 *                it. @see Registry::access()
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
 * @thread_safety Thread-confined -- the handle, not the array. Holds
 *                are counted per thread, so holders on different threads do not even share a
 *                cache line; a hold may be released by a different thread than took it, which
 *                is why the shard travels inside the object.
 *
 *                While one is outstanding, everything that relocates sectors waits: clear,
 *                defragment, a middle insert, copy and move assignment. A view keeps one for
 *                its whole life, so those calls cannot finish while a view is open -- from
 *                another thread they block, from this one they deadlock.
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
 * @warning Structural changes to an array are not allowed while the calling thread holds a
 * view or a pin on that same array. Relocating sectors would invalidate the iterator that is
 * reading them, so writers wait for every pin and hold to drain -- correct against other
 * threads, and unsatisfiable against yourself: the thread blocks on a condition only it could
 * clear. In a release build that is a hang with no diagnosis; debug builds assert instead.
 *
 * Illegal while a view or pin on the same array is alive -- anything that relocates sectors:
 *   - insert / emplace / push of an id that lands anywhere but past the end
 *   - insertBulk, and Registry::addComponents
 *   - defragment, clear, shrinkToFit, copy and move assignment
 *   - erase with defragmentation
 * Registry::update() is deliberately not in that list: it attempts compaction rather than
 * waiting for it, and leaves a busy array for the next call.
 * Also illegal: destroying or overwriting in place the one sector you are holding a pin to.
 *
 * Legal, because nothing moves:
 *   - appending an id above every id already stored
 *   - destroying or overwriting a sector in place, other than one you pin yourself
 *   - anything at all on a *different* array
 *
 * A different thread doing any of this is fine and is what the waiting is for; it blocks
 * until your view ends.
 *
 * Every public member carries an @thread_safety line. It answers whether two threads may
 * call it on the same object at once:
 *   - "Internally synchronized."             yes.
 *   - "Thread-confined."                     no, and do not try: the object belongs to one
 *                                            thread and holds no lock, as a std::vector does.
 *                                            Give each thread its own rather than a mutex.
 *   - "Caller must ensure exclusive access." no; this one is shared, and you must supply the
 *                                            ordering yourself.
 *   - "Not applicable (single-threaded build)."
 *
 * and adds "; blocks" when the call waits for a pin or a hold to be released -- for one
 * sector's pins, or for the whole array to carry neither; the method says which. Only that
 * counts as blocking here: it is state a reader controls, so a caller can be the reader it
 * waits for. Ordinary contention for the array's mutex is not marked, because nearly every
 * synchronized call has some. The two axes are independent: a destructor waits for in-flight
 * readers and still must not race with new ones.
 *
 * Many members take `template<bool TS = ThreadSafe>`. Leaving it defaulted is the safe
 * choice and gives the behaviour documented on the method. Passing TS=false on a
 * ThreadSafe array deliberately drops the lock, and then the guarantee is yours: it is
 * only correct where exclusivity is already established -- inside a body handed to
 * exclusiveWhenUnpinned/exclusiveWhenQuiescent, or under a lock you hold yourself.
 * Passing TS=true on a non-thread-safe array is a compile error.
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

	/// @brief Relaxed load of a sector id. The id array is written by a writer holding only
	/// the array's write lock, which excludes other writers but not the lock-free readers, so
	/// both sides go through atomic_ref.
	/// Declared static so nested iterator classes can call it without an enclosing instance.
	/// @thread_safety Internally synchronized. A single load with the ordering the thread-safe
	///                build needs and none in the plain build. Given a pointer, not an index into
	///                anything it validates -- the caller supplies both, and both come from a
	///                snapshot it already holds.
	static FORCE_INLINE SectorId loadId(const SectorId* p, size_t i) noexcept {
		return detail::loadRelaxed<ThreadSafe>(p, i);
	}

	/// @thread_safety Internally synchronized. A single load with the ordering the thread-safe
	///                build needs and none in the plain build. Given a pointer, not an index into
	///                anything it validates -- the caller supplies both, and both come from a
	///                snapshot it already holds.
	static FORCE_INLINE uint32_t loadAliveRelaxed(const uint32_t* p, size_t i) noexcept {
		return detail::loadRelaxed<ThreadSafe>(p, i);
	}

	/// @thread_safety Internally synchronized. A single load with the ordering the thread-safe
	///                build needs and none in the plain build. Given a pointer, not an index into
	///                anything it validates -- the caller supplies both, and both come from a
	///                snapshot it already holds.
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

	/**
	 * @brief Forward iterator over all slots (alive or dead).
	 * Optimized: uses chunk-aware pointer increment for O(1) per-element access.
	 * Uses atomic view snapshots for thread-safe iteration.
	 */
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
	/// @thread_safety Internally synchronized. Building one takes no lock: an iterator is made
	///                entirely from lock-free snapshots. The iterator itself belongs to one thread,
	///                and it stays valid only while nothing compacts the array -- which is what the
	///                view's structural hold is for. Do not carry one across a defragment.
	template<bool TS = ThreadSafe> Iterator begin() const { enforceTSMode<TS>(); return Iterator(this, 0); }
	/// @thread_safety Internally synchronized. Building one takes no lock: an iterator is made
	///                entirely from lock-free snapshots. The iterator itself belongs to one thread,
	///                and it stays valid only while nothing compacts the array -- which is what the
	///                view's structural hold is for. Do not carry one across a defragment.
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

		/// @brief Jump to the first alive sector whose id is >= @p minId (or end).
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
	/// @thread_safety Internally synchronized. One relaxed load. True when no dead slot is
	///                waiting to be compacted away.
	template<bool TS = ThreadSafe>
	bool isPacked() const { enforceTSMode<TS>(); return mDefragmentSize.load(std::memory_order_relaxed) == 0; }

	/// @thread_safety Internally synchronized. Building one takes no lock: an iterator is made
	///                entirely from lock-free snapshots. The iterator itself belongs to one thread,
	///                and it stays valid only while nothing compacts the array -- which is what the
	///                view's structural hold is for. Do not carry one across a defragment.
	template<class T, bool TS = ThreadSafe>
	IteratorAlive beginAlive() const {
		// Note: isPacked=false because we're filtering by a specific component's alive mask,
		// not just checking if any component is alive. mDefragmentSize==0 only means no dead
		// sectors, not that all sectors have this specific component.
		enforceTSMode<TS>();
		return IteratorAlive(this, 0, sizeImpl(), getLayoutData<T>().isAliveMask, false);
	}
	/// @thread_safety Internally synchronized. Building one takes no lock: an iterator is made
	///                entirely from lock-free snapshots. The iterator itself belongs to one thread,
	///                and it stays valid only while nothing compacts the array -- which is what the
	///                view's structural hold is for. Do not carry one across a defragment.
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
		/// @brief Binary search to find first linear index where mIds[idx] >= sectorId
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
		std::vector<std::pair<size_t, size_t>> mLinearRanges;  ///< Linear index ranges [begin, end)
		size_t mIdx = 0;
		size_t mSize = 0;
		size_t mRangeIdx = 0;
		uint16_t mStride = 0;
	};

	/// @thread_safety Internally synchronized. Building one takes no lock: an iterator is made
	///                entirely from lock-free snapshots. The iterator itself belongs to one thread,
	///                and it stays valid only while nothing compacts the array -- which is what the
	///                view's structural hold is for. Do not carry one across a defragment.
	template<bool TS = ThreadSafe>
	RangedIterator beginRanged(const Ranges<SectorId>& ranges) const {
		enforceTSMode<TS>();
		return RangedIterator(this, ranges);
	}
	/// @thread_safety Internally synchronized. Building one takes no lock: an iterator is made
	///                entirely from lock-free snapshots. The iterator itself belongs to one thread,
	///                and it stays valid only while nothing compacts the array -- which is what the
	///                view's structural hold is for. Do not carry one across a defragment.
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
	/// @thread_safety Internally synchronized; blocks. The destructor clears, and clearing waits
	///                for every pin and hold to drain, so a view open on another thread stalls it
	///                for as long as that view lives -- in-flight readers are waited for.
	///
	///                That is a synchronization guarantee, not a lifetime one: nothing may start
	///                reading the array once destruction has begun.
	~SectorsArray() { clear(); shrinkToFit(); }

	/// @thread_safety Internally synchronized, vacuously: nothing can name the array until this
	///                returns, so there is nothing to race with.
	template <typename... Types>
	static SectorsArray* create(Allocator&& allocator = {}) {
		static_assert(types::areUnique<Types...>, "Duplicates detected in SectorsArray types!");
		// One per distinct type pack, for the lifetime of the process: the layout an array
		// reports never changes, and the LayoutData records keep fixed addresses.
		static const SectorLayoutMeta* meta = SectorLayoutMeta::create<Types...>();
		return new SectorsArray(meta, std::move(allocator));
	}

	// ==================== Layout helpers ====================

	/// @thread_safety Internally synchronized. The layout is fixed when the array is created and
	///                never changes, so this needs no lock and cannot go stale.
	template<typename T>
	FORCE_INLINE const LayoutData& getLayoutData() const { return getLayout()->template getLayoutData<T>(); }

	/// @thread_safety Internally synchronized. The layout is fixed at creation and never changes.
	FORCE_INLINE const SectorLayoutMeta* getLayout() const { return mAllocator.getSectorLayout(); }

	// ==================== Pin API (ThreadSafe builds) ====================

	// No lock: pinning validates itself against the structural epoch (see pinSectorImpl).
	// The shared lock used to be what made "writer holds the unique lock => no new pins"
	// true; the epoch handshake replaces it, and every reader stops serialising on one word.

	/// @thread_safety Internally synchronized. Takes no lock. While the pin lives, that sector
	///                will not be moved, destroyed or reused -- which is also what makes another
	///                thread destroying this entity wait. Do not pin a sector and then destroy or
	///                overwrite it from the same thread.
	template<bool TS = true>
	[[nodiscard]] PinnedSector pinSector(SectorId id) const requires(ThreadSafe) {
		enforceTSMode<TS>();
		return pinSectorImpl(id);
	}

	/// @thread_safety Internally synchronized. As pinSector(), addressed by dense index. The
	///                index is only meaningful while the array is not being compacted, so this is
	///                for callers already iterating.
	template<bool TS = true>
	[[nodiscard]] PinnedSector pinSectorAt(size_t idx) const requires(ThreadSafe) {
		enforceTSMode<TS>();
		return pinSectorAtImpl(idx);
	}

	/// @thread_safety Internally synchronized. As pinSector(), for whichever sector is last at
	///                the moment of the call.
	template<bool TS = true>
	[[nodiscard]] PinnedSector pinBackSector() const requires(ThreadSafe) {
		enforceTSMode<TS>();
		return pinBackSectorImpl();
	}

	/// @brief Block compaction of this array for as long as the returned object lives.
	/// Use this, not a pin, when the point is "do not move things", not "leave this sector".
	/// @thread_safety Internally synchronized. Cheaper than a pin and per thread rather than per
	///                sector. While one lives no sector in this array may be relocated -- so every
	///                whole-array writer waits for it. This is what a view holds for its lifetime.
	///                Hold it for as short a time as the reading takes.
	[[nodiscard]] StructuralHold holdStructure() const requires(ThreadSafe) {
		// Same courtesy the pin path pays: a few threads building views in a loop would
		// otherwise keep the array permanently held and compaction would never run.
		yieldToWriters();
		return StructuralHold(mPinsCounter);
	}

	// ==================== Erase & maintenance ====================

	/// @thread_safety Internally synchronized; blocks on the whole array. Erasing by position
	///                closes the gap, which moves sectors, so it waits until the array carries no
	///                pins and no holds. Illegal from a thread holding a view on this array.
	///
	///                The deferred counterpart, safe from anywhere, is eraseAsync().
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

	/// @thread_safety Internally synchronized; blocks on the whole array. Same as erase(idx):
	///                it moves sectors, so it waits for every pin and hold. Passing an iterator
	///                obtained from a view that is still open is exactly the deadlock case.
	///
	///                The deferred counterpart, safe from anywhere, is eraseAsync().
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

	/**
	 * @brief Ask for the array to be cleared at the next safe point, instead of now.
	 *
	 * The deferred counterpart to clear(). Clearing destroys every sector and drops the
	 * published size to zero, so a reader mid-iteration would be walking sectors that are
	 * gone -- which is why clear() waits for the array to carry no pins and no holds, and why
	 * calling it from a thread that is itself iterating deadlocks.
	 *
	 * This only records the wish and returns. processPendingErases() -- which Registry::update()
	 * calls for every array -- performs it the first time it finds the array free. That makes it
	 * safe to ask for from anywhere, including from inside a loop over a view.
	 *
	 * @note Asked for, not promised. An array that something is always iterating stays busy and
	 *       the clear keeps waiting, quietly, for a frame where it can run. If you need it to
	 *       have happened, call clear() at a point where nothing is reading.
	 * @thread_safety Internally synchronized. One relaxed store; nothing is waited for.
	 * @see clear(), eraseAsync(), Registry::update()
	 */
	void clearAsync() requires(ThreadSafe) {
		mHasPendingClear.store(true, std::memory_order_release);
	}

	/// @brief Whether a clearAsync() is still waiting for a frame it can run in.
	/// @thread_safety Internally synchronized. One atomic load.
	bool hasPendingClear() const noexcept { return mHasPendingClear.load(std::memory_order_acquire); }

	/// @brief Clear if the array is free right now; leave it for the next call if not.
	/// @return true if the clear ran.
	/// @thread_safety Internally synchronized. Gives up rather than waiting, so it is safe to
	///                call from a thread holding a view -- it simply does nothing that time.
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

	/// @thread_safety Internally synchronized. Queues the ids and returns; nothing is moved and
	///                nothing is waited for. The work happens in processPendingErases(), which
	///                Registry::update() calls. This is the erase to use while others iterate.
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

	/// @thread_safety Internally synchronized. Lock-free: reads a published snapshot, takes
	///                no lock and waits for nothing. The index is a position, and a position is only
	///                stable while nothing compacts the array -- hold a view or a pin if you mean
	///                to use it afterwards.
	template<bool TS = ThreadSafe>
	size_t findLinearIdx(SectorId sectorId) const {
		enforceTSMode<TS>();
		return findLinearIdxImpl(sectorId);
	}

	/// @thread_safety Internally synchronized. Lock-free: reads a published snapshot, takes
	///                no lock and waits for nothing. True when the call was made; another thread may
	///                destroy the entity immediately after.
	template<bool TS = ThreadSafe>
	bool containsSector(SectorId id) const {
		enforceTSMode<TS>();
		return containsSectorImpl(id);
	}

	/// @thread_safety Internally synchronized. Lock-free: reads a published snapshot, takes
	///                no lock and waits for nothing. The pointer is only good while the sector cannot
	///                move: pin it, or hold the array, if the pointer outlives the call.
	template<bool TS = ThreadSafe>
	std::byte* findSectorData(SectorId id) const {
		enforceTSMode<TS>();
		return findSectorDataImpl(id);
	}

	/// @brief Find slot info (data pointer + linearIdx) for fast sparse lookup
	/// @return SlotInfo with data pointer and linear index, or INVALID_SLOT if not found
	/// @thread_safety Internally synchronized. Lock-free: reads a published snapshot, takes
	///                no lock and waits for nothing. Same caveat as findSectorData(): the slot is a
	///                position, valid only while nothing compacts.
	template<bool TS = ThreadSafe>
	detail::SlotInfo findSlot(SectorId id) const {
		enforceTSMode<TS>();
		return findSlotImpl(id);
	}

	/// @thread_safety Internally synchronized. Lock-free: reads a published snapshot, takes
	///                no lock and waits for nothing.
	template<bool TS = ThreadSafe>
	uint32_t getIsAlive(SectorId id) const {
		enforceTSMode<TS>();
		const auto idx = findLinearIdxImpl(id);
		// loadAliveWord, not isAliveAt: the latter dereferences the live vector, which a
		// concurrent push_back may be reallocating. The seqlock snapshot is bounds-checked.
		return idx != INVALID_IDX ? loadAliveWord<ThreadSafe>(idx) : 0;
	}

	/// @thread_safety Caller must ensure exclusive access. Hands out a reference into the live
	///                liveness array, so it is only sound where the array cannot be compacted and
	///                nobody else writes that word -- inside a body given to exclusiveWhenUnpinned,
	///                or under a lock you hold. Prefer loadAliveWord() to read one.
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
	/// @thread_safety Internally synchronized. Lock-free: reads a published snapshot, takes
	///                no lock and waits for nothing. Bounds-checked: an index past the published
	///                size reads as not alive rather than out of bounds.
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

	/// @thread_safety Internally synchronized. Lock-free: reads a published snapshot, takes
	///                no lock and waits for nothing.
	template<bool TS = ThreadSafe>
	SectorId getId(size_t linearIdx) const {
		return mDenseArrays.idAt(linearIdx);
	}

	// ==================== Capacity ====================

	// sparseCapacity/size/empty read a single atomic -- no lock needed (see Lookup note).
	// capacity() keeps the shared lock: it reads the chunk vector, which a concurrent
	// allocate() may be reallocating, and that vector is not published through a seqlock.
	/// @thread_safety Internally synchronized. One atomic load.
	template<bool TS = ThreadSafe> size_t sparseCapacity() const { enforceTSMode<TS>(); return mSparseMap.capacity(); }
	/// @thread_safety Internally synchronized. Takes the shared lock, briefly: it reads the chunk
	///                vector, which a concurrent growth may be reallocating, and that vector is not
	///                published through a seqlock the way the dense arrays are.
	template<bool TS = ThreadSafe> size_t capacity() const { TS_GUARD(TS && ThreadSafe, SHARED, return mAllocator.capacity()); }
	/// @thread_safety Internally synchronized. One atomic load. A count, not a promise: another
	///                thread may add or destroy before you act on it.
	template<bool TS = ThreadSafe> size_t size() const { enforceTSMode<TS>(); return sizeImpl(); }
	/// @thread_safety Internally synchronized. One atomic load. @see size()
	template<bool TS = ThreadSafe> bool empty() const { enforceTSMode<TS>(); return sizeImpl() == 0; }
	/// @thread_safety Internally synchronized. Takes the write lock and returns the chunks past
	///                the end. It moves no sector, so it does not wait for pins or holds. The chunks
	///                are retired rather than freed, so a reader still holding a pointer into one is
	///                safe until the grace period expires.
	template<bool TS = ThreadSafe> void shrinkToFit() { TS_GUARD(TS && ThreadSafe, UNIQUE, shrinkToFitImpl()); }

	/// @thread_safety Internally synchronized. Takes the write lock and adds chunks. Nothing is
	///                moved, so it does not wait for pins or holds -- growing is always legal, even
	///                while others iterate. Doing it up front keeps it off the frame.
	template<bool TS = ThreadSafe> void reserve(uint32_t newCapacity) { TS_GUARD(TS && ThreadSafe, UNIQUE, reserveImpl(newCapacity)); }
	/// @thread_safety Internally synchronized; blocks on the whole array. Destroys every sector,
	///                no pins and no holds. A view open on another thread holds it up; a view
	///                open on this thread deadlocks.
	///
	///                The deferred way to ask for this from anywhere, including from inside a
	///                loop over a view, is clearAsync().
	template<bool TS = ThreadSafe> void clear() {
		if constexpr (TS && ThreadSafe) { exclusiveWhenQuiescent([&] { clearImpl(); }); }
		else { clearImpl(); }
		// A clearAsync() still waiting for a free frame has just had its wish granted.
		// Leaving the flag set sends the next maintenance pass through a write lock and a
		// published structural epoch to clear an array that is already empty.
		mHasPendingClear.store(false, std::memory_order_release);
	}

	// ==================== Defragmentation ====================

	/// @thread_safety Internally synchronized; blocks on the whole array. Compaction moves
	///                sectors, so it waits until the array carries no pins and no holds. For the
	///                non-waiting version, which gives up if the array is busy, see
	///                tryDefragment() -- or Registry::update(), which calls it for every array.
	template<bool TS = ThreadSafe>
	void defragment() {
		if constexpr (TS && ThreadSafe) { exclusiveWhenQuiescent([&] { defragmentImpl(); }); }
		else { defragmentImpl(); }
	}

	/// @thread_safety Internally synchronized. The non-blocking counterpart to defragment():
	///                if anything is pinned or held it returns without compacting, and the work is
	///                left for a later call. Safe to call on a schedule from a busy frame.
	template<bool TS = ThreadSafe>
	void tryDefragment() { TS_GUARD_S(TS && ThreadSafe, UNIQUE, if (mPinsCounter.hasAnyPins()) return;, defragmentImpl();); }

	/// @thread_safety Internally synchronized. One relaxed increment of the dead-slot counter.
	void incDefragmentSize(uint32_t count = 1) { mDefragmentSize.fetch_add(count, std::memory_order_relaxed); }

	/// @thread_safety Internally synchronized. One relaxed load.
	template<bool TS = ThreadSafe> auto getDefragmentationSize() const { enforceTSMode<TS>(); return mDefragmentSize.load(std::memory_order_relaxed); }
	/// @thread_safety Internally synchronized. Two relaxed loads; the ratio may be a hair stale.
	template<bool TS = ThreadSafe> auto getDefragmentationRatio() const {
		enforceTSMode<TS>();
		const auto sz = sizeImpl();
		return sz ? (static_cast<float>(mDefragmentSize.load(std::memory_order_relaxed)) / static_cast<float>(sz)) : 0.f;
	}
	/// @thread_safety Internally synchronized. Relaxed loads against the threshold.
	template<bool TS = ThreadSafe> bool needDefragment() const {
		enforceTSMode<TS>();
		return getDefragmentationRatio<false>() > loadDefragThreshold();
	}
	/// @thread_safety Internally synchronized. One relaxed store. Changes when compaction is
	///                asked for, never compaction itself.
	template<bool TS = ThreadSafe> void setDefragmentThreshold(float threshold) { 
		enforceTSMode<TS>(); storeDefragThreshold(std::max(0.f, std::min(threshold, 1.f))); 
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
	 * @thread_safety Internally synchronized. Advances the grace period and frees what has
	 *                expired. Takes a bin's mutex only when that bin holds something.
	 */
	size_t tick() { return tickRetired(); }


	/// @thread_safety Internally synchronized; blocks. An id above every id already stored is
	///                appended, and that waits for nothing. An id landing anywhere else has to shift
	///                the sectors after it, so it waits until the array carries no pins and no
	///                holds -- from a thread holding a view on this array, a deadlock. Overwriting
	///                an id that is already stored waits only on that one sector.
	///
	///                To add from inside a loop over a view, record into an ecss::CommandBuffer
	///                and apply it once the loop is done.
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

	/// @thread_safety Internally synchronized; blocks. An id above every id already stored is
	///                appended, and that waits for nothing. An id landing anywhere else has to shift
	///                the sectors after it, so it waits until the array carries no pins and no
	///                holds -- from a thread holding a view on this array, a deadlock. Overwriting
	///                an id that is already stored waits only on that one sector.
	///
	///                To add from inside a loop over a view, record into an ecss::CommandBuffer
	///                and apply it once the loop is done.
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

	/// @thread_safety Internally synchronized; blocks. An id above every id already stored is
	///                appended, and that waits for nothing. An id landing anywhere else has to shift
	///                the sectors after it, so it waits until the array carries no pins and no
	///                holds -- from a thread holding a view on this array, a deadlock. Overwriting
	///                an id that is already stored waits only on that one sector.
	///
	///                To add from inside a loop over a view, record into an ecss::CommandBuffer
	///                and apply it once the loop is done.
	template<typename T, bool TS = ThreadSafe, class... Args>
	T* push(SectorId sectorId, Args&&... args) noexcept {
		if constexpr (sizeof...(Args) == 1 && (std::is_same_v<std::remove_cvref_t<Args>, T> && ...)) {
			return insert<Args..., TS>(sectorId, std::forward<Args>(args)...);
		} else {
			return emplace<T, TS>(sectorId, std::forward<Args>(args)...);
		}
	}

	/// @brief Bulk insert. Each *it yields a pair-like {SectorId, C}.
	///
	/// Ids may arrive in any order and may fall anywhere in the range already stored; an id
	/// that is already present is overwritten. Reserves once and publishes the dense view
	/// once, skipping the per-element existence check / insert-position search / view publish
	/// that addComponent() pays. In the TS build it also batches the write lock, the pin wait
	/// and the dense-view publish across the whole range.
	///
	/// Prefer this over a loop of addComponent() whenever the ids are not ascending. Adding M
	/// components one at a time costs O(M*N): each middle insert shifts the tail and rewrites
	/// the sparse entry of every sector it shifted past. This sorts the batch once and merges
	/// it in a single pass, so each sector moves at most once.
	/// @thread_safety Internally synchronized; blocks. A batch entirely above what is stored is
	///                appended in one pass and waits for nothing. Otherwise the batch is merged into
	///                place, which moves existing sectors, and that waits until the array carries no
	///                pins and no holds. Merging is linear in the batch, so this is the cheap way to
	///                add many ids at once -- one wait instead of one per id.
	template<typename C, typename It, bool TS = ThreadSafe>
	void insertBulk(It first, It last) noexcept {
		if constexpr (TS && ThreadSafe) { exclusiveWhenQuiescent([&] { insertBulkImpl<C>(first, last); }); }
		else { insertBulkImpl<C>(first, last); }
	}

	/// @thread_safety Internally synchronized. Nothing here waits. The queued erases destroy
	///                sectors in place, and compaction is attempted rather than awaited: an array
	///                something is iterating right now is left for the next call. This is what makes
	///                Registry::update() safe to call from inside a loop over a view.
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

	/// @brief Compact if the array is free right now; leave it for the next call if not.
	/// @return true if compaction ran.
	/// @thread_safety Internally synchronized. Gives up instead of waiting: if anything is
	///                pinned or held it returns false and compacts nothing.
	/// @return true if compaction ran.
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
	/// @thread_safety Caller must ensure exclusive access -- to the decision, not the lock.
	///                Hands out the raw shared lock, so it waits for the mutex like any lock
	///                does; that is not the "blocks" above, which means waiting on a pin or a
	///                hold. What you do under it is yours to get right. Note it
	///                guards the chunk table and the sparse map, not the sectors: holding it does
	///                not stop compaction, only a structural hold or a pin does.
	auto readLock() const requires(ThreadSafe) { return std::shared_lock(mtx); }
	/// @thread_safety Caller must ensure exclusive access -- to the decision, not the lock.
	///                Hands out the raw unique lock, so it waits for the mutex and for readers
	///                holding it shared; that is not the "blocks" above, which means waiting on
	///                a pin or a hold. Taking it is not enough to relocate sectors: readers
	///                pin and hold without any lock at all, so anything that moves a sector must
	///                establish quiescence first. Use exclusiveWhenQuiescent() rather than this.
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
	/// @thread_safety Internally synchronized; blocks. Waits for one sector only. Runs fn under the
	///                write lock once that sector carries no pins; pins on other sectors and holds
	///                over the array do not delay it, because fn is expected to change that sector
	///                in place and move nothing. Deadlocks if this thread pins that sector.
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

	/// @brief Same as exclusiveWhenUnpinned(id), for a batch of in-place destroys.
	///
	/// Does not wait for pins or holds on sectors outside @p [begin, end). A camera pin
	/// on the same array must not stall destroying unrelated entities — that was
	/// exclusiveWhenQuiescent, which is for relocation only.
	/// @thread_safety Internally synchronized; blocks. Waits for the named sectors only, then runs fn
	///                under the write lock. As the single-id form: fn must change those sectors in
	///                place and move nothing.
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

	/// @brief Run @p fn under the write lock once no sector at all is pinned.
	///        Required by anything that relocates sectors (shift, defragment, clear, copy).
	/// @thread_safety Internally synchronized; blocks. Waits for the whole array: fn runs under the
	///                write lock once nothing is pinned and nothing is held. This is the gate for
	///                everything that relocates sectors, and the one to use when fn moves anything.
	///                It re-checks after taking the lock and waits again if a reader slipped in, so
	///                a thread that keeps opening views on this array can hold it off; from the
	///                thread that holds the view itself, it never completes.
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

	/// @brief Acquire (or reuse) the slot for @p sectorId and run @p fn(linearIdx) on it.
	///
	/// Appends and overwrites only need the target sector unpinned. A middle insert also
	/// shifts every following sector, so it additionally needs quiescence -- but that is
	/// only discoverable under the lock, hence the two-tier retry: tryAcquireSlotImpl
	/// declines with INVALID_IDX and the wait happens after the lock is released.
	/// @thread_safety Internally synchronized; blocks. Two tiers, because how much it must wait for is
	///                only knowable under the lock. An append or an overwrite of an existing id
	///                proceeds immediately; an id landing in the middle needs the array quiescent,
	///                and that wait happens after the lock is dropped.
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
	/// @brief Stand aside for a writer that is trying to reach quiescence.
	///
	/// A bounded yield was not enough. Readers that open views back to back keep at least one
	/// hold outstanding at every instant, so a writer waiting for *all* of them to be gone
	/// never got its moment: it spun, gave up, and went round the loop with the same odds.
	/// Measured at 400k sectors, defragment() never completed with eight such readers.
	///
	/// So a reader that holds nothing waits properly instead of yielding -- it can afford to,
	/// because it cannot be what the writer is waiting for. A reader that already holds a pin
	/// or a hold keeps the old bounded spin: blocking there would be waiting for itself.
	/// @thread_safety Internally synchronized; blocks, but only a thread that holds nothing.
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

	/// @brief Place @p sectorId at the end. Caller has established that it belongs there.
	size_t appendSlotImpl(SectorId sectorId, size_t sz) {
		mDenseArrays.pushBack(sectorId, 0);
		mSparseMap.set(sectorId, static_cast<uint32_t>(sz));
		mSize.store(sz + 1, std::memory_order_relaxed);
		mDenseArrays.storeView(sz + 1);
		return sz;
	}

	/// @brief Acquire a slot only when @p sectorId appends past every id stored, else kNoSlot.
	///
	/// Returns without touching anything when it is not an append, so the caller can fall
	/// through to the full protocol. Sorted ids mean an id above the last one cannot already
	/// be present -- a dead entry keeps its id and its place -- but the sparse map is still
	/// consulted, because it is one load and it is the authority on presence.
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

	/// @brief Bulk insert for ids that are not a pure append: order the batch, then merge it
	///        into the dense array back to front so every sector moves at most once.
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

	/// @brief Find a slot @p id can be written into without changing the array's shape.
	/// @return the linear index of a live slot or of a dead entry to resurrect, else kNoSlot.
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

	/// @brief Move one sector, its id and its liveness from @p from to @p to, repointing the
	/// sparse entry. Mirrors shiftRightImpl: for a non-trivial layout moveSectorData carries
	/// the alive flags itself, for a trivial one they are copied alongside the bytes.
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
	/// Lock-free mirror of "mPendingErase is non-empty", so the per-frame maintenance pass
	/// can skip the write lock for arrays with no deferred erases.
	std::atomic<bool> mHasPendingErase{ false };

	/// Set by clearAsync(), cleared once the clear actually runs. Separate from the erase
	/// queue because clearing supersedes it: there is no point destroying named sectors in a
	/// array that is about to lose all of them.
	std::atomic<bool> mHasPendingClear{ false };

	/// @brief Structural epoch: odd while sector storage is being relocated or destroyed.
	/// Read on every pin, written only by structural mutations, so it gets its own line.
	/// How many times a reader yields to a waiting writer before pinning anyway.
	static constexpr int kWriterYields = 64;
	static constexpr size_t kEpochAlign = ThreadSafe ? 64 : alignof(std::atomic<uint64_t>);
	alignas(kEpochAlign) mutable std::atomic<uint64_t> mStructEpoch{ 0 };
	static_assert(types::isLockFreeAtomic<uint64_t>, "the structural epoch must be lock-free");

	alignas(kSizeAlign) std::atomic<size_t> mSize{0};
	std::atomic<uint32_t> mDefragmentSize{0};
	/// Plain storage read through atomic_ref in the thread-safe build: needDefragment()
	/// reads it lock-free on the maintenance path while setDefragmentThreshold() writes
	/// it, and two protocols on one word is a race however benign the value looks.
	/// @see loadDefragThreshold(), storeDefragThreshold()
	float mDefragThreshold = 0.2f;
};

#undef SHARED_LOCK
#undef UNIQUE_LOCK
#undef TS_GUARD
#undef TS_GUARD_S
#undef ITERATOR_COMMON_USING

} // namespace ecss::Memory
