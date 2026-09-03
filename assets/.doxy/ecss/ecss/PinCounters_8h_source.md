

# File PinCounters.h

[**File List**](files.md) **>** [**ecss**](dir_194708e763cf312315c6b23555bce86f.md) **>** [**threads**](dir_a9a674ced088cdcac6c51605566c5246.md) **>** [**PinCounters.h**](PinCounters_8h.md)

[Go to the documentation of this file](PinCounters_8h.md)


```C++
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
    struct SelfWaitDebug {
        std::map<const void*, size_t>                       holds;  
        std::map<std::pair<const void*, SectorId>, size_t>  pins;   

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
    struct PinCounters {
        PinCounters() = default;
        PinCounters(const PinCounters&) = delete;
        PinCounters& operator=(const PinCounters&) = delete;

        ~PinCounters() {
            for (auto* table : mTables) { delete[] table->blocks; delete table; }
            for (auto* block : mBlocks) { delete[] block; }
        }

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

        bool canMoveSector(SectorId sectorId) const {
            assert(sectorId != INVALID_ID);
            // seq_cst: read by a writer after it has published its structural epoch.
            return get(sectorId).load(std::memory_order_seq_cst) == 0;
        }

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

        [[nodiscard]] uint32_t acquireHold() const noexcept {
            const auto shard = static_cast<uint32_t>(holdShard());
            addOutstanding();
            mHolds[shard].count.fetch_add(1, std::memory_order_seq_cst);
#ifndef NDEBUG
            SelfWaitDebug::addHold(this);
#endif
            return shard;
        }

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

        static std::atomic<uint32_t>& waitTimeoutSeconds() noexcept {
            static std::atomic<uint32_t> seconds{ 10 };
            return seconds;
        }

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

        static std::chrono::steady_clock::time_point waitDeadline(bool& bounded) noexcept {
            const auto secs = waitTimeoutSeconds().load(std::memory_order_relaxed);
            bounded = secs != 0;
            return std::chrono::steady_clock::now() + std::chrono::seconds(secs ? secs : 1);
        }

        static int& threadOutstanding() noexcept { static thread_local int n = 0; return n; }

        static void addOutstanding() noexcept { ++threadOutstanding(); }

        static void dropOutstanding() noexcept {
            auto& n = threadOutstanding();
            if (n > 0) { --n; }
        }

        static bool threadHoldsNothing() noexcept { return threadOutstanding() == 0; }

        void waitForWritersToPass() const noexcept {
            // waitOrDeadline, not mWritersWaiting.wait(): this was the one wait in the file
            // still parking on std::atomic::wait directly, and that is not the same latency
            // everywhere. glibc backs it with a futex and the wake is immediate; libc++ on
            // Apple falls back to polling with exponential backoff when no platform primitive
            // is available, and the sleep grows into the hundreds of milliseconds. The notify
            // arrives on time and the sleeper notices it a quarter of a second later.
            //
            // A reader stands here holding nothing, so nobody is blocked behind it -- but it
            // is not making progress either, and a caller measuring throughput sees a stall
            // it cannot explain. The polling helper bounds that at a millisecond on every
            // platform, and brings the diagnostic every other wait here already had.
            bool bounded = false;
            const auto deadline = waitDeadline(bounded);
            for (;;) {
                const auto w = mWritersWaiting.load(std::memory_order_acquire);
                if (w == 0) { return; }
                if (!waitOrDeadline(mWritersWaiting, w, deadline, bounded)) {
                    reportStuckWait("a reader standing aside for structural writers");
                }
            }
        }

        FORCE_INLINE bool writersWaiting() const noexcept {
            return mWritersWaiting.load(std::memory_order_relaxed) != 0;
        }

        struct WriterIntent {
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

        FORCE_INLINE bool isPinned(SectorId id) const {
            return get(id).load(std::memory_order_seq_cst) != 0;
        }

        FORCE_INLINE bool hasAnyPins() const noexcept {
            // seq_cst: see the note in pin(). This is the writer half of the handshake.
            return anyPin() || anyHold();
        }

        FORCE_INLINE bool hasAnyPinnedSector() const noexcept {
            // seq_cst: same handshake as hasAnyPins(), and read after the writer has published
            // its structural epoch.
            return anyPin();
        }

        FORCE_INLINE bool isArrayLocked() const {
            return hasAnyPins();
        }

        void reserve(SectorId maxId) { (void)get(maxId); }

    private:
        static constexpr size_t BLOCK = 4096;
        using Counter = std::atomic<uint16_t>;

        static constexpr size_t kHoldShards = 16;

        static constexpr size_t kPinShards = 16;
        static_assert((kPinShards & (kPinShards - 1)) == 0, "kPinShards must be a power of two");

        struct alignas(64) PinShard { std::atomic<uint32_t> count{ 0 }; };

        FORCE_INLINE PinShard& pinShardFor(SectorId id) const noexcept {
            return mPinShards[static_cast<size_t>(id) & (kPinShards - 1)];
        }

        struct alignas(64) HoldShard { std::atomic<uint32_t> count{ 0 }; };

        static size_t holdShard() noexcept {
            static std::atomic<size_t> counter{ 0 };
            static thread_local const size_t slot = counter.fetch_add(1, std::memory_order_relaxed);
            return slot % kHoldShards;
        }

        struct Table {
            size_t    count;
            Counter** blocks;
        };

        struct WaiterScope {
            explicit WaiterScope(std::atomic<uint32_t>& w) : waiters(w) { waiters.fetch_add(1, std::memory_order_seq_cst); }
            ~WaiterScope() { waiters.fetch_sub(1, std::memory_order_seq_cst); }
            WaiterScope(const WaiterScope&) = delete;
            WaiterScope& operator=(const WaiterScope&) = delete;
            std::atomic<uint32_t>& waiters;
        };

        FORCE_INLINE Counter& get(SectorId id) const {
            const size_t bi = id / BLOCK;
            if (const auto* table = mTable.load(std::memory_order_acquire); table && bi < table->count) [[likely]] {
                return table->blocks[bi][id % BLOCK];
            }
            return grow(bi)[id % BLOCK];
        }

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
        alignas(64) mutable std::atomic<Table*> mTable{ nullptr }; 
        mutable std::mutex            mGrowMtx;          
        mutable std::vector<Table*>   mTables;           
        mutable std::vector<Counter*> mBlocks;           

        // Written by every first-pin / last-unpin: keep off the read-mostly line above.
        mutable PinShard              mPinShards[kPinShards]{};

        alignas(64) mutable std::atomic<uint32_t> mWaiters{ 0 }; 
        mutable std::atomic<uint32_t> mWritersWaiting{ 0 };  

        mutable HoldShard             mHolds[kHoldShards]{}; 
        mutable std::atomic<uint64_t> mReleaseGeneration{ 0 };
    };

}
```


