// Black-box, per-method coverage of ecss::detail::AccessTracker, ecss::detail::AccessScope,
// accessTypeName<T>(), readScope<T>() and writeScope<T>(), driven by AccessTracker.h's doc
// comments. The class "compiles to nothing when NDEBUG is set" -- so this file's behavioral
// suite only exists under #ifndef NDEBUG, and Release gets a much smaller suite proving the
// four surviving entry points are harmless no-ops. Build both configs to get full coverage
// (this project's build-tests directory is a multi-config MSVC solution: `cmake --build
// build-tests --config Debug` alongside the usual Release).
//
// AccessTracker::enabled() is one flag for the whole process, also flipped by
// Registry::setAccessTracking() and by other test files (RegressionTests.cpp's
// Regression_Access suite). Every test below that turns it on restores it via an RAII guard,
// so a test that aborts partway never leaves it on for whatever runs next.
//
// What this file deliberately does NOT attempt: driving report() to a real conflict. It ends
// in assert(false) followed by std::abort(), which is gtest-testable in principle via
// EXPECT_DEATH -- but assert() in an MSVC debug CRT defaults to a blocking "Debug Assertion
// Failed" dialog rather than a clean abort, and this project has no existing death-test
// infrastructure (report mode redirection, CI style flags) to say that's been made safe here.
// Introducing that blind, in a repo where nothing else does it, risks a hung test run instead
// of a red one. Every test below stays on the non-conflicting side of the tracker's logic --
// same-thread reentrancy in every direction, different types, and tracking left off across a
// real overlap -- which is where the actual bug risk in this kind of bookkeeping lives anyway.
// Flagged for a decision rather than attempted.

#include <gtest/gtest.h>

#include <atomic>
#include <string>
#include <thread>
#include <type_traits>

#include <ecss/AccessTracker.h>

namespace AccessTrackerTests {
	using namespace ecss;
	using namespace ecss::detail;

	// ===================================================================== AccessScope: traits (both configs)

	TEST(AccessScope_Traits, DefaultConstructibleMovableNotCopyable) {
		static_assert(std::is_default_constructible_v<AccessScope>);
		static_assert(std::is_move_constructible_v<AccessScope>);
		static_assert(std::is_move_assignable_v<AccessScope>);
		static_assert(!std::is_copy_constructible_v<AccessScope>);
		static_assert(!std::is_copy_assignable_v<AccessScope>);
		SUCCEED();
	}

#ifndef NDEBUG
	// ===================================================================== Debug build: the tracker is live

	namespace {
		struct TrackerFlagGuard {
			bool was = AccessTracker::enabled();
			~TrackerFlagGuard() { AccessTracker::setEnabled(was); }
		};
	}

	TEST(AccessTracker_Enabled, TogglingChangesWhatEnabledReturns) {
		TrackerFlagGuard restore;
		AccessTracker::setEnabled(true);
		EXPECT_TRUE(AccessTracker::enabled());
		AccessTracker::setEnabled(false);
		EXPECT_FALSE(AccessTracker::enabled());
	}

	TEST(AccessTracker_Reentrancy, NestedReadOnSameThreadIsFine) {
		TrackerFlagGuard restore;
		AccessTracker::setEnabled(true);
		constexpr ECSType type = 40001;
		AccessTracker::beginRead(type, "T");
		AccessTracker::beginRead(type, "T");   // "re-entering from the same thread is fine"
		AccessTracker::endRead(type);
		AccessTracker::endRead(type);
		SUCCEED();
	}

	TEST(AccessTracker_Reentrancy, WriteThenNestedReadOnSameThreadIsFine) {
		TrackerFlagGuard restore;
		AccessTracker::setEnabled(true);
		constexpr ECSType type = 40002;
		AccessTracker::beginWrite(type, "T");
		AccessTracker::beginRead(type, "T");   // "a system routinely reads what it just wrote"
		AccessTracker::endRead(type);
		AccessTracker::endWrite(type);
		SUCCEED();
	}

	TEST(AccessTracker_Reentrancy, ReadThenNestedWriteOnSameThreadIsFine) {
		TrackerFlagGuard restore;
		AccessTracker::setEnabled(true);
		constexpr ECSType type = 40003;
		AccessTracker::beginRead(type, "T");
		AccessTracker::beginWrite(type, "T");
		AccessTracker::endWrite(type);
		AccessTracker::endRead(type);
		SUCCEED();
	}

	TEST(AccessTracker_Reentrancy, NestedWriteOnSameThreadIsFine) {
		TrackerFlagGuard restore;
		AccessTracker::setEnabled(true);
		constexpr ECSType type = 40004;
		AccessTracker::beginWrite(type, "T");
		AccessTracker::beginWrite(type, "T");
		AccessTracker::endWrite(type);
		AccessTracker::endWrite(type);
		SUCCEED();
	}

	TEST(AccessTracker_NoConflict, DifferentComponentTypesOnDifferentThreadsNeverConflict) {
		TrackerFlagGuard restore;
		AccessTracker::setEnabled(true);
		constexpr ECSType typeA = 40005, typeB = 40006;
		std::atomic<bool> aHolding{ false };
		std::thread other([&] {
			AccessTracker::beginWrite(typeB, "B");
			aHolding.store(true, std::memory_order_release);
			std::this_thread::sleep_for(std::chrono::milliseconds(30));
			AccessTracker::endWrite(typeB);
		});
		while (!aHolding.load(std::memory_order_acquire)) { std::this_thread::yield(); }
		AccessTracker::beginWrite(typeA, "A");   // different type: must not see the other thread's B
		AccessTracker::endWrite(typeA);
		other.join();
		SUCCEED();
	}

	TEST(AccessTracker_NoConflict, DisabledTrackingIgnoresARealOverlap) {
		// Tracking is left off (the default), and two threads deliberately overlap on one
		// type -- proving the enabled() gate actually short-circuits before any bookkeeping,
		// not just that non-overlapping cases happen not to trip it.
		ASSERT_FALSE(AccessTracker::enabled()) << "tracking must already be off for this test to mean anything";
		constexpr ECSType type = 40007;
		std::atomic<bool> holding{ false };
		std::thread other([&] {
			AccessTracker::beginWrite(type, "T");
			holding.store(true, std::memory_order_release);
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			AccessTracker::endWrite(type);
		});
		while (!holding.load(std::memory_order_acquire)) { std::this_thread::yield(); }
		AccessTracker::beginWrite(type, "T");   // genuinely overlaps; must not report while disabled
		AccessTracker::endWrite(type);
		other.join();
		SUCCEED();
	}

	TEST(AccessTracker_Boundaries, EndReadWithoutAMatchingBeginIsHarmless) {
		TrackerFlagGuard restore;
		AccessTracker::setEnabled(true);
		constexpr ECSType type = 40008;
		EXPECT_NO_THROW(AccessTracker::endRead(type));
	}

	TEST(AccessTracker_Boundaries, EndWriteWithoutAMatchingBeginIsHarmless) {
		TrackerFlagGuard restore;
		AccessTracker::setEnabled(true);
		constexpr ECSType type = 40009;
		EXPECT_NO_THROW(AccessTracker::endWrite(type));
		// And a genuine begin/end pair afterward still behaves normally.
		EXPECT_NO_THROW(AccessTracker::beginWrite(type, "T"));
		EXPECT_NO_THROW(AccessTracker::endWrite(type));
	}

	TEST(AccessScope_Debug, NestedWriteThenReadViaFactoryFunctionsIsFine) {
		TrackerFlagGuard restore;
		AccessTracker::setEnabled(true);
		constexpr ECSType type = 40010;
		auto outer = writeScope<int>(type);
		{
			auto inner = readScope<int>(type);
		}
		SUCCEED();
	}

	TEST(AccessScope_Debug, DefaultConstructedScopeReleasesNothing) {
		// A default-constructed scope must not call endRead/endWrite on destruction --
		// verified indirectly: constructing and destroying one changes nothing observable,
		// so a following independent begin/end pair on a fresh type behaves normally.
		TrackerFlagGuard restore;
		AccessTracker::setEnabled(true);
		{
			AccessScope empty;
		}
		constexpr ECSType type = 40011;
		EXPECT_NO_THROW(AccessTracker::beginWrite(type, "T"));
		EXPECT_NO_THROW(AccessTracker::endWrite(type));
	}

	TEST(AccessTypeName_Debug, ReturnsANonEmptyName) {
		struct SomeLocalType {};
		const char* n = accessTypeName<SomeLocalType>();
		ASSERT_NE(n, nullptr);
		EXPECT_GT(std::string(n).size(), 0u);
	}

#else
	// ===================================================================== Release build: compiled to no-ops

	TEST(AccessTracker_ReleaseBuild, TheFourEntryPointsAreCallableNoOps) {
		// "Compiled out entirely when NDEBUG is set." enabled()/setEnabled() do not exist at
		// all in this configuration -- only these four survive, as no-ops.
		EXPECT_NO_THROW(AccessTracker::beginRead(0, "T"));
		EXPECT_NO_THROW(AccessTracker::endRead(0));
		EXPECT_NO_THROW(AccessTracker::beginWrite(0, "T"));
		EXPECT_NO_THROW(AccessTracker::endWrite(0));
	}

	TEST(AccessTypeName_Release, ReturnsAnEmptyString) {
		struct SomeLocalType {};
		EXPECT_STREQ(accessTypeName<SomeLocalType>(), "");
	}

	TEST(AccessScope_Release, ScopesConstructAndReleaseWithoutObservableEffect) {
		auto w = writeScope<int>(0);
		auto r = readScope<int>(1);
		AccessScope empty;
		SUCCEED();
	}

#endif

} // namespace AccessTrackerTests
