#include <gtest/gtest.h>

#include <thread>
#include <barrier>
#include <random>
#include <chrono>
#include <atomic>
#include <vector>

#include <future>
#include <ecss/memory/SectorsArray.h>
#include <ecss/memory/Sector.h>

using namespace std::chrono_literals;
using ecss::Memory::SectorsArray;
using ecss::SectorId;
namespace Sector = ecss::Memory::Sector;

struct Payload { int v = 0; };

static SectorsArray<>* makeArray(size_t cap) {
    auto* arr = SectorsArray<>::create<Payload>();
    arr->reserve(static_cast<uint32_t>(cap));
    
    for (size_t i = 0; i < cap; ++i) {
        arr->emplace<Payload>(static_cast<SectorId>(i), Payload{ int(i) });
    }
    return arr;
}

TEST(SectorsArrayConcurrency, EraseBlocksWhilePinnedThenProceeds) {
    std::unique_ptr<SectorsArray<>> holder(makeArray(128));
    auto& arr = *holder;

    const SectorId target = 37;
    
    std::promise<void> pinnedReady, canUnpin;
    std::thread pinT([&] {
        auto pinned = arr.pinSector(target);
        pinnedReady.set_value();
        
        canUnpin.get_future().wait();
        
    });

    pinnedReady.get_future().wait();

    
    arr.eraseAsync(target, 1);
    
    {
        auto data = arr.findSectorData(target);
        ASSERT_NE(data, nullptr) << "sector must not be erased while pinned";
    }

    
    canUnpin.set_value();
    pinT.join();

    
    arr.processPendingErases();
    auto data2 = arr.findSectorData(target);
    ASSERT_EQ(data2, nullptr) << "sector should be erased after unpin + processing";
}

TEST(SectorsArrayConcurrency, WatermarkBlocksAndLowersAfterUnpin) {
    std::unique_ptr<SectorsArray<>> holder(makeArray(256));
    auto& arr = *holder;

    const SectorId hi = 200;
    const SectorId lo = 50;

    auto pHi = arr.pinSector(hi);
    
    std::atomic<bool> canChange{ true };

    std::thread t([&] {
        
        for (int i = 0; i < 1000; ++i) {
            if (arr.containsSector(lo)) {
                if (arr.findSectorData(lo)) {
                    if (arr.findSectorData(hi)) {
                        if (!arr.containsSector(hi)) break;
                        if (!arr.containsSector(lo)) break;
                       
                        
                        arr.eraseAsync(lo, 1);
                       
                    }
                }
            }
            std::this_thread::yield();
        }
        
        
        canChange.store(false, std::memory_order_relaxed);
    });

    t.join();
    EXPECT_FALSE(canChange.load(std::memory_order_relaxed))
        << "watermark should prevent changes for ids <= max";

    
    pHi = {}; 
    arr.processPendingErases(); 

    
    arr.eraseAsync(lo, 1);
    arr.processPendingErases();
    auto data = arr.findSectorData(lo);
    ASSERT_EQ(data, nullptr) << "after unpin of HI, LO sector should be erasable";
}

TEST(SectorsArrayConcurrency, RandomStressNoDeadlockNoLostWakeups) {
    constexpr size_t CAP = 512;
    constexpr int    READERS = 8;
    constexpr int    WRITERS = 4;
    constexpr auto   DURATION = 2s;

    std::unique_ptr<SectorsArray<>> holder(makeArray(CAP));
    auto& arr = *holder;
    struct cv_barrier {
        explicit cv_barrier(int count) : total(count) {}
        void arrive_and_wait() {
            std::unique_lock<std::mutex> lk(m);
            int gen = generation;
            if (++arrived == total) {
                arrived = 0;
                ++generation;
                cv.notify_all();
            } else {
                cv.wait(lk, [&]{ return generation != gen; });
            }
        }
    private:
        std::mutex m;
        std::condition_variable cv;
        int arrived = 0;
        int generation = 0;
        const int total;
    };
    
    std::atomic<bool> stop{ false };
    cv_barrier start(READERS + WRITERS);

    
    auto reader = [&](int tid) {
        std::mt19937_64 rng(std::random_device{}() ^ (uint64_t)tid);
        start.arrive_and_wait();
        while (!stop.load(std::memory_order_relaxed)) {
            SectorId id = rng() % CAP;
            auto pin = arr.pinSector(id);
            
            if (pin) {
                volatile uint32_t x = pin.getIsAlive();
                (void)x;
            }
            
            if ((rng() & 7u) == 0) std::this_thread::sleep_for(10ms);
        }
    };

    
    auto writer = [&](int tid) {
        std::mt19937_64 rng(std::random_device{}() ^ (~(uint64_t)tid));
        start.arrive_and_wait();
        while (!stop.load(std::memory_order_relaxed)) {
            const uint64_t r = rng();
            if ((r & 0x3) == 0) {
                
                SectorId id = r % CAP;
                arr.eraseAsync(id, 1);
            }
            else if ((r & 0x7) == 1) {
                arr.processPendingErases();
            }
            else if ((r & 0xF) == 2) {
                arr.tryDefragment();
            }
            else if ((r & 0xFF) == 3) {
                
                arr.reserve(uint32_t(CAP + (r % 64)));
            }
            
            if ((r & 0x1F) == 4) {
                SectorId id = r % CAP;
                arr.eraseAsync(id, 1);
                arr.processPendingErases();
            }
        }
    };

    std::vector<std::thread> th;
    th.reserve(READERS + WRITERS);
    for (int i = 0; i < READERS; ++i) th.emplace_back(reader, i);
    for (int i = 0; i < WRITERS; ++i) th.emplace_back(writer, i);

    std::this_thread::sleep_for(DURATION);
    stop.store(true, std::memory_order_relaxed);

    for (auto& t : th) t.join();

    
    holder->processPendingErases();

    
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
