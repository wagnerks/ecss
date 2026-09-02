// Black-box, per-method coverage of ecss::IdSet<Type, ThreadSafe>, driven by its own doc
// comments (IdSet.h) rather than by how the bitmap/CAS machinery happens to implement it.
// Registry uses this internally (mEntities) for entity id allocation -- it has no test file
// of its own in the public-API sense, but the class is complex enough (two specializations,
// one lock-free) to deserve a standalone spec covering both against the same contract.
//
// Type is EntityId (uint32_t) throughout, matching the only instantiation the library ships
// and the type its own docs assume ("id space exhausted" is checked against ecss::INVALID_ID,
// a 32-bit sentinel). Nothing here exercises a Type whose numeric_limits::max() collides with
// a real id space small enough to reach it -- IdSet was never documented for that.

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

#include <ecss/IdSet.h>

namespace IdSetTests {
	using namespace ecss;
	using Id = EntityId;
	using STSet = IdSet<Id, false>;
	using TSSet = IdSet<Id, true>;

	// ===================================================================== single-threaded: take()

	TEST(IdSet_SingleThreaded_Take, FirstCallReturnsZero) {
		STSet s;
		EXPECT_EQ(s.take(), 0u);
	}

	TEST(IdSet_SingleThreaded_Take, SuccessiveCallsAreAscendingFromZero) {
		STSet s;
		for (Id i = 0; i < 200; ++i) {
			ASSERT_EQ(s.take(), i);
		}
	}

	TEST(IdSet_SingleThreaded_Take, ReclaimsTheLowestErasedId) {
		STSet s;
		for (int i = 0; i < 10; ++i) { s.take(); }
		s.erase(3);
		EXPECT_EQ(s.take(), 3u);
	}

	TEST(IdSet_SingleThreaded_Take, SkipsIdsMarkedByInsert) {
		STSet s;
		s.insert(0);
		s.insert(1);
		EXPECT_EQ(s.take(), 2u);
	}

	TEST(IdSet_SingleThreaded_Take, EveryTakenIdIsMarkedContained) {
		STSet s;
		std::vector<Id> taken;
		for (int i = 0; i < 300; ++i) { taken.push_back(s.take()); }
		for (auto id : taken) { EXPECT_TRUE(s.contains(id)); }
	}

	TEST(IdSet_SingleThreaded_Take, CrossesAWordBoundaryCorrectly) {
		// 64 ids per word; 130 crosses it twice.
		STSet s;
		for (Id i = 0; i < 130; ++i) {
			ASSERT_EQ(s.take(), i);
		}
	}

	// ===================================================================== single-threaded: take(count, out)

	TEST(IdSet_SingleThreaded_TakeBatch, AppendsWithoutClearingExistingContents) {
		STSet s;
		std::vector<Id> out{ 999 };
		s.take(5, out);
		ASSERT_EQ(out.size(), 6u);
		EXPECT_EQ(out[0], 999u);
	}

	TEST(IdSet_SingleThreaded_TakeBatch, ZeroCountIsANoOp) {
		STSet s;
		std::vector<Id> out;
		s.take(0, out);
		EXPECT_TRUE(out.empty());
		EXPECT_TRUE(s.empty());
	}

	TEST(IdSet_SingleThreaded_TakeBatch, ResultsAreAscendingAndDisjointFromExisting) {
		STSet s;
		s.take(); s.take(); s.take();   // 0,1,2 already taken
		std::vector<Id> out;
		s.take(150, out);
		ASSERT_EQ(out.size(), 150u);
		EXPECT_TRUE(std::is_sorted(out.begin(), out.end()));
		for (auto id : out) { EXPECT_GE(id, 3u); }
	}

	TEST(IdSet_SingleThreaded_TakeBatch, EveryReturnedIdBecomesAllocated) {
		STSet s;
		std::vector<Id> out;
		s.take(200, out);
		EXPECT_EQ(s.size(), 200u);
		for (auto id : out) { EXPECT_TRUE(s.contains(id)); }
	}

	// ===================================================================== single-threaded: insert / erase

	TEST(IdSet_SingleThreaded_Insert, MarksAnArbitraryIdAllocated) {
		STSet s;
		s.insert(50);
		EXPECT_TRUE(s.contains(50));
		EXPECT_EQ(s.size(), 1u);
	}

	TEST(IdSet_SingleThreaded_Insert, RepeatingItIsANoOp) {
		STSet s;
		s.insert(7);
		s.insert(7);
		EXPECT_EQ(s.size(), 1u);
	}

	TEST(IdSet_SingleThreaded_Erase, RemovesAnAllocatedId) {
		STSet s;
		auto id = s.take();
		s.erase(id);
		EXPECT_FALSE(s.contains(id));
		EXPECT_EQ(s.size(), 0u);
	}

	TEST(IdSet_SingleThreaded_Erase, OnAnIdNeverAllocatedIsHarmless) {
		STSet s;
		EXPECT_NO_THROW(s.erase(12345));
		EXPECT_EQ(s.size(), 0u);
	}

	TEST(IdSet_SingleThreaded_Erase, RangeRemovesExactlyTheGivenIds) {
		STSet s;
		std::vector<Id> taken;
		for (int i = 0; i < 20; ++i) { taken.push_back(s.take()); }
		std::vector<Id> doomed{ taken[2], taken[5], taken[9] };
		s.erase(doomed.data(), doomed.data() + doomed.size());
		for (auto id : doomed) { EXPECT_FALSE(s.contains(id)); }
		EXPECT_EQ(s.size(), 17u);
	}

	TEST(IdSet_SingleThreaded_Erase, RangeAcrossMultipleWordsIsHarmlessAndCorrect) {
		STSet s;
		std::vector<Id> out;
		s.take(140, out);   // spans 3 words
		s.erase(out.data(), out.data() + out.size());
		EXPECT_TRUE(s.empty());
	}

	TEST(IdSet_SingleThreaded_Erase, EmptyRangeIsANoOp) {
		STSet s;
		s.take();
		std::vector<Id> none;
		EXPECT_NO_THROW(s.erase(none.data(), none.data()));
		EXPECT_EQ(s.size(), 1u);
	}

	// ===================================================================== single-threaded: contains

	TEST(IdSet_SingleThreaded_Contains, FalseForAnIdNeverTouched) {
		STSet s;
		EXPECT_FALSE(s.contains(42));
	}

	// ===================================================================== single-threaded: getAll

	TEST(IdSet_SingleThreaded_GetAll, EmptySetReturnsEmptyVector) {
		STSet s;
		EXPECT_TRUE(s.getAll().empty());
	}

	TEST(IdSet_SingleThreaded_GetAll, ReturnsExactlyTheAllocatedIdsAscending) {
		STSet s;
		s.take(); s.take(); s.take();
		s.erase(1);
		s.insert(100);
		const std::vector<Id> expected{ 0, 2, 100 };
		EXPECT_EQ(s.getAll(), expected);
	}

	// ===================================================================== single-threaded: clear

	TEST(IdSet_SingleThreaded_Clear, EmptiesTheSet) {
		STSet s;
		for (int i = 0; i < 10; ++i) { s.take(); }
		s.clear();
		EXPECT_TRUE(s.empty());
		EXPECT_EQ(s.size(), 0u);
	}

	TEST(IdSet_SingleThreaded_Clear, NextTakeStartsFromZeroAgain) {
		STSet s;
		for (int i = 0; i < 10; ++i) { s.take(); }
		s.clear();
		EXPECT_EQ(s.take(), 0u);
	}

	TEST(IdSet_SingleThreaded_Clear, OnAnAlreadyEmptySetIsANoOp) {
		STSet s;
		EXPECT_NO_THROW(s.clear());
		EXPECT_TRUE(s.empty());
	}

	// ===================================================================== single-threaded: reserve

	TEST(IdSet_SingleThreaded_Reserve, DoesNotAllocateAnyId) {
		STSet s;
		s.reserve(1000);
		EXPECT_TRUE(s.empty());
		EXPECT_FALSE(s.contains(500));
	}

	TEST(IdSet_SingleThreaded_Reserve, TakeStillStartsFromZeroAfterReserving) {
		STSet s;
		s.reserve(1000);
		EXPECT_EQ(s.take(), 0u);
	}

	// ===================================================================== single-threaded: size / empty / byteSize

	TEST(IdSet_SingleThreaded_SizeAndEmpty, EmptyInitially) {
		STSet s;
		EXPECT_TRUE(s.empty());
		EXPECT_EQ(s.size(), 0u);
	}

	TEST(IdSet_SingleThreaded_SizeAndEmpty, TracksTakesAndErases) {
		STSet s;
		const auto a = s.take(); const auto b = s.take(); const auto c = s.take();
		EXPECT_EQ(s.size(), 3u);
		s.erase(b);
		EXPECT_EQ(s.size(), 2u);
		EXPECT_FALSE(s.empty());
		EXPECT_TRUE(s.contains(a));
		EXPECT_TRUE(s.contains(c));
	}

	TEST(IdSet_SingleThreaded_ByteSize, ZeroForAFreshSet) {
		STSet s;
		EXPECT_EQ(s.byteSize(), 0u);
	}

	TEST(IdSet_SingleThreaded_ByteSize, GrowsAfterAnyAllocation) {
		STSet s;
		s.take();
		EXPECT_GT(s.byteSize(), 0u);
	}

	TEST(IdSet_SingleThreaded_ByteSize, DoesNotShrinkAfterClear) {
		// clear() is documented to keep capacity ("the next take() starts from 0 again"),
		// and there is no public method that ever frees a word.
		STSet s;
		for (int i = 0; i < 200; ++i) { s.take(); }
		const auto before = s.byteSize();
		s.clear();
		EXPECT_EQ(s.byteSize(), before);
	}

	// ===================================================================== thread-safe: same contract, single-thread use

	TEST(IdSet_ThreadSafe_Take, FirstCallReturnsZero) {
		TSSet s;
		EXPECT_EQ(s.take(), 0u);
	}

	TEST(IdSet_ThreadSafe_Take, SuccessiveCallsAreAscendingFromZero_NoContention) {
		// "while nothing has ever lost a CAS on this set... strict lowest-free allocation":
		// guaranteed under exactly the condition this test creates -- one thread, no contention.
		TSSet s;
		for (Id i = 0; i < 200; ++i) {
			ASSERT_EQ(s.take(), i);
		}
	}

	TEST(IdSet_ThreadSafe_Take, ReclaimsTheLowestErasedId) {
		TSSet s;
		for (int i = 0; i < 10; ++i) { s.take(); }
		s.erase(3);
		EXPECT_EQ(s.take(), 3u);
	}

	TEST(IdSet_ThreadSafe_Take, SkipsIdsMarkedByInsert) {
		TSSet s;
		s.insert(0);
		s.insert(1);
		EXPECT_EQ(s.take(), 2u);
	}

	TEST(IdSet_ThreadSafe_Take, CrossesABlockBoundaryCorrectly) {
		// kWordsPerBlock * kWordBits = 65536 ids per block; cross it to exercise grow().
		TSSet s;
		for (Id i = 0; i < 65600; ++i) {
			ASSERT_EQ(s.take(), i) << "diverged at id " << i;
		}
	}

	TEST(IdSet_ThreadSafe_TakeBatch, AppendsWithoutClearingExistingContents) {
		TSSet s;
		std::vector<Id> out{ 999 };
		s.take(5, out);
		ASSERT_EQ(out.size(), 6u);
		EXPECT_EQ(out[0], 999u);
	}

	TEST(IdSet_ThreadSafe_TakeBatch, ZeroCountIsANoOp) {
		TSSet s;
		std::vector<Id> out;
		s.take(0, out);
		EXPECT_TRUE(out.empty());
	}

	TEST(IdSet_ThreadSafe_TakeBatch, ResultsAreAscendingAndDisjointFromExisting_NoContention) {
		TSSet s;
		s.take(); s.take(); s.take();
		std::vector<Id> out;
		s.take(150, out);
		ASSERT_EQ(out.size(), 150u);
		EXPECT_TRUE(std::is_sorted(out.begin(), out.end()));
		for (auto id : out) { EXPECT_GE(id, 3u); }
	}

	TEST(IdSet_ThreadSafe_InsertErase, InsertMarksAnArbitraryIdAllocated) {
		TSSet s;
		s.insert(50);
		EXPECT_TRUE(s.contains(50));
		EXPECT_EQ(s.size(), 1u);
	}

	TEST(IdSet_ThreadSafe_InsertErase, RepeatingInsertIsANoOp) {
		TSSet s;
		s.insert(7);
		s.insert(7);
		EXPECT_EQ(s.size(), 1u);
	}

	TEST(IdSet_ThreadSafe_InsertErase, EraseRemovesAnAllocatedId) {
		TSSet s;
		auto id = s.take();
		s.erase(id);
		EXPECT_FALSE(s.contains(id));
	}

	TEST(IdSet_ThreadSafe_InsertErase, EraseOnAnIdNeverAllocatedIsHarmless) {
		TSSet s;
		s.take();   // grow a table first
		EXPECT_NO_THROW(s.erase(12345));
	}

	TEST(IdSet_ThreadSafe_InsertErase, EraseOnAnUngrownSetIsHarmless) {
		// erase() before any table has ever been published: table is null.
		TSSet s;
		EXPECT_NO_THROW(s.erase(0));
		EXPECT_FALSE(s.contains(0));
	}

	TEST(IdSet_ThreadSafe_InsertErase, EraseRangeRemovesExactlyTheGivenIds) {
		TSSet s;
		std::vector<Id> taken;
		for (int i = 0; i < 20; ++i) { taken.push_back(s.take()); }
		std::vector<Id> doomed{ taken[2], taken[5], taken[9] };
		s.erase(doomed.data(), doomed.data() + doomed.size());
		for (auto id : doomed) { EXPECT_FALSE(s.contains(id)); }
		EXPECT_EQ(s.size(), 17u);
	}

	TEST(IdSet_ThreadSafe_InsertErase, EmptyRangeIsANoOp) {
		TSSet s;
		s.take();
		std::vector<Id> none;
		EXPECT_NO_THROW(s.erase(none.data(), none.data()));
		EXPECT_EQ(s.size(), 1u);
	}

	TEST(IdSet_ThreadSafe_Contains, FalseForAnIdNeverTouched) {
		TSSet s;
		EXPECT_FALSE(s.contains(42));
	}

	TEST(IdSet_ThreadSafe_Contains, FalseOnAFreshUngrownSet) {
		TSSet s;   // no table published yet
		EXPECT_FALSE(s.contains(999999));
	}

	TEST(IdSet_ThreadSafe_GetAll, EmptySetReturnsEmptyVector) {
		TSSet s;
		EXPECT_TRUE(s.getAll().empty());
	}

	TEST(IdSet_ThreadSafe_GetAll, ReturnsExactlyTheAllocatedIds) {
		TSSet s;
		s.take(); s.take(); s.take();
		s.erase(1);
		s.insert(100);
		auto all = s.getAll();
		std::sort(all.begin(), all.end());
		const std::vector<Id> expected{ 0, 2, 100 };
		EXPECT_EQ(all, expected);
	}

	TEST(IdSet_ThreadSafe_Clear, EmptiesTheSet) {
		TSSet s;
		for (int i = 0; i < 10; ++i) { s.take(); }
		s.clear();
		EXPECT_TRUE(s.empty());
	}

	TEST(IdSet_ThreadSafe_Clear, NextTakeStartsFromZeroAgain) {
		TSSet s;
		for (int i = 0; i < 10; ++i) { s.take(); }
		s.clear();
		EXPECT_EQ(s.take(), 0u);
	}

	TEST(IdSet_ThreadSafe_Clear, OnAnAlreadyEmptySetIsANoOp) {
		// No table has ever been published; clear() must not dereference a null one.
		TSSet s;
		EXPECT_NO_THROW(s.clear());
		EXPECT_TRUE(s.empty());
	}

	TEST(IdSet_ThreadSafe_Reserve, DoesNotAllocateAnyId) {
		TSSet s;
		s.reserve(1000);
		EXPECT_TRUE(s.empty());
		EXPECT_FALSE(s.contains(500));
	}

	TEST(IdSet_ThreadSafe_Reserve, TakeStillStartsFromZeroAfterReserving) {
		TSSet s;
		s.reserve(1000);
		EXPECT_EQ(s.take(), 0u);
	}

	TEST(IdSet_ThreadSafe_SizeAndEmpty, TracksTakesAndErases) {
		TSSet s;
		const auto a = s.take(); const auto b = s.take(); const auto c = s.take();
		EXPECT_EQ(s.size(), 3u);
		s.erase(b);
		EXPECT_EQ(s.size(), 2u);
		EXPECT_TRUE(s.contains(a));
		EXPECT_TRUE(s.contains(c));
	}

	TEST(IdSet_ThreadSafe_ByteSize, ZeroForAFreshSet) {
		TSSet s;
		EXPECT_EQ(s.byteSize(), 0u);
	}

	TEST(IdSet_ThreadSafe_ByteSize, GrowsAfterAnyAllocation) {
		TSSet s;
		s.take();
		EXPECT_GT(s.byteSize(), 0u);
	}

	TEST(IdSet_ThreadSafe_ByteSize, DoesNotShrinkAfterClear) {
		TSSet s;
		for (int i = 0; i < 200; ++i) { s.take(); }
		const auto before = s.byteSize();
		s.clear();
		EXPECT_EQ(s.byteSize(), before);
	}

	// ===================================================================== thread-safe: concurrency guarantees

	TEST(IdSet_ThreadSafe_Concurrency, ConcurrentTakesNeverDuplicateAnId) {
		TSSet s;
		constexpr int kThreads = 8;
		constexpr int kPerThread = 2000;
		std::vector<std::vector<Id>> results(kThreads);
		std::vector<std::thread> threads;
		for (int t = 0; t < kThreads; ++t) {
			threads.emplace_back([&, t] {
				results[t].reserve(kPerThread);
				for (int i = 0; i < kPerThread; ++i) { results[t].push_back(s.take()); }
			});
		}
		for (auto& th : threads) { th.join(); }

		std::vector<Id> all;
		for (auto& r : results) { all.insert(all.end(), r.begin(), r.end()); }
		ASSERT_EQ(all.size(), static_cast<size_t>(kThreads * kPerThread));
		std::sort(all.begin(), all.end());
		EXPECT_EQ(std::adjacent_find(all.begin(), all.end()), all.end()) << "two threads were handed the same id";
		EXPECT_EQ(s.size(), static_cast<size_t>(kThreads * kPerThread));
	}

	TEST(IdSet_ThreadSafe_Concurrency, ConcurrentBatchTakesNeverDuplicateAnId) {
		TSSet s;
		constexpr int kThreads = 8;
		constexpr size_t kPerThread = 3000;
		std::vector<std::vector<Id>> results(kThreads);
		std::vector<std::thread> threads;
		for (int t = 0; t < kThreads; ++t) {
			threads.emplace_back([&, t] { s.take(kPerThread, results[t]); });
		}
		for (auto& th : threads) { th.join(); }

		std::vector<Id> all;
		for (auto& r : results) {
			EXPECT_EQ(r.size(), kPerThread);
			all.insert(all.end(), r.begin(), r.end());
		}
		std::sort(all.begin(), all.end());
		EXPECT_EQ(std::adjacent_find(all.begin(), all.end()), all.end()) << "two threads were handed the same id";
	}

	TEST(IdSet_ThreadSafe_Concurrency, ConcurrentInsertOfDisjointIdsAllSurvive) {
		TSSet s;
		constexpr int kThreads = 8;
		constexpr int kPerThread = 500;
		std::vector<std::thread> threads;
		for (int t = 0; t < kThreads; ++t) {
			threads.emplace_back([&, t] {
				for (int i = 0; i < kPerThread; ++i) { s.insert(static_cast<Id>(t * kPerThread + i)); }
			});
		}
		for (auto& th : threads) { th.join(); }

		EXPECT_EQ(s.size(), static_cast<size_t>(kThreads * kPerThread));
		for (Id id = 0; id < static_cast<Id>(kThreads * kPerThread); ++id) { EXPECT_TRUE(s.contains(id)); }
	}

	TEST(IdSet_ThreadSafe_Concurrency, TakeAndEraseChurnLeavesAnEmptyConsistentSet) {
		// Each iteration takes then erases its own id before moving on, so the net effect
		// across every thread is an empty set -- this is a property of the test's own
		// pairing, not an extra claim about IdSet.
		TSSet s;
		constexpr int kThreads = 6;
		constexpr int kIters = 3000;
		std::vector<std::thread> threads;
		for (int t = 0; t < kThreads; ++t) {
			threads.emplace_back([&] {
				for (int i = 0; i < kIters; ++i) {
					const auto id = s.take();
					s.erase(id);
				}
			});
		}
		for (auto& th : threads) { th.join(); }

		EXPECT_TRUE(s.empty());
		EXPECT_EQ(s.size(), s.getAll().size()) << "size() and getAll() disagree after churn";
	}

} // namespace IdSetTests
