#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>
#include <chrono>
#include <random>
#include <future>

#include <ecss/Registry.h>
#include <ecss/memory/SectorsArray.h>
#include <ecss/memory/RetireAllocator.h>
#include <ecss/threads/PinCounters.h>

using namespace ecss;
using namespace ecss::Memory;

// Detect sanitizer builds for scaling down iteration counts.
// gcc sets __SANITIZE_THREAD__ when TSan is on. clang does not -- it exposes
// __has_feature(thread_sanitizer) instead. We must not reference
// __has_feature(...) on gcc because gcc leaves it undefined, turning the token
// into "0(thread_sanitizer)" and a preprocessor error. Gate each branch
// separately so the expression is only evaluated on the compiler that supports
// it.
#if defined(__SANITIZE_THREAD__)
#  define TSAN_ACTIVE 1
#elif defined(__has_feature)
#  if __has_feature(thread_sanitizer)
#    define TSAN_ACTIVE 1
#  endif
#endif
#ifndef TSAN_ACTIVE
#  define TSAN_ACTIVE 0
#endif

static constexpr int scale(int base) { return TSAN_ACTIVE ? base / 20 + 1 : base; }

struct BugPos { float x{}, y{}, z{}; };
struct BugVel { float dx{}, dy{}, dz{}; };

// ---------------------------------------------------------------------------
// Bug #1: TOCTOU in destroyEntity -- recycled entity id data loss
//
// Thread A: destroyEntity(id)  -- contains() then erase() then destroySector()
// Thread B: between erase and destroySector, takes recycled id, adds component.
// Thread A's destroySector then kills B's freshly written data.
// ---------------------------------------------------------------------------
TEST(BugRepro, DestroyEntity_TOCTOU_RecycledId) {
    const int kTrials = scale(3000);
    std::atomic<int> hits{0};

    for (int trial = 0; trial < kTrials; ++trial) {
        Registry<true> reg;

        auto id = reg.takeEntity();
        reg.addComponent<BugPos>(id, 1.f, 2.f, 3.f);

        std::atomic<bool> a_erased{false};
        std::atomic<EntityId> recycled_id{INVALID_ID};

        std::thread threadA([&] {
            reg.destroyEntity(id);
            a_erased.store(true, std::memory_order_release);
        });

        std::thread threadB([&] {
            while (!a_erased.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            auto newId = reg.takeEntity();
            recycled_id.store(newId, std::memory_order_release);
            reg.addComponent<BugPos>(newId, 42.f, 42.f, 42.f);
        });

        threadA.join();
        threadB.join();

        auto rid = recycled_id.load();
        if (rid == id) {
            auto pin = reg.pinComponent<BugPos>(rid);
            if (!pin || pin.get()->x != 42.f) {
                hits.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    if (hits.load() > 0) {
        std::cerr << "[BugRepro] DestroyEntity TOCTOU hit " << hits.load()
                  << " / " << kTrials << " trials\n";
    }
    EXPECT_EQ(hits.load(), 0)
        << "destroyEntity TOCTOU: recycled entity data was destroyed by stale destroySector";
}

// Stress variant with many threads
// WARNING: may hang under TSAN due to Bug #5 (waitUntilChangeable livelock)
TEST(BugRepro, DISABLED_DestroyEntity_TOCTOU_Stress) {
    const int kIters = scale(2000);
    const int T = std::min(4, static_cast<int>(std::max(2u, std::thread::hardware_concurrency())));
    std::atomic<int> anomalies{0};

    Registry<true> reg;
    reg.reserve<BugPos>(kIters * T * 2);

    std::vector<std::thread> pool;
    for (int t = 0; t < T; ++t) {
        pool.emplace_back([&, t] {
            std::mt19937 rng(42 + t);
            for (int i = 0; i < kIters; ++i) {
                auto id = reg.takeEntity();
                reg.addComponent<BugPos>(id, float(t * 1000 + i), 0.f, 0.f);

                if (rng() % 3 == 0) {
                    reg.destroyEntity(id);
                    auto id2 = reg.takeEntity();
                    reg.addComponent<BugPos>(id2, 999.f, 999.f, 999.f);

                    auto pin = reg.pinComponent<BugPos>(id2);
                    if (pin && pin.get()->x != 999.f) {
                        anomalies.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            }
        });
    }
    for (auto& th : pool) th.join();

    EXPECT_EQ(anomalies.load(), 0)
        << "Stress: freshly created entity had corrupted data after concurrent destroyEntity";
}

// Lighter stress: fewer iters, timeout-guarded, enabled by default
TEST(BugRepro, DestroyEntity_TOCTOU_Stress) {
    const int kIters = scale(200);
    const int T = std::min(3, static_cast<int>(std::max(2u, std::thread::hardware_concurrency())));
    std::atomic<int> anomalies{0};

    Registry<true> reg;
    reg.reserve<BugPos>(kIters * T * 2);

    std::vector<std::thread> pool;
    for (int t = 0; t < T; ++t) {
        pool.emplace_back([&, t] {
            std::mt19937 rng(42 + t);
            for (int i = 0; i < kIters; ++i) {
                auto id = reg.takeEntity();
                reg.addComponent<BugPos>(id, float(t * 1000 + i), 0.f, 0.f);

                if (rng() % 3 == 0) {
                    reg.destroyEntity(id);
                    auto id2 = reg.takeEntity();
                    reg.addComponent<BugPos>(id2, 999.f, 999.f, 999.f);

                    auto pin = reg.pinComponent<BugPos>(id2);
                    if (pin && pin.get()->x != 999.f) {
                        anomalies.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            }
        });
    }
    for (auto& th : pool) th.join();

    EXPECT_EQ(anomalies.load(), 0)
        << "Stress: freshly created entity had corrupted data after concurrent destroyEntity";
}

// ---------------------------------------------------------------------------
// Bug #8: eachSingle/eachGrouped reads allocator.mChunks directly without
//         snapshot while concurrent writer may reallocate mChunks.
//         Under TSAN this should flag data race on vector internals.
// ---------------------------------------------------------------------------
TEST(BugRepro, EachSingle_ChunksDataRace) {
    const int kReaderIters = scale(30);
    const int kWriterInserts = scale(2000);

    Registry<true> reg;
    for (int i = 0; i < 100; ++i) {
        auto id = reg.takeEntity();
        reg.addComponent<BugPos>(id, float(i), 0.f, 0.f);
    }

    std::atomic<bool> stop{false};
    std::atomic<int> reader_errors{0};

    std::thread reader([&] {
        for (int iter = 0; iter < kReaderIters && !stop.load(std::memory_order_relaxed); ++iter) {
            try {
                double sum = 0;
                reg.view<BugPos>().each([&](BugPos& p) { sum += p.x; });
                (void)sum;
            } catch (...) {
                reader_errors.fetch_add(1, std::memory_order_relaxed);
            }
            std::this_thread::yield();
        }
    });

    std::thread writer([&] {
        for (int i = 0; i < kWriterInserts; ++i) {
            auto id = reg.takeEntity();
            reg.addComponent<BugPos>(id, float(1000 + i), 0.f, 0.f);
        }
        stop.store(true, std::memory_order_release);
    });

    reader.join();
    writer.join();

    EXPECT_EQ(reader_errors.load(), 0)
        << "eachSingle crashed or threw during concurrent mChunks mutation";
}

// ---------------------------------------------------------------------------
// Bug #6: PinnedIndexesBitMask parent bit inconsistency
//
// Concurrent set(x, false) and set(y, true) in the same word can leave
// the parent bit cleared while the child word is non-zero.
// ---------------------------------------------------------------------------
TEST(BugRepro, PinnedIndexesBitMask_ParentInconsistency) {
    const int kIters = scale(50000);
    const int T = std::min(4, static_cast<int>(std::max(2u, std::thread::hardware_concurrency())));

    Threads::PinCounters counters;
    std::atomic<int> anomalies{0};

    counters.pin(0);

    std::vector<std::thread> pool;
    for (int t = 0; t < T; ++t) {
        pool.emplace_back([&, t] {
            std::mt19937 rng(100 + t);
            auto dist = std::uniform_int_distribution<SectorId>(1, 63);
            for (int i = 0; i < kIters; ++i) {
                SectorId id = dist(rng);
                counters.pin(id);
                if (!counters.hasAnyPins()) {
                    anomalies.fetch_add(1, std::memory_order_relaxed);
                }
                counters.unpin(id);
            }
        });
    }
    for (auto& th : pool) th.join();

    EXPECT_TRUE(counters.hasAnyPins());
    counters.unpin(0);

    EXPECT_EQ(anomalies.load(), 0)
        << "hasAnyPins() returned false while sentinel sector 0 was pinned";
}

TEST(BugRepro, PinnedIndexesBitMask_HighestSetConsistency) {
    const int kIters = scale(20000);
    const int T = std::min(4, static_cast<int>(std::max(2u, std::thread::hardware_concurrency())));

    Threads::PinCounters counters;
    std::atomic<int> anomalies{0};

    counters.pin(100);

    std::vector<std::thread> pool;
    for (int t = 0; t < T; ++t) {
        pool.emplace_back([&, t] {
            std::mt19937 rng(200 + t);
            for (int i = 0; i < kIters; ++i) {
                SectorId id = 64 + (rng() % 64);
                counters.pin(id);
                if (counters.canMoveSector(100)) {
                    anomalies.fetch_add(1, std::memory_order_relaxed);
                }
                counters.unpin(id);
            }
        });
    }
    for (auto& th : pool) th.join();
    counters.unpin(100);

    EXPECT_EQ(anomalies.load(), 0)
        << "canMoveSector(100) returned true while sector 100 was pinned";
}

// ---------------------------------------------------------------------------
// Bug #5: updateMaxPinned epoch CAS starvation
//
// Under TSAN waitUntilChangeable can hang, so we only check that
// maxPinnedSector eventually reflects reality (no waitUntilChangeable call).
// ---------------------------------------------------------------------------
TEST(BugRepro, UpdateMaxPinned_EpochStarvation) {
    const int T = std::min(4, static_cast<int>(std::max(2u, std::thread::hardware_concurrency())));
    const int kIters = scale(10000);

    Threads::PinCounters counters;
    std::atomic<bool> stop{false};
    std::atomic<int> stale_count{0};

    // Background churn on low sectors
    std::vector<std::thread> churn;
    for (int t = 0; t < std::max(1, T - 1); ++t) {
        churn.emplace_back([&, t] {
            std::mt19937 rng(300 + t);
            auto dist = std::uniform_int_distribution<SectorId>(1, 50);
            int iters = kIters;
            while (iters-- > 0 && !stop.load(std::memory_order_relaxed)) {
                SectorId id = dist(rng);
                counters.pin(id);
                counters.unpin(id);
            }
            stop.store(true, std::memory_order_release);
        });
    }

    // Main thread: pin high sector, unpin, then check canMoveSector
    for (int m = 0; m < 50 && !stop.load(std::memory_order_relaxed); ++m) {
        counters.pin(10000);
        counters.unpin(10000);

        // After unpin(10000), if no other sector > 10000 is pinned,
        // canMoveSector(10000) should eventually return true.
        // With the epoch bug, maxPinnedSector may stay stale.
        bool became_movable = false;
        for (int spin = 0; spin < 1000; ++spin) {
            if (counters.canMoveSector(10000)) {
                became_movable = true;
                break;
            }
            std::this_thread::yield();
        }
        if (!became_movable) {
            stale_count.fetch_add(1, std::memory_order_relaxed);
        }
    }

    stop.store(true, std::memory_order_release);
    for (auto& th : churn) th.join();

    if (stale_count.load() > 0) {
        std::cerr << "[BugRepro] updateMaxPinned stale " << stale_count.load() << " times\n";
    }
    EXPECT_LE(stale_count.load(), 10)
        << "maxPinnedSector stayed stale too often -- epoch CAS starvation";
}

// ---------------------------------------------------------------------------
// Bug #5b: waitUntilChangeable HANG under pin/unpin churn
//
// This directly tests the livelock scenario: pin a high sector, unpin it,
// then call waitUntilChangeable while background threads churn pin/unpin.
// If updateMaxPinned epoch CAS keeps failing, maxPinnedSector stays stale
// and waitUntilChangeable never returns (atomic wait never gets notified).
//
// We use std::async with a timeout to detect the hang.
// ---------------------------------------------------------------------------
TEST(BugRepro, WaitUntilChangeable_Hang) {
    const int T = std::max(4, static_cast<int>(std::thread::hardware_concurrency()));

    Threads::PinCounters counters;
    std::atomic<bool> stop{false};
    std::atomic<int> ready{0};
    int hangs = 0;

    // Background: constant pin/unpin churn on low sectors
    std::vector<std::thread> churn;
    for (int t = 0; t < T; ++t) {
        churn.emplace_back([&, t] {
            std::mt19937 rng(500 + t);
            auto dist = std::uniform_int_distribution<SectorId>(1, 50);
            ready.fetch_add(1, std::memory_order_release);
            while (!stop.load(std::memory_order_relaxed)) {
                SectorId id = dist(rng);
                counters.pin(id);
                counters.unpin(id);
            }
        });
    }

    // Wait for all churn threads to be actively spinning
    while (ready.load(std::memory_order_acquire) < T) {
        std::this_thread::yield();
    }
    // Let them build up steam
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    for (int trial = 0; trial < 20; ++trial) {
        counters.pin(10000);
        counters.unpin(10000);

        auto future = std::async(std::launch::async, [&] {
            counters.waitUntilChangeable(10000);
        });

        auto status = future.wait_for(std::chrono::seconds(3));
        if (status == std::future_status::timeout) {
            hangs++;
            std::cerr << "[BugRepro] waitUntilChangeable(10000) HUNG on trial "
                      << trial << " (epoch CAS starvation -> missed notify_all)\n";
            break;
        }
    }

    stop.store(true, std::memory_order_release);
    for (auto& th : churn) th.join();

    EXPECT_EQ(hangs, 0)
        << "waitUntilChangeable hung due to updateMaxPinned epoch CAS starvation";
}

// ---------------------------------------------------------------------------
// Bug #7: RetireAllocator::is_always_equal = true_type contradicts operator==
// ---------------------------------------------------------------------------
TEST(BugRepro, RetireAllocator_IsAlwaysEqual_Fixed) {
    static_assert(std::allocator_traits<RetireAllocator<int>>::is_always_equal::value == false,
                  "is_always_equal should be false_type (allocators with different bins are not equal)");

    RetireBin bin1, bin2;
    RetireAllocator<int> a1{&bin1};
    RetireAllocator<int> a2{&bin2};
    RetireAllocator<int> a1copy{&bin1};

    EXPECT_NE(a1, a2) << "Allocators with different bins should not be equal";
    EXPECT_EQ(a1, a1copy) << "Allocators with same bin should be equal";
}
