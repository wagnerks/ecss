#include <gtest/gtest.h>
#include <chrono>
#include <vector>
#include <thread>
#include <atomic>
#include <iostream>
#include <iomanip>
#include <numeric>

#include <ecss/Registry.h>

using namespace ecss;
using Clock = std::chrono::high_resolution_clock;

struct Pos { float x{}, y{}, z{}; };
struct Vel { float dx{}, dy{}, dz{}; };
struct Hp  { int v{100}; };

static constexpr int N = 500000;
static constexpr int MT_N = 100000;

static double ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

TEST(PerfBench, CreateAndAdd) {
    Registry<true> reg;
    reg.reserve<Pos>(N);
    reg.reserve<Vel>(N);

    auto t0 = Clock::now();
    for (int i = 0; i < N; ++i) {
        auto e = reg.takeEntity();
        reg.addComponent<Pos>(e, float(i), 0.f, 0.f);
        reg.addComponent<Vel>(e, 1.f, 1.f, 1.f);
    }
    auto t1 = Clock::now();
    std::cerr << "[Perf] CreateAndAdd " << N << ": " << std::fixed << std::setprecision(2) << ms(t0, t1) << " ms\n";
}

TEST(PerfBench, IterateView) {
    Registry<true> reg;
    reg.reserve<Pos>(N);
    reg.reserve<Vel>(N);
    for (int i = 0; i < N; ++i) {
        auto e = reg.takeEntity();
        reg.addComponent<Pos>(e, float(i), 0.f, 0.f);
        reg.addComponent<Vel>(e, 1.f, 1.f, 1.f);
    }

    volatile double sink = 0;
    auto t0 = Clock::now();
    for (auto [e, p, v] : reg.view<Pos, Vel>()) {
        if (p && v) sink += p->x + v->dx;
    }
    auto t1 = Clock::now();
    std::cerr << "[Perf] IterateView " << N << ": " << std::fixed << std::setprecision(2) << ms(t0, t1) << " ms\n";
}

TEST(PerfBench, IterateEach) {
    Registry<true> reg;
    reg.registerArray<Pos, Vel>();
    reg.reserve<Pos>(N);
    for (int i = 0; i < N; ++i) {
        auto e = reg.takeEntity();
        reg.addComponent<Pos>(e, float(i), 0.f, 0.f);
        reg.addComponent<Vel>(e, 1.f, 1.f, 1.f);
    }

    volatile double sink = 0;
    auto t0 = Clock::now();
    reg.view<Pos, Vel>().each([&](Pos& p, Vel& v) {
        sink += p.x + v.dx;
    });
    auto t1 = Clock::now();
    std::cerr << "[Perf] IterateEach " << N << ": " << std::fixed << std::setprecision(2) << ms(t0, t1) << " ms\n";
}

TEST(PerfBench, EachSingle) {
    Registry<true> reg;
    reg.reserve<Pos>(N);
    for (int i = 0; i < N; ++i) {
        auto e = reg.takeEntity();
        reg.addComponent<Pos>(e, float(i), 0.f, 0.f);
    }

    volatile double sink = 0;
    auto t0 = Clock::now();
    reg.view<Pos>().each([&](Pos& p) { sink += p.x; });
    auto t1 = Clock::now();
    std::cerr << "[Perf] EachSingle " << N << ": " << std::fixed << std::setprecision(2) << ms(t0, t1) << " ms\n";
}

TEST(PerfBench, PinUnpin) {
    Registry<true> reg;
    reg.reserve<Pos>(N);
    std::vector<EntityId> ids;
    ids.reserve(N);
    for (int i = 0; i < N; ++i) {
        auto e = reg.takeEntity();
        reg.addComponent<Pos>(e, float(i), 0.f, 0.f);
        ids.push_back(e);
    }

    volatile float sink = 0;
    auto t0 = Clock::now();
    for (auto id : ids) {
        auto pin = reg.pinComponent<Pos>(id);
        if (pin) sink += pin->x;
    }
    auto t1 = Clock::now();
    std::cerr << "[Perf] PinUnpin " << N << ": " << std::fixed << std::setprecision(2) << ms(t0, t1) << " ms\n";
}

TEST(PerfBench, DestroyEntity) {
    Registry<true> reg;
    reg.reserve<Pos>(N);
    reg.reserve<Vel>(N);
    std::vector<EntityId> ids;
    ids.reserve(N);
    for (int i = 0; i < N; ++i) {
        auto e = reg.takeEntity();
        reg.addComponent<Pos>(e, float(i), 0.f, 0.f);
        reg.addComponent<Vel>(e, 1.f, 1.f, 1.f);
        ids.push_back(e);
    }

    auto t0 = Clock::now();
    for (auto id : ids) {
        reg.destroyEntity(id);
    }
    auto t1 = Clock::now();
    std::cerr << "[Perf] DestroyEntity " << N << ": " << std::fixed << std::setprecision(2) << ms(t0, t1) << " ms\n";
}

TEST(PerfBench, DestroyEntitiesBatch) {
    Registry<true> reg;
    reg.reserve<Pos>(N);
    reg.reserve<Vel>(N);
    std::vector<EntityId> ids;
    ids.reserve(N);
    for (int i = 0; i < N; ++i) {
        auto e = reg.takeEntity();
        reg.addComponent<Pos>(e, float(i), 0.f, 0.f);
        reg.addComponent<Vel>(e, 1.f, 1.f, 1.f);
        ids.push_back(e);
    }

    auto t0 = Clock::now();
    reg.destroyEntities(ids);
    auto t1 = Clock::now();
    std::cerr << "[Perf] DestroyEntitiesBatch " << N << ": " << std::fixed << std::setprecision(2) << ms(t0, t1) << " ms\n";
}

TEST(PerfBench, MTReadWriteChurn) {
    const int T = std::max(4u, std::thread::hardware_concurrency());
    Registry<true> reg;
    reg.reserve<Pos>(MT_N * 2);

    for (int i = 0; i < MT_N; ++i) {
        auto e = reg.takeEntity();
        reg.addComponent<Pos>(e, float(i), 0.f, 0.f);
    }

    std::atomic<bool> stop{false};
    std::atomic<size_t> read_ops{0};
    std::atomic<size_t> write_ops{0};

    auto t0 = Clock::now();

    std::vector<std::thread> pool;
    int writers = std::max(1, T / 4);
    int readers = T - writers;

    for (int w = 0; w < writers; ++w) {
        pool.emplace_back([&, w] {
            size_t ops = 0;
            while (!stop.load(std::memory_order_relaxed)) {
                auto id = reg.takeEntity();
                reg.addComponent<Pos>(id, 0.f, 0.f, 0.f);
                reg.destroyEntity(id);
                ops += 2;
            }
            write_ops.fetch_add(ops, std::memory_order_relaxed);
        });
    }

    for (int r = 0; r < readers; ++r) {
        pool.emplace_back([&] {
            size_t ops = 0;
            while (!stop.load(std::memory_order_relaxed)) {
                double s = 0;
                for (auto [e, p] : reg.view<Pos>()) {
                    if (p) s += p->x;
                }
                (void)s;
                ++ops;
            }
            read_ops.fetch_add(ops, std::memory_order_relaxed);
        });
    }

    std::this_thread::sleep_for(std::chrono::seconds(2));
    stop.store(true, std::memory_order_release);
    for (auto& th : pool) th.join();

    auto t1 = Clock::now();
    auto elapsed = ms(t0, t1);
    std::cerr << "[Perf] MTChurn " << T << "T (" << writers << "W/" << readers << "R) "
              << elapsed << " ms: writes=" << write_ops.load()
              << " reads=" << read_ops.load() << "\n";
}
