#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include <ecss/Types.h>

namespace ecss::Threads {
	/**
	 * @brief Per-sector pin tracking & synchronization for safe structural mutations.
	 *
	 * Two predicates, both exact -- neither can ever report "safe" while a pin is live:
	 *   - isPinned(id) / waitUntilChangeable(id): that one sector is not in use.
	 *     Sufficient before destroying or overwriting that sector *in place*.
	 *   - hasAnyPins() / waitUntilQuiescent(): no sector is in use at all.
	 *     Required before *relocating* sectors (middle-insert shift, defragment,
	 *     clear, copy, move), because relocation moves sectors the caller never named.
	 *
	 * Why there is no longer a "highest pinned id":
	 *   The previous design kept maxPinnedSector, recomputed from a hierarchical bit mask
	 *   over pinned ids, and let a writer proceed when its target id was above it.
	 *   That value could be under-reported: the bitmask clears a parent bit before
	 *   re-checking the child word, so a concurrent highestSet() could miss a whole
	 *   subtree and return a lower id -- or -1 -- while sectors were still pinned, and
	 *   the writer then relocated or destroyed pinned data. Both predicates below are
	 *   single atomic loads over counters that were already maintained exactly, so the
	 *   correct version is also the cheaper one: pin/unpin no longer touch the bitmask
	 *   (whose ensurePath() took a global unique_lock on every first pin) and no longer
	 *   walk it to recompute a maximum.
	 *
	 * Invariants:
	 *   - A sector is "pinned" while its counter > 0.
	 *   - mTotalPinned == number of distinct sectors whose counter > 0.
	 *   - mTotalPinned == 0  <=>  every per-sector counter is 0.
	 *
	 * Locking: pin()/unpin() take no locks at all. The counter block table grows under a
	 * mutex and is published as an immutable snapshot, so lookup is lock-free.
	 *
	 * Ordering: pins are always taken while holding at least the owning array shared
	 * lock, and writers test these predicates under its unique lock, so the array mutex
	 * supplies the happens-before between "reader pinned" and "writer looked"; the
	 * atomics here only need to be individually coherent. unpin() runs without any lock,
	 * which is why the wait primitives use atomic wait/notify.
	 */
	struct PinCounters {
		PinCounters() = default;
		PinCounters(const PinCounters&) = delete;
		PinCounters& operator=(const PinCounters&) = delete;

		~PinCounters() {
			for (auto* table : mTables) { delete[] table->blocks; delete table; }
			for (auto* block : mBlocks) { delete[] block; }
		}

		/**
		 * @brief Increment the pin counter for sector id.
		 * @param id Sector id (!= INVALID_ID).
		 */
		void pin(SectorId id) {
			assert(id != INVALID_ID);

			// Claim the aggregate *before* publishing the per-sector count, and hand it back
			// if we turn out not to be the first pinner. Incrementing the aggregate second
			// leaves a window where a sector reads as pinned while hasAnyPins() still reads
			// false: a second pinner of the same sector observes a non-zero counter, skips
			// the aggregate bump and returns, all before the first pinner has bumped it.
			// hasAnyPins() is the gate that lets a writer relocate sectors, so it must only
			// ever err by over-reporting.
			//
			// Cost is unchanged in the common case (distinct sectors): one aggregate RMW plus
			// one counter RMW either way. The give-back only runs when two threads pin the
			// same sector at the same time.
			// seq_cst, not acq_rel: pins are taken without any array lock, so this forms a
			// store-load handshake with a writer that publishes a structural epoch and then
			// re-reads the pin count. Both sides must sit in the single total order or each
			// can miss the other, which acquire/release cannot arrange across two variables.
			mTotalPinned.fetch_add(1, std::memory_order_seq_cst);
			if (get(id).fetch_add(1, std::memory_order_seq_cst) != 0) {
				mTotalPinned.fetch_sub(1, std::memory_order_seq_cst);
			}
		}

		/**
		 * @brief Decrement the pin counter for sector id; wakes writers on the last unpin.
		 * @param id Sector id.
		 */
		void unpin(SectorId id) {
			assert(id != INVALID_ID);

			auto& var = get(id);
			// seq_cst, not acq_rel: this decrement and the mWaiters load below form a
			// store-load pair with the waiter (announce -> sample). Every operation on the
			// chain must sit in the single seq_cst total order, otherwise the standard
			// permits this load to miss a waiter that has already committed to blocking,
			// and the wake-up is lost forever. On x86 LOCK XADD is a full barrier anyway,
			// so this costs nothing there.
			if (var.fetch_sub(1, std::memory_order_seq_cst) != 1) {
				return;
			}
			const auto remaining = mTotalPinned.fetch_sub(1, std::memory_order_seq_cst) - 1;

			// Skip the wake syscalls when provably nobody is blocked: a waiter announces
			// itself in mWaiters before sampling the counter it is about to wait on, and
			// every step of that handshake is seq_cst, so a waiter that observed a non-zero
			// value is necessarily visible here.
			if (mWaiters.load(std::memory_order_seq_cst) == 0) {
				return;
			}
			var.notify_all();
			if (remaining == 0) {
				mTotalPinned.notify_all();
			}
		}

		/**
		 * @brief Exact test: may sector @p sectorId be destroyed / overwritten in place?
		 * @note Says nothing about *other* sectors. Anything that relocates sectors must
		 *       use hasAnyPins() / waitUntilQuiescent() instead.
		 */
		bool canMoveSector(SectorId sectorId) const {
			assert(sectorId != INVALID_ID);
			// seq_cst: read by a writer after it has published its structural epoch.
			return get(sectorId).load(std::memory_order_seq_cst) == 0;
		}

		/**
		 * @brief Block until sector @p sid carries no pins.
		 * @warning Covers only @p sid. Use waitUntilQuiescent() before relocating sectors.
		 */
		void waitUntilChangeable(SectorId sid) const {
			assert(sid != INVALID_ID);
			WriterIntent intent(*this);
			auto& var = get(sid);
			for (;;) {
				if (var.load(std::memory_order_acquire) == 0) {
					return;
				}
				WaiterScope scope(mWaiters);
				// Re-sample once announced (seq_cst: same total order as unpin's decrement).
				// The pin may have been dropped between the load above and the announcement,
				// which would otherwise lose the wake-up.
				const auto c = var.load(std::memory_order_seq_cst);
				if (c == 0) {
					return;
				}
				var.wait(c, std::memory_order_acquire);
			}
		}

		/**
		 * @brief Block until no sector at all is pinned.
		 * @note Required before any operation that moves sectors between linear indices.
		 */
		void waitUntilQuiescent() const {
			WriterIntent intent(*this);
			for (;;) {
				if (mTotalPinned.load(std::memory_order_acquire) == 0) {
					return;
				}
				WaiterScope scope(mWaiters);
				const auto n = mTotalPinned.load(std::memory_order_seq_cst);
				if (n == 0) {
					return;
				}
				mTotalPinned.wait(n, std::memory_order_acquire);
			}
		}

		/// @brief True while a writer is waiting for pins to drain.
		///
		/// Readers consult this and yield a bounded number of times before pinning again.
		/// Without it a handful of threads that pin in a loop keep the array permanently
		/// non-quiescent and structural writes never run -- deferred erases pile up and
		/// compaction never happens. The yield is bounded on purpose: a reader may already
		/// hold a pin the writer is waiting for, and blocking here would deadlock.
		FORCE_INLINE bool writersWaiting() const noexcept {
			return mWritersWaiting.load(std::memory_order_relaxed) != 0;
		}

		/// @brief RAII announcement that a writer wants the array to go quiet.
		struct WriterIntent {
			explicit WriterIntent(const PinCounters& p) noexcept : pins(p) {
				pins.mWritersWaiting.fetch_add(1, std::memory_order_relaxed);
			}
			~WriterIntent() { pins.mWritersWaiting.fetch_sub(1, std::memory_order_relaxed); }
			WriterIntent(const WriterIntent&) = delete;
			const PinCounters& pins;
		};

		/// @brief Test whether a sector presently has a non-zero pin counter.
		FORCE_INLINE bool isPinned(SectorId id) const {
			return get(id).load(std::memory_order_seq_cst) != 0;
		}

		/// @brief True if any sector is currently pinned (exact).
		FORCE_INLINE bool hasAnyPins() const noexcept {
			// seq_cst: see the note in pin(). This is the writer half of the handshake.
			return mTotalPinned.load(std::memory_order_seq_cst) != 0;
		}

		/// @brief Alias of hasAnyPins(): no sector may be relocated while this is true.
		FORCE_INLINE bool isArrayLocked() const {
			return hasAnyPins();
		}

		/// @brief Pre-allocate counter blocks covering ids up to and including @p maxId.
		void reserve(SectorId maxId) { (void)get(maxId); }

	private:
		static constexpr size_t BLOCK = 4096;
		using Counter = std::atomic<uint16_t>;

		/// @brief Immutable published snapshot of the block pointer array.
		struct Table {
			size_t    count;
			Counter** blocks;
		};

		/// @brief RAII announce/retract of a blocked waiter; gates the notify syscalls.
		struct WaiterScope {
			explicit WaiterScope(std::atomic<uint32_t>& w) : waiters(w) { waiters.fetch_add(1, std::memory_order_seq_cst); }
			~WaiterScope() { waiters.fetch_sub(1, std::memory_order_seq_cst); }
			WaiterScope(const WaiterScope&) = delete;
			WaiterScope& operator=(const WaiterScope&) = delete;
			std::atomic<uint32_t>& waiters;
		};

		/// @brief Lock-free counter lookup; falls back to the growth path for new blocks.
		FORCE_INLINE Counter& get(SectorId id) const {
			const size_t bi = id / BLOCK;
			if (const auto* table = mTable.load(std::memory_order_acquire); table && bi < table->count) [[likely]] {
				return table->blocks[bi][id % BLOCK];
			}
			return grow(bi)[id % BLOCK];
		}

		/// @brief Allocate the missing blocks and publish a fresh (immutable) table.
		Counter* grow(size_t blockIndex) const {
			auto guard = std::lock_guard(mGrowMtx);

			const auto* cur = mTable.load(std::memory_order_relaxed);
			if (cur && blockIndex < cur->count) {
				return cur->blocks[blockIndex];
			}

			const size_t oldCount = cur ? cur->count : 0;
			// Grow geometrically so the number of superseded tables stays logarithmic.
			const size_t newCount = std::max(blockIndex + 1, oldCount * 2);

			auto** blocks = new Counter*[newCount];
			for (size_t i = 0; i < oldCount; ++i) { blocks[i] = cur->blocks[i]; }
			for (size_t i = oldCount; i < newCount; ++i) {
				auto* block = new Counter[BLOCK];
				for (size_t j = 0; j < BLOCK; ++j) { block[j].store(0, std::memory_order_relaxed); }
				blocks[i] = block;
				mBlocks.push_back(block);
			}

			auto* table = new Table{ newCount, blocks };
			// Superseded tables stay alive until destruction: a lock-free reader may still
			// hold a pointer to one, and they are only O(log maxId) small pointer arrays.
			mTables.push_back(table);
			mTable.store(table, std::memory_order_release);

			return blocks[blockIndex];
		}

	private:
		static_assert(types::isLockFreeAtomic<uint16_t>, "per-sector pin counters must be lock-free");
		static_assert(types::isLockFreeAtomic<uint32_t>, "pin aggregates must be lock-free");
		static_assert(types::isLockFreeAtomic<Table*>,   "the block table snapshot must be lock-free");

		// Read-mostly: every pin/unpin/isPinned loads this, nobody writes it except growth.
		alignas(64) mutable std::atomic<Table*> mTable{ nullptr }; ///< Published block table snapshot.
		mutable std::mutex            mGrowMtx;          ///< Serializes table growth only.
		mutable std::vector<Table*>   mTables;           ///< Every table ever published (freed in dtor).
		mutable std::vector<Counter*> mBlocks;           ///< Every counter block (freed in dtor).

		// Written by every first-pin / last-unpin: keep off the read-mostly line above.
		alignas(64) std::atomic<uint32_t> mTotalPinned{ 0 }; ///< Distinct sectors with counter > 0.
		mutable std::atomic<uint32_t> mWaiters{ 0 };         ///< Threads blocked in a wait primitive.
		mutable std::atomic<uint32_t> mWritersWaiting{ 0 };  ///< Writers waiting for pins to drain.
	};

}
