#pragma once

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <vector>
#include <mutex>
#include <type_traits>
#include <cstdint>

namespace ecss::Memory {
	/**
	 * @brief Deferred memory reclamation bin with grace period support.
	 *
	 * Memory blocks are not freed immediately when retired. Instead, they wait
	 * for a configurable number of tick() calls (grace period) before being freed.
	 * This allows concurrent readers to safely finish using old memory even after
	 * a container reallocation.
	 *
	 * Usage patterns:
	 * 1. Call tick() once per frame/update cycle to gradually free old memory
	 * 2. Call drainAll() to immediately free everything (use only at safe points)
	 *
	 * Default grace period is 3 ticks, which is safe for typical game loops where
	 * iterators don't survive across frames.
	 */
	struct RetireBin {
		static constexpr uint32_t DEFAULT_GRACE_PERIOD = 3;

		/// @thread_safety Caller must ensure exclusive access. Applies to construction, assignment and
		///                destruction. The destructor drains the bin, which frees everything still
		///                queued regardless of its countdown -- so nothing may still be holding a
		///                pointer into it.
		RetireBin() = default;
		explicit RetireBin(uint32_t gracePeriod) : mGracePeriod(gracePeriod) {}
		~RetireBin() { drainAll(); }

		RetireBin(const RetireBin&) {}
		RetireBin& operator=(const RetireBin&) { return *this; }

		// Moving carries the queued blocks, and with them mPending: tick() bails out on that
		// counter alone, so a bin that inherits the blocks without it reports nothing to do
		// while still holding them -- they would live until the destructor.
		RetireBin(RetireBin&& other) noexcept
			: mRetired(std::move(other.mRetired))
			, mGracePeriod(other.mGracePeriod.load(std::memory_order_relaxed)) {
			mPending.store(mRetired.size(), std::memory_order_release);
			other.mRetired.clear();
			other.mPending.store(0, std::memory_order_release);
		}
		
		RetireBin& operator=(RetireBin&& other) noexcept {
			if (this == &other) { return *this; }
			// Ours first: the assignment below overwrites the vector that owns them, and
			// nothing else has a pointer to those blocks.
			drainAll();
			mRetired = std::move(other.mRetired);
			mGracePeriod.store(other.mGracePeriod.load(std::memory_order_relaxed), std::memory_order_relaxed);
			mPending.store(mRetired.size(), std::memory_order_release);
			other.mRetired.clear();
			other.mPending.store(0, std::memory_order_release);
			return *this;
		}

		/// @brief Queue memory block for deferred freeing, or free it now if the grace
		/// period is zero.
		///
		/// A zero grace period means the owner has no lock-free readers to protect, so
		/// there is nothing for the block to wait for. Queuing it anyway would need a
		/// tick() the single-threaded build has no reason to call, and the block would
		/// live until the bin was destroyed.
		/// @thread_safety Internally synchronized. Takes the bin's mutex, briefly. With a zero grace
		///                period the block is freed immediately instead of queued, which is right only
		///                when there are no lock-free readers to outlive -- the single-threaded build.
		void retire(void* p) {
			if (!p) return;
			if (mGracePeriod.load(std::memory_order_relaxed) == 0) {
				std::free(p);
				return;
			}
			auto lock = std::lock_guard(mMtx);
			mRetired.push_back({p, mGracePeriod.load(std::memory_order_relaxed)});
			mPending.store(mRetired.size(), std::memory_order_release);
		}

		/**
		 * @brief Process one tick of the grace period.
		 * 
		 * Call this once per frame/update cycle. Memory blocks whose countdown
		 * reaches zero will be freed. This is safe to call from any thread.
		 * 
		 * @return Number of blocks freed this tick
		 * @thread_safety Internally synchronized. Lock-free when the bin is empty, which is the
		 *                common case and costs one acquire load; takes the mutex only when there is
		 *                something to count down. Safe from any thread, including several at once.
		 */
		size_t tick() {
			// Lock-free early-out. tick() runs for every array every frame and is almost
			// always a no-op, but taking the mutex to discover that cost ~58 ns per array
			// per frame (three bins per array, plus the write lock in processPendingErases).
			if (mPending.load(std::memory_order_acquire) == 0) [[likely]] {
				return 0;
			}

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
				mPending.store(mRetired.size(), std::memory_order_release);
			}

			for (auto p : toFree) {
				std::free(p);
			}
			return toFree.size();
		}

		/// @brief Immediately free all retired memory (use only at safe sync points)
		/// @thread_safety Caller must ensure exclusive access. Frees everything immediately, ignoring the grace
		///                period. The mutex makes the bookkeeping safe, not the freeing: the caller has
		///                to know no reader still holds a pointer into any queued block. Only for
		///                teardown, or a point where all readers are known to be gone.
		void drainAll() {
			std::vector<void*> tmp;
			{
				auto lock = std::lock_guard(mMtx);
				tmp.reserve(mRetired.size());
				for (auto& block : mRetired) {
					tmp.push_back(block.ptr);
				}
				mRetired.clear();
				mPending.store(0, std::memory_order_release);
			}

			for (auto b : tmp) {
				std::free(b);
			}
		}

		void setGracePeriod(uint32_t ticks) { mGracePeriod.store(ticks, std::memory_order_relaxed); }
		uint32_t getGracePeriod() const { return mGracePeriod.load(std::memory_order_relaxed); }
		
		/// @brief Get number of blocks waiting to be freed
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
		/// Mirror of mRetired.size(), readable without the mutex so tick() can bail out.
		std::atomic<size_t> mPending{ 0 };
		std::atomic<uint32_t> mGracePeriod{DEFAULT_GRACE_PERIOD};
	};

	/**
 * @brief Allocator that defers memory reclamation to avoid use-after-free
 *        during container reallocation.
 *
 * Standard containers like std::vector will call deallocate() on the old
 * buffer immediately after a reallocation. In concurrent scenarios, a
 * reader may still access the old buffer, leading to crashes or undefined
 * behavior. RetireAllocator solves this by not freeing memory right away:
 * deallocate() places the old block into a RetireBin. The user is then
 * responsible for calling RetireBin::drainAll() at a safe point, when no
 * readers can reference retired buffers anymore.
 *
 * Typical usage: construct a container with a RetireAllocator bound to a
 * shared RetireBin. Push-backs that trigger reallocation will queue the old
 * memory into the bin instead of freeing it. Later, at a known quiescent
 * state, the program calls drainAll() to release all retired memory.
 *
 * This approach prevents reallocation races from invalidating concurrent
 * readers, at the cost of temporarily higher memory usage until retired
 * blocks are drained.
 */

	template<class T>
	struct RetireAllocator {
		using value_type = T;

		explicit RetireAllocator(RetireBin* bin) noexcept : bin(bin) {}

		template<class U>
		RetireAllocator(const RetireAllocator<U>& other) noexcept : bin(other.bin) {}

		T* allocate(size_t n) {
			auto* p = static_cast<T*>(std::calloc(n, sizeof(T)));
			// The Allocator requirements say allocate() either returns storage or throws.
			// Handing back null instead makes the container write through it.
			if (!p && n != 0) { throw std::bad_alloc(); }
			return p;
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
