#pragma once

#include <atomic>
#include <cstddef>
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

		/// @brief Queue memory block for deferred freeing
		void retire(void* p) {
			if (!p) return;
			auto lock = std::lock_guard(mMtx);
			mRetired.push_back({p, mGracePeriod.load(std::memory_order_relaxed)});
		}

		/**
		 * @brief Process one tick of the grace period.
		 * 
		 * Call this once per frame/update cycle. Memory blocks whose countdown
		 * reaches zero will be freed. This is safe to call from any thread.
		 * 
		 * @return Number of blocks freed this tick
		 */
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

		/// @brief Immediately free all retired memory (use only at safe sync points)
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
