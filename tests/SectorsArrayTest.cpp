#include <gtest/gtest.h>

#include <thread>
#include <vector>
#include <string>
#include <atomic>
#include <set>
#include <chrono>
#include <random>
#include <sstream>
#include <map>
#include <numeric>
#include <future>
#include <mutex>

#include <ecss/memory/SectorsArray.h>
#include <ecss/memory/Reflection.h>

namespace SectorsArrayTest
{
    struct Trivial { int a = 0; };
    struct NonTrivial { std::string s; NonTrivial() = default; NonTrivial(std::string str) : s(std::move(str)) {} };

    struct Position { float x, y; };
    struct Velocity { float dx, dy; };
    struct Health { int value; };

    struct BigStruct { char data[512]; int id; };
    struct MoveOnly { std::unique_ptr<int> v; MoveOnly(int x) : v(new int(x)) {} MoveOnly(MoveOnly&& o) noexcept : v(std::move(o.v)) {} MoveOnly& operator=(MoveOnly&& o) noexcept { v = std::move(o.v); return *this; } };
    struct CtorCounter { static int constructed, destroyed; CtorCounter() { ++constructed; } ~CtorCounter() { ++destroyed; } };
    int CtorCounter::constructed = 0; int CtorCounter::destroyed = 0;


using namespace ecss::Memory;

ReflectionHelper& GetReflection() {
    static ReflectionHelper reflection;
    return reflection;
}
using SA_T = SectorsArray;

TEST(SectorsArray, DefaultConstructEmpty) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    EXPECT_EQ(arr->size(), 0);
    EXPECT_TRUE(arr->empty());
    delete arr;
}

TEST(SectorsArray, InsertAndFind_Trivial) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    arr->insertSafe<Trivial>(123, Trivial{ 42 });
    EXPECT_EQ(arr->size(), 1);
    auto* sector = arr->findSector(123);
    ASSERT_NE(sector, nullptr);
    auto* v = sector->getMember<Trivial>(arr->getLayoutData<Trivial>());
    EXPECT_EQ(v->a, 42);
    delete arr;
}

TEST(SectorsArray, InsertAndFind_NonTrivial) {
    auto* arr = SA_T::create<NonTrivial>(GetReflection());
    arr->insertSafe<NonTrivial>(1, NonTrivial{ "test" });
    auto* sector = arr->findSector(1);
    ASSERT_NE(sector, nullptr);
    auto* v = sector->getMember<NonTrivial>(arr->getLayoutData<NonTrivial>());
    EXPECT_EQ(v->s, "test");
    delete arr;
}

TEST(SectorsArray, MultipleInsertionsAndFind) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    for (int i = 0; i < 100; ++i) {
        arr->insertSafe<Trivial>(i, Trivial{ i * 2 });
    }
    EXPECT_EQ(arr->size(), 100);
    for (int i = 0; i < 100; ++i) {
        auto* sector = arr->findSector(i);
        ASSERT_NE(sector, nullptr);
        EXPECT_EQ(sector->getMember<Trivial>(arr->getLayoutData<Trivial>())->a, i * 2);
    }
    delete arr;
}

TEST(SectorsArray, InsertOverwrite) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    arr->insertSafe<Trivial>(0, Trivial{ 1 });
    arr->insertSafe<Trivial>(0, Trivial{ 2 });
    EXPECT_EQ(arr->findSector(0)->getMember<Trivial>(arr->getLayoutData<Trivial>())->a, 2);
    delete arr;
}

TEST(SectorsArray, EraseSingle) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    arr->insertSafe<Trivial>(0, Trivial{ 1 });
    arr->erase(0);
    EXPECT_EQ(arr->size(), 0);
    EXPECT_EQ(arr->findSector(0), nullptr);
    delete arr;
}

TEST(SectorsArray, EraseRange) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    for (int i = 0; i < 10; ++i) arr->insertSafe<Trivial>(i, Trivial{ i });
    arr->erase(2, 5);
    EXPECT_EQ(arr->size(), 5);
    EXPECT_EQ(arr->findSector(2), nullptr);
    delete arr;
}

TEST(SectorsArray, Clear) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    for (int i = 0; i < 3; ++i) arr->insertSafe<Trivial>(i, Trivial{ i });
    arr->clear();
    EXPECT_EQ(arr->size(), 0);
    EXPECT_TRUE(arr->empty());
    delete arr;
}

TEST(SectorsArray, DefragmentRemovesDeadAndShiftsAlive) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    arr->insertSafe<Trivial>(1, Trivial{ 1 });
    arr->insertSafe<Trivial>(2, Trivial{ 2 });
    arr->insertSafe<Trivial>(3, Trivial{ 3 });
    arr->erase(1);
    arr->defragment();
    EXPECT_EQ(arr->size(), 2);
    EXPECT_NE(arr->findSector(1), nullptr);
    EXPECT_EQ(arr->findSector(2), nullptr);
    EXPECT_NE(arr->findSector(3), nullptr);
    delete arr;
}

TEST(SectorsArray, ReserveAndShrink) {
    auto* arr = SA_T::create<Trivial>(GetReflection(), 0, 8192);
    arr->reserve(100);
    EXPECT_GE(arr->capacity(), 8192);
    for (int i = 0; i < 10; ++i) arr->insertSafe<Trivial>(i, Trivial{ i });
    arr->shrinkToFit();
    EXPECT_LE(arr->capacity(), 8192);
    delete arr;
}

TEST(SectorsArray, OperatorAtAndBracket) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    arr->insertSafe<Trivial>(0, Trivial{ 42 });
    auto* sector1 = (*arr)[0];
    auto* sector2 = arr->at(0);
    EXPECT_EQ(sector1, sector2);
    delete arr;
}

TEST(SectorsArray, IteratorBasic) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    for (int i = 0; i < 30000; ++i) arr->insertSafe<Trivial>(i, Trivial{ i });
    int sum = 0;
    for (auto it = arr->begin(); it != arr->end(); ++it) {
        sum += (*it)->getMember<Trivial>(arr->getLayoutData<Trivial>())->a;
    }
    EXPECT_EQ(sum, 449985000);
    delete arr;
}

TEST(SectorsArray, IteratorBasicStress) {
    auto t0 = std::chrono::high_resolution_clock::now();
    auto* arr = SA_T::create<Trivial>(GetReflection());
    constexpr size_t count = 60000000;
    arr->reserve(count);
    for (int i = 0; i < count; ++i) arr->emplace<Trivial>(i, Trivial{ i });

    unsigned long long sum = 0;
    auto t1 = std::chrono::high_resolution_clock::now();
    for (auto it = arr->begin(); it != arr->end(); ++it) {
        sum += (*it)->getMember<Trivial>(arr->getLayoutData<Trivial>())->a;
    }
    auto t2 = std::chrono::high_resolution_clock::now();

    auto create_us = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    auto iterate_us = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
    std::cout << "[StressTest] Create time: " << create_us << " ms\n";
    std::cout << "[StressTest] Iterate time: " << iterate_us << " ms\n";
    delete arr;
}

TEST(SectorsArray, InsertMove) {
    auto* arr = SA_T::create<NonTrivial>(GetReflection());
    NonTrivial ntr("abc");
    arr->insertSafe<NonTrivial>(5, std::move(ntr));
    auto* sector = arr->findSector(5);
    ASSERT_NE(sector, nullptr);
    EXPECT_EQ(sector->getMember<NonTrivial>(arr->getLayoutData<NonTrivial>())->s, "abc");
    delete arr;
}

TEST(SectorsArray, MappingAndCapacity) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    arr->reserve(32);
    arr->insertSafe<Trivial>(10, Trivial{ 100 });
    EXPECT_TRUE(arr->containsSector(10));
    EXPECT_FALSE(arr->containsSector(99));
    EXPECT_EQ(arr->getSectorIndex(10), 0);
    delete arr;
}

TEST(SectorsArray_STRESS, ThreadedInsert) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    constexpr int threadCount = 10;
    constexpr int N = 2000;
    std::vector<std::thread> threads;
    std::atomic<int> ready{ 0 };

    for (int t = 0; t < threadCount; ++t) {
        threads.emplace_back([&, t] {
            ++ready;
            while (ready < threadCount) {}
            for (int i = 0; i < N; ++i) {
                arr->insertSafe<Trivial>(t * N + i, Trivial{ i });
            }
        });
    }
    for (auto& th : threads) th.join();
    EXPECT_EQ(arr->size(), threadCount * N);
    delete arr;
}

TEST(SectorsArray, ThreadedFindAndErase) {
    constexpr int N = 1000;
    for (auto k = 0; k < 100; k++) {
        auto* arr = SA_T::create<Trivial>(GetReflection());

        for (int i = 0; i < N; ++i) arr->insertSafe<Trivial>(i, Trivial{ i });

        std::atomic<int> sum{ 0 };
        std::thread reader([&] {
            for (int i = 0; i < N; ++i) {
                auto lock = arr->readLock();
                auto* s = arr->findSector(i);
                if (s)
                    sum += s->getMember<Trivial>(arr->getLayoutData<Trivial>())->a;
            }
        });

        std::thread eraser([&] {
            for (int i = 0; i < N; i += 2) arr->eraseSector(i, 1);
        });

        reader.join();
        eraser.join();

        EXPECT_GE(sum, 0);
        delete arr;
    }
}

TEST(SectorsArray, InsertInvalidEraseOutOfBounds) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    arr->insertSafe<Trivial>(0, Trivial{ 10 });
    arr->erase(10); // out of bounds
    EXPECT_EQ(arr->size(), 1);
    delete arr;
}

TEST(SectorsArray, DoubleClear) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    arr->clear();
    arr->clear();
    SUCCEED();
    delete arr;
}

TEST(SectorsArray, DefragmentEmpty) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    arr->defragment();
    SUCCEED();
    delete arr;
}

TEST(SectorsArray, SectorsMapGrowShrink) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    for (int i = 0; i < 100; ++i) arr->insertSafe<Trivial>(i, Trivial{ i });
    arr->clear();
    arr->reserve(200);
    EXPECT_GE(arr->sectorsCapacity(), 200);
    delete arr;
}

TEST(SectorsArray, CopyMoveConstructor) {
    ecss::Memory::ReflectionHelper helper;
    auto* arr = ecss::Memory::SectorsArray::create<Health, Velocity>(helper, 10);
    for (int i = 0; i < 10; ++i)
        arr->emplace<Health>(i, i);

    auto copy = *arr;
    for (int i = 0; i < 10; ++i)
        EXPECT_EQ(copy.getSector(i)->getMember<Health>(0, 0)->value, i);

    auto moved = std::move(copy);
    for (int i = 0; i < 10; ++i)
        EXPECT_EQ(moved.getSector(i)->getMember<Health>(0, 0)->value, i);

    delete arr;
}

TEST(SectorsArray, Defragmentation) {
    ecss::Memory::ReflectionHelper helper;
    auto* arr = ecss::Memory::SectorsArray::create<Health, Velocity>(helper, 100);
    for (int i = 0; i < 100; ++i)
        arr->emplace<Health>(i, i);

    for (int i = 0; i < 100; i += 2) {
	    arr->eraseSector(i, false);
    }

    arr->defragment();
    int count = 0;
    for (auto it = arr->begin(); it != arr->end(); ++it) {
        auto sector = *it;
        if (sector && sector->isSectorAlive())
            ++count;
    }
    EXPECT_EQ(count, 50);
    delete arr;
}

TEST(SectorsArray, MassiveInsertErase_Sequential) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    constexpr int N = 100000;
    for (int i = 0; i < N; ++i)
        arr->insertSafe<Trivial>(i, Trivial{ i });
    EXPECT_EQ(arr->size(), N);

    for (int i = 0; i < N; i += N / 10)
        EXPECT_EQ(arr->findSector(i)->getMember<Trivial>(arr->getLayoutData<Trivial>())->a, i);

    for (int i = 0; i < N; i += 2)
        arr->eraseSector(i, false);
    arr->defragment();
    EXPECT_LE(arr->size(), N / 2 + 1);

    arr->clear();
    EXPECT_EQ(arr->size(), 0);
    delete arr;
}

TEST(SectorsArray_STRESS, Stress_MassiveInsertErase_RandomOrder) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    constexpr int N = 10000;
    std::vector<int> keys(N);
    std::iota(keys.begin(), keys.end(), 0);
    std::shuffle(keys.begin(), keys.end(), std::mt19937{ std::random_device{}() });
    for (int i = 0; i < N; ++i)
        arr->insertSafe<Trivial>(keys[i], Trivial{ i });
    EXPECT_EQ(arr->size(), N);

    std::shuffle(keys.begin(), keys.end(), std::mt19937{ std::random_device{}() });
    for (int i = 0; i < N / 2; ++i)
        arr->eraseSector(keys[i]);
    EXPECT_EQ(arr->size(), N - N / 2);
    delete arr;
}

TEST(SectorsArray, Stress_MassiveInsertEraseDefragment) {
    auto* arr = SA_T::create<Position, Velocity, Health>(GetReflection());
    constexpr int N = 50000;
    for (int i = 0; i < N; ++i)
        arr->emplace<Position>(i, i, -i);

    std::vector<int> rm;
    for (int i = 0; i < N; i += 3) rm.push_back(i);
    for (int id : rm) arr->eraseSector(id, false);

    arr->defragment();
    EXPECT_EQ(arr->size(), N - rm.size());
    delete arr;
}

TEST(SectorsArray, Stress_ReuseAfterClear) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    constexpr int N = 10000;
    for (int r = 0; r < 10; ++r) {
        for (int i = 0; i < N; ++i)
            arr->insertSafe<Trivial>(i, Trivial{ r * N + i });
        arr->clear();
        EXPECT_EQ(arr->size(), 0);
    }
    delete arr;
}

TEST(SectorsArray, InsertRemove_BoundarySectorIds) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    arr->insertSafe<Trivial>(0, Trivial{ 10 });
    arr->insertSafe<Trivial>(9999, Trivial{ 99 });
    arr->eraseSector(0);
    arr->eraseSector(9999);
    EXPECT_EQ(arr->size(), 0);
    delete arr;
}

TEST(SectorsArray, OutOfBoundsAccess_DoesNotCrash) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    EXPECT_EQ(arr->findSector(12345), nullptr);
    arr->erase(54321);
    SUCCEED();
    delete arr;
}

TEST(SectorsArray, Correctness_MoveOnlyType) {
    auto* arr = SA_T::create<MoveOnly>(GetReflection());
    arr->insertSafe<MoveOnly>(0, MoveOnly{ 10 });
    EXPECT_EQ(*arr->findSector(0)->getMember<MoveOnly>(arr->getLayoutData<MoveOnly>())->v, 10);
    arr->eraseSector(0);
    delete arr;
}

TEST(SectorsArray, Correctness_BigStruct) {
    auto* arr = SA_T::create<BigStruct>(GetReflection());
    BigStruct b; b.id = 777;
    arr->insertSafe(123, b);
    EXPECT_EQ(arr->findSector(123)->getMember<BigStruct>(arr->getLayoutData<BigStruct>())->id, 777);
    delete arr;
}

TEST(SectorsArray, NonTrivialDestructor_IsCalled) {
    CtorCounter::constructed = 0; CtorCounter::destroyed = 0;
    {
        auto* arr = SA_T::create<CtorCounter>(GetReflection());
        for (int i = 0; i < 100; ++i) {
	        arr->insertSafe<CtorCounter>(i, {});
        }
        delete arr;
    }
    EXPECT_EQ(CtorCounter::constructed * 2, CtorCounter::destroyed);
}

TEST(SectorsArray_STRESS, ThreadedInsert_Simple) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    constexpr int N = 10000, T = 8;
    std::vector<std::thread> ths;
    std::atomic<int> ready{ 0 };
    for (int t = 0; t < T; ++t) {
        ths.emplace_back([&, t] {
            ++ready; while (ready < T) {}
            for (int i = 0; i < N; ++i)
                arr->insertSafe<Trivial>(t * N + i, Trivial{ t * N + i });
        });
    }
    for (auto& th : ths) th.join();
    EXPECT_EQ(arr->size(), N * T);
    delete arr;
}

TEST(SectorsArray_STRESS, ThreadedInsertErase_Concurrent) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    constexpr int N = 3000, T = 6;
    std::vector<std::thread> ths;
    for (int t = 0; t < T; ++t) {
        ths.emplace_back([&, t] {
            for (int i = 0; i < N; ++i) {
                arr->insertSafe<Trivial>(t * N + i, Trivial{ t * N + i });
                if (i % 100 == 0) arr->eraseSector(t * N + i, false);
            }
        });
    }
    for (auto& th : ths) th.join();
    delete arr;
}

TEST(SectorsArray, ThreadedConcurrent_ClearInsert) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    constexpr int N = 400;
    std::thread insertSafeer([&] {
        for (int i = 0; i < N; ++i) arr->insertSafe<Trivial>(i, Trivial{ i });
    });
    std::thread clearer([&] { arr->clear(); });
    insertSafeer.join(); clearer.join();
    SUCCEED();
    delete arr;
}

TEST(SectorsArray_STRESS, ThreadedStress_RandomOps) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    constexpr int N = 10000, T = 8;
    std::atomic<int> ops{ 0 };
    std::vector<std::thread> ths;
    for (int t = 0; t < T; ++t) {
        ths.emplace_back([&, t] {
            std::mt19937 gen(t + 1); std::uniform_int_distribution<> dis(0, N - 1);
            for (int i = 0; i < N; ++i) {
                int id = dis(gen);
                arr->insertSafe<Trivial>(id, Trivial{ id });
                if (i % 10 == 0) arr->eraseSafe(id);
                ops++;
            }
        });
    }
    for (auto& th : ths) th.join();
    EXPECT_GT(ops, 0);
    delete arr;
}

TEST(SectorsArray, ThreadedIterateReadDuringInsert) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    std::atomic<bool> running{ true };
    std::thread writer([&] {
        for (int i = 0; i < 200000; ++i) arr->insertSafe<Trivial>(i, Trivial{ i });
        running = false;
    });
    int sum = 0;
    std::thread reader([&] {
        while (running) {
            auto lock = arr->readLock();
            for (auto it = arr->begin(), end = arr->end(); it != end; ++it) {
                auto* v = (*it)->getMember<Trivial>(arr->getLayoutData<Trivial>());
                if (v) sum += v->a;
            }
        }
    });
    writer.join(); running = false; reader.join();
    delete arr;
}

TEST(SectorsArray_STRESS, ThreadedIterateInsertEraseFuzz) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    std::atomic<bool> running{ true };

    constexpr int N = 200000;
    std::vector<int> insertSafeed(N, 0);

    std::thread writer([&] {
        for (int i = 0; i < N; ++i) {
            arr->insertSafe<Trivial>(i, Trivial{ i });
            insertSafeed[i] = 1;
            if (i % 10000 == 0) std::this_thread::yield();
        }
        running = false;
    });

    std::atomic<int64_t> sum{ 0 };
    std::thread reader([&] {
        while (running) {
            int64_t local_sum = 0;
            auto lock = arr->readLock();
            for (auto it = arr->begin(), end = arr->end(); it != end; ++it) {
                auto* v = (*it)->getMember<Trivial>(arr->getLayoutData<Trivial>());
                if (v) local_sum += v->a;
            }
            sum = local_sum;
            std::this_thread::yield();
        }
    });

    std::thread remover([&] {
        std::mt19937 rng{ std::random_device{}() };
        std::uniform_int_distribution<int> dist(0, N - 1);
        while (running) {
            int idx = dist(rng);
            if (insertSafeed[idx]) {
                arr->eraseSector(idx, true);
                insertSafeed[idx] = 0;
            }
            if (dist(rng) % 100 == 0) std::this_thread::yield();
        }
    });

    writer.join();
    running = false;
    remover.join();
    reader.join();

    int64_t manual_sum = 0;
    for (int i = 0; i < N; ++i) if (insertSafeed[i]) manual_sum += i;
    int64_t arr_sum = 0;
    for (auto it = arr->begin(), end = arr->end(); it != end; ++it) {
        auto* v = (*it)->getMember<Trivial>(arr->getLayoutData<Trivial>());
        if (v) arr_sum += v->a;
    }
    EXPECT_EQ(arr_sum, manual_sum);

    delete arr;
}

TEST(SectorsArray, ThreadedSimultaneousClearInsert) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    std::atomic<bool> done{ false };
    std::thread insertSafeer([&] {
        for (int i = 0; i < 1000; ++i) arr->insertSafe<Trivial>(i, Trivial{ i });
        done = true;
    });
    std::thread clearer([&] {
        for (int i = 0; i < 100; ++i) arr->clear();
    });
    insertSafeer.join(); done = true; clearer.join();
    SUCCEED();
    delete arr;
}

TEST(SectorsArray, ThreadedSimultaneousDefragment) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    for (int i = 0; i < 1000; ++i) arr->insertSafe<Trivial>(i, Trivial{ i });
    std::thread defrag([&] { for (int i = 0; i < 10; ++i) arr->defragment(); });
    std::thread eraser([&] { for (int i = 0; i < 1000; ++i) arr->erase(i); });
    defrag.join(); eraser.join();
    SUCCEED();
    delete arr;
}

TEST(SectorsArray, ThreadedStress_AllMethods) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    constexpr int N = 1000, T = 8;
    std::vector<std::thread> ths;
    for (int t = 0; t < T; ++t) {
        ths.emplace_back([&, t] {
            for (int i = 0; i < N; ++i) {
                arr->insertSafe<Trivial>(i, Trivial{ i });
                arr->eraseSector(i, false);
            }
        });
    }
    for (auto& th : ths) th.join();
    delete arr;
}

TEST(SectorsArray, ThreadedInsertMove) {
    auto* arr = SA_T::create<NonTrivial>(GetReflection());
    constexpr int N = 400;
    std::vector<std::thread> ths;
    for (int t = 0; t < 4; ++t) {
        ths.emplace_back([&, t] {
            for (int i = 0; i < N; ++i) {
                arr->insertSafe<NonTrivial>(t * N + i, NonTrivial{ std::to_string(t * N + i) });
            }
        });
    }
    for (auto& th : ths) th.join();
    delete arr;
}

TEST(SectorsArray, ThreadedReserve) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    std::thread res1([&] { arr->reserve(10000); });
    std::thread res2([&] { arr->reserve(5000); });
    res1.join(); res2.join();
    SUCCEED();
    delete arr;
}

TEST(SectorsArray, ThreadedShrinkToFit) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    for (int i = 0; i < 1000; ++i) arr->insertSafe<Trivial>(i, Trivial{ i });
    std::thread s1([&] { arr->shrinkToFit(); });
    std::thread s2([&] { arr->clear(); });
    s1.join(); s2.join();
    SUCCEED();
    delete arr;
}

TEST(SectorsArray, ThreadedStress_InsertEraseClear) {
    for (auto k = 0; k < 100; k++) {
        auto* arr = SA_T::create<Trivial>(GetReflection());
        std::thread t1([&] { for (int i = 0; i < 1000; ++i) arr->insertSafe<Trivial>(i, Trivial{ i }); });
        std::thread t2([&] { for (int i = 0; i < 500; ++i) arr->eraseSector(i); });
        std::thread t3([&] { arr->clear(); });
        t1.join(); t2.join(); t3.join();
        delete arr;
    }

    SUCCEED();
}

TEST(SectorsArray, ThreadedEraseRange) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    for (int i = 0; i < 500; ++i) arr->insertSafe<Trivial>(i, Trivial{ i });
    std::thread t([&] { arr->erase(100, 200); });
    t.join();
    EXPECT_LE(arr->size(), 300);
    delete arr;
}

TEST(SectorsArray_STRESS, ThreadedCopyMoveAssign) {
    std::atomic<size_t> counter = 0;
    for (auto k = 0; k < 100; k++) {
        auto* arr = SA_T::create<Trivial>(GetReflection());
        for (int i = 0; i < 200; ++i) arr->insertSafe<Trivial>(i, Trivial{ i });
        std::thread t1([&] {
            auto c = *arr;
            counter += c.size() != arr->size();
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        std::thread t2([&] {
            auto m = std::move(*arr);
            EXPECT_GE(m.size(), 0);
        });
        t1.join();
    	t2.join();
        delete arr;
    }

    EXPECT_EQ(counter, 0);
}

TEST(SectorsArray, ThreadedStress_DefragmentClear) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    for (int i = 0; i < 100; ++i) arr->insertSafe<Trivial>(i, Trivial{ i });
    std::thread t1([&] { arr->defragment(); });
    std::thread t2([&] { arr->clear(); });
    t1.join(); t2.join();
    SUCCEED();
    delete arr;
}

TEST(SectorsArray, ThreadedSimultaneousInsertDefrag) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    std::thread t1([&] { for (int i = 0; i < 1000; ++i) arr->insertSafe<Trivial>(i, Trivial{ i }); });
    std::thread t2([&] { arr->defragment(); });
    t1.join(); t2.join();
    SUCCEED();
    delete arr;
}

TEST(SectorsArray, ThreadedRandomized) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    constexpr int N = 10000, T = 6;
    std::vector<std::thread> ths;
    std::atomic<int> val{ 0 };
    for (int t = 0; t < T; ++t) {
        ths.emplace_back([&, t] {
            std::mt19937 g(t + 1); std::uniform_int_distribution<> d(0, N - 1);
            for (int i = 0; i < N; ++i) {
                int op = d(g) % 4;
                int id = d(g);
                switch (op) {
                case 0:
	                {
		                arr->insertSafe<Trivial>(id, Trivial{ id });
                		break;
	                }
                case 1: {
                	arr->eraseSector(id, false);
                	break;
                }
                case 2:
	                {
						//auto lock = arr->readLock();
                		if (auto* s = arr->findSector(id)) {
                            if (auto member = s->getMember<Trivial>(arr->getLayoutData<Trivial>())) {
                                val += member->a;
                            }
                            
                		}
                            
                		break;
	                }
                case 3:
	                {
						arr->reserveSafe(10 + (d(g) % 100));
                		break;
	                }
                }
            }
        });
    }
    for (auto& th : ths) th.join();
    delete arr;
}

TEST(SectorsArray_STRESS_light, MassiveConcurrentInsertEraseAndDefrag) {
    constexpr int threads = 8, N = 10000;
    auto* arr = SA_T::create<Health>(GetReflection());
    std::vector<std::thread> ts;
    std::atomic<bool> stop = false;
    for (int t = 0; t < threads; ++t) {
        ts.emplace_back([&, t] {
            std::mt19937 rng(t + 1337);
            while (!stop) {
                int i = rng() % N;
                arr->insertSafe<Health>(i, Health{ t * 10 });
                if (rng() % 7 == 0) arr->erase(i);
                if (rng() % 15 == 0) arr->defragment();
                if (rng() % 100 == 0) arr->reserveSafe(N + (rng() % 100));
                std::this_thread::sleep_for(std::chrono::microseconds(rng() % 30));
            }
        });
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    stop = true;
    for (auto& th : ts) th.join();
    delete arr;
    SUCCEED();
}

TEST(SectorsArray_STRESS_light, MoveOnly_InsertAndErase) {
    auto* arr = SA_T::create<MoveOnly>(GetReflection());
    arr->insertSafe<MoveOnly>(0, MoveOnly{ 123 });
    EXPECT_EQ(*arr->findSector(0)->getMember<MoveOnly>(arr->getLayoutData<MoveOnly>())->v, 123);
    arr->erase(0);
    delete arr;
}

TEST(SectorsArray_STRESS_light, TrivialType_StressDefrag) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    for (int i = 0; i < 5000; ++i) arr->emplace<Trivial>(i, i);
    for (int i = 0; i < 5000; i += 3) arr->erase(i);
    arr->defragment();
    int alive = 0;
    for (size_t i = 0; i < arr->size(); ++i)
        if (auto s = arr->at(i); s && s->isSectorAlive())
            ++alive;
    EXPECT_GE(alive, 3000);
    delete arr;
}

TEST(SectorsArray_STRESS_light, ThreadedRandomEraseClear) {
    constexpr int threads = 4, N = 4000;
    auto* arr = SA_T::create<Velocity>(GetReflection());
    for (int i = 0; i < N; ++i) arr->insertSafe<Velocity>(i, Velocity{ (float)i, (float)-i });
    std::atomic<bool> stop = false;
    std::vector<std::thread> ts;
    for (int t = 0; t < threads; ++t)
        ts.emplace_back([&, t] {
        std::mt19937 rng(t + 43);
        while (!stop) {
            int i = rng() % N;
            arr->erase(i);
            if (rng() % 500 == 0) arr->clear();
            std::this_thread::sleep_for(std::chrono::microseconds(rng() % 200));
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    stop = true;
    for (auto& th : ts) th.join();
    delete arr;
}

TEST(SectorsArray_STRESS_light, ABA_ProblemStress) {
    auto* arr = SA_T::create<Health>(GetReflection());
    for (int rep = 0; rep < 1000; ++rep) {
        arr->insertSafe<Health>(0, Health{ rep });
        arr->erase(0);
    }
    delete arr;
    SUCCEED();
}

TEST(SectorsArray_STRESS_light, AllApiBrutalMix) {
    constexpr int threads = 6, N = 1200;
    auto* arr = SA_T::create<Health, Velocity>(GetReflection());
    std::vector<std::thread> ts;
    std::atomic<bool> stop = false;
    for (int t = 0; t < threads; ++t) {
        ts.emplace_back([&, t] {
            std::mt19937 rng(t + 999);
            while (!stop) {
                int i = rng() % N;
                arr->insertSafe<Health>(i, Health{ t });
                arr->insertSafe<Velocity>(i, Velocity{ (float)i, (float)t });
                if (rng() % 10 == 0) arr->erase(i);
                if (rng() % 200 == 0) arr->reserve(N + (rng() % 500));
                if (rng() % 300 == 0) arr->defragment();
                if (rng() % 900 == 0) arr->clear();
                std::this_thread::sleep_for(std::chrono::microseconds(rng() % 100));
            }
        });
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    stop = true;
    for (auto& th : ts) th.join();
    delete arr;
    SUCCEED();
}

TEST(SectorsArray_STRESS_light, AliveAfterEraseInsertRace) {
    constexpr int threads = 4, N = 1500;
    auto* arr = SA_T::create<Health>(GetReflection());
    std::vector<std::thread> ts;
    std::atomic<bool> stop = false;
    for (int t = 0; t < threads; ++t) {
        ts.emplace_back([&, t] {
            std::mt19937 rng(t + 1);
            while (!stop) {
                int i = rng() % N;
                arr->eraseSector(i);
                arr->insertSafe<Health>(i, Health{ 1000 + t });
            }
        });
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    stop = true;
    for (auto& th : ts) th.join();
    int alive = 0;
    for (size_t i = 0; i < arr->size(); ++i)
        if (auto s = arr->findSector(i); s && s->isSectorAlive())
            ++alive;
    EXPECT_GT(alive, 1000);
    delete arr;
}

TEST(SectorsArray_STRESS_light, MultiComponentParallelRumble) {
    using Pair = std::pair<Health, Velocity>;
    constexpr int N = 4096, threads = 8;
    auto* arr = SA_T::create<Health, Velocity>(GetReflection());
    std::vector<std::thread> ts;
    std::atomic<bool> stop = false;
    for (int t = 0; t < threads; ++t) {
        ts.emplace_back([&, t] {
            std::mt19937 rng(t + 111);
            while (!stop) {
                int i = rng() % N;
                arr->insertSafe<Health>(i, Health{ (int)rng() });
                arr->insertSafe<Velocity>(i, Velocity{ (float)rng(), (float)rng() });
                if (rng() % 5 == 0) arr->eraseSector(i);
                if (rng() % 33 == 0) arr->defragment();
                if (rng() % 200 == 0) arr->clear();
            }
        });
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(900));
    stop = true;
    for (auto& th : ts) th.join();
    delete arr;
    SUCCEED();
}

TEST(SectorsArray_STRESS_light, ReserveEraseInsertDeadlock) {
    auto* arr = SA_T::create<Health>(GetReflection());
    arr->reserve(100);
    std::thread t1([&] {
        for (int i = 0; i < 100; ++i) arr->insertSafe<Health>(i, Health{ i });
    });
    std::thread t2([&] {
        for (int i = 0; i < 100; ++i) arr->erase(i);
    });
    t1.join(); t2.join();
    delete arr;
    SUCCEED();
}

TEST(SectorsArray_STRESS_light, DefragAfterRandomChaos) {
    auto* arr = SA_T::create<Health>(GetReflection());
    for (int i = 0; i < 500; ++i) arr->insertSafe<Health>(i, Health{ i });
    for (int i = 0; i < 200; ++i) arr->erase(i * 2);
    arr->defragment();
    int alive = 0;
    for (size_t i = 0; i < arr->size(); ++i)
        if (auto s = arr->at(i); s && s->isSectorAlive())
            ++alive;
    EXPECT_GE(alive, 250);
    delete arr;
}

TEST(SectorsArray, InsertErase_Alternating) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    for (int i = 0; i < 100; ++i) arr->insertSafe<Trivial>(i, Trivial{ i });
    for (int i = 0; i < 100; i += 2) arr->eraseSector(i);
    for (int i = 1; i < 100; i += 2) EXPECT_TRUE(arr->findSector(i)->isSectorAlive());
    for (int i = 0; i < 100; i += 2) EXPECT_FALSE(arr->findSector(i));
    delete arr;
}

TEST(SectorsArray, Insert_Defrag_AliveCount) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    for (int i = 0; i < 1000; ++i) arr->insertSafe<Trivial>(i, Trivial{ i });
    for (int i = 0; i < 1000; i += 3) arr->eraseSector(i);
    arr->defragment();
    int alive = 0;
    for (auto it = arr->begin(); it != arr->end(); ++it) alive += (*it)->isSectorAlive();
    EXPECT_EQ(alive, 1000 - (1000 / 3) - 1);
    delete arr;
}

TEST(SectorsArray, MoveOnly_StressInsertErase) {
    auto* arr = SA_T::create<MoveOnly>(GetReflection());
    for (int i = 0; i < 256; ++i) arr->insertSafe<MoveOnly>(i, MoveOnly{ i });
    for (int i = 0; i < 256; ++i) EXPECT_TRUE(arr->findSector(i)->getMember<MoveOnly>(arr->getLayoutData<MoveOnly>())->v);
    for (int i = 0; i < 256; i += 2) arr->eraseSector(i, false);
    arr->defragment();
    delete arr;
}

TEST(SectorsArray, ABA_ProblemStress) {
    static constexpr int N = 200, threads = 6;
    auto* arr = SA_T::create<Trivial>(GetReflection());
    std::vector<std::thread> ts;
    for (int t = 0; t < threads; ++t)
        ts.emplace_back([&, t] {
        for (int i = t * N; i < (t + 1) * N; ++i)
            arr->insertSafe<Trivial>(i, Trivial{ i });
    });
    for (auto& th : ts) th.join();

    std::atomic<int> alive = 0;

    for (int i = 0; i < threads * N; ++i)
        if (auto s = arr->findSector(i))
            alive += s->isSectorAlive();

    EXPECT_EQ(alive, threads * N);
    alive = 0;
    
    std::vector<std::thread> dels;
    for (int t = 0; t < threads; ++t)
        dels.emplace_back([&, t] {
        for (int i = t * N; i < (t + 1) * N; i += 2)
            arr->eraseSector(i);
    });
    for (auto& th : dels) th.join();
    for (int i = 0; i < threads * N; ++i)
        if (auto s = arr->findSector(i))
            alive += s->isSectorAlive();
    EXPECT_EQ(alive, threads * N / 2);
    delete arr;
}

TEST(SectorsArray, ReserveEraseInsertDeadlock) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    std::thread t1([&] { for (int i = 0; i < 10000; ++i) arr->insertSafe<Trivial>(i, Trivial{ i }); });
    std::thread t2([&] { for (int i = 9999; i >= 0; --i) arr->eraseSector(i); });
    t1.join(); t2.join();
    delete arr;
}

TEST(SectorsArray, ThreadedRandomEraseClear) {
    auto* arr = SA_T::create<Trivial>(GetReflection());
    std::vector<int> ids(10000); std::iota(ids.begin(), ids.end(), 0);
    for (int id : ids) arr->insertSafe<Trivial>(id, Trivial{ id });
    std::shuffle(ids.begin(), ids.end(), std::mt19937{ std::random_device{}() });
    std::thread t1([&] { for (int i = 0; i < 5000; ++i) arr->eraseSector(ids[i]); });
    std::thread t2([&] { for (int i = 5000; i < 10000; ++i) arr->eraseSector(ids[i]); });
    t1.join(); t2.join();
    delete arr;
}

TEST(SectorsArray, AllApiBrutalMix) {
    auto* arr = SA_T::create<Trivial, MoveOnly>(GetReflection());
    for (int i = 0; i < 1000; ++i) arr->insertSafe<Trivial>(i, Trivial{ i });
    for (int i = 0; i < 1000; i += 2) arr->insertSafe<MoveOnly>(i, MoveOnly{ i });
    for (int i = 0; i < 500; ++i) arr->eraseSector(i);
    arr->defragment();
    auto* arr2 = new SA_T(*arr); // copy
    delete arr;
    delete arr2;
}

TEST(SectorsArray_STRESS, MassiveConcurrentInsertEraseAndDefrag) {
    static constexpr int N = 10000;
    auto* arr = SA_T::create<Trivial>(GetReflection());
    std::vector<std::thread> insertSafe_threads, erase_threads;
    for (int t = 0; t < 4; ++t)
        insertSafe_threads.emplace_back([&, t] {
        for (int i = t * N; i < (t + 1) * N; ++i)
            arr->insertSafe<Trivial>(i, Trivial{ i });
    });
    for (auto& th : insertSafe_threads) th.join();
    for (int t = 0; t < 4; ++t)
        erase_threads.emplace_back([&, t] {
        for (int i = t * N; i < (t + 1) * N; i += 2)
            arr->eraseSector(i);
    });
    for (auto& th : erase_threads) th.join();
    arr->defragment();
    delete arr;
}

TEST(SectorsArray, MultiComponentParallelRumble) {
    auto* arr = SA_T::create<Trivial, MoveOnly>(GetReflection());
    std::thread t1([&] {
        for (int i = 0; i < 10000; ++i) arr->insertSafe<Trivial>(i, Trivial{ i });
    });
    std::thread t2([&] {
        for (int i = 0; i < 10000; ++i) arr->insertSafe<MoveOnly>(i, MoveOnly{ i });
    });
    t1.join(); t2.join();
    delete arr;
}

}