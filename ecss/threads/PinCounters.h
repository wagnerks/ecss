#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#ifndef NDEBUG
#include <map>
#include <utility>
#endif

#include <ecss/Types.h>

namespace ecss::Threads {

#ifndef NDEBUG
	/**
	 * @brief Debug-only record of what the calling thread is holding, per array.
	 *
	 * Structural changes are not allowed while a view or pin on the same array is alive --
	 * relocating sectors would invalidate the iterator that is reading them. Writers enforce
	 * that by waiting for the pins and holds to drain, which is correct against other
	 * threads and unsatisfiable against yourself: the thread blocks on a condition only it
	 * could clear, and hangs forever with no diagnosis.
	 *
	 * These maps let the wait primitives recognise that case and abort with the reason
	 * attached. They exist only in debug builds; release keeps the original code exactly,
	 * because maintaining them would tax the view hot path (hold acquire/release runs at
	 * ~199M pairs per second and is what makes iteration cheap).
	 */
	struct SelfWaitDebug {
		std::map<const void*, size_t>                       holds;  ///< array -> holds taken here
		std::map<std::pair<const void*, SectorId>, size_t>  pins;   ///< (array, sector) -> pins

		static SelfWaitDebug& current() {
			static thread_local SelfWaitDebug state;
			return state;
		}

		static void addHold(const void* owner) { ++current().holds[owner]; }
		static void dropHold(const void* owner) {
			auto& m = current().holds;
			const auto it = m.find(owner);
			if (it != m.end() && --it->second == 0) { m.erase(it); }
		}
		static void addPin(const void* owner, SectorId id) { ++current().pins[{ owner, id }]; }
		static void dropPin(const void* owner, SectorId id) {
			auto& m = current().pins;
			const auto it = m.find({ owner, id });
			if (it != m.end() && --it->second == 0) { m.erase(it); }
		}

		/// @return true if this thread holds anything at all on @p owner.
		static bool holdsAnythingOn(const void* owner) {
			const auto& state = current();
			if (state.holds.count(owner)) { return true; }
			const auto lo = state.pins.lower_bound({ owner, SectorId{ 0 } });
			return lo != state.pins.end() && lo->first.first == owner;
		}

		static bool holdsPinOn(const void* owner, SectorId id) {
			return current().pins.count({ owner, id }) != 0;
		}
	};
#endif
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
#ifndef NDEBUG
			SelfWaitDebug::addPin(this, id);
#endif
		}

		/**
		 * @brief Decrement the pin counter for sector id; wakes writers on the last unpin.
		 * @param id Sector id.
		 */
		void unpin(SectorId id) {
			assert(id != INVALID_ID);

#ifndef NDEBUG
			SelfWaitDebug::dropPin(this, id);
#endif
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

			// Nothing to wait for is the overwhelmingly common case, and it costs one load to
			// find out. Announcing first meant two atomic RMWs on a shared counter on every
			// single write, to advertise a wait that was not going to happen: measured at
			// 8.3 ns against 0.15 for checking first.
			auto& var = get(sid);
			if (var.load(std::memory_order_acquire) == 0) {
				return;
			}

			// Only this thread could release its own pin on sid, and it is here instead of
			// there, so the wait below would never end. Destroying or overwriting a component
			// you are holding a pin to is the usual way in. Checked here rather than above
			// because a zero count already proves this thread holds no pin on it.
			assert(!SelfWaitDebug::holdsPinOn(this, sid)
				&& "this thread already pins the sector it is trying to change in place -- "
				   "release the pin (or the component pointer that owns it) first");
			WriterIntent intent(*this);
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
			// As in waitUntilChangeable: find out whether there is anything to wait for before
			// paying to announce a wait.
			if (!hasAnyPins()) {
				return;
			}

			// Same self-deadlock, wider: relocating sectors needs *every* pin and hold on the
			// array to be gone, and a live view on it is one of them. Adding a component that
			// lands in the middle, defragmenting, clearing, copying -- all of them come
			// through here, and all of them are illegal while iterating the same array,
			// because the relocation would invalidate the iterator doing the reading.
			assert(!SelfWaitDebug::holdsAnythingOn(this)
				&& "structural change to an array this thread is already iterating or pinning "
				   "-- end the view (or release the pin) before inserting, defragmenting, "
				   "clearing or copying it");
			WriterIntent intent(*this);
			for (;;) {
				if (!hasAnyPins()) {
					return;
				}
				WaiterScope scope(mWaiters);

				// Sample both wake-up sources before re-checking, so a release that lands in
				// between still wakes us. Holds are summed across shards, so there is no single
				// word to wait on; mHoldGeneration is bumped on every release and serves as it.
				const auto gen = mHoldGeneration.load(std::memory_order_seq_cst);
				const auto pinned = mTotalPinned.load(std::memory_order_seq_cst);
				if (pinned == 0 && !anyHold()) {
					return;
				}
				if (pinned != 0) {
					mTotalPinned.wait(pinned, std::memory_order_acquire);
				}
				else {
					mHoldGeneration.wait(gen, std::memory_order_acquire);
				}
			}
		}

		/// @brief Take a structural hold: no sector may be relocated while one is outstanding.
		///
		/// This is a different question from "is this sector busy". A view needs the array not
		/// to be compacted underneath it; it does not care which sector it names. It used to
		/// express that by pinning the back sector, so every view on every thread contended on
		/// one sector counter. Holds are keyed by thread instead, so they spread: measured
		/// 25.7M -> 199M acquire/release pairs per second at four threads.
		///
		/// @return the shard to hand back to releaseHold().
		[[nodiscard]] uint32_t acquireHold() const noexcept {
			const auto shard = static_cast<uint32_t>(holdShard());
			mHolds[shard].count.fetch_add(1, std::memory_order_seq_cst);
#ifndef NDEBUG
			SelfWaitDebug::addHold(this);
#endif
			return shard;
		}

		/// @param shard the value returned by acquireHold(); a hold may be released by a
		///        different thread than took it, so the shard travels with the holder.
		void releaseHold(uint32_t shard) const noexcept {
#ifndef NDEBUG
			SelfWaitDebug::dropHold(this);
#endif
			mHolds[shard].count.fetch_sub(1, std::memory_order_seq_cst);
			mHoldGeneration.fetch_add(1, std::memory_order_seq_cst);
			if (mWaiters.load(std::memory_order_seq_cst) != 0) {
				mHoldGeneration.notify_all();
			}
		}

		/// @brief True if any structural hold is outstanding. Sums the shards, so it is
		/// deliberately not something to spin on -- waitUntilQuiescent samples it once per
		/// wait, not in a tight loop.
		FORCE_INLINE bool anyHold() const noexcept {
			for (size_t i = 0; i < kHoldShards; ++i) {
				if (mHolds[i].count.load(std::memory_order_seq_cst) != 0) { return true; }
			}
			return false;
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
		/// @brief The relocation gate: true if anything at all forbids moving sectors.
		FORCE_INLINE bool hasAnyPins() const noexcept {
			// seq_cst: see the note in pin(). This is the writer half of the handshake.
			return mTotalPinned.load(std::memory_order_seq_cst) != 0 || anyHold();
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

		/// Sixteen shards carry nearly all of the benefit -- measured 199M acquire/release
		/// pairs per second at four threads against 198M for sixty-four -- while keeping the
		/// writer-side sum at about 4 ns instead of 14 ns.
		static constexpr size_t kHoldShards = 16;

		struct alignas(64) HoldShard { std::atomic<uint32_t> count{ 0 }; };

		/// @brief Stable per-thread shard index, so holds from different threads rarely share
		/// a cache line.
		static size_t holdShard() noexcept {
			static std::atomic<size_t> counter{ 0 };
			static thread_local const size_t slot = counter.fetch_add(1, std::memory_order_relaxed);
			return slot % kHoldShards;
		}

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

		mutable HoldShard             mHolds[kHoldShards]{}; ///< Structural holds, keyed by thread.
		mutable std::atomic<uint64_t> mHoldGeneration{ 0 };  ///< Bumped on release; what waiters wait on.
	};

}
