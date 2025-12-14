#include <random>
#include <chrono>
#include <iostream>
#include <thread>

#include <ecss/memory/SectorsArray.h>
#include <ecss/memory/Sector.h>

#include <gtest/gtest.h>

namespace SectorsArrayTest
{
	struct Trivial { int a; };
	struct NonTrivial { std::string s; NonTrivial() = default; NonTrivial(std::string str) : s(std::move(str)) {} };

	struct Position { float x, y; };
	struct Velocity { float dx, dy; };
	struct Health { int value; };

	struct BigStruct { char data[512]; int id; };
	struct MoveOnly { std::unique_ptr<int> v; MoveOnly(int x) : v(new int(x)) {} MoveOnly(MoveOnly&& o) noexcept : v(std::move(o.v)) {} MoveOnly& operator=(MoveOnly&& o) noexcept { v = std::move(o.v); return *this; } };
	struct CtorCounter { static int constructed, destroyed; CtorCounter() { ++constructed; } ~CtorCounter() { ++destroyed; } };
	int CtorCounter::constructed = 0; int CtorCounter::destroyed = 0;

using namespace ecss;
using namespace ecss::Memory;

using SA_T = SectorsArray<true, ChunksAllocator<32>>;
using SA_T_OT = SectorsArray<false, ChunksAllocator<32>>;

// Helper to get component from sector data using public API
template<typename T, typename Arr>
T* getComponent(Arr* arr, SectorId id) {
	auto* data = arr->template findSectorData<false>(id);
	if (!data) return nullptr;
	auto isAlive = arr->template getIsAlive<false>(id);
	const auto& layout = arr->template getLayoutData<T>();
	return Sector::getMember<T>(data, layout, isAlive);
}

// Helper to check if sector exists and is alive
template<typename Arr>
bool sectorExists(Arr* arr, SectorId id) {
	auto idx = arr->template findLinearIdx<false>(id);
	if (idx == INVALID_IDX) return false;
	return Sector::isSectorAlive(arr->template getIsAliveRef<false>(idx));
}

TEST(SectorsArray, DefaultConstructEmpty) {
	auto* arr = SA_T::create<Trivial>();
	EXPECT_EQ(arr->size(), 0);
	EXPECT_TRUE(arr->empty());
	delete arr;
}

TEST(SectorsArray, InsertAndFind_Trivial) {
	auto* arr = SA_T::create<Trivial>();
	arr->insert<Trivial>(123, Trivial{ 42 });
	EXPECT_EQ(arr->size(), 1);
	auto* v = getComponent<Trivial>(arr, 123);
	ASSERT_NE(v, nullptr);
	EXPECT_EQ(v->a, 42);
	delete arr;
}

TEST(SectorsArray, InsertAndFind_NonTrivial) {
	auto* arr = SA_T::create<NonTrivial>();
	arr->insert<NonTrivial>(1, NonTrivial{ "test" });
	auto* v = getComponent<NonTrivial>(arr, 1);
	ASSERT_NE(v, nullptr);
	EXPECT_EQ(v->s, "test");
	delete arr;
}

TEST(SectorsArray, MultipleInsertionsAndFind) {
	auto* arr = SA_T::create<Trivial>();
	for (int i = 0; i < 100; ++i) {
		arr->insert<Trivial>(i, Trivial{ i * 2 });
	}
	EXPECT_EQ(arr->size(), 100);
	for (int i = 0; i < 100; ++i) {
		auto* v = getComponent<Trivial>(arr, i);
		ASSERT_NE(v, nullptr);
		EXPECT_EQ(v->a, i * 2);
	}
	delete arr;
}

TEST(SectorsArray, InsertOverwrite) {
	auto* arr = SA_T::create<Trivial>();
	arr->insert<Trivial>(0, Trivial{ 1 });
	arr->insert<Trivial>(0, Trivial{ 2 });
	auto* v = getComponent<Trivial>(arr, 0);
	ASSERT_NE(v, nullptr);
	EXPECT_EQ(v->a, 2);
	delete arr;
}

TEST(SectorsArray, EraseSingle) {
	auto* arr = SA_T::create<Trivial>();
	arr->insert<Trivial>(0, Trivial{ 1 });
	arr->erase(0, 1, true);
	arr->processPendingErases();
	EXPECT_EQ(arr->size(), 0);
	EXPECT_FALSE(arr->containsSector(0));
	delete arr;
}

TEST(SectorsArray, EraseRange) {
	auto* arr = SA_T::create<Trivial>();
	for (int i = 0; i < 10; ++i) arr->insert<Trivial>(i, Trivial{ i });
	arr->erase(2, 5, true);
	arr->processPendingErases();
	EXPECT_EQ(arr->size(), 5);
	EXPECT_FALSE(arr->containsSector(2));
	delete arr;
}

TEST(SectorsArray, Clear) {
	auto* arr = SA_T::create<Trivial>();
	for (int i = 0; i < 3; ++i) arr->insert<Trivial>(i, Trivial{ i });
	arr->clear();
	EXPECT_EQ(arr->size(), 0);
	EXPECT_TRUE(arr->empty());
	delete arr;
}

TEST(SectorsArray, DefragmentRemovesDeadAndShiftsAlive) {
	auto* arr = SA_T::create<Trivial>();
	arr->insert<Trivial>(1, Trivial{ 1 });
	arr->insert<Trivial>(2, Trivial{ 2 });
	arr->insert<Trivial>(3, Trivial{ 3 });
	// erase() takes a linear index, not a sector ID. Find the linear index first.
	auto idx = arr->findLinearIdx(1);
	ASSERT_NE(idx, INVALID_IDX);
	arr->erase(idx);
	arr->defragment();
	EXPECT_EQ(arr->size(), 2);
	EXPECT_FALSE(arr->containsSector(1));
	EXPECT_TRUE(arr->containsSector(2));
	EXPECT_TRUE(arr->containsSector(3));
	delete arr;
}

TEST(SectorsArray, ReserveAndShrink) {
	auto* arr = SA_T::create<Trivial>();
	arr->reserve(100);
	EXPECT_GE(arr->capacity(), 32);
	for (int i = 0; i < 10; ++i) arr->insert<Trivial>(i, Trivial{ i });
	arr->shrinkToFit();
	delete arr;
}

TEST(SectorsArray, IteratorBasic) {
	auto* arr = SA_T::create<Trivial>();
	for (int i = 0; i < 30000; ++i) arr->insert<Trivial>(i, Trivial{ i });
	int sum = 0;
	const auto& layout = arr->getLayoutData<Trivial>();
	for (auto it = arr->begin(); it != arr->end(); ++it) {
		auto slot = *it;
		auto* v = Sector::getMember<Trivial>(slot.data, layout, slot.isAlive);
		if (v) sum += v->a;
	}
	EXPECT_EQ(sum, 449985000);
	delete arr;
}

TEST(SectorsArray, IteratorsTest) {
	constexpr size_t count = 100;
	constexpr size_t deleted = 5;
	auto* arr = SectorsArray<true, ChunksAllocator<count>>::create<Trivial>();
	arr->reserve(static_cast<uint32_t>(count));

	std::vector<ecss::SectorId> alives;
	alives.reserve(count);

	for (size_t i = 0; i < count; ++i) {
		arr->emplace<Trivial>(static_cast<SectorId>(i), static_cast<int>(i));
		alives.push_back(static_cast<SectorId>(i));
	}

	for (size_t i = 0; i < count; i += deleted) {
		arr->erase(static_cast<SectorId>(i), 1, false);
		alives.erase(std::find(alives.begin(), alives.end(), static_cast<SectorId>(i)));
	}

	size_t alivesCount = alives.size();

	size_t counter = 0;
	for (auto it = arr->begin(), itEnd = arr->end(); it != itEnd; ++it) {
		counter++;
	}
	EXPECT_EQ(count, counter);

	size_t counterAlive = 0;
	for (auto it = arr->begin(), itEnd = arr->end(); it != itEnd; ++it) {
		auto slot = *it;
		if (slot.data && Sector::isSectorAlive(slot.isAlive)) {
			EXPECT_EQ(alives[counterAlive], slot.id);
			counterAlive++;
		}
	}
	EXPECT_EQ(alivesCount, counterAlive);

	size_t counterAliveIt = 0;
	for (auto it = arr->beginAlive<Trivial>(), itEnd = arr->endAlive(); it != itEnd; ++it) {
		auto slot = *it;
		EXPECT_EQ(alives[counterAliveIt], slot.id);
		counterAliveIt++;
	}
	EXPECT_EQ(alivesCount, counterAliveIt);

	delete arr;
}

TEST(SectorsArray, InsertMove) {
	auto* arr = SA_T::create<NonTrivial>();
	NonTrivial ntr("abc");
	arr->insert<NonTrivial>(5, std::move(ntr));
	auto* v = getComponent<NonTrivial>(arr, 5);
	ASSERT_NE(v, nullptr);
	EXPECT_EQ(v->s, "abc");
	delete arr;
}

TEST(SectorsArray, MappingAndCapacity) {
	auto* arr = SA_T::create<Trivial>();
	arr->reserve(32);
	arr->insert<Trivial>(10, Trivial{ 100 });
	EXPECT_TRUE(arr->containsSector(10));
	EXPECT_FALSE(arr->containsSector(99));
	EXPECT_EQ(arr->findLinearIdx(10), 0u);
	delete arr;
}

TEST(SectorsArray_STRESS, ThreadedInsert) {
	auto* arr = SA_T::create<Trivial>();
	constexpr int threadCount = 10;
	constexpr int N = 200;
	std::vector<std::thread> threads;
	std::atomic<int> ready{ 0 };

	for (int t = 0; t < threadCount; ++t) {
		threads.emplace_back([&, t] {
			++ready;
			while (ready < threadCount) {}
			for (int i = 0; i < N; ++i) {
				arr->insert<Trivial>(t * N + i, Trivial{ i });
			}
		});
	}
	for (auto& th : threads) th.join();
	EXPECT_EQ(arr->size(), threadCount * N);
	delete arr;
}

TEST(SectorsArray, ThreadedFindAndErase) {
	constexpr int N = 1000;
	auto* arr = SA_T::create<Trivial>();
	for (int i = 0; i < N; ++i) arr->insert<Trivial>(i, Trivial{ i });

	std::atomic<int> sum{ 0 };
	std::thread reader([&] {
		for (int i = 0; i < N; ++i) {
			if (auto pin = arr->pinSector(i)) {
				auto* v = Sector::getComponent<Trivial>(pin.getData(), pin.getIsAlive(), arr->getLayout());
				if (v) sum += v->a;
			}
		}
	});

	std::thread eraser([&] {
		for (int i = 0; i < N; i += 2) arr->eraseAsync(i,1);
	});

	reader.join();
	eraser.join();

	EXPECT_GE(sum, 0);
	delete arr;
}

TEST(SectorsArray, InsertInvalidEraseOutOfBounds) {
	auto* arr = SA_T::create<Trivial>();
	arr->insert<Trivial>(0, Trivial{ 10 });
	arr->erase(10); 
	EXPECT_EQ(arr->size(), 1);
	delete arr;
}

TEST(SectorsArray, DoubleClear) {
	auto* arr = SA_T::create<Trivial>();
	arr->clear();
	arr->clear();
	SUCCEED();
	delete arr;
}

TEST(SectorsArray, DefragmentEmpty) {
	auto* arr = SA_T::create<Trivial>();
	arr->defragment();
	SUCCEED();
	delete arr;
}

TEST(SectorsArray, SectorsMapGrowShrink) {
	auto* arr = SA_T::create<Trivial>();
	for (int i = 0; i < 100; ++i) arr->insert<Trivial>(i, Trivial{ i });
	arr->clear();
	arr->shrinkToFit();
	arr->reserve(200);
	EXPECT_GE(arr->sparseCapacity(), 100u);
	delete arr;
}

TEST(SectorsArray, CopyMoveConstructor) {
	auto* arr = ecss::Memory::SectorsArray<>::create<Health, Velocity>();
	arr->reserve(10);
	for (int i = 0; i < 10; ++i)
		arr->emplace<Health>(i, i);

	auto copy = *arr;
	for (int i = 0; i < 10; ++i) {
		auto* v = getComponent<Health>(&copy, i);
		ASSERT_NE(v, nullptr);
		EXPECT_EQ(v->value, i);
	}

	auto moved = std::move(copy);
	for (int i = 0; i < 10; ++i) {
		auto* v = getComponent<Health>(&moved, i);
		ASSERT_NE(v, nullptr);
		EXPECT_EQ(v->value, i);
	}

	delete arr;
}

TEST(SectorsArray, Defragmentation) {
	auto* arr = ecss::Memory::SectorsArray<>::create<Health, Velocity>();
	arr->reserve(100);
	for (int i = 0; i < 100; ++i)
		arr->emplace<Health>(i, i);

	for (int i = 0; i < 100; i += 2) {
		arr->eraseAsync(i, 1);
	}
	arr->processPendingErases();
	arr->defragment();
	int count = 0;
	for (auto it = arr->begin(); it != arr->end(); ++it) {
		auto slot = *it;
		if (slot.data && Sector::isSectorAlive(slot.isAlive))
			++count;
	}
	EXPECT_EQ(count, 50);
	delete arr;
}

TEST(SectorsArray, MassiveInsertErase_Sequential) {
	auto* arr = SA_T::create<Trivial>();
	constexpr int N = 100000;
	for (int i = 0; i < N; ++i)
		arr->insert<Trivial>(i, Trivial{ i });
	EXPECT_EQ(arr->size(), N);

	for (int i = 0; i < N; i += N / 10) {
		auto* v = getComponent<Trivial>(arr, i);
		ASSERT_NE(v, nullptr);
		EXPECT_EQ(v->a, i);
	}

	for (int i = 0; i < N; i += 2)
		arr->erase(i, 1);
	arr->defragment();
	EXPECT_LE(arr->size(), N / 2 + 1);

	arr->clear();
	EXPECT_EQ(arr->size(), 0);
	delete arr;
}

TEST(SectorsArray_STRESS, Stress_MassiveInsertErase_RandomOrder) {
	auto* arr = SA_T::create<Trivial>();
	constexpr int N = 1000;
	std::vector<int> keys(N);
	std::iota(keys.begin(), keys.end(), 0);
	std::shuffle(keys.begin(), keys.end(), std::mt19937{ std::random_device{}() });
	for (int i = 0; i < N; ++i)
		arr->insert<Trivial>(keys[i], Trivial{ i });
	EXPECT_EQ(arr->size(), N);

	std::shuffle(keys.begin(), keys.end(), std::mt19937{ std::random_device{}() });
	for (int i = 0; i < N / 2; ++i)
		arr->erase(keys[i]);
	arr->defragment();
	EXPECT_EQ(arr->size(), N - N / 2);
	delete arr;
}

TEST(SectorsArray, Stress_MassiveInsertEraseDefragment) {
	auto* arr = SA_T::create<Position, Velocity, Health>();
	constexpr int N = 50000;
	for (int i = 0; i < N; ++i)
		arr->emplace<Position>(i, (float)i, (float)-i);

	size_t deleted = 0;
	for (int i = 0; i < N; i += 3) { deleted++; arr->erase(i, 1, false); }

	arr->defragment();
	EXPECT_EQ(arr->size(), N - deleted);
	delete arr;
}

TEST(SectorsArray, Stress_ReuseAfterClear) {
	auto* arr = SA_T::create<Trivial>();
	constexpr int N = 10000;
	for (int r = 0; r < 10; ++r) {
		for (int i = 0; i < N; ++i)
			arr->insert<Trivial>(i, Trivial{ r * N + i });
		arr->clear();
		EXPECT_EQ(arr->size(), 0);
	}
	delete arr;
}

TEST(SectorsArray, InsertRemove_BoundarySectorIds) {
	auto* arr = SA_T::create<Trivial>();
	arr->insert<Trivial>(0, Trivial{ 10 });
	arr->insert<Trivial>(9999, Trivial{ 99 });
	arr->eraseAsync(0, 1);
	arr->eraseAsync(9999, 1);
	arr->defragment();
	EXPECT_EQ(arr->size(), 0);
	delete arr;
}

TEST(SectorsArray, OutOfBoundsAccess_DoesNotCrash) {
	auto* arr = SA_T::create<Trivial>();
	EXPECT_EQ(arr->findSectorData(12345), nullptr);
	arr->erase(54321);
	SUCCEED();
	delete arr;
}

TEST(SectorsArray, Correctness_MoveOnlyType) {
	auto* arr = SA_T::create<MoveOnly>();
	arr->insert<MoveOnly>(0, MoveOnly{ 10 });
	auto* v = getComponent<MoveOnly>(arr, 0);
	ASSERT_NE(v, nullptr);
	EXPECT_EQ(*v->v, 10);
	arr->eraseAsync(0);
	delete arr;
}

TEST(SectorsArray, Correctness_BigStruct) {
	auto* arr = SA_T::create<BigStruct>();
	BigStruct b; b.id = 777;
	arr->insert(123, b);
	auto* v = getComponent<BigStruct>(arr, 123);
	ASSERT_NE(v, nullptr);
	EXPECT_EQ(v->id, 777);
	delete arr;
}

TEST(SectorsArray, NonTrivialDestructor_IsCalled) {
	CtorCounter::constructed = 0; CtorCounter::destroyed = 0;
	{
		auto* arr = SA_T::create<CtorCounter>();
		for (int i = 0; i < 100; ++i) {
			arr->push<CtorCounter>(i, CtorCounter{});
		}
		delete arr;
	}
	EXPECT_EQ(CtorCounter::constructed * 2, CtorCounter::destroyed);
}

TEST(SectorsArray_STRESS, ThreadedInsert_Simple) {
	auto* arr = SectorsArray<true, ChunksAllocator<4>>::create<Trivial>();
	constexpr int N = 100, T = 8;
	std::vector<std::thread> ths;
	std::atomic<int> ready{ 0 };
	for (int t = 0; t < T; ++t) {
		ths.emplace_back([&, t] {
			++ready; while (ready < T) {}
			for (int i = 0; i < N; ++i)
				arr->insert<Trivial>(t * N + i, Trivial{ t * N + i });
		});
	}
	for (auto& th : ths) th.join();
	EXPECT_EQ(arr->size(), N * T);
	delete arr;
}

TEST(SectorsArray_STRESS, ThreadedInsertErase_Concurrent) {
	auto* arr = SA_T::create<Trivial>();
	constexpr int N = 300, T = 6;
	std::vector<std::thread> ths;
	for (int t = 0; t < T; ++t) {
		ths.emplace_back([&, t] {
			for (int i = 0; i < N; ++i) {
				arr->insert<Trivial>(t * N + i, Trivial{ t * N + i });
				if (i % 10 == 0) arr->eraseAsync(t * N + i, 1);
			}
		});
	}
	for (auto& th : ths) th.join();
	delete arr;
}

TEST(SectorsArray, ThreadedConcurrent_ClearInsert) {
	auto* arr = SA_T::create<Trivial>();
	constexpr int N = 400;
	std::thread inserter([&] {
		for (int i = 0; i < N; ++i) arr->insert<Trivial>(i, Trivial{ i });
	});
	std::thread clearer([&] { arr->clear(); });
	inserter.join(); clearer.join();
	SUCCEED();
	delete arr;
}

TEST(SectorsArray_STRESS, ThreadedStress_RandomOps) {
	auto* arr = SA_T::create<Trivial>();
	constexpr int N = 1000, T = 8;
	std::atomic<int> ops{ 0 };
	std::vector<std::thread> ths;
	for (int t = 0; t < T; ++t) {
		ths.emplace_back([&, t] {
			std::mt19937 gen(t + 1); std::uniform_int_distribution<> dis(0, N - 1);
			for (int i = 0; i < N; ++i) {
				int id = dis(gen);
				arr->insert<Trivial>(id, Trivial{ id });
				if (i % 3 == 0) arr->erase(id);
				ops++;
			}
		});
	}
	for (auto& th : ths) th.join();
	EXPECT_GT(ops, 0);
	delete arr;
}

TEST(SectorsArray, ThreadedIterateReadDuringInsert) {
	auto* arr = SectorsArray<true, ChunksAllocator<64>>::create<Trivial>();
	std::atomic<bool> running{ true };
	std::thread writer([&] {
		for (int i = 0; i < 2000; ++i) arr->insert<Trivial>(i, Trivial{ i });
		running = false;
	});
	int sum = 0;
	std::thread reader([&] {
		while (running) {
			auto end = arr->end();
			auto sz = arr->size();
			if (sz > 0) {
				auto endPinned = arr->pinSector(static_cast<ecss::SectorId>(sz - 1));
			}
			for (auto it = arr->begin(); it != end; ++it) {
				auto slot = *it;
				auto pin = arr->pinSector(slot.id);
				if (pin) {
					auto* v = Sector::getComponent<Trivial>(pin.getData(), pin.getIsAlive(), arr->getLayout());
					if (v) sum += v->a;
				}
			}
		}
	});
	writer.join(); running = false; reader.join();
	delete arr;
}

TEST(SectorsArray, ThreadedSimultaneousClearInsert) {
	auto* arr = SA_T::create<Trivial>();
	std::atomic<bool> done{ false };
	std::thread inserter([&] {
		for (int i = 0; i < 1000; ++i) arr->insert<Trivial>(i, Trivial{ i });
		done = true;
	});
	std::thread clearer([&] {
		for (int i = 0; i < 100; ++i) arr->clear();
	});
	inserter.join(); done = true; clearer.join();
	SUCCEED();
	delete arr;
}

TEST(SectorsArray, ThreadedSimultaneousDefragment) {
	auto* arr = SA_T::create<Trivial>();
	for (int i = 0; i < 1000; ++i) arr->insert<Trivial>(i, Trivial{ i });
	std::thread defrag([&] { for (int i = 0; i < 10; ++i) arr->defragment(); });
	std::thread eraser([&] { for (int i = 0; i < 1000; ++i) arr->erase(i, 1, false); });
	defrag.join(); eraser.join();
	SUCCEED();
	delete arr;
}

TEST(SectorsArray, ThreadedStress_AllMethods) {
	auto* arr = SA_T::create<Trivial>();
	constexpr int N = 1000, T = 8;
	std::vector<std::thread> ths;
	for (int t = 0; t < T; ++t) {
		ths.emplace_back([&] {
			for (int i = 0; i < N; ++i) {
				arr->insert<Trivial>(i, Trivial{ i });
				arr->eraseAsync(i, 1);
			}
		});
	}
	for (auto& th : ths) th.join();
	delete arr;
}

TEST(SectorsArray, ThreadedInsertMove) {
	auto* arr = SA_T::create<NonTrivial>();
	constexpr int N = 400;
	std::vector<std::thread> ths;
	for (int t = 0; t < 4; ++t) {
		ths.emplace_back([&, t] {
			for (int i = 0; i < N; ++i) {
				arr->insert<NonTrivial>(t * N + i, NonTrivial{ std::to_string(t * N + i) });
			}
		});
	}
	for (auto& th : ths) th.join();
	delete arr;
}

TEST(SectorsArray, ThreadedReserve) {
	auto* arr = SA_T::create<Trivial>();
	std::thread res1([&] { arr->reserve(10000); });
	std::thread res2([&] { arr->reserve(5000); });
	res1.join(); res2.join();
	SUCCEED();
	delete arr;
}

TEST(SectorsArray, ThreadedShrinkToFit) {
	auto* arr = SA_T::create<Trivial>();
	for (int i = 0; i < 1000; ++i) arr->insert<Trivial>(i, Trivial{ i });
	std::thread s1([&] { arr->shrinkToFit(); });
	std::thread s2([&] { arr->clear(); });
	s1.join(); s2.join();
	SUCCEED();
	delete arr;
}

TEST(SectorsArray_STRESS, ThreadedStress_InsertEraseClear) {
	for (auto k = 0; k < 100; k++) {
		auto* arr = SA_T::create<Trivial>();
		std::thread t1([&] { for (int i = 0; i < 1000; ++i) arr->insert<Trivial>(i, Trivial{ i }); });
		std::thread t2([&] { for (int i = 0; i < 500; ++i) arr->eraseAsync(i); });
		std::thread t3([&] { arr->clear(); });
		t1.join();
		t2.join();
		t3.join();

		delete arr;
	}

	SUCCEED();
}

TEST(SectorsArray, ThreadedEraseRange) {
	auto* arr = SA_T::create<Trivial>();
	for (int i = 0; i < 500; ++i) arr->insert<Trivial>(i, Trivial{ i });
	std::thread t([&] { arr->erase(100, 200, true); arr->defragment(); });
	t.join();

	EXPECT_LE(arr->size(), 300);
	delete arr;
}

TEST(SectorsArray_STRESS, ThreadedCopyMoveAssign) {
	std::atomic<size_t> counter = 0;
   
	auto* arr = SA_T::create<Trivial>();
	for (int i = 0; i < 200; ++i) arr->insert<Trivial>(i, Trivial{ i });
	std::thread t1([&] {
		auto c = *arr;
		counter += c.size() != arr->size();
	});
	std::this_thread::sleep_for(std::chrono::milliseconds(10));
	std::thread t2([&] {
		auto m = std::move(*arr);
		EXPECT_GE(m.size(), 0);
	});
	t1.join();
	t2.join();
	delete arr;

	EXPECT_EQ(counter, 0);
}

TEST(SectorsArray, ThreadedStress_DefragmentClear) {
	auto* arr = SA_T::create<Trivial>();
	for (int i = 0; i < 100; ++i) arr->insert<Trivial>(i, Trivial{ i });
	std::thread t1([&] { arr->defragment(); });
	std::thread t2([&] { arr->clear(); });
	t1.join(); t2.join();
	SUCCEED();
	delete arr;
}

TEST(SectorsArray, ThreadedSimultaneousInsertDefrag) {
	auto* arr = SA_T::create<Trivial>();
	std::thread t1([&] { for (int i = 0; i < 1000; ++i) arr->insert<Trivial>(i, Trivial{ i }); });
	std::thread t2([&] { arr->defragment(); });
	t1.join(); t2.join();
	SUCCEED();
	delete arr;
}

TEST(SectorsArray, ThreadedRandomized) {
	auto* arr = SA_T::create<Trivial>();
	constexpr int N = 1000, T = 6;
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
					arr->insert<Trivial>(id, Trivial{ id });
					break;
				case 1:
					arr->eraseAsync(id, 1);
					break;
				case 2:
					if (auto pin = arr->pinSector(id)) {
						auto* member = Sector::getComponent<Trivial>(pin.getData(), pin.getIsAlive(), arr->getLayout());
						if (member) {
							val += member->a;
						}
					}
					break;
				case 3:
					arr->reserve(10 + (d(g) % 100));
					break;
				}
			}
		});
	}
	for (auto& th : ths) th.join();
	delete arr;
}

TEST(SectorsArray_STRESS_light, MassiveConcurrentInsertEraseAndDefrag) {
	constexpr int threads = 8, N = 10000;
	auto* arr = SA_T::create<Health>();
	std::vector<std::thread> ts;
	std::atomic<bool> stop = false;
	for (int t = 0; t < threads; ++t) {
		ts.emplace_back([&, t] {
			std::mt19937 rng(t + 1337);
			while (!stop) {
				int i = rng() % N;
				arr->insert<Health>(i, Health{ t * 10 });
				if (rng() % 7 == 0) arr->erase(i);
				if (rng() % 15 == 0) arr->defragment();
				if (rng() % 100 == 0) arr->reserve(N + (rng() % 100));
				std::this_thread::sleep_for(std::chrono::microseconds(rng() % 30));
			}
		});
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	stop = true;
	for (auto& th : ts) th.join();
	delete arr;
	SUCCEED();
}

TEST(SectorsArray_STRESS_light, MoveOnly_InsertAndErase) {
	auto* arr = SA_T::create<MoveOnly>();
	arr->insert<MoveOnly>(0, MoveOnly{ 123 });
	auto* v = getComponent<MoveOnly>(arr, 0);
	ASSERT_NE(v, nullptr);
	EXPECT_EQ(*v->v, 123);
	arr->erase(0);
	delete arr;
}

TEST(SectorsArray_STRESS_light, TrivialType_StressDefrag) {
	auto* arr = SA_T::create<Trivial>();
	for (int i = 0; i < 5000; ++i) arr->emplace<Trivial>(i, i);
	for (int i = 0; i < 5000; i += 3) arr->erase(i);
	arr->defragment();
	int alive = 0;
	for (auto it = arr->begin(); it != arr->end(); ++it) {
		auto slot = *it;
		if (slot.data && Sector::isSectorAlive(slot.isAlive))
			++alive;
	}
	EXPECT_GE(alive, 3000);
	delete arr;
}

TEST(SectorsArray_STRESS_light, ABA_ProblemStress) {
	auto* arr = SA_T::create<Health>();
	for (int rep = 0; rep < 1000; ++rep) {
		arr->insert<Health>(0, Health{ rep });
		arr->erase(0);
	}
	delete arr;
	SUCCEED();
}

TEST(SectorsArray_STRESS_light, AliveAfterEraseInsertRace) {
	constexpr int threads = 4, N = 1500;
	auto* arr = SA_T::create<Health>();
	std::vector<std::thread> ts;
	std::atomic<bool> stop = false;
	for (int t = 0; t < threads; ++t) {
		ts.emplace_back([&, t] {
			std::mt19937 rng(t + 1);
			while (!stop) {
				int i = rng() % N;
				arr->erase(i, 1);
				arr->defragment();
				arr->insert<Health>(i, Health{ 1000 + t });
			}
		});
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(300));
	stop = true;
	for (auto& th : ts) th.join();
	
	arr->defragment();
	int alive = 0;
	for (auto it = arr->beginAlive<Health>(), end = arr->endAlive(); it != end; ++it){
		++alive;
	}
   
	EXPECT_GT(alive, 0);
	delete arr;
}

TEST(SectorsArray, InsertErase_Alternating) {
	auto* arr = SA_T::create<Trivial>();
	for (int i = 0; i < 100; ++i) arr->insert<Trivial>(i, Trivial{ i });
	for (int i = 0; i < 100; i += 2) arr->eraseAsync(i);
	for (int i = 1; i < 100; i += 2) EXPECT_TRUE(sectorExists(arr, i));
	for (int i = 0; i < 100; i += 2) EXPECT_FALSE(sectorExists(arr, i));
	delete arr;
}

TEST(SectorsArray, Insert_Defrag_AliveCount) {
	auto* arr = SA_T::create<Trivial>();
	for (int i = 0; i < 1000; ++i) arr->insert<Trivial>(i, Trivial{ i });
	for (int i = 0; i < 1000; i += 3) arr->eraseAsync(i);
	arr->defragment();
	int alive = 0;
	for (auto it = arr->begin(); it != arr->end(); ++it) {
		auto slot = *it;
		alive += Sector::isSectorAlive(slot.isAlive) ? 1 : 0;
	}
	EXPECT_EQ(alive, 1000 - (1000 / 3) - 1);
	delete arr;
}

TEST(SectorsArray, MoveOnly_StressInsertErase) {
	auto* arr = SA_T::create<MoveOnly>();
	for (int i = 0; i < 256; ++i) arr->insert<MoveOnly>(i, MoveOnly{ i });
	for (int i = 0; i < 256; ++i) {
		auto* v = getComponent<MoveOnly>(arr, i);
		ASSERT_NE(v, nullptr);
		EXPECT_TRUE(v->v != nullptr);
	}
	for (int i = 0; i < 256; i += 2) arr->eraseAsync(i, 1);
	arr->defragment();
	delete arr;
}

TEST(SectorsArray, ABA_ProblemStress) {
	static constexpr int N = 200, threads = 6;
	auto* arr = SA_T::create<Trivial>();
	std::vector<std::thread> ts;
	for (int t = 0; t < threads; ++t)
		ts.emplace_back([&, t] {
		for (int i = t * N; i < (t + 1) * N; ++i)
			arr->insert<Trivial>(i, Trivial{ i });
	});
	for (auto& th : ts) th.join();

	std::atomic<int> alive = 0;

	for (int i = 0; i < threads * N; ++i)
		if (sectorExists(arr, i))
			alive++;

	EXPECT_EQ(alive, threads * N);
	alive = 0;
	
	std::vector<std::thread> dels;
	for (int t = 0; t < threads; ++t)
		dels.emplace_back([&, t] {
		for (int i = t * N; i < (t + 1) * N; i += 2)
			arr->eraseAsync(i);
	});
	for (auto& th : dels) th.join();
	for (int i = 0; i < threads * N; ++i)
		if (sectorExists(arr, i))
			alive++;
	EXPECT_EQ(alive, threads * N / 2);
	delete arr;
}

TEST(SectorsArray, ReserveEraseInsertDeadlock) {
	auto* arr = SA_T::create<Trivial>();
	std::thread t1([&] { for (int i = 0; i < 10000; ++i) arr->insert<Trivial>(i, Trivial{ i }); });
	std::thread t2([&] { for (int i = 9999; i >= 0; --i) arr->eraseAsync(i); });
	t1.join(); t2.join();
	delete arr;
}

TEST(SectorsArray, AllApiBrutalMix) {
	auto* arr = SA_T::create<Trivial, NonTrivial>();
	for (int i = 0; i < 1000; ++i) arr->insert<Trivial>(i, Trivial{ i });
	for (int i = 0; i < 1000; i += 2) arr->insert<NonTrivial>(i, NonTrivial{ "i" });
	for (int i = 0; i < 500; ++i) arr->eraseAsync(i);
	arr->defragment();
	auto* arr2 = new SA_T(*arr); 
	delete arr;
	delete arr2;
}

TEST(SectorsArray_STRESS, MassiveConcurrentInsertEraseAndDefrag) {
	static constexpr int N = 1000;
	auto* arr = SectorsArray<true, ChunksAllocator<32>>::create<Trivial>();
	std::vector<std::thread> insert_threads, erase_threads;
	for (int t = 0; t < 4; ++t)
		insert_threads.emplace_back([&, t] {
		for (int i = t * N; i < (t + 1) * N; ++i)
			arr->insert<Trivial>(i, Trivial{ i });
	});
	for (auto& th : insert_threads) th.join();
	for (int t = 0; t < 4; ++t)
		erase_threads.emplace_back([&, t] {
		for (int i = t * N; i < (t + 1) * N; i += 2)
			arr->eraseAsync(i);
	});
	for (auto& th : erase_threads) th.join();
	arr->defragment();
	delete arr;
}

TEST(SectorsArray, MultiComponentParallelRumble) {
	auto* arr = SA_T::create<Trivial, MoveOnly>();
	std::thread t1([&] {
		for (int i = 0; i < 10000; ++i) arr->insert<Trivial>(i, Trivial{ i });
	});
	std::thread t2([&] {
		for (int i = 0; i < 10000; ++i) arr->insert<MoveOnly>(i, MoveOnly{ i });
	});
	t1.join(); t2.join();
	delete arr;
}



struct CompA { int x{ 0 }; };
struct CompB { float y{ 0.f }; };

TEST(SectorsArrayPins, CreateAndInsertFind) {
	auto* arr = SectorsArray<>::create<CompA, CompB>();
	arr->reserve(4);
	
	constexpr ecss::SectorId id = 3;
	auto* a = arr->emplace<CompA>(id);
	ASSERT_NE(a, nullptr);
	a->x = 42;

	
	EXPECT_TRUE(arr->containsSector(id));
	auto* data = arr->findSectorData(id);
	ASSERT_NE(data, nullptr);
	auto idx = arr->findLinearIdx(id);
	auto& isAlive = arr->getIsAliveRef(idx);
	auto* a2 = Sector::getMember<CompA>(data, arr->getLayoutData<CompA>(), isAlive);
	ASSERT_NE(a2, nullptr);
	EXPECT_EQ(a2->x, 42);

	delete arr;
}

TEST(SectorsArrayPins, PinPreventsImmediateErase) {
	auto* arr = SectorsArray<>::create<CompA, CompB>();
	arr->reserve(2);
	constexpr ecss::SectorId id = 1;
	arr->emplace<CompA>(id);

	
	auto pinned = arr->pinSector(id);
	ASSERT_TRUE(bool(pinned));
	EXPECT_TRUE(arr->containsSector(id));

	
	arr->eraseAsync(id);
	EXPECT_TRUE(arr->containsSector(id));

	
	pinned.release();
	arr->processPendingErases();

	
	EXPECT_FALSE(arr->containsSector(id));

	delete arr;
}

TEST(SectorsArrayPins, ImmediateEraseRightmostUnpinned) {
	auto* arr = SectorsArray<>::create<CompA, CompB>();
	arr->reserve(8);
	constexpr ecss::SectorId low = 2;
	constexpr ecss::SectorId high = 7;
	arr->emplace<CompA>(low);
	arr->emplace<CompA>(high);

	
	arr->eraseAsync(high);
	EXPECT_FALSE(arr->containsSector(high));
	EXPECT_TRUE(arr->containsSector(low));

	delete arr;
}

TEST(SectorsArrayPins, PendingThenEraseAfterUnpin) {
	auto* arr = SectorsArray<>::create<CompA, CompB>();
	arr->reserve(4);

	constexpr ecss::SectorId id = 0;
	arr->emplace<CompA>(id);

	{
		auto p = arr->pinSector(id);
		ASSERT_TRUE(bool(p));
		arr->eraseAsync(id);
		
		EXPECT_TRUE(arr->containsSector(id));
	} 

	
	arr->processPendingErases();
	EXPECT_FALSE(arr->containsSector(id));

	delete arr;
}

TEST(SectorsArrayPins, SidecarsAutoGrowOnAcquireAndReserve) {
	auto* arr = SectorsArray<>::create<CompA, CompB>();

	constexpr ecss::SectorId big = 123;
	arr->emplace<CompA>(big);
	EXPECT_TRUE(arr->containsSector(big));

	{
		auto p = arr->pinSector(big);
		ASSERT_TRUE(bool(p));
	}
	arr->processPendingErases(); 

	
	arr->reserve(512);
	constexpr ecss::SectorId bigger = 400;
	arr->emplace<CompA>(bigger);
	EXPECT_TRUE(arr->containsSector(bigger));

	delete arr;
}


TEST(SectorsArrayPins, EraseByContiguousIndexRespectsPins) {
	auto* arr = SectorsArray<>::create<CompA, CompB>();
	arr->reserve(4);
	
	constexpr ecss::SectorId id0 = 10;
	constexpr ecss::SectorId id1 = 11;
	arr->emplace<CompA>(id0);
	arr->emplace<CompA>(id1);

	
	auto p = arr->pinSector(id0);
	ASSERT_TRUE(bool(p));

	arr->eraseAsync(10, 2);
	
	EXPECT_TRUE(arr->containsSector(id0));
	 
	p.release();
	
	arr->processPendingErases();
	EXPECT_FALSE(arr->containsSector(id1));

	arr->processPendingErases(); 
	arr->eraseAsync(id0);
	arr->processPendingErases();
	EXPECT_FALSE(arr->containsSector(id0));

	delete arr;
}

// ==================== View Snapshot & RetireAllocator Tests ====================

TEST(SectorsArray_ViewSnapshot, IteratorSnapshotSurvivesReallocation) {
	// Test that iterator created before reallocation still works with its snapshot
	auto* arr = SA_T::create<Trivial>();
	
	// Insert initial data
	for (int i = 0; i < 10; ++i) {
		arr->insert<Trivial>(i, Trivial{i * 10});
	}
	
	// Create iterator (captures view snapshot)
	auto beginIt = arr->begin<false>();
	auto endIt = arr->end<false>();
	size_t initialSize = arr->size<false>();
	
	// Force reallocation by inserting many more elements
	for (int i = 100; i < 300; ++i) {
		arr->insert<Trivial>(i, Trivial{i});
	}
	
	// Original iterator should still work with its snapshot
	// (it sees the old size, old data pointers)
	size_t count = 0;
	for (auto it = beginIt; it != endIt && count < initialSize; ++it, ++count) {
		auto slot = *it;
		EXPECT_NE(slot.data, nullptr);
	}
	EXPECT_EQ(count, initialSize);
	
	delete arr;
}

TEST(SectorsArray_ViewSnapshot, MultipleIteratorsIndependent) {
	auto* arr = SA_T::create<Trivial>();
	
	for (int i = 0; i < 50; ++i) {
		arr->insert<Trivial>(i, Trivial{i});
	}
	
	// Create first iterator
	auto it1Begin = arr->begin<false>();
	size_t size1 = arr->size<false>();
	
	// Modify and create second iterator
	for (int i = 100; i < 150; ++i) {
		arr->insert<Trivial>(i, Trivial{i});
	}
	
	auto it2Begin = arr->begin<false>();
	size_t size2 = arr->size<false>();
	
	// Both iterators should be valid with their respective snapshots
	EXPECT_EQ(size1, 50u);
	EXPECT_EQ(size2, 100u);
	
	// Count elements in each iterator's view
	size_t count1 = 0, count2 = 0;
	for (auto it = it1Begin; count1 < size1; ++it, ++count1) {
		EXPECT_NE((*it).data, nullptr);
	}
	for (auto it = it2Begin; count2 < size2; ++it, ++count2) {
		EXPECT_NE((*it).data, nullptr);
	}
	
	EXPECT_EQ(count1, size1);
	EXPECT_EQ(count2, size2);
	
	delete arr;
}

TEST(SectorsArray_ViewSnapshot, IteratorAfterDefragment) {
	auto* arr = SA_T::create<Trivial>();
	
	// Insert and delete to create fragmentation
	for (int i = 0; i < 100; ++i) {
		arr->insert<Trivial>(i, Trivial{i});
	}
	size_t sizeBefore = arr->size<false>();
	
	for (int i = 0; i < 100; i += 2) {
		arr->erase<false>(arr->findLinearIdx<false>(i), 1, false);
	}
	
	// Defragment compacts the array
	arr->defragment<false>();
	
	// New iterator after defragment sees compacted state
	size_t sizeAfter = arr->size<false>();
	EXPECT_LT(sizeAfter, sizeBefore);
	
	// Iterate with new iterator - should work correctly
	size_t count = 0;
	for (auto it = arr->begin<false>(); it != arr->end<false>(); ++it) {
		auto slot = *it;
		EXPECT_NE(slot.data, nullptr);
		++count;
	}
	EXPECT_EQ(count, sizeAfter);
	
	delete arr;
}

TEST(SectorsArray_CopyMove, CopyWithRetiredMemory) {
	auto* arr = SA_T::create<Trivial>();
	
	// Do many insert/erase cycles to accumulate retired memory
	for (int cycle = 0; cycle < 5; ++cycle) {
		for (int i = 0; i < 100; ++i) {
			arr->insert<Trivial>(cycle * 1000 + i, Trivial{i});
		}
		for (int i = 0; i < 50; ++i) {
			arr->erase<false>(0, 1, true);
		}
	}
	
	// Copy the array
	auto* arr2 = new SA_T(*arr);
	
	// Both arrays should work correctly
	EXPECT_EQ(arr->size<false>(), arr2->size<false>());
	
	// Modify both independently
	arr->insert<Trivial>(9999, Trivial{9999});
	arr2->insert<Trivial>(8888, Trivial{8888});
	
	EXPECT_TRUE(arr->containsSector<false>(9999));
	EXPECT_FALSE(arr->containsSector<false>(8888));
	EXPECT_FALSE(arr2->containsSector<false>(9999));
	EXPECT_TRUE(arr2->containsSector<false>(8888));
	
	delete arr;
	delete arr2;
}

TEST(SectorsArray_CopyMove, MoveWithRetiredMemory) {
	auto* arr = SA_T::create<Trivial>();
	
	// Accumulate retired memory
	for (int i = 0; i < 200; ++i) {
		arr->insert<Trivial>(i, Trivial{i});
	}
	for (int i = 0; i < 100; ++i) {
		arr->erase<false>(0, 1, true);
	}
	
	size_t sizeBefore = arr->size<false>();
	
	// Move construct
	auto* arr2 = new SA_T(std::move(*arr));
	
	EXPECT_EQ(arr2->size<false>(), sizeBefore);
	
	// Original should be empty
	EXPECT_EQ(arr->size<false>(), 0u);
	
	// Moved-to array should work
	arr2->insert<Trivial>(9999, Trivial{9999});
	EXPECT_TRUE(arr2->containsSector<false>(9999));
	
	delete arr;
	delete arr2;
}

TEST(SectorsArray_CopyMove, SelfAssignment) {
	auto* arr = SA_T::create<Trivial>();
	
	for (int i = 0; i < 50; ++i) {
		arr->insert<Trivial>(i, Trivial{i});
	}
	
	size_t sizeBefore = arr->size<false>();
	
	// Self copy assignment - should be no-op
	*arr = *arr;
	
	EXPECT_EQ(arr->size<false>(), sizeBefore);
	EXPECT_TRUE(arr->containsSector<false>(25));
	
	// Self move assignment - should be no-op
	*arr = std::move(*arr);
	
	EXPECT_EQ(arr->size<false>(), sizeBefore);
	EXPECT_TRUE(arr->containsSector<false>(25));
	
	delete arr;
}

TEST(SectorsArray_CopyMove, CopyEmptyArray) {
	auto* arr = SA_T::create<Trivial>();
	
	// Copy empty array
	auto* arr2 = new SA_T(*arr);
	
	EXPECT_EQ(arr->size<false>(), 0u);
	EXPECT_EQ(arr2->size<false>(), 0u);
	
	// Both should be usable
	arr->insert<Trivial>(1, Trivial{1});
	arr2->insert<Trivial>(2, Trivial{2});
	
	EXPECT_EQ(arr->size<false>(), 1u);
	EXPECT_EQ(arr2->size<false>(), 1u);
	
	delete arr;
	delete arr2;
}

TEST(SectorsArray_CopyMove, MoveEmptyArray) {
	auto* arr = SA_T::create<Trivial>();
	
	// Move empty array
	auto* arr2 = new SA_T(std::move(*arr));
	
	EXPECT_EQ(arr->size<false>(), 0u);
	EXPECT_EQ(arr2->size<false>(), 0u);
	
	// Moved-to should be usable
	arr2->insert<Trivial>(1, Trivial{1});
	EXPECT_EQ(arr2->size<false>(), 1u);
	
	delete arr;
	delete arr2;
}

TEST(SectorsArray_Concurrent, CopyWhileModifying) {
	auto* arr = SA_T::create<Trivial>();
	std::atomic<bool> running{true};
	std::atomic<int> copies{0};
	
	// Writer thread
	std::thread writer([&] {
		for (int i = 0; i < 500 && running; ++i) {
			arr->insert<Trivial>(i, Trivial{i});
			if (i % 10 == 0) {
				arr->defragment();
			}
		}
	});
	
	// Copier thread
	std::thread copier([&] {
		while (running && copies < 10) {
			auto* copy = new SA_T(*arr);
			EXPECT_GE(copy->size<false>(), 0u);
			delete copy;
			++copies;
		}
	});
	
	writer.join();
	running = false;
	copier.join();
	
	EXPECT_GT(copies.load(), 0);
	delete arr;
}

TEST(SectorsArray_EmptyView, LoadViewOnEmpty) {
	auto* arr = SA_T::create<Trivial>();
	
	// Iterate empty array - should not crash
	size_t count = 0;
	for (auto it = arr->begin<false>(); it != arr->end<false>(); ++it) {
		++count;
	}
	EXPECT_EQ(count, 0u);
	
	// beginAlive on empty - should not crash
	auto it = arr->beginAlive<Trivial, false>();
	EXPECT_EQ(it, arr->endAlive<false>());
	
	delete arr;
}

TEST(SectorsArray_EmptyView, ClearAndIterate) {
	auto* arr = SA_T::create<Trivial>();
	
	// Add and clear multiple times
	for (int cycle = 0; cycle < 3; ++cycle) {
		for (int i = 0; i < 20; ++i) {
			arr->insert<Trivial>(i, Trivial{i});
		}
		EXPECT_EQ(arr->size<false>(), 20u);
		
		arr->clear<false>();
		EXPECT_EQ(arr->size<false>(), 0u);
		
		// Iterate after clear
		size_t count = 0;
		for (auto it = arr->begin<false>(); it != arr->end<false>(); ++it) {
			++count;
		}
		EXPECT_EQ(count, 0u);
	}
	
	delete arr;
}

TEST(SectorsArray_ViewSnapshot, ViewConsistencyThroughOperations) {
	auto* arr = SA_T::create<Trivial>();
	
	// Sequence of operations that might cause view inconsistency
	for (int i = 0; i < 100; ++i) {
		arr->insert<Trivial>(i, Trivial{i});
	}
	
	// Erase some elements
	for (int i = 0; i < 50; i += 2) {
		auto idx = arr->findLinearIdx<false>(i);
		if (idx != INVALID_IDX) {
			arr->erase<false>(idx, 1, false);
		}
	}
	
	// Defragment
	arr->defragment<false>();
	
	// Insert more
	for (int i = 200; i < 250; ++i) {
		arr->insert<Trivial>(i, Trivial{i});
	}
	
	// View should be consistent - all iterations should see valid data
	size_t count = 0;
	for (auto it = arr->begin<false>(); it != arr->end<false>(); ++it) {
		auto slot = *it;
		EXPECT_NE(slot.data, nullptr);
		++count;
	}
	EXPECT_EQ(count, arr->size<false>());
	
	// Alive iterator should also work
	size_t aliveCount = 0;
	for (auto it = arr->beginAlive<Trivial, false>(); it != arr->endAlive<false>(); ++it) {
		++aliveCount;
	}
	EXPECT_GT(aliveCount, 0u);
	
	delete arr;
}

TEST(SectorsArray_NonTrivial, CopyMoveWithStrings) {
	auto* arr = SA_T::create<NonTrivial>();
	
	// Insert strings
	for (int i = 0; i < 50; ++i) {
		arr->insert<NonTrivial>(i, NonTrivial{"string_" + std::to_string(i)});
	}
	
	// Copy
	auto* arr2 = new SA_T(*arr);
	
	// Verify copy is independent
	auto* orig = getComponent<NonTrivial>(arr, 25);
	auto* copied = getComponent<NonTrivial>(arr2, 25);
	
	ASSERT_NE(orig, nullptr);
	ASSERT_NE(copied, nullptr);
	EXPECT_EQ(orig->s, copied->s);
	EXPECT_NE(&orig->s, &copied->s); // Different memory
	
	// Move
	auto* arr3 = new SA_T(std::move(*arr));
	
	EXPECT_EQ(arr->size<false>(), 0u);
	EXPECT_EQ(arr3->size<false>(), 50u);
	
	delete arr;
	delete arr2;
	delete arr3;
}

// ============== Additional Coverage Tests ==============

// Test: Non-trivial types during shiftRight (insert in middle)
TEST(SectorsArray_NonTrivial, ShiftRightPreservesStrings) {
	auto* arr = SA_T::create<NonTrivial>();
	
	// Insert in order 0, 100, 200, 300
	arr->insert<NonTrivial>(0, NonTrivial{"first"});
	arr->insert<NonTrivial>(100, NonTrivial{"second"});
	arr->insert<NonTrivial>(200, NonTrivial{"third"});
	arr->insert<NonTrivial>(300, NonTrivial{"fourth"});
	
	// Now insert in middle - triggers shiftRight
	arr->insert<NonTrivial>(50, NonTrivial{"inserted_middle"});
	arr->insert<NonTrivial>(150, NonTrivial{"inserted_middle2"});
	
	// Verify all strings are intact
	EXPECT_EQ(getComponent<NonTrivial>(arr, 0)->s, "first");
	EXPECT_EQ(getComponent<NonTrivial>(arr, 50)->s, "inserted_middle");
	EXPECT_EQ(getComponent<NonTrivial>(arr, 100)->s, "second");
	EXPECT_EQ(getComponent<NonTrivial>(arr, 150)->s, "inserted_middle2");
	EXPECT_EQ(getComponent<NonTrivial>(arr, 200)->s, "third");
	EXPECT_EQ(getComponent<NonTrivial>(arr, 300)->s, "fourth");
	
	delete arr;
}

// Test: Non-trivial defragment with gaps
TEST(SectorsArray_NonTrivial, DefragmentPreservesStrings) {
	auto* arr = SA_T::create<NonTrivial>();
	
	// Insert 100 strings
	for (int i = 0; i < 100; ++i) {
		arr->insert<NonTrivial>(i, NonTrivial{"str_" + std::to_string(i)});
	}
	
	// Erase every other (creates gaps)
	for (int i = 0; i < 100; i += 2) {
		arr->eraseAsync(i);
	}
	
	// Defragment
	arr->defragment();
	
	// Verify remaining strings are correct (odd numbers)
	EXPECT_EQ(arr->size<false>(), 50u);
	
	for (int i = 1; i < 100; i += 2) {
		auto* nt = getComponent<NonTrivial>(arr, i);
		ASSERT_NE(nt, nullptr);
		EXPECT_EQ(nt->s, "str_" + std::to_string(i));
	}
	
	delete arr;
}

// Test: Mixed trivial + non-trivial in same sector
TEST(SectorsArray_Mixed, TrivialAndNonTrivialTogether) {
	auto* arr = SA_T::create<Trivial, NonTrivial>();
	
	// Add both components to same entities
	for (int i = 0; i < 50; ++i) {
		arr->insert<Trivial>(i, Trivial{i * 10});
		arr->insert<NonTrivial>(i, NonTrivial{"mixed_" + std::to_string(i)});
	}
	
	// Erase some (i % 3 == 0)
	for (int i = 0; i < 50; i += 3) {
		arr->eraseAsync(i);
	}
	
	// Defragment (tests mixed type handling)
	arr->defragment();
	
	// Copy (tests mixed type copy)
	auto* arr2 = new SA_T(*arr);
	
	// Verify both arrays have correct data
	for (int i = 0; i < 50; ++i) {
		if (i % 3 == 0) continue; // These were erased
		
		auto* t = getComponent<Trivial>(arr2, i);
		auto* nt = getComponent<NonTrivial>(arr2, i);
		ASSERT_NE(t, nullptr);
		ASSERT_NE(nt, nullptr);
		
		EXPECT_EQ(t->a, i * 10);
		EXPECT_EQ(nt->s, "mixed_" + std::to_string(i));
	}
	
	delete arr;
	delete arr2;
}

// Test: Concurrent insert with non-trivial triggers shifts
TEST(SectorsArray_NonTrivial, ConcurrentInsertWithShifts) {
	auto* arr = SA_T::create<NonTrivial>();
	constexpr int N = 200;
	std::vector<std::thread> threads;
	
	// 4 threads inserting interleaved IDs (causes many shifts)
	for (int t = 0; t < 4; ++t) {
		threads.emplace_back([&, t] {
			for (int i = 0; i < N; ++i) {
				// Interleaved IDs: 0,4,8,12... / 1,5,9,13... / etc
				int id = i * 4 + t;
				arr->insert<NonTrivial>(id, NonTrivial{"thread" + std::to_string(t) + "_" + std::to_string(i)});
			}
		});
	}
	
	for (auto& th : threads) th.join();
	
	// Verify all entries are valid (no corruption)
	EXPECT_EQ(arr->size<false>(), static_cast<size_t>(N * 4));
	
	// Check a sample of entries
	for (int id = 0; id < N * 4; ++id) {
		auto* nt = getComponent<NonTrivial>(arr, id);
		ASSERT_NE(nt, nullptr) << "Missing entry for id " << id;
		EXPECT_FALSE(nt->s.empty());
		EXPECT_EQ(nt->s.find("thread"), 0u); // Starts with "thread"
	}
	
	delete arr;
}

// Test: Large string (heap allocated, not SSO)
TEST(SectorsArray_NonTrivial, LargeStringsNoSSO) {
	auto* arr = SA_T::create<NonTrivial>();
	
	// Create strings larger than SSO buffer (typically 15-23 chars)
	std::string longStr(100, 'x');
	
	for (int i = 0; i < 50; ++i) {
		arr->insert<NonTrivial>(i * 2, NonTrivial{longStr + std::to_string(i)});
	}
	
	// Insert in middle (shift)
	arr->insert<NonTrivial>(25, NonTrivial{longStr + "_middle"});
	
	// Erase first half
	for (int i = 0; i < 25; ++i) {
		arr->eraseAsync(i * 2);
	}
	arr->defragment();
	
	// Copy
	auto* arr2 = new SA_T(*arr);
	
	// Verify remaining entries (ids 25 and 50+ even numbers)
	auto* nt25 = getComponent<NonTrivial>(arr2, 25);
	ASSERT_NE(nt25, nullptr);
	EXPECT_GE(nt25->s.size(), 100u);
	EXPECT_EQ(nt25->s, longStr + "_middle");
	
	for (int i = 25; i < 50; ++i) {
		auto* nt = getComponent<NonTrivial>(arr2, i * 2);
		ASSERT_NE(nt, nullptr);
		EXPECT_GE(nt->s.size(), 100u);
	}
	
	delete arr;
	delete arr2;
}

// Test: Move-only type with defragment
TEST(SectorsArray_MoveOnly, DefragmentMoveOnlyType) {
	auto* arr = SA_T::create<MoveOnly>();
	
	for (int i = 0; i < 100; ++i) {
		arr->insert<MoveOnly>(i, MoveOnly{i * 100});
	}
	
	// Erase half (even numbers)
	for (int i = 0; i < 100; i += 2) {
		arr->eraseAsync(i);
	}
	
	// Defragment (must use move, not copy)
	arr->defragment();
	
	EXPECT_EQ(arr->size<false>(), 50u);
	
	// Verify values (odd numbers remain)
	for (int i = 1; i < 100; i += 2) {
		auto* mo = getComponent<MoveOnly>(arr, i);
		ASSERT_NE(mo, nullptr);
		EXPECT_EQ(*mo->v, i * 100);
	}
	
	delete arr;
}

// Test: Empty array operations don't crash
TEST(SectorsArray_EdgeCases, EmptyArrayOperations) {
	auto* arr = SA_T::create<Trivial, NonTrivial>();
	
	// Operations on empty array
	arr->defragment();
	arr->shrinkToFit();
	arr->clear();
	
	// Copy empty
	auto* arr2 = new SA_T(*arr);
	EXPECT_EQ(arr2->size<false>(), 0u);
	
	// Move empty
	auto* arr3 = new SA_T(std::move(*arr));
	EXPECT_EQ(arr3->size<false>(), 0u);
	
	delete arr;
	delete arr2;
	delete arr3;
}

// Test: Insert at exact capacity boundary
TEST(SectorsArray_EdgeCases, InsertAtCapacityBoundary) {
	auto* arr = SA_T::create<NonTrivial>();
	
	// Fill exactly to chunk capacity (32)
	for (int i = 0; i < 32; ++i) {
		arr->insert<NonTrivial>(i * 10, NonTrivial{"cap_" + std::to_string(i)});
	}
	
	// Insert one more (triggers new chunk allocation)
	arr->insert<NonTrivial>(5, NonTrivial{"boundary_insert"});
	
	EXPECT_EQ(arr->size<false>(), 33u);
	EXPECT_EQ(getComponent<NonTrivial>(arr, 5)->s, "boundary_insert");
	
	// All others should be intact
	for (int i = 0; i < 32; ++i) {
		auto* nt = getComponent<NonTrivial>(arr, i * 10);
		ASSERT_NE(nt, nullptr);
		EXPECT_EQ(nt->s, "cap_" + std::to_string(i));
	}
	
	delete arr;
}

// Test: Rapid insert/erase cycles with non-trivial
TEST(SectorsArray_NonTrivial, RapidInsertEraseCycles) {
	auto* arr = SA_T::create<NonTrivial>();
	
	for (int cycle = 0; cycle < 10; ++cycle) {
		// Insert batch
		for (int i = 0; i < 100; ++i) {
			arr->insert<NonTrivial>(i, NonTrivial{"cycle" + std::to_string(cycle) + "_" + std::to_string(i)});
		}
		
		// Erase all
		for (int i = 0; i < 100; ++i) {
			arr->eraseAsync(i);
		}
		
		arr->defragment();
		EXPECT_EQ(arr->size<false>(), 0u);
	}
	
	delete arr;
}

// Test: Copy during reads doesn't corrupt data
TEST(SectorsArray_Concurrent, CopyDuringReads) {
	auto* arr = SA_T::create<NonTrivial>();
	
	for (int i = 0; i < 100; ++i) {
		arr->insert<NonTrivial>(i, NonTrivial{"item_" + std::to_string(i)});
	}
	
	std::atomic<int> copyCount{0};
	std::vector<std::thread> threads;
	
	// Multiple threads copying simultaneously
	for (int t = 0; t < 4; ++t) {
		threads.emplace_back([&] {
			for (int i = 0; i < 5; ++i) {
				auto* copy = new SA_T(*arr);
				EXPECT_EQ(copy->size<false>(), 100u);
				
				// Verify copy has valid data
				for (int j = 0; j < 100; ++j) {
					auto* nt = getComponent<NonTrivial>(copy, j);
					ASSERT_NE(nt, nullptr);
					EXPECT_EQ(nt->s, "item_" + std::to_string(j));
				}
				
				delete copy;
				++copyCount;
			}
		});
	}
	
	for (auto& th : threads) th.join();
	
	EXPECT_EQ(copyCount.load(), 20);
	
	// Original should be unchanged
	for (int i = 0; i < 100; ++i) {
		auto* nt = getComponent<NonTrivial>(arr, i);
		ASSERT_NE(nt, nullptr);
		EXPECT_EQ(nt->s, "item_" + std::to_string(i));
	}
	
	delete arr;
}

// Test: ChunksAllocator shrink doesn't free valid data
TEST(SectorsArray_Memory, ShrinkToFitPreservesData) {
	auto* arr = SA_T::create<NonTrivial>();
	
	// Insert many
	for (int i = 0; i < 1000; ++i) {
		arr->insert<NonTrivial>(i, NonTrivial{"data_" + std::to_string(i)});
	}
	
	// Erase most
	for (int i = 100; i < 1000; ++i) {
		arr->eraseAsync(i);
	}
	
	arr->defragment();
	arr->shrinkToFit();
	
	// Verify remaining 100 are intact
	EXPECT_EQ(arr->size<false>(), 100u);
	
	for (int i = 0; i < 100; ++i) {
		auto* nt = getComponent<NonTrivial>(arr, i);
		ASSERT_NE(nt, nullptr);
		EXPECT_EQ(nt->s, "data_" + std::to_string(i));
	}
	
	delete arr;
}

// Test: Multiple component types, some trivial some not
TEST(SectorsArray_Mixed, ThreeComponentTypes) {
	struct StringComp { std::string s; };
	struct IntComp { int x; };
	struct FloatComp { float f; };
	
	auto* arr = SA_T::create<StringComp, IntComp, FloatComp>();
	
	for (int i = 0; i < 50; ++i) {
		arr->insert<StringComp>(i, StringComp{"str_" + std::to_string(i)});
		arr->insert<IntComp>(i, IntComp{i * 10});
		arr->insert<FloatComp>(i, FloatComp{i * 0.5f});
	}
	
	// Partial erase
	for (int i = 10; i < 40; ++i) {
		arr->eraseAsync(i);
	}
	
	arr->defragment();
	
	// Copy
	auto* arr2 = new SA_T(*arr);
	
	EXPECT_EQ(arr2->size<false>(), 20u);
	
	delete arr;
	delete arr2;
}

}
