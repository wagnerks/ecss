#pragma once

#include <cstddef>
#include <vector>
#include <mutex>
#include <type_traits>

namespace ecss::Memory {
	struct RetireBin {
		RetireBin() = default;

		RetireBin(const RetireBin&){}
		RetireBin& operator=(const RetireBin&) { return *this; }

		RetireBin(RetireBin&& other) noexcept : mRetired(std::move(other.mRetired)){}
		RetireBin& operator=(RetireBin&& other) noexcept {
			if (this == &other) { return *this; }
				
			mRetired = std::move(other.mRetired);
			return *this;
		}

		struct Block { void* ptr; size_t bytes; size_t align; };

		void retire(void* p, size_t bytes, size_t align) {
			auto lock = std::lock_guard(mMtx);
			mRetired.emplace_back(p, bytes, align);
		}

		void drainAll() {
			std::vector<Block> tmp;
			{ auto lock = std::lock_guard(mMtx); tmp.swap(mRetired); }

			for (auto& b : tmp){
				::operator delete(b.ptr, static_cast<std::align_val_t>(b.align));
			}
		}

		size_t queuedBytes() const {
			auto lock = std::lock_guard(mMtx);
			size_t s = 0; for (auto& b : mRetired) s += b.bytes; return s;
		}

	private:
		mutable std::mutex mMtx;
		std::vector<Block> mRetired;
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
			return static_cast<T*>(::operator new(n * sizeof(T), std::align_val_t(alignof(T))));
		}

		void deallocate(T* p, size_t n) noexcept {
			if (!bin) { ::operator delete(p, std::align_val_t(alignof(T))); return; }
			bin->retire(p, n * sizeof(T), alignof(T));
		}

		template<class U> friend struct RetireAllocator;
		template<class U>
		bool operator==(const RetireAllocator<U>& rhs) const noexcept { return bin == rhs.bin; }
		template<class U>
		bool operator!=(const RetireAllocator<U>& rhs) const noexcept { return !(*this == rhs); }

		using propagate_on_container_move_assignment = std::false_type;
		using is_always_equal = std::true_type;

		RetireBin* bin = nullptr;
	};
}
