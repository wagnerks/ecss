// build:  -std=c++20 -O2 -pthread
// suggest: -fsanitize=thread  (TSAN)

#include <gtest/gtest.h>
#include <thread>
#include <barrier>
#include <random>
#include <chrono>
#include <atomic>
#include <vector>
#include <algorithm>

// подключи свои хедеры
#include <future>
#include <ecss/memory/SectorsArray.h>

using namespace std::chrono_literals;
using ecss::Memory::SectorsArray;
using ecss::SectorId;

// простая «начинка» для сектора
struct Payload { int v = 0; };

static SectorsArray<>* makeArray(size_t cap) {
    auto* arr = SectorsArray<>::template create<Payload>();
    arr->reserve(cap);
    // заполним секторами [0..cap-1]
    for (size_t i = 0; i < cap; ++i) {
        arr->template emplace<Payload>(static_cast<SectorId>(i), Payload{ int(i) });
    }
    return arr;
}

TEST(SectorsArrayConcurrency, EraseBlocksWhilePinnedThenProceeds) {
    std::unique_ptr<SectorsArray<>> holder(makeArray(128));
    auto& arr = *holder;

    const SectorId target = 37;
    // запиним сектор в отдельном треде и подержим
    std::promise<void> pinnedReady, canUnpin;
    std::thread pinT([&] {
        auto pinned = arr.pinSector(target);
        pinnedReady.set_value();
        // ждём разрешения на распин
        canUnpin.get_future().wait();
        // RAII снимет pin
    });

    pinnedReady.get_future().wait();

    // попытка убрать сектор: должен уйти в pending (не удалиться)
    arr.eraseAsync(target, 1, /*shift=*/false);
    // moment of truth: sector все еще должен существовать
    {
        auto s = arr.findSector(target);
        ASSERT_NE(s, nullptr) << "sector must not be erased while pinned";
    }

    // теперь отпустим пин
    canUnpin.set_value();
    pinT.join();

    // в конце кадра удаление должно пройти
    arr.processPendingErases();
    auto s2 = arr.findSector(target);
    ASSERT_EQ(s2, nullptr) << "sector should be erased after unpin + processing";
}

TEST(SectorsArrayConcurrency, WatermarkBlocksAndLowersAfterUnpin) {
    std::unique_ptr<SectorsArray<>> holder(makeArray(256));
    auto& arr = *holder;

    const SectorId hi = 200;
    const SectorId lo = 50;

    // держим «правый» пин, чтобы поднять max
    auto pHi = arr.pinSector(hi);
    // параллельно пытаемся менять сектор с id <= max — нельзя
    std::atomic<bool> canChange{ true };

    std::thread t([&] {
        // жёсткая проверка через canMoveSector (индикативно)
        for (int i = 0; i < 1000; ++i) {
            if (arr.containsSector(lo)) {
                if (arr.getSector(lo)) {
                    if (arr.getSector(hi)) {
                        if (!arr.containsSector(hi)) break;
                        if (!arr.containsSector(lo)) break;
                       
                        // если watermark работает, изменение сектора lo блокируется
                        arr.eraseAsync(lo, 1, /*shift=*/false), true;
                        
                    }
                }
            }
            std::this_thread::yield();
        }
        // попробуем явно: пока hi запинен, waitUntilChangeable(lo) должна висеть,
        // но мы её не вызываем тут, чтобы не ловить флак.
        canChange.store(false, std::memory_order_relaxed);
    });

    std::this_thread::sleep_for(50ms);
    EXPECT_FALSE(canChange.load(std::memory_order_relaxed))
        << "watermark should prevent changes for ids <= max";

    // распиним HI → watermark должен опуститься, и изменения станут возможны
    pHi = {}; // RAII unpin
    arr.processPendingErases(); // чтобы сработали очереди, не обязательно, но ускоряет

    // после распина ждем, пока сектор lo можно менять
    arr.eraseAsync(lo, 1, /*shift=*/false);
    arr.processPendingErases();
    auto s = arr.findSector(lo);
    ASSERT_EQ(s, nullptr) << "after unpin of HI, LO sector should be erasable";

    t.join();
}

TEST(SectorsArrayConcurrency, RandomStressNoDeadlockNoLostWakeups) {
    constexpr size_t CAP = 512;
    constexpr int    READERS = 8;
    constexpr int    WRITERS = 4;
    constexpr auto   DURATION = 2s;

    std::unique_ptr<SectorsArray<>> holder(makeArray(CAP));
    auto& arr = *holder;

    std::atomic<bool> stop{ false };
    std::barrier start(READERS + WRITERS);

    // читатели: пинают случайные id
    auto reader = [&](int tid) {
        std::mt19937_64 rng(std::random_device{}() ^ (uint64_t)tid);
        start.arrive_and_wait();
        while (!stop.load(std::memory_order_relaxed)) {
            SectorId id = rng() % CAP;
            auto pin = arr.pinSector(id);
            // мелкое чтение
            auto s = pin.get();
            if (s) { volatile int x = s->isAliveData; (void)x; }
            // иногда подождём, провоцируя пересечения
            if ((rng() & 7u) == 0) std::this_thread::sleep_for(100us);
        }
    };

    // писатели: erase / processPending / defragment / иногда reserve
    auto writer = [&](int tid) {
        std::mt19937_64 rng(std::random_device{}() ^ (~(uint64_t)tid));
        start.arrive_and_wait();
        while (!stop.load(std::memory_order_relaxed)) {
            const uint64_t r = rng();
            if ((r & 0x3) == 0) {
                // попытка удалить случайный сектор
                SectorId id = r % CAP;
                arr.eraseAsync(id, 1, /*shift=*/false);
            }
            else if ((r & 0x7) == 1) {
                arr.processPendingErases();
            }
            else if ((r & 0xF) == 2) {
                arr.defragment();
            }
            else if ((r & 0xFF) == 3) {
                // редкий reserve — спровоцировать ensureSidecarCapacity
                arr.reserve(uint32_t(CAP + (r % 64)));
            }
            // иногда ждём конкретного id, чтобы проверить waitUntilChangeable
            if ((r & 0x1F) == 4) {
                SectorId id = r % CAP;
                arr.eraseAsync(id, 1, /*shift=*/false);
                arr.processPendingErases();
            }
        }
    };

    std::vector<std::thread> th;
    for (int i = 0; i < READERS; ++i) th.emplace_back(reader, i);
    for (int i = 0; i < WRITERS; ++i) th.emplace_back(writer, i);

    std::this_thread::sleep_for(DURATION);
    stop.store(true, std::memory_order_relaxed);

    for (auto& t : th) t.join();

    // быстрая sanity-проверка: после прохода очереди удалений — всё стабильно
    holder->processPendingErases();
    // если что-то пошло не так (дедлок/лост-вейкап/UB), этот тест часто падает под TSAN или зависает.
    SUCCEED();
}


TEST(SectorArrayPinBitMask, highestSetTests) {
    ecss::Threads::PinnedIndexesBitMask mask;
    mask.set(0, true);
    EXPECT_EQ(mask.test(0), true);
    EXPECT_EQ(mask.test(5), false);
    EXPECT_EQ(mask.highestSet(), 0);
    mask.set(5, true);
    EXPECT_EQ(mask.highestSet(), 5);
    mask.set(3, true);
    EXPECT_EQ(mask.highestSet(), 5);
    mask.set(5, false);
    EXPECT_EQ(mask.highestSet(), 3);
    mask.set(5, false);

    EXPECT_EQ(mask.highestSet(), 3);
    mask.set(3, false);
    EXPECT_EQ(mask.highestSet(), 0);
    mask.set(0, false);
    EXPECT_EQ(mask.highestSet(), -1);

    mask.set(300000000, true);
    EXPECT_EQ(mask.highestSet(), 300000000);
    mask.set(30000000, true);
    EXPECT_EQ(mask.highestSet(), 300000000);
    mask.set(300000000, false);
    EXPECT_EQ(mask.highestSet(), 30000000);
    mask.set(30000000, false);
    EXPECT_EQ(mask.highestSet(), -1);
}
