// Black-box, per-method coverage of ecss::Read<T>, ecss::Write<T>, and the guard object
// Registry::access() hands back (ecss::detail::AccessGuard). Assertions trace back to the
// doc comments on Access.h, Registry::access(), and docs/batching.md's "access<>" section
// -- not to how AccessGuard happens to implement the bookkeeping.
//
// RegressionTests.cpp already carries this class's coverage for specific past defects
// (KeepsAReaderAndAWriterOffOneType, OppositeClaimOrdersDoNotDeadlock, NestingOnOneThreadIsAllowed,
// WriteWhileHoldingReadFailsInEveryBuild). This file is the standalone spec: single claims,
// multiple claims, cross-thread exclusion per component type, and same-thread reentrancy,
// each as its own case.
//
// One thing the documentation does not say anything about: naming the SAME component type
// twice in one access<...>() call (e.g. access<Read<T>, Write<T>>()). Nothing in Access.h,
// Registry::access(), or docs/batching.md describes that case, so this file does not assert
// an outcome for it -- see the note near the bottom instead of a test.
//
// Two other documented boundaries are compile-time, not runtime: "name at least one component
// type" (0 claims) and "name at most 8" (a 9th) are both static_assert failures inside
// AccessGuard's constructor template, so neither can appear as a passing or failing TEST here.
// The 8-claim side of that boundary that IS runtime-reachable -- exactly 8 -- has its own case
// below.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <type_traits>

#include <ecss/Access.h>
#include <ecss/Registry.h>

namespace AccessTests {
	using namespace ecss;

	struct AccInt { int v{}; };
	struct AccOther { int v{}; };

	/// @brief Spin until @p done or the deadline passes. Returns false on timeout.
	template <class Pred>
	bool waitFor(Pred done, std::chrono::milliseconds limit = std::chrono::seconds(2)) {
		const auto deadline = std::chrono::steady_clock::now() + limit;
		while (std::chrono::steady_clock::now() < deadline) {
			if (done()) { return true; }
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		return done();
	}

	// ===================================================================== Read<T> / Write<T> tags

	TEST(Access_Tags, ReadNamesTheComponentAndDoesNotWrite) {
		static_assert(std::is_same_v<Read<AccInt>::Component, AccInt>);
		static_assert(Read<AccInt>::kWrites == false);
		SUCCEED();
	}

	TEST(Access_Tags, WriteNamesTheComponentAndWrites) {
		static_assert(std::is_same_v<Write<AccInt>::Component, AccInt>);
		static_assert(Write<AccInt>::kWrites == true);
		SUCCEED();
	}

	// ===================================================================== guard object traits

	TEST(Access_Guard, IsNeitherCopyableNorMovable) {
		// "Neither copyable nor movable, and the move is deleted on purpose": a guard's
		// bookkeeping lives in thread_local storage keyed by the thread that took it.
		using Guard = decltype(std::declval<Registry<true>&>().access<Read<AccInt>>());
		static_assert(!std::is_copy_constructible_v<Guard>);
		static_assert(!std::is_copy_assignable_v<Guard>);
		static_assert(!std::is_move_constructible_v<Guard>);
		static_assert(!std::is_move_assignable_v<Guard>);
		SUCCEED();
	}

	// ===================================================================== single claim

	TEST(Access_SingleClaim, ReadDoesNotThrow) {
		Registry<true> reg;
		EXPECT_NO_THROW({ auto g = reg.access<Read<AccInt>>(); });
	}

	TEST(Access_SingleClaim, WriteDoesNotThrow) {
		Registry<true> reg;
		EXPECT_NO_THROW({ auto g = reg.access<Write<AccInt>>(); });
	}

	TEST(Access_SingleClaim, WorksForATypeNeverAddedToAnyEntity) {
		// The claim locks a mutex keyed by type id; nothing about it requires the type to
		// already have an array or any live components.
		Registry<true> reg;
		EXPECT_NO_THROW({ auto g = reg.access<Write<AccOther>>(); });
	}

	// ===================================================================== multiple claims

	// A multi-claim access<A, B>() call has a top-level comma inside its template argument
	// list, which the preprocessor treats as a macro-argument separator -- so these do not
	// wrap the call in EXPECT_NO_THROW like the single-claim cases above. Gtest already fails
	// a test whose body lets an exception escape, which is exactly the coverage wanted here.
	TEST(Access_MultipleClaims, DistinctTypesInOneCallSucceed) {
		Registry<true> reg;
		auto g = reg.access<Read<AccInt>, Write<AccOther>>();
		SUCCEED();
	}

	TEST(Access_MultipleClaims, ReadAndWriteOnDifferentTypesTogetherSucceed) {
		Registry<true> reg;
		struct Third { int v{}; };
		auto g = reg.access<Write<AccInt>, Read<AccOther>, Read<Third>>();
		SUCCEED();
	}

	// "name at most 8 component types in one access()" is a static_assert on AccessGuard's
	// constructor, so naming a 9th is a compile error -- not something a runtime TEST can
	// exercise. The boundary that IS runtime-reachable is the documented maximum itself: 8.
	TEST(Access_MultipleClaims, EightDistinctTypesInOneCallSucceed) {
		struct T1 { int v{}; }; struct T2 { int v{}; }; struct T3 { int v{}; }; struct T4 { int v{}; };
		struct T5 { int v{}; }; struct T6 { int v{}; }; struct T7 { int v{}; }; struct T8 { int v{}; };
		Registry<true> reg;
		auto g = reg.access<Read<T1>, Write<T2>, Read<T3>, Write<T4>, Read<T5>, Write<T6>, Read<T7>, Write<T8>>();
		SUCCEED();
	}

	// ===================================================================== cross-thread exclusion

	TEST(Access_MutualExclusion, WriteBlocksAnotherThreadsWriteOnTheSameType) {
		Registry<true> reg;
		std::atomic<bool> waiterAcquired{ false };
		std::thread t;
		{
			auto held = reg.access<Write<AccInt>>();
			t = std::thread([&] {
				auto g = reg.access<Write<AccInt>>();
				waiterAcquired.store(true, std::memory_order_release);
			});
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			EXPECT_FALSE(waiterAcquired.load(std::memory_order_acquire))
				<< "a second Write claim on the same type must wait for the first";
		}   // held's destructor releases the lock here
		ASSERT_TRUE(waitFor([&] { return waiterAcquired.load(std::memory_order_acquire); }))
			<< "the waiting Write claim never acquired after the holder released";
		t.join();
	}

	TEST(Access_MutualExclusion, WriteBlocksAnotherThreadsReadOnTheSameType) {
		Registry<true> reg;
		std::atomic<bool> waiterAcquired{ false };
		std::thread t;
		{
			auto held = reg.access<Write<AccInt>>();
			t = std::thread([&] {
				auto g = reg.access<Read<AccInt>>();
				waiterAcquired.store(true, std::memory_order_release);
			});
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			EXPECT_FALSE(waiterAcquired.load(std::memory_order_acquire))
				<< "a Read claim on a type another thread holds Write on must wait";
		}
		ASSERT_TRUE(waitFor([&] { return waiterAcquired.load(std::memory_order_acquire); }));
		t.join();
	}

	TEST(Access_MutualExclusion, ReadBlocksAnotherThreadsWriteOnTheSameType) {
		Registry<true> reg;
		std::atomic<bool> waiterAcquired{ false };
		std::thread t;
		{
			auto held = reg.access<Read<AccInt>>();
			t = std::thread([&] {
				auto g = reg.access<Write<AccInt>>();
				waiterAcquired.store(true, std::memory_order_release);
			});
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			EXPECT_FALSE(waiterAcquired.load(std::memory_order_acquire))
				<< "a Write claim on a type another thread holds Read on must wait";
		}
		ASSERT_TRUE(waitFor([&] { return waiterAcquired.load(std::memory_order_acquire); }));
		t.join();
	}

	TEST(Access_MutualExclusion, TwoReadersOnTheSameTypeProceedConcurrently) {
		// "A reader-writer lock per component type": two readers must not exclude each other.
		Registry<true> reg;
		std::atomic<int> insideCount{ 0 };
		std::atomic<int> readyToLeave{ 0 };
		std::atomic<int> sawBothInside{ 0 };

		auto worker = [&] {
			auto g = reg.access<Read<AccInt>>();
			insideCount.fetch_add(1, std::memory_order_acq_rel);
			const bool sawTwo = waitFor([&] { return insideCount.load(std::memory_order_acquire) == 2; }, std::chrono::milliseconds(500));
			readyToLeave.fetch_add(1, std::memory_order_acq_rel);
			// Neither thread decrements until both have had their chance to observe the peak,
			// so a fast thread cannot drop the count before the other's poll tick catches it.
			waitFor([&] { return readyToLeave.load(std::memory_order_acquire) == 2; }, std::chrono::milliseconds(500));
			if (sawTwo) { sawBothInside.fetch_add(1, std::memory_order_acq_rel); }
			insideCount.fetch_sub(1, std::memory_order_acq_rel);
		};
		std::thread t1(worker), t2(worker);
		t1.join();
		t2.join();

		EXPECT_EQ(sawBothInside.load(), 2) << "both readers should have observed each other inside the claim";
	}

	TEST(Access_MutualExclusion, DifferentTypesDoNotBlockEachOther) {
		Registry<true> reg;
		std::atomic<bool> otherAcquired{ false };
		{
			auto held = reg.access<Write<AccInt>>();
			std::thread t([&] {
				auto g = reg.access<Write<AccOther>>();
				otherAcquired.store(true, std::memory_order_release);
			});
			ASSERT_TRUE(waitFor([&] { return otherAcquired.load(std::memory_order_acquire); }, std::chrono::milliseconds(500)))
				<< "a claim on an unrelated type must not wait for this one";
			t.join();
		}
	}

	TEST(Access_MutualExclusion, SameComponentTypeOnTwoDifferentRegistriesDoesNotBlock) {
		// typeMutex() is a Registry member (Registry.h: "The reader-writer lock guarding one
		// component type's values"), allocated per instance -- so a claim on one registry has
		// nothing to do with the same component type claimed on a different registry.
		Registry<true> reg1, reg2;
		std::atomic<bool> otherAcquired{ false };
		{
			auto held = reg1.access<Write<AccInt>>();
			std::thread t([&] {
				auto g = reg2.access<Write<AccInt>>();
				otherAcquired.store(true, std::memory_order_release);
			});
			ASSERT_TRUE(waitFor([&] { return otherAcquired.load(std::memory_order_acquire); }, std::chrono::milliseconds(500)))
				<< "a claim on another registry's instance of the same component type must not wait";
			t.join();
		}
	}

	// ===================================================================== same-thread reentrancy

	TEST(Access_Reentrancy, NestedReadOnSameTypeDoesNotDeadlock) {
		Registry<true> reg;
		auto outer = reg.access<Read<AccInt>>();
		EXPECT_NO_THROW({ auto inner = reg.access<Read<AccInt>>(); });
	}

	TEST(Access_Reentrancy, NestedWriteOnSameTypeDoesNotDeadlock) {
		Registry<true> reg;
		auto outer = reg.access<Write<AccInt>>();
		EXPECT_NO_THROW({ auto inner = reg.access<Write<AccInt>>(); });
	}

	TEST(Access_Reentrancy, NestedReadAfterOuterWriteOnSameTypeDoesNotDeadlock) {
		// Only "write while holding read" is documented as refused; re-entering is otherwise
		// "allowed and does nothing", and reading under an exclusive claim this thread already
		// holds cannot itself be unsafe.
		Registry<true> reg;
		auto outer = reg.access<Write<AccInt>>();
		EXPECT_NO_THROW({ auto inner = reg.access<Read<AccInt>>(); });
	}

	TEST(Access_Reentrancy, NestedWriteAfterOuterReadOnSameTypeThrows) {
		Registry<true> reg;
		auto outer = reg.access<Read<AccInt>>();
		EXPECT_THROW((void)reg.access<Write<AccInt>>(), std::logic_error);
	}

	TEST(Access_Reentrancy, FailedUpgradeLeavesTheOuterGuardAndUnrelatedTypesUnaffected) {
		Registry<true> reg;
		std::atomic<bool> waiterAcquired{ false };
		std::thread t;
		{
			auto outer = reg.access<Read<AccInt>>();
			EXPECT_THROW((void)reg.access<Write<AccInt>>(), std::logic_error);

			// an unrelated type never touched by outer is still free
			EXPECT_NO_THROW({ auto g = reg.access<Write<AccOther>>(); });

			// outer's claim on AccInt is still the real thing: a second thread must still wait
			t = std::thread([&] {
				auto g = reg.access<Write<AccInt>>();
				waiterAcquired.store(true, std::memory_order_release);
			});
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			EXPECT_FALSE(waiterAcquired.load(std::memory_order_acquire))
				<< "the failed upgrade attempt must not have released the outer guard's lock";
		}
		ASSERT_TRUE(waitFor([&] { return waiterAcquired.load(std::memory_order_acquire); }));
		t.join();
	}

	// Naming the same component type twice in one access<...>() call (e.g.
	// access<Read<T>, Write<T>>() or access<Read<T>, Read<T>>()) is not addressed by any of
	// Access.h's doc comments, Registry::access()'s doc comment, or docs/batching.md -- the
	// documented sorting/reentrancy rules are written in terms of *distinct* claims and of
	// claims made across *separate* access<>() calls on one thread. Deliberately left
	// unspecified here rather than asserted on; flagged for a decision.

} // namespace AccessTests
