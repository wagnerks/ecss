#include <gtest/gtest.h>
#include <chrono>
#include <vector>
#include <thread>
#include <atomic>
#include <algorithm>
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
                    // Walk the whole array -- ids, liveness, pointer maths, the appends and
                    // erases the writers are making -- but only read the value of an entity
                    // the writers never touch. The first MT_N ids are taken before the threads
                    // start and are never destroyed, so their ids are never recycled, and an
                    // open view keeps the container from relocating them mid-pass.
                    //
                    // Reading a value while another thread writes it is the caller's to
                    // arrange, not the container's; the churn region is where the writers
                    // live, so its values are not ours to read.
                    if (p && e < static_cast<EntityId>(MT_N)) { s += p->x; }
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

TEST(PerfBench, False_EachSingle) {
    Registry<false> reg;
    reg.reserve<Pos>(N);
    for (int i = 0; i < N; ++i) {
        auto e = reg.takeEntity();
        reg.addComponent<Pos>(e, float(i), 0.f, 0.f);
    }

    volatile double sink = 0;
    auto t0 = Clock::now();
    reg.view<Pos>().each([&](Pos& p) { sink += p.x; });
    auto t1 = Clock::now();
    std::cerr << "[Perf] False_EachSingle " << N << ": " << std::fixed << std::setprecision(2) << ms(t0, t1) << " ms\n";
}

TEST(PerfBench, False_Lookup) {
    Registry<false> reg;
    reg.reserve<Pos>(N);
    std::vector<EntityId> ids;
    ids.reserve(N);
    for (int i = 0; i < N; ++i) {
        auto e = reg.takeEntity();
        reg.addComponent<Pos>(e, float(i), 0.f, 0.f);
        ids.push_back(e);
    }

    auto* container = reg.getComponentContainer<Pos>();
    const auto& layout = container->getLayout();
    volatile float sink = 0;
    auto t0 = Clock::now();
    for (auto id : ids) {
        auto slot = container->findSlot<false>(id);
        if (slot) {
            auto* p = Memory::Sector::getComponent<Pos>(slot.data, container->getIsAliveRef(slot.linearIdx), layout);
            if (p) sink += p->x;
        }
    }
    auto t1 = Clock::now();
    std::cerr << "[Perf] False_Lookup " << N << ": " << std::fixed << std::setprecision(2) << ms(t0, t1) << " ms\n";
}

TEST(PerfBench, True_FindSlotNoLock) {
    Registry<true> reg;
    reg.reserve<Pos>(N);
    std::vector<EntityId> ids;
    ids.reserve(N);
    for (int i = 0; i < N; ++i) {
        auto e = reg.takeEntity();
        reg.addComponent<Pos>(e, float(i), 0.f, 0.f);
        ids.push_back(e);
    }

    auto* container = reg.getComponentContainer<Pos>();
    const auto& layout = container->getLayout();
    volatile float sink = 0;
    auto t0 = Clock::now();
    for (auto id : ids) {
        auto slot = container->findSlot<false>(id);
        if (slot) {
            auto* p = Memory::Sector::getComponent<Pos>(slot.data, container->loadAliveWord(slot.linearIdx), layout);
            if (p) sink += p->x;
        }
    }
    auto t1 = Clock::now();
    std::cerr << "[Perf] True_FindSlotNoLock " << N << ": " << std::fixed << std::setprecision(2) << ms(t0, t1) << " ms\n";
}

TEST(PerfBench, Fair_Grouped_Each) {
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
    reg.view<Pos, Vel>().each([&](Pos& p, Vel& v) { sink += p.x + v.dx; });
    auto t1 = Clock::now();
    std::cerr << "[Perf] Fair_Grouped_Each " << N << ": " << std::fixed << std::setprecision(2) << ms(t0, t1) << " ms\n";
}

TEST(PerfBench, Fair_Grouped_RangeFor) {
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
    for (auto [e, p, v] : reg.view<Pos, Vel>()) {
        if (p && v) sink += p->x + v->dx;
    }
    auto t1 = Clock::now();
    std::cerr << "[Perf] Fair_Grouped_RangeFor " << N << ": " << std::fixed << std::setprecision(2) << ms(t0, t1) << " ms\n";
}

TEST(PerfBench, Fair_Grouped_TryInvoke) {
    Registry<true> reg;
    reg.registerArray<Pos, Vel>();
    reg.reserve<Pos>(N);
    for (int i = 0; i < N; ++i) {
        auto e = reg.takeEntity();
        reg.addComponent<Pos>(e, float(i), 0.f, 0.f);
        reg.addComponent<Vel>(e, 1.f, 1.f, 1.f);
    }
    auto v = reg.view<Pos, Vel>();
    volatile double sink = 0;
    auto t0 = Clock::now();
    for (auto it = v.begin(); it != v.end(); ++it) {
        it.tryInvoke([&](Pos& p, Vel& vel) { sink += p.x + vel.dx; });
    }
    auto t1 = Clock::now();
    std::cerr << "[Perf] Fair_Grouped_TryInvoke " << N << ": " << std::fixed << std::setprecision(2) << ms(t0, t1) << " ms\n";
}

TEST(PerfBench, Fair_Ungrouped_Each) {
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
    reg.view<Pos, Vel>().each([&](Pos& p, Vel& v) { sink += p.x + v.dx; });
    auto t1 = Clock::now();
    std::cerr << "[Perf] Fair_Ungrouped_Each " << N << ": " << std::fixed << std::setprecision(2) << ms(t0, t1) << " ms\n";
}

TEST(PerfBench, Fair_Ungrouped_RangeFor) {
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
    std::cerr << "[Perf] Fair_Ungrouped_RangeFor " << N << ": " << std::fixed << std::setprecision(2) << ms(t0, t1) << " ms\n";
}

namespace TypeLookupBench {

struct C00 { int value{}; }; struct C01 { int value{}; };
struct C02 { int value{}; }; struct C03 { int value{}; };
struct C04 { int value{}; }; struct C05 { int value{}; };
struct C06 { int value{}; }; struct C07 { int value{}; };
struct C08 { int value{}; }; struct C09 { int value{}; };
struct C10 { int value{}; }; struct C11 { int value{}; };
struct C12 { int value{}; }; struct C13 { int value{}; };
struct C14 { int value{}; }; struct C15 { int value{}; };
struct C16 { int value{}; }; struct C17 { int value{}; };
struct C18 { int value{}; }; struct C19 { int value{}; };
struct C20 { int value{}; }; struct C21 { int value{}; };
struct C22 { int value{}; }; struct C23 { int value{}; };
struct C24 { int value{}; }; struct C25 { int value{}; };
struct C26 { int value{}; }; struct C27 { int value{}; };
struct C28 { int value{}; }; struct C29 { int value{}; };
struct C30 { int value{}; }; struct C31 { int value{}; };

#if defined(_MSC_VER)
#define ECSS_BENCH_NOINLINE __declspec(noinline)
#else
#define ECSS_BENCH_NOINLINE __attribute__((noinline))
#endif

using BenchRegistry = Registry<false>;
using BenchArray = Memory::SectorsArray<false, Memory::ChunksAllocator<8192>>;

template<class Component>
ECSS_BENCH_NOINLINE bool hasComponent(BenchRegistry& registry, EntityId entity) {
    return registry.hasComponent<Component>(entity);
}

template<class Component>
ECSS_BENCH_NOINLINE uint32_t layoutMask(BenchArray* array) {
    return array->getLayoutData<Component>().isAliveMask;
}

template<class Operation>
double medianNsPerOperation(Operation&& operation, size_t operationsPerSample) {
    std::vector<double> samples;
    samples.reserve(9);
    uint64_t checksum = 0;

    for (int sample = 0; sample < 9; ++sample) {
        const auto begin = Clock::now();
        checksum += operation();
        const auto end = Clock::now();
        samples.push_back(std::chrono::duration<double, std::nano>(end - begin).count()
            / static_cast<double>(operationsPerSample));
    }

    std::ranges::sort(samples);
    if (checksum == 0) std::abort();
    return samples[samples.size() / 2];
}

template<class... Components>
double groupedLastTypeHasNs() {
    using Last = std::tuple_element_t<sizeof...(Components) - 1, std::tuple<Components...>>;
    static constexpr size_t entityCount = 4096;
    static constexpr size_t passes = 1024;
    static constexpr size_t operations = entityCount * passes;

    BenchRegistry registry;
    registry.registerArray<Components...>();
    registry.reserve<Last>(static_cast<uint32_t>(entityCount));

    std::vector<EntityId> entities;
    entities.reserve(entityCount);
    for (size_t i = 0; i < entityCount; ++i) {
        const auto entity = registry.takeEntity();
        registry.addComponent<Last>(entity, static_cast<int>(i));
        entities.push_back(entity);
    }

    return medianNsPerOperation([&] {
        uint64_t sum = 0;
        for (size_t pass = 0; pass < passes; ++pass)
            for (const auto entity : entities) sum += hasComponent<Last>(registry, entity);
        return sum;
    }, operations);
}

TEST(PerfBench, TypeIdLayoutScaling) {
    const auto group01 = groupedLastTypeHasNs<C00>();
    const auto group02 = groupedLastTypeHasNs<C00, C01>();
    const auto group04 = groupedLastTypeHasNs<C00, C01, C02, C03>();
    const auto group08 = groupedLastTypeHasNs<C00, C01, C02, C03, C04, C05, C06, C07>();
    const auto group16 = groupedLastTypeHasNs<
        C00, C01, C02, C03, C04, C05, C06, C07,
        C08, C09, C10, C11, C12, C13, C14, C15>();
    const auto group32 = groupedLastTypeHasNs<
        C00, C01, C02, C03, C04, C05, C06, C07,
        C08, C09, C10, C11, C12, C13, C14, C15,
        C16, C17, C18, C19, C20, C21, C22, C23,
        C24, C25, C26, C27, C28, C29, C30, C31>();

    std::cerr << std::fixed << std::setprecision(2)
              << "[Perf] TypeLookup hasComponent group 1/2/4/8/16/32: "
              << group01 << " / " << group02 << " / " << group04 << " / "
              << group08 << " / " << group16 << " / " << group32 << " ns/op\n";
}

TEST(PerfBench, TypeIdAndLayoutLookup) {
    static constexpr size_t entityCount = 16384;
    static constexpr size_t passes = 256;
    static constexpr size_t operations = entityCount * passes;

    BenchRegistry registry;
    registry.registerArray<
        C00, C01, C02, C03, C04, C05, C06, C07,
        C08, C09, C10, C11, C12, C13, C14, C15,
        C16, C17, C18, C19, C20, C21, C22, C23,
        C24, C25, C26, C27, C28, C29, C30, C31>();
    registry.reserve<C00>(static_cast<uint32_t>(entityCount));

    std::vector<EntityId> entities;
    entities.reserve(entityCount);
    for (size_t i = 0; i < entityCount; ++i) {
        const auto entity = registry.takeEntity();
        registry.addComponent<C00>(entity, static_cast<int>(i));
        registry.addComponent<C31>(entity, static_cast<int>(i));
        entities.push_back(entity);
    }

    auto* array = registry.getComponentContainer<C00>();
    const auto directFirst = medianNsPerOperation([&] {
        uint64_t sum = 0;
        for (size_t i = 0; i < operations; ++i) sum += layoutMask<C00>(array);
        return sum;
    }, operations);
    const auto directLast = medianNsPerOperation([&] {
        uint64_t sum = 0;
        for (size_t i = 0; i < operations; ++i) sum += layoutMask<C31>(array);
        return sum;
    }, operations);
    const auto hasFirst = medianNsPerOperation([&] {
        uint64_t sum = 0;
        for (size_t pass = 0; pass < passes; ++pass)
            for (const auto entity : entities) sum += hasComponent<C00>(registry, entity);
        return sum;
    }, operations);
    const auto hasLast = medianNsPerOperation([&] {
        uint64_t sum = 0;
        for (size_t pass = 0; pass < passes; ++pass)
            for (const auto entity : entities) sum += hasComponent<C31>(registry, entity);
        return sum;
    }, operations);

    std::cerr << std::fixed << std::setprecision(2)
              << "[Perf] TypeLookup direct-layout first/last: " << directFirst << " / " << directLast << " ns/op\n"
              << "[Perf] TypeLookup hasComponent first/last: " << hasFirst << " / " << hasLast << " ns/op\n";
}

#undef ECSS_BENCH_NOINLINE

} // namespace TypeLookupBench
