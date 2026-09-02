// Black-box, per-method coverage of ecss::Memory::GlobalTypeId<T>() and
// ecss::Memory::DenseTypeIdGenerator, driven by their own doc comments.
//
// DenseTypeIdGenerator's counter is one process-wide atomic, shared by every translation
// unit and every other test file that names a component type -- by the time this file runs,
// hundreds of ids have already been handed out. So every assertion here is relative (does
// this brand-new type get a fresh, unique, stable id?) rather than assuming any absolute
// starting value. Every type used below is declared locally inside its TEST body, specifically
// so it has never been named anywhere else in the binary before these tests run.
//
// getTypeId<T>()'s doc comment says it strips "cv and reference, so const T, T& and T share
// one id" -- but the implementation is
//   remove_const_t<remove_pointer_t<remove_reference_t<T>>>
// which is narrower than "cv" (it strips const but not volatile: remove_const_t, not
// remove_cv_t) and broader than the doc's own claim (it also strips one level of pointer,
// which the comment never mentions). Both are real mismatches between the comment and the
// code, not a matter of interpretation -- so this file tests only what the comment actually
// promises (const and reference stripping) and asserts nothing about volatile or pointer
// types either way. Flagged for a decision rather than silently tested one way or the other.

#include <gtest/gtest.h>

#include <thread>
#include <vector>

#include <ecss/memory/Reflection.h>

namespace ReflectionTests {
	using namespace ecss::Memory;

	// ===================================================================== GlobalTypeId

	TEST(GlobalTypeId, StableAcrossRepeatedCallsForTheSameType) {
		struct LocalA {};
		const auto first = GlobalTypeId<LocalA>();
		const auto second = GlobalTypeId<LocalA>();
		EXPECT_EQ(first, second);
	}

	TEST(GlobalTypeId, DifferentForDifferentTypes) {
		struct LocalB {};
		struct LocalC {};
		EXPECT_NE(GlobalTypeId<LocalB>(), GlobalTypeId<LocalC>());
	}

	TEST(GlobalTypeId, SameAcrossConcurrentCalls) {
		struct LocalD {};
		constexpr int kThreads = 8;
		std::vector<size_t> results(kThreads);
		std::vector<std::thread> threads;
		for (int t = 0; t < kThreads; ++t) {
			threads.emplace_back([&, t] { results[t] = GlobalTypeId<LocalD>(); });
		}
		for (auto& th : threads) { th.join(); }
		for (auto v : results) { EXPECT_EQ(v, results[0]); }
	}

	// ===================================================================== DenseTypeIdGenerator::getId

	TEST(DenseTypeIdGenerator_GetId, StableAcrossRepeatedCallsForTheSameType) {
		struct LocalE {};
		const auto first = DenseTypeIdGenerator::getId<LocalE>();
		const auto second = DenseTypeIdGenerator::getId<LocalE>();
		EXPECT_EQ(first, second);
	}

	TEST(DenseTypeIdGenerator_GetId, DifferentForDifferentTypes) {
		struct LocalF {};
		struct LocalG {};
		EXPECT_NE(DenseTypeIdGenerator::getId<LocalF>(), DenseTypeIdGenerator::getId<LocalG>());
	}

	TEST(DenseTypeIdGenerator_GetId, FirstUseOfThreeNewTypesGetsThreeConsecutiveIds) {
		// Compares the three ids to each other rather than to a captured getCount() snapshot:
		// on a --gtest_repeat run these locally-declared types are already memoized from an
		// earlier iteration (their ids are fixed for the process's lifetime), while getCount()
		// keeps reading whatever the live total is by then -- the two would disagree from the
		// second iteration on even though nothing is wrong. Comparing cached id to cached id
		// stays true forever once it's true once.
		struct LocalH {}; struct LocalI {}; struct LocalJ {};
		const auto idH = DenseTypeIdGenerator::getId<LocalH>();
		const auto idI = DenseTypeIdGenerator::getId<LocalI>();
		const auto idJ = DenseTypeIdGenerator::getId<LocalJ>();
		EXPECT_EQ(idI, idH + 1);
		EXPECT_EQ(idJ, idH + 2);
	}

	TEST(DenseTypeIdGenerator_GetId, SameAcrossConcurrentFirstUse) {
		// The function-local static's initialization is guaranteed by the language to run
		// exactly once even under a race between threads all hitting it for the first time.
		struct LocalK {};
		constexpr int kThreads = 8;
		std::vector<ecss::ECSType> results(kThreads);
		std::vector<std::thread> threads;
		for (int t = 0; t < kThreads; ++t) {
			threads.emplace_back([&, t] { results[t] = DenseTypeIdGenerator::getId<LocalK>(); });
		}
		for (auto& th : threads) { th.join(); }
		for (auto v : results) { EXPECT_EQ(v, results[0]); }
	}

	// ===================================================================== DenseTypeIdGenerator::getTypeId

	TEST(DenseTypeIdGenerator_GetTypeId, StripsConstQualifier) {
		struct LocalL {};
		EXPECT_EQ(DenseTypeIdGenerator::getTypeId<LocalL>(), DenseTypeIdGenerator::getTypeId<const LocalL>());
	}

	TEST(DenseTypeIdGenerator_GetTypeId, StripsReference) {
		struct LocalM {};
		EXPECT_EQ(DenseTypeIdGenerator::getTypeId<LocalM>(), DenseTypeIdGenerator::getTypeId<LocalM&>());
	}

	TEST(DenseTypeIdGenerator_GetTypeId, StripsConstReferenceTogether) {
		struct LocalN {};
		EXPECT_EQ(DenseTypeIdGenerator::getTypeId<LocalN>(), DenseTypeIdGenerator::getTypeId<const LocalN&>());
	}

	TEST(DenseTypeIdGenerator_GetTypeId, AgreesWithGetIdForAnUnqualifiedType) {
		// getTypeId<T>() is what Registry::componentTypeId<T>() forwards to (Registry.h), so
		// for a plain, non-reference, non-const T it must return exactly what getId<T>() does.
		struct LocalO {};
		EXPECT_EQ(DenseTypeIdGenerator::getId<LocalO>(), DenseTypeIdGenerator::getTypeId<LocalO>());
	}

	// ===================================================================== DenseTypeIdGenerator::getCount

	TEST(DenseTypeIdGenerator_GetCount, UnaffectedByRepeatedQueriesOfAnAlreadyKnownType) {
		struct LocalP {};
		DenseTypeIdGenerator::getId<LocalP>();   // first use: bumps the count once
		const auto count = DenseTypeIdGenerator::getCount();
		DenseTypeIdGenerator::getId<LocalP>();
		DenseTypeIdGenerator::getId<LocalP>();
		EXPECT_EQ(DenseTypeIdGenerator::getCount(), count);
	}

	TEST(DenseTypeIdGenerator_GetCount, IncreasesByExactlyOneForOneNewType) {
		// Same repeat-safety concern as FirstUseOfThreeNewTypesGetsThreeConsecutiveIds above:
		// this proves the claim via two ids' adjacency rather than a live getCount() snapshot
		// taken before a possibly-already-memoized type.
		struct LocalQ1 {};
		struct LocalQ2 {};
		const auto idQ1 = DenseTypeIdGenerator::getId<LocalQ1>();
		const auto idQ2 = DenseTypeIdGenerator::getId<LocalQ2>();
		EXPECT_EQ(idQ2, idQ1 + 1) << "exactly one new type between them must advance the counter by exactly one";
	}

} // namespace ReflectionTests
