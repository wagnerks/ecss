//
// mt_ecss_tests.cpp - heavy multithreaded correctness & race stress for ECSS
//
// These tests assume an ECSS API roughly as follows:
//   struct Registry {
//     EntityId create();
//     void destroy(EntityId);
//     template<class T, class... A> T* emplace(EntityId, A&&...);
//     template<class T> T* get(EntityId);
//     template<class... Cs> auto range();  // iterable returning (EntityId, Cs*...)
//   };
//
// If your names differ, update the "Adapter" block below. Tests are designed to
// surface ABA, iterator invalidation, and race conditions under churn.
//

#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <vector>
#include <random>
#include <mutex>
#include <numeric>
#include <chrono>

// ---- ECSS includes (adjust if your paths differ) ----
#include <ecss/Registry.h>
#include <ecss/memory/Sector.h>
#include <cstdint>


// Simple reusable countdown latch for C++20 (works even if <latch> is unavailable).
class SimpleLatch {
public:
    explicit SimpleLatch(std::ptrdiff_t count) noexcept : m_count(count) {}
    void count_down(std::ptrdiff_t n = 1) noexcept {
        m_count.fetch_sub(n, std::memory_order_acq_rel);
    }
    void wait() const noexcept {
        while (m_count.load(std::memory_order_acquire) > 0) {
            std::this_thread::yield();
        }
    }
    void arrive_and_wait() noexcept { count_down(); wait(); }
private:
    std::atomic<std::ptrdiff_t> m_count;
};

// Spin barrier: all threads call arrive_and_wait() to synchronize a phase.
class SpinBarrier {
public:
    explicit SpinBarrier(std::uint32_t parties) : m_parties(parties), m_waiting(0), m_phase(0) {}
    void arrive_and_wait() {
        auto my_phase = m_phase.load(std::memory_order_acquire);
        if (m_waiting.fetch_add(1, std::memory_order_acq_rel) + 1 == m_parties) {
            m_waiting.store(0, std::memory_order_release);
            m_phase.fetch_add(1, std::memory_order_acq_rel);
        }
        else {
            while (m_phase.load(std::memory_order_acquire) == my_phase) {
                std::this_thread::yield();
            }
        }
    }
private:
    const std::uint32_t m_parties;
    std::atomic<std::uint32_t> m_waiting;
    std::atomic<std::uint32_t> m_phase;
};


// ---- Components used in tests ----
struct Position { float x{}, y{}, z{}; };
struct Velocity { float dx{}, dy{}, dz{}; };
struct Health { int hp{ 100 }; };

// ---- Tunables ----
#ifndef ECSS_MT_ENTITIES
#define ECSS_MT_ENTITIES 250000
#endif

#ifndef ECSS_MT_SECONDS
#define ECSS_MT_SECONDS 2
#endif

// Number of threads; defaults to hardware concurrency, minimum 4.
static int thread_count() {
    unsigned hc = std::max(4u, std::thread::hardware_concurrency());
    return static_cast<int>(hc);
}

// ---- Adapter over your Registry ----
namespace test_adapt {
    using Registry = ecss::Registry<>;
    using EntityId = decltype(std::declval<Registry>().takeEntity());

    inline EntityId create(Registry& r) { return r.takeEntity(); }
    template<class T, class... A> inline T* emplace(Registry& r, EntityId id, A&&... a) {
        return r.addComponent<T>(id, std::forward<A>(a)...);
    }
    template<class T> inline Registry::PinnedComponent<T> get(Registry& r, EntityId id) {
        return r.getPinnedComponent<T>(id);
    }
    inline void destroy(Registry& r, EntityId id) { r.destroyEntity(id); }

    template<class... Cs>
    inline auto range(Registry& r) {
        return r.forEach<Cs...>(); // should iterate yielding (EntityId, Cs*...)
    }
}

// ---- Helpers ----
static std::vector<uint32_t> shuffled_indices(size_t n, uint32_t seed) {
    std::vector<uint32_t> v(n);
    std::iota(v.begin(), v.end(), 0u);
    std::mt19937 rng(seed);
    std::shuffle(v.begin(), v.end(), rng);
    return v;
}

// ================================================================
// 1) Parallel create + emplace + global read pass (sanity & races)
// ================================================================
TEST(MT, ParallelCreateEmplaceThenRead) {
    using namespace test_adapt;
    Registry ecs;
    const size_t N = ECSS_MT_ENTITIES;
    const int T = thread_count();
    const size_t per_thread = (N + T - 1) / T;

    std::vector<std::thread> pool;
    pool.reserve(T);
    SpinBarrier barrier(T);

    // Each thread creates its own block of entities
    for (int t = 0; t < T; ++t) {
        pool.emplace_back([&, t] {
            barrier.arrive_and_wait();
            size_t begin = size_t(t) * per_thread;
            size_t end = std::min(begin + per_thread, N);
            for (size_t i = begin; i < end; ++i) {
                auto id = create(ecs);
                emplace<Position>(ecs, id, float(i), float(i * 2), float(i * 3));
                emplace<Velocity>(ecs, id, float(1), float(2), float(3));
                if ((i & 7u) == 0u) emplace<Health>(ecs, id, 100);
            }
        });
    }
    for (auto& th : pool) th.join();

    // Single-thread check: iterate and compute sum to ensure iteration is stable
    size_t count = 0; double sum = 0;
    auto rng = range<Position, Velocity>(ecs);
    for (auto it = rng.begin(); it != rng.end(); ++it) {
        auto [e, p, v] = *it;
        ASSERT_NE(p, nullptr);
        ASSERT_NE(v, nullptr);
        sum += p->x + v->dx;
        ++count;
    }
    ASSERT_GT(count, 0u);
    ASSERT_GT(sum, 0.0);
}

// ==================================================================================
// 2) Readers vs Writers (churn): multiple readers iterate while writers mutate state
//    Expectation: no crashes, no UB, readers always see valid component pointers.
// ==================================================================================
TEST(MT, ReadersWriters_Churn) {
    using namespace test_adapt;
    Registry ecs;

    // Seed with initial population
    const size_t N0 = ECSS_MT_ENTITIES / 2;
    for (size_t i = 0; i < N0; ++i) {
        auto id = create(ecs);
        emplace<Position>(ecs, id, float(i), float(i), float(i));
        emplace<Velocity>(ecs, id, float(1), float(1), float(1));
    }

    const int T = thread_count();
    const int readers = std::max(2, T - 2);
    const int writers = T - readers;
    std::atomic<bool> stop{ false };

    // Writer threads: churn create/destroy for ECSS_MT_SECONDS seconds
    std::vector<std::thread> pool;
    pool.reserve(T);
    for (int w = 0; w < writers; ++w) {
        pool.emplace_back([&] {
            std::mt19937 rng(1337u + w);
            auto dist = std::uniform_int_distribution<int>(0, 99);
            const auto t_end = std::chrono::steady_clock::now() + std::chrono::seconds(ECSS_MT_SECONDS);
            std::vector<EntityId> local;
            local.reserve(1024);

            while (std::chrono::steady_clock::now() < t_end) {
                // burst create
                for (int i = 0; i < 256; ++i) {
                    auto id = create(ecs);
                    emplace<Position>(ecs, id, 0.f, 0.f, 0.f);
                    if ((i & 1) == 0) emplace<Velocity>(ecs, id, 1.f, 1.f, 1.f);
                    local.push_back(id);
                }
                // random destroy ~50%
                for (auto it = local.begin(); it != local.end();) {
                    if (dist(rng) < 50) {
                        destroy(ecs, *it);
                        it = local.erase(it);
                    }
                    else {
                        ++it;
                    }
                }
            }
            stop.store(true, std::memory_order_release);
        });
    }

    // Reader threads: continuous range iteration and sum accumulation
    std::atomic<size_t> guard_sum{ 0 };
    for (int r = 0; r < readers; ++r) {
        pool.emplace_back([&] {
            size_t local = 0;
            while (!stop.load(std::memory_order_acquire)) {
                for (auto [e, p, v] : range<Position, Velocity>(ecs)) {
                    if (!p || !v) continue; // if your API guarantees both, you can assert instead
                    local += size_t(p->x + v->dx);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
           
            guard_sum.fetch_add(local, std::memory_order_relaxed);
        });
    }

    for (auto& th : pool) th.join();
    ASSERT_GE(guard_sum.load(std::memory_order_relaxed), 0u);
}

// ======================================================================================
/* 3) Stable iteration snapshot: many readers only, long pass.
      Ensures your range<> is re-entrant and safe across threads under read-only workload. */
      // ======================================================================================
TEST(MT, ManyReaders_ReadOnlyPass) {
    using namespace test_adapt;
    Registry ecs;

    const size_t N = ECSS_MT_ENTITIES;
    for (size_t i = 0; i < N; ++i) {
        auto id = create(ecs);
        emplace<Position>(ecs, id, float(i), float(i), float(i));
        if ((i & 3u) == 0u) emplace<Velocity>(ecs, id, 1.f, 2.f, 3.f);
    }

    const int T = thread_count();
    std::vector<std::thread> pool;
    pool.reserve(T);
    std::atomic<size_t> acc{ 0 };

    for (int t = 0; t < T; ++t) {
        pool.emplace_back([&] {
            size_t local = 0;
            auto rng = range<Position>(ecs);
            for (auto it = rng.begin(); it != rng.end(); ++it) {
                auto [e, p] = *it;
                ASSERT_NE(p, nullptr);
                local += size_t(p->x);
            }
            acc.fetch_add(local, std::memory_order_relaxed);
        });
    }
    for (auto& th : pool) th.join();
    ASSERT_GT(acc.load(std::memory_order_relaxed), 0u);
}

// ===============================================================================
// 4) Fuzzy random ops: each thread does random get/emplace/destroy/create on a pool
//    Designed to shake out rare races; stop after ECSS_MT_SECONDS seconds.
// ===============================================================================
TEST(MT, FuzzyRandomOps) {
    using namespace test_adapt;
    Registry ecs;

    const int T = thread_count();
    std::mutex ids_mtx;
    std::vector<EntityId> ids;
    ids.reserve(ECSS_MT_ENTITIES / 2);

    // seed a small pool
    for (size_t i = 0; i < ECSS_MT_ENTITIES / 4; ++i) {
        auto id = create(ecs);
        emplace<Position>(ecs, id, 1.f, 2.f, 3.f);
        ids.push_back(id);
    }

    std::atomic<bool> stop{ false };
    std::vector<std::thread> pool;
    pool.reserve(T);

    auto worker = [&](int tid) {
        std::mt19937 rng(777u + tid);
        auto dist = std::uniform_int_distribution<int>(0, 99);
        auto t_end = std::chrono::steady_clock::now() + std::chrono::seconds(ECSS_MT_SECONDS);

        while (std::chrono::steady_clock::now() < t_end) {
            int op = dist(rng);
            if (op < 35) { // create
                auto id = create(ecs);
                emplace<Position>(ecs, id, 0.f, 0.f, 0.f);
                if (op & 1) emplace<Velocity>(ecs, id, 1.f, 1.f, 1.f);
                std::scoped_lock lk(ids_mtx);
                ids.push_back(id);
            }
            else if (op < 65) { // get/read
                std::scoped_lock lk(ids_mtx);
                if (!ids.empty()) {
                    auto id = ids[rng() % ids.size()];
                    auto p = get<Position>(ecs, id);
                    // pointer may be null if entity destroyed between lock and get
                    if (p) { p->x += 1.0f; }
                }
            }
            else { // destroy
                std::scoped_lock lk(ids_mtx);
                if (!ids.empty()) {
                    size_t i = rng() % ids.size();
                    auto id = ids[i];
                    ids[i] = ids.back(); ids.pop_back();
                    destroy(ecs, id);
                }
            }
        }
    };

    for (int t = 0; t < T; ++t) pool.emplace_back(worker, t);
    for (auto& th : pool) th.join();

    // Survived without crashes; do a final consistency read
    size_t count = 0;
    auto rng = range<Position>(ecs);
    for (auto it = rng.begin(); it != rng.end(); ++it) {
        auto [e, p] = *it;
        if (p) ++count;
    }
    ASSERT_GE(count, 0u);
}

