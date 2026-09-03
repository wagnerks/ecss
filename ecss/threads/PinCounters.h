#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
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

		/// @thread_safety Internally synchronized. Per-thread state: each thread sees only its own
		///                map, so there is nothing to share and nothing to lock. Debug builds only.
		static void addHold(const void* owner) { ++current().holds[owner]; }
		/// @thread_safety Internally synchronized. Per-thread state: each thread sees only its own
		///                map, so there is nothing to share and nothing to lock. Debug builds only.
		static void dropHold(const void* owner) {
			auto& m = current().holds;
			const auto it = m.find(owner);
			if (it != m.end() && --it->second == 0) { m.erase(it); }
		}
		/// @thread_safety Internally synchronized. Per-thread state: each thread sees only its own
		///                map, so there is nothing to share and nothing to lock. Debug builds only.
		static void addPin(const void* owner, SectorId id) { ++current().pins[{ owner, id }]; }
		/// @thread_safety Internally synchronized. Per-thread state: each thread sees only its own
		///                map, so there is nothing to share and nothing to lock. Debug builds only.
		static void dropPin(const void* owner, SectorId id) {
			auto& m = current().pins;
			const auto it = m.find({ owner, id });
			if (it != m.end() && --it->second == 0) { m.erase(it); }
		}

		/// @return true if this thread holds anything at all on @p owner.
		/// @thread_safety Internally synchronized. Per-thread state: each thread sees only its own
		///                map, so there is nothing to share and nothing to lock. Debug builds only.
		static bool holdsAnythingOn(const void* owner) {
			const auto& state = current();
			if (state.holds.count(owner)) { return true; }
			const auto lo = state.pins.lower_bound({ owner, SectorId{ 0 } });
			return lo != state.pins.end() && lo->first.first == owner;
		}

		/// @thread_safety Internally synchronized. Per-thread state: each thread sees only its own
		///                map, so there is nothing to share and nothing to lock. Debug builds only.
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
	 *   - the pin shards sum to the number of distinct sectors whose counter > 0.
	 *   - that sum is 0  <=>  every per-sector counter is 0.
	 *
	 * Locking: pin()/unpin() take no locks at all. The counter block table grows under a
	 * mutex and is published as an immutable snapshot, so lookup is lock-free.
	 *
	 * Ordering: pins are always taken while holding at least the owning array shared
	 * lock, and writers test these predicates under its unique lock, so the array mutex
	 * supplies the happens-before between "reader pinned" and "writer looked"; the
	 * atomics here only need to be individually coherent. unpin() runs without any lock,
	 * which is why the wait primitives use atomic wait/notify.
	 * @thread_safety Internally synchronized. Every member is atomic and takes no lock; readers
	 *                pin and hold without holding the array's mutex at all, which is why writers
	 *                publish a structural epoch and re-check rather than trusting the lock.
	 *
	 *                The wait members are the exception and block by design:
	 *                  - waitUntilChangeable(id) waits for one sector's pins.
	 *                  - waitUntilQuiescent()    waits for every pin and every hold on the array.
	 *                Neither can be satisfied by the thread that is itself holding what they wait
	 *                for; debug builds assert on that, release builds hang. @see SectorsArray
	 */
	struct PinCounters {
		PinCounters() = default;
		PinCounters(const PinCounters&) = delete;
		PinCounters& operator=(const PinCounters&) = delete;

		/// @thread_safety Caller must ensure exclusive access. Frees the counter tables and blocks outright.
		///                Nothing may be pinning or holding, and nothing may call get() afterwards.
		~PinCounters() {
			for (auto* table : mTables) { delete[] table->blocks; delete table; }
			for (auto* block : mBlocks) { delete[] block; }
		}

		/**
		 * @brief Increment the pin counter for sector id.
		 * @param id Sector id (!= INVALID_ID).
		 * @thread_safety Internally synchronized. Lock-free: atomic increments, no lock taken and
		 *                no wait. May grow the counter table on a first touch of a high id, and that
		 *                growth is itself synchronized. Publishes before it checks, so a writer that
		 *                bumped its epoch first is always seen.
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
			addOutstanding();
			auto& shard = pinShardFor(id);
			shard.count.fetch_add(1, std::memory_order_seq_cst);
			if (get(id).fetch_add(1, std::memory_order_seq_cst) != 0) {
				shard.count.fetch_sub(1, std::memory_order_seq_cst);
			}
#ifndef NDEBUG
			SelfWaitDebug::addPin(this, id);
#endif
		}

		/**
		 * @brief Decrement the pin counter for sector id; wakes writers on the last unpin.
		 * @param id Sector id.
		 * @thread_safety Internally synchronized. Lock-free: atomic decrements, plus a wake for any
		 *                waiter that announced itself. Skips the wake syscall when provably nobody is
		 *                blocked -- the announce/sample handshake is seq_cst on both sides so a waiter
		 *                that committed to blocking cannot be missed.
		 */
		void unpin(SectorId id) {
			assert(id != INVALID_ID);

#ifndef NDEBUG
			SelfWaitDebug::dropPin(this, id);
#endif
			dropOutstanding();
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
			pinShardFor(id).count.fetch_sub(1, std::memory_order_seq_cst);

			// Skip the wake syscalls when provably nobody is blocked: a waiter announces
			// itself in mWaiters before sampling the counter it is about to wait on, and
			// every step of that handshake is seq_cst, so a waiter that observed a non-zero
			// value is necessarily visible here.
			if (mWaiters.load(std::memory_order_seq_cst) == 0) {
				return;
			}
			var.notify_all();
			// A quiescence waiter sleeps on the generation rather than on a count: with the
			// aggregate sharded there is no single word left to wait on. It re-reads every
			// shard when it wakes, so bumping on any last-unpin instead of only on the last
			// one on the array costs a spurious wake-up and never a missed one.
			mReleaseGeneration.fetch_add(1, std::memory_order_seq_cst);
			mReleaseGeneration.notify_all();
		}

		/**
		 * @brief Exact test: may sector @p sectorId be destroyed / overwritten in place?
		 * @note Says nothing about *other* sectors. Anything that relocates sectors must
		 *       use hasAnyPins() / waitUntilQuiescent() instead.
		 * @thread_safety Internally synchronized. One seq_cst load. Meaningful only after the caller
		 *                has published its structural epoch; before that it is a stale sample.
		 */
		bool canMoveSector(SectorId sectorId) const {
			assert(sectorId != INVALID_ID);
			// seq_cst: read by a writer after it has published its structural epoch.
			return get(sectorId).load(std::memory_order_seq_cst) == 0;
		}

		/**
		 * @brief Block until sector @p sid carries no pins.
		 * @warning Covers only @p sid. Use waitUntilQuiescent() before relocating sectors.
		 * @thread_safety Internally synchronized; blocks on one sector. Returns at once when that
		 *                sector carries no pins, which is the common case and costs one load. Waits
		 *                otherwise -- and cannot be satisfied by the thread that holds the pin itself.
		 *                Debug builds assert on that; release builds hang.
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
			bool bounded = false;
			const auto deadline = waitDeadline(bounded);
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
				if (!waitOrDeadline(var, c, deadline, bounded)) {
					reportStuckWait("waiting for a sector's pins to be released");
				}
			}
		}

		/**
		 * @brief Block until no sector at all is pinned.
		 * @note Required before any operation that moves sectors between linear indices.
		 * @thread_safety Internally synchronized; blocks on the whole array. Waits for every pin and
		 *                every hold, so a single open view anywhere holds it. From the thread that
		 *                owns that view it never returns. Debug builds assert; release builds hang.
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
			bool bounded = false;
			const auto deadline = waitDeadline(bounded);
			WriterIntent intent(*this);
			for (;;) {
				if (!hasAnyPins()) {
					return;
				}
				WaiterScope scope(mWaiters);

				// Sample the generation before re-checking, so a release that lands in between
				// still wakes us. Pins and holds are both summed across shards, so neither has a
				// single word left to wait on; every release bumps this one instead -- and only
				// once we have announced ourselves above, which is what keeps the release paths
				// off a shared cache line when nobody is waiting.
				const auto gen = mReleaseGeneration.load(std::memory_order_seq_cst);
				if (!hasAnyPins()) {
					return;
				}
				if (!waitOrDeadline(mReleaseGeneration, gen, deadline, bounded)) {
					reportStuckWait("waiting for an array to carry no pins and no holds");
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
		/// @thread_safety Internally synchronized. One atomic increment on a per-thread shard, so
		///                holders on different threads do not share a cache line. Never waits.
		[[nodiscard]] uint32_t acquireHold() const noexcept {
			const auto shard = static_cast<uint32_t>(holdShard());
			addOutstanding();
			mHolds[shard].count.fetch_add(1, std::memory_order_seq_cst);
#ifndef NDEBUG
			SelfWaitDebug::addHold(this);
#endif
			return shard;
		}

		/// @param shard the value returned by acquireHold(); a hold may be released by a
		///        different thread than took it, so the shard travels with the holder.
		/// @thread_safety Internally synchronized. Atomic decrement plus a generation bump, and a
		///                wake only when a waiter has announced itself. May be called from a different
		///                thread than took the hold -- the shard travels with the holder.
		void releaseHold(uint32_t shard) const noexcept {
#ifndef NDEBUG
			SelfWaitDebug::dropHold(this);
#endif
			dropOutstanding();
			mHolds[shard].count.fetch_sub(1, std::memory_order_seq_cst);
			// The generation exists so a waiter has one word to sleep on; with nobody waiting
			// there is nothing to tell. Bumping it unconditionally put every view close on this
			// thread back onto one cache line -- the very contention the sharded counts above
			// exist to avoid, and it capped view throughput at about twice single-threaded
			// however many cores were available.
			//
			// Safe because of the order the waiter uses: it announces itself in mWaiters, then
			// samples the counts, then re-checks them. Every step is seq_cst, so either this
			// load sees the announcement and wakes it, or the announcement came after this load
			// and its sample therefore comes after the decrement above -- and it sees the array
			// free without needing to be woken.
			if (mWaiters.load(std::memory_order_seq_cst) != 0) {
				mReleaseGeneration.fetch_add(1, std::memory_order_seq_cst);
				mReleaseGeneration.notify_all();
			}
		}

		/// @brief True if any structural hold is outstanding. Sums the shards, so it is
		/// deliberately not something to spin on -- waitUntilQuiescent samples it once per
		/// wait, not in a tight loop.
		/// @thread_safety Internally synchronized. Sums the shards, so it is a handful of loads rather
		///                than one. Deliberately not something to spin on.
		/// @brief True if any sector on this array carries a pin.
		///
		/// Inexact by construction, in the safe direction: a pin taken while the loop is
		/// midway through can be missed. The structural epoch is what closes that -- a writer
		/// publishes it before asking, and a pinner re-reads it after pinning and starts
		/// over if it moved. anyHold() is inexact the same way, for the same reason.
		/// @thread_safety Internally synchronized. One load per shard.
		FORCE_INLINE bool anyPin() const noexcept {
			for (size_t i = 0; i < kPinShards; ++i) {
				if (mPinShards[i].count.load(std::memory_order_seq_cst) != 0) { return true; }
			}
			return false;
		}

		FORCE_INLINE bool anyHold() const noexcept {
			for (size_t i = 0; i < kHoldShards; ++i) {
				if (mHolds[i].count.load(std::memory_order_seq_cst) != 0) { return true; }
			}
			return false;
		}

		/// @brief How long a structural wait may run before it is treated as a deadlock.
		///
		/// A writer waiting for quiescence cannot be satisfied by the thread that is itself
		/// holding a view on the array -- it waits for a condition only it could clear. Debug
		/// builds assert on that; release builds used to simply stop, with no output and no
		/// stack worth reading, which is the least diagnosable failure a library can have.
		///
		/// So the wait is bounded. Nothing legitimate takes anywhere near this long: the
		/// longest honest wait is one frame's worth of open views. Set it to zero to wait
		/// forever, which restores the old behaviour.
		/// @thread_safety Internally synchronized. One relaxed store; set it at startup.
		static std::atomic<uint32_t>& waitTimeoutSeconds() noexcept {
			static std::atomic<uint32_t> seconds{ 10 };
			return seconds;
		}

		/// @brief Wait for @p word to stop reading @p expected, or give up at the deadline.
		/// @return false if the deadline passed with the value unchanged.
		///
		/// std::atomic::wait has no timed form, and the case worth catching is exactly the one
		/// where no notify is ever coming -- so this spins, then yields, then polls. The spin
		/// covers the common handover, which is microseconds; the poll costs one wakeup per
		/// millisecond on a wait that was going to be long anyway.
		template <class T>
		static bool waitOrDeadline(const std::atomic<T>& word, T expected,
		                           std::chrono::steady_clock::time_point deadline,
		                           bool bounded) noexcept {
			for (int i = 0; i < 64; ++i) {
				if (word.load(std::memory_order_acquire) != expected) { return true; }
				cpuRelax();
			}
			for (int i = 0; i < 64; ++i) {
				if (word.load(std::memory_order_acquire) != expected) { return true; }
				std::this_thread::yield();
			}
			while (word.load(std::memory_order_acquire) == expected) {
				if (bounded && std::chrono::steady_clock::now() > deadline) { return false; }
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			return true;
		}

		/// @brief Report a wait that never ended, and stop. @see waitTimeoutSeconds
		[[noreturn]] static void reportStuckWait(const char* what) noexcept {
			std::fprintf(stderr,
				"ecss: %s did not complete within %u seconds.\n"
				"      A structural change waits for every pin and hold on the array to be\n"
				"      released. If this thread is itself holding a view on that array, it is\n"
				"      waiting for something only it can release, and will wait forever.\n"
				"      Close the view first, or use the deferred form -- clearAsync(),\n"
				"      eraseAsync(), Registry::update(), or an ecss::CommandBuffer.\n",
				what, waitTimeoutSeconds().load(std::memory_order_relaxed));
			std::fflush(stderr);
			std::abort();
		}

		/// @brief The deadline a wait starting now should honour.
		static std::chrono::steady_clock::time_point waitDeadline(bool& bounded) noexcept {
			const auto secs = waitTimeoutSeconds().load(std::memory_order_relaxed);
			bounded = secs != 0;
			return std::chrono::steady_clock::now() + std::chrono::seconds(secs ? secs : 1);
		}

		/// @brief How many pins and holds this thread has outstanding, on any array.
		///
		/// Consulted for one question only -- may I block here without waiting for myself --
		/// so it is built to err upward. A handle released by a different thread than took it
		/// leaves the taker's count high, which costs that thread a chance to block and never
		/// the other way round; the release side clamps at zero for the same reason.
		/// @thread_safety Internally synchronized. Thread-local: no shared state at all.
		static int& threadOutstanding() noexcept { static thread_local int n = 0; return n; }

		/// @thread_safety Internally synchronized. Thread-local.
		static void addOutstanding() noexcept { ++threadOutstanding(); }

		/// @thread_safety Internally synchronized. Thread-local; clamped, see threadOutstanding().
		static void dropOutstanding() noexcept {
			auto& n = threadOutstanding();
			if (n > 0) { --n; }
		}

		/// @brief True when this thread holds no pin and no hold anywhere.
		/// @thread_safety Internally synchronized. Thread-local.
		static bool threadHoldsNothing() noexcept { return threadOutstanding() == 0; }

		/// @brief Block until no writer is waiting on this array.
		///
		/// The counterpart to the bounded yield: a reader that holds nothing can afford to
		/// wait properly, and that is what lets a writer ever reach quiescence when readers
		/// are opening views back to back. A thread that already holds a pin or a hold must
		/// NOT come here -- the writer may be waiting for exactly that.
		/// @thread_safety Internally synchronized; blocks until the writers pass.
		void waitForWritersToPass() const noexcept {
			for (;;) {
				const auto w = mWritersWaiting.load(std::memory_order_acquire);
				if (w == 0) { return; }
				mWritersWaiting.wait(w, std::memory_order_acquire);
			}
		}

		/// @brief True while a writer is waiting for pins to drain.
		///
		/// Readers consult this and yield a bounded number of times before pinning again.
		/// Without it a handful of threads that pin in a loop keep the array permanently
		/// non-quiescent and structural writes never run -- deferred erases pile up and
		/// compaction never happens. The yield is bounded on purpose: a reader may already
		/// hold a pin the writer is waiting for, and blocking here would deadlock.
		/// @thread_safety Internally synchronized. One relaxed load. A hint for readers to yield, not
		///                a gate: it is allowed to be stale in either direction.
		FORCE_INLINE bool writersWaiting() const noexcept {
			return mWritersWaiting.load(std::memory_order_relaxed) != 0;
		}

		/// @brief RAII announcement that a writer wants the array to go quiet.
		struct WriterIntent {
			/// @thread_safety Internally synchronized. Two relaxed increments over its lifetime. Announces
			///                that a writer is waiting so readers back off; never waits itself.
			explicit WriterIntent(const PinCounters& p) noexcept : pins(p) {
				// Release, not relaxed: a reader parks on this word, so the announcement has to
				// be ordered against the wait rather than merely eventually visible.
				pins.mWritersWaiting.fetch_add(1, std::memory_order_acq_rel);
			}
			~WriterIntent() {
				if (pins.mWritersWaiting.fetch_sub(1, std::memory_order_acq_rel) == 1) {
					pins.mWritersWaiting.notify_all();
				}
			}
			WriterIntent(const WriterIntent&) = delete;
			const PinCounters& pins;
		};

		/// @brief Test whether a sector presently has a non-zero pin counter.
		/// @thread_safety Internally synchronized. One acquire load; true at the moment of the call.
		FORCE_INLINE bool isPinned(SectorId id) const {
			return get(id).load(std::memory_order_seq_cst) != 0;
		}

		/// @brief True if any sector is currently pinned (exact).
		/// @brief The relocation gate: true if anything at all forbids moving sectors.
		/// @thread_safety Internally synchronized. One load plus anyHold(). This is the gate for
		///                relocating sectors, so it errs by over-reporting and never the other way.
		FORCE_INLINE bool hasAnyPins() const noexcept {
			// seq_cst: see the note in pin(). This is the writer half of the handshake.
			return anyPin() || anyHold();
		}

		/// @brief True if any sector on this array carries a pin. Says nothing about holds.
		///
		/// An empty sum across the pin shards is exactly "every per-sector counter is zero",
		/// so a writer that
		/// only changes named sectors in place can ask this one question instead of asking
		/// about each id it named. Not enough to relocate anything -- that needs hasAnyPins(),
		/// which counts holds too.
		/// @thread_safety Internally synchronized. One seq_cst load.
		FORCE_INLINE bool hasAnyPinnedSector() const noexcept {
			// seq_cst: same handshake as hasAnyPins(), and read after the writer has published
			// its structural epoch.
			return anyPin();
		}

		/// @brief Alias of hasAnyPins(): no sector may be relocated while this is true.
		/// @thread_safety Internally synchronized. Alias of hasAnyPins().
		FORCE_INLINE bool isArrayLocked() const {
			return hasAnyPins();
		}

		/// @brief Pre-allocate counter blocks covering ids up to and including @p maxId.
		/// @thread_safety Internally synchronized. Touches the counter for maxId, which allocates the
		///                blocks up to it under the growth mutex. Doing it up front keeps that
		///                allocation off the first pin.
		void reserve(SectorId maxId) { (void)get(maxId); }

	private:
		static constexpr size_t BLOCK = 4096;
		using Counter = std::atomic<uint16_t>;

		/// Sixteen shards carry nearly all of the benefit -- measured 199M acquire/release
		/// pairs per second at four threads against 198M for sixty-four -- while keeping the
		/// writer-side sum at about 4 ns instead of 14 ns.
		static constexpr size_t kHoldShards = 16;

		/// Same bargain as kHoldShards, for the pin aggregate. Sixteen because sequential
		/// ids -- which is how they are handed out -- then land one per shard.
		static constexpr size_t kPinShards = 16;
		static_assert((kPinShards & (kPinShards - 1)) == 0, "kPinShards must be a power of two");

		struct alignas(64) PinShard { std::atomic<uint32_t> count{ 0 }; };

		/// @brief The shard a sector's pin is counted in.
		///
		/// Keyed by id, not by thread, and that is not interchangeable here: the increment
		/// and the decrement for one sector have to land on the same shard, and a pin taken
		/// on one thread can be released from another.
		FORCE_INLINE PinShard& pinShardFor(SectorId id) const noexcept {
			return mPinShards[static_cast<size_t>(id) & (kPinShards - 1)];
		}

		struct alignas(64) HoldShard { std::atomic<uint32_t> count{ 0 }; };

		/// @brief Stable per-thread shard index, so holds from different threads rarely share
		/// a cache line.
		/// @thread_safety Internally synchronized. Derived from the calling thread's identity; no
		///                shared state is read.
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
			/// @thread_safety Internally synchronized. Two seq_cst increments over its lifetime. Announces
			///                a blocked waiter *before* it samples what it is about to wait on, which is
			///                what stops a concurrent release from losing the wake-up.
			explicit WaiterScope(std::atomic<uint32_t>& w) : waiters(w) { waiters.fetch_add(1, std::memory_order_seq_cst); }
			~WaiterScope() { waiters.fetch_sub(1, std::memory_order_seq_cst); }
			WaiterScope(const WaiterScope&) = delete;
			WaiterScope& operator=(const WaiterScope&) = delete;
			std::atomic<uint32_t>& waiters;
		};

		/// @brief Lock-free counter lookup; falls back to the growth path for new blocks.
		/// @thread_safety Internally synchronized. Lock-free on the warm path: one acquire load of the
		///                published table. A first touch of an id past the end goes through grow(),
		///                which takes the growth mutex.
		FORCE_INLINE Counter& get(SectorId id) const {
			const size_t bi = id / BLOCK;
			if (const auto* table = mTable.load(std::memory_order_acquire); table && bi < table->count) [[likely]] {
				return table->blocks[bi][id % BLOCK];
			}
			return grow(bi)[id % BLOCK];
		}

		/// @brief Allocate the missing blocks and publish a fresh (immutable) table.
		/// @thread_safety Internally synchronized. Takes the growth mutex and publishes a fresh,
		///                immutable table. Superseded tables are kept, not freed, so a reader holding a
		///                pointer into the old one stays valid until the counters are destroyed.
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
		/// Distinct sectors with a non-zero counter, split across shards so that pinning
		/// unrelated sectors does not put every thread on one line. Was a single counter, and
		/// that made pin/unpin throughput *fall* as threads were added: 39.8 ns/op on one
		/// thread against 89.6 across thirty-two. @see bench/MTBench.cpp
		mutable PinShard              mPinShards[kPinShards]{};

		alignas(64) mutable std::atomic<uint32_t> mWaiters{ 0 }; ///< Threads blocked in a wait primitive.
		mutable std::atomic<uint32_t> mWritersWaiting{ 0 };  ///< Writers waiting for pins to drain.

		mutable HoldShard             mHolds[kHoldShards]{}; ///< Structural holds, keyed by thread.
		/// The one word a waiter sleeps on. Pins and holds are both sharded, so neither has a
		/// single counter left to wait on; every release bumps this instead -- but only while
		/// a waiter has announced itself in mWaiters, so the common case touches nothing here.
		mutable std::atomic<uint64_t> mReleaseGeneration{ 0 };
	};

}
