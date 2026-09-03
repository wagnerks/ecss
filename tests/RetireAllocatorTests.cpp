// Black-box, per-method coverage of ecss::Memory::RetireBin and ecss::Memory::RetireAllocator,
// driven by the doc comments in RetireAllocator.h. RetireBin frees raw memory via std::free(),
// so this file checks the documented bookkeeping (pending counts, grace-period countdown,
// what survives a move) rather than trying to observe a free() call directly -- that part is
// libc's contract, not ecss's.
//
// One thing flagged rather than asserted with confidence: RetireBin's copy constructor and
// copy assignment operator exist (Registry code presumably needs the type copyable somewhere),
// but neither has a doc comment, and they disagree with each other in a way that looks more
// like a placeholder than a designed contract -- the copy constructor produces an empty bin,
// while copy assignment mutates neither side at all (not even clearing the target). Move
// construction/assignment are fully documented and tested for real semantics; the copy
// operations are only checked for "does not crash, target stays usable" below.

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <type_traits>
#include <vector>

#include <ecss/memory/RetireAllocator.h>

namespace RetireAllocatorTests {
	using namespace ecss::Memory;

	void* makeBlock(size_t bytes = 16) { return std::malloc(bytes); }

	// ===================================================================== RetireBin: construction

	TEST(RetireBin_Construction, DefaultGracePeriodIsThree) {
		RetireBin bin;
		EXPECT_EQ(bin.getGracePeriod(), 3u);
	}

	TEST(RetireBin_Construction, ExplicitGracePeriodIsHonoured) {
		RetireBin bin(7);
		EXPECT_EQ(bin.getGracePeriod(), 7u);
	}

	TEST(RetireBin_Construction, StartsWithNoPendingBlocks) {
		RetireBin bin;
		EXPECT_EQ(bin.pendingCount(), 0u);
	}

	// ===================================================================== RetireBin: retire()

	TEST(RetireBin_Retire, NullPointerIsHarmless) {
		RetireBin bin;
		EXPECT_NO_THROW(bin.retire(nullptr));
		EXPECT_EQ(bin.pendingCount(), 0u);
	}

	TEST(RetireBin_Retire, IncreasesPendingCountByOne) {
		RetireBin bin;
		bin.retire(makeBlock());
		EXPECT_EQ(bin.pendingCount(), 1u);
	}

	TEST(RetireBin_Retire, MultipleRetiresAccumulateInPendingCount) {
		RetireBin bin;
		for (int i = 0; i < 5; ++i) { bin.retire(makeBlock()); }
		EXPECT_EQ(bin.pendingCount(), 5u);
	}

	TEST(RetireBin_Retire, ZeroGracePeriodFreesImmediatelyWithoutQueuing) {
		// "A zero grace period means the owner has no lock-free readers to protect... the
		// block is freed immediately instead of queued."
		RetireBin bin(0);
		bin.retire(makeBlock());
		EXPECT_EQ(bin.pendingCount(), 0u);
	}

	// ===================================================================== RetireBin: tick()

	TEST(RetireBin_Tick, OnAnEmptyBinReturnsZero) {
		RetireBin bin;
		EXPECT_EQ(bin.tick(), 0u);
	}

	TEST(RetireBin_Tick, DoesNotFreeBeforeGracePeriodElapses) {
		RetireBin bin(3);
		bin.retire(makeBlock());
		EXPECT_EQ(bin.tick(), 0u);
		EXPECT_EQ(bin.tick(), 0u);
		EXPECT_EQ(bin.pendingCount(), 1u) << "two ticks against a grace period of three must not free yet";
	}

	TEST(RetireBin_Tick, FreesExactlyAtGracePeriodTicks) {
		RetireBin bin(3);
		bin.retire(makeBlock());
		bin.tick();
		bin.tick();
		EXPECT_EQ(bin.tick(), 1u) << "the third tick must free exactly one block";
		EXPECT_EQ(bin.pendingCount(), 0u);
	}

	TEST(RetireBin_Tick, IndependentlyRetiredBlocksFreeOnTheirOwnSchedule) {
		RetireBin bin(3);
		bin.retire(makeBlock());      // countdown 3, frees on tick #3
		bin.tick();                   // block A: countdown 2
		bin.retire(makeBlock());      // countdown 3, frees on tick #3 counted from here (this tick's #2)
		EXPECT_EQ(bin.tick(), 0u);    // A: 1, B: 2
		EXPECT_EQ(bin.tick(), 1u) << "A should free here, B should not yet";
		EXPECT_EQ(bin.pendingCount(), 1u);
		EXPECT_EQ(bin.tick(), 1u) << "B frees on its own third tick";
		EXPECT_EQ(bin.pendingCount(), 0u);
	}

	TEST(RetireBin_Tick, GracePeriodIsCapturedAtRetireTimeNotAtTickTime) {
		RetireBin bin(5);
		bin.retire(makeBlock());   // captures countdown = 5
		bin.tick();                // 4
		bin.tick();                // 3
		bin.setGracePeriod(1);     // must not retroactively shrink the already-queued block's countdown
		bin.tick();                // 2
		bin.tick();                // 1
		EXPECT_EQ(bin.pendingCount(), 1u) << "the block must still need its originally-captured countdown";
		EXPECT_EQ(bin.tick(), 1u) << "fifth tick against the original grace period of five";
	}

	// ===================================================================== RetireBin: drainAll()

	TEST(RetireBin_DrainAll, FreesEverythingRegardlessOfCountdown) {
		RetireBin bin(1000);
		for (int i = 0; i < 4; ++i) { bin.retire(makeBlock()); }
		bin.drainAll();
		EXPECT_EQ(bin.pendingCount(), 0u);
	}

	TEST(RetireBin_DrainAll, OnAnEmptyBinIsANoOp) {
		RetireBin bin;
		EXPECT_NO_THROW(bin.drainAll());
		EXPECT_EQ(bin.pendingCount(), 0u);
	}

	TEST(RetireBin_DrainAll, DestructorDrainsPendingBlocks) {
		// "the destructor drains the bin, which frees everything still queued regardless of
		// its countdown". Not directly observable without a leak checker; this is a smoke test
		// that the path runs without crashing.
		{
			RetireBin bin(1000);
			for (int i = 0; i < 4; ++i) { bin.retire(makeBlock()); }
		}
		SUCCEED();
	}

	// ===================================================================== RetireBin: grace period accessors

	TEST(RetireBin_GracePeriodAccessors, SetGracePeriodChangesWhatGetGracePeriodReturns) {
		RetireBin bin;
		bin.setGracePeriod(9);
		EXPECT_EQ(bin.getGracePeriod(), 9u);
	}

	// ===================================================================== RetireBin: copy / move

	TEST(RetireBin_CopyAndMove, CopyConstructorProducesAnEmptyIndependentBin) {
		RetireBin src(5);
		src.retire(makeBlock());
		RetireBin copy(src);
		EXPECT_EQ(copy.pendingCount(), 0u);
		EXPECT_EQ(src.pendingCount(), 1u) << "copying must not disturb the source";
	}

	TEST(RetireBin_CopyAndMove, CopyAssignmentDoesNotCrashAndLeavesTargetUsable) {
		RetireBin src(5);
		src.retire(makeBlock());
		RetireBin dst;
		EXPECT_NO_THROW(dst = src);
		// Deliberately not asserting what dst.pendingCount() or getGracePeriod() equal here --
		// see the file-level note. Only that dst is still a live, working bin afterward.
		dst.retire(makeBlock());
		EXPECT_NO_THROW(dst.tick());
		EXPECT_NO_THROW(dst.drainAll());
	}

	TEST(RetireBin_CopyAndMove, MoveConstructorTransfersPendingBlocksAndEmptiesSource) {
		RetireBin src(5);
		src.retire(makeBlock());
		src.retire(makeBlock());
		RetireBin moved(std::move(src));
		EXPECT_EQ(moved.pendingCount(), 2u);
		EXPECT_EQ(src.pendingCount(), 0u);
	}

	TEST(RetireBin_CopyAndMove, MoveConstructorCarriesGracePeriod) {
		RetireBin src(11);
		RetireBin moved(std::move(src));
		EXPECT_EQ(moved.getGracePeriod(), 11u);
	}

	TEST(RetireBin_CopyAndMove, MoveAssignmentDrainsItsOwnBlocksFirstThenTakesSources) {
		RetireBin dst(1000);
		dst.retire(makeBlock());
		dst.retire(makeBlock());   // dst has 2 of its own, would need 1000 ticks to free naturally

		RetireBin src(3);
		src.retire(makeBlock());   // src has 1

		dst = std::move(src);
		EXPECT_EQ(dst.pendingCount(), 1u) << "dst's own blocks must be drained, not merged with src's";
		EXPECT_EQ(dst.getGracePeriod(), 3u);
	}

	TEST(RetireBin_CopyAndMove, MoveAssignmentLeavesSourceEmpty) {
		RetireBin dst;
		RetireBin src(3);
		src.retire(makeBlock());
		dst = std::move(src);
		EXPECT_EQ(src.pendingCount(), 0u);
	}

	// ===================================================================== RetireBin: concurrency

	TEST(RetireBin_Concurrency, ConcurrentRetireAndTickAcrossThreadsEventuallyFreesEverything) {
		// "Safe from any thread, including several at once."
		RetireBin bin(2);
		constexpr int kThreads = 4;
		constexpr int kPerThread = 500;
		std::atomic<size_t> totalFreed{ 0 };
		std::atomic<bool> stop{ false };

		std::vector<std::thread> retirers;
		for (int t = 0; t < kThreads; ++t) {
			retirers.emplace_back([&] {
				for (int i = 0; i < kPerThread; ++i) { bin.retire(makeBlock()); }
			});
		}
		std::thread ticker([&] {
			while (!stop.load(std::memory_order_acquire)) {
				totalFreed.fetch_add(bin.tick(), std::memory_order_relaxed);
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		});

		for (auto& th : retirers) { th.join(); }
		// Drain whatever is left with a bounded number of extra ticks (grace period 2).
		for (int i = 0; i < 10; ++i) {
			totalFreed.fetch_add(bin.tick(), std::memory_order_relaxed);
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		stop.store(true, std::memory_order_release);
		ticker.join();
		totalFreed.fetch_add(bin.tick(), std::memory_order_relaxed);   // catch anything ticked concurrently with join

		EXPECT_EQ(bin.pendingCount(), 0u);
		EXPECT_EQ(totalFreed.load(), static_cast<size_t>(kThreads * kPerThread));
	}

	// ===================================================================== RetireAllocator

	TEST(RetireAllocator_Construction, StoresTheBinPointer) {
		RetireBin bin;
		RetireAllocator<int> alloc(&bin);
		EXPECT_EQ(alloc.bin, &bin);
	}

	TEST(RetireAllocator_Construction, RebindingConstructorCarriesTheBinPointer) {
		RetireBin bin;
		RetireAllocator<double> src(&bin);
		RetireAllocator<int> rebound(src);
		EXPECT_EQ(rebound.bin, &bin);
	}

	TEST(RetireAllocator_Allocate, ZeroSizeDoesNotThrow) {
		RetireBin bin;
		RetireAllocator<int> alloc(&bin);
		int* p = nullptr;
		EXPECT_NO_THROW(p = alloc.allocate(0));
		if (p) { alloc.deallocate(p, 0); }
	}

	TEST(RetireAllocator_Allocate, NonZeroSizeReturnsUsableStorage) {
		RetireBin bin;
		RetireAllocator<int> alloc(&bin);
		int* p = alloc.allocate(4);
		ASSERT_NE(p, nullptr);
		for (int i = 0; i < 4; ++i) { p[i] = i * 10; }
		for (int i = 0; i < 4; ++i) { EXPECT_EQ(p[i], i * 10); }
		alloc.deallocate(p, 4);
	}

	TEST(RetireAllocator_Deallocate, WithABoundBinRoutesThroughRetireInsteadOfFreeingImmediately) {
		RetireBin bin(3);
		RetireAllocator<int> alloc(&bin);
		int* p = alloc.allocate(4);
		alloc.deallocate(p, 4);
		EXPECT_EQ(bin.pendingCount(), 1u);
	}

	TEST(RetireAllocator_Deallocate, WithNoBinFreesImmediately) {
		RetireAllocator<int> alloc(nullptr);
		int* p = alloc.allocate(4);
		ASSERT_NE(p, nullptr);
		EXPECT_NO_THROW(alloc.deallocate(p, 4));
	}

	TEST(RetireAllocator_Equality, SameBinPointerCompareEqual) {
		RetireBin bin;
		RetireAllocator<int> a(&bin), b(&bin);
		EXPECT_TRUE(a == b);
		EXPECT_FALSE(a != b);
	}

	TEST(RetireAllocator_Equality, DifferentBinPointersCompareUnequal) {
		RetireBin bin1, bin2;
		RetireAllocator<int> a(&bin1), b(&bin2);
		EXPECT_FALSE(a == b);
		EXPECT_TRUE(a != b);
	}

	TEST(RetireAllocator_Traits, DoesNotPropagateOnMoveAssignmentAndIsNeverAlwaysEqual) {
		using Alloc = RetireAllocator<int>;
		static_assert(!Alloc::propagate_on_container_move_assignment::value);
		static_assert(!Alloc::is_always_equal::value);
		SUCCEED();
	}

	TEST(RetireAllocator_Integration, VectorReallocationRetiresTheOldBufferInsteadOfFreeingIt) {
		// The class's own stated purpose: "Push-backs that trigger reallocation will queue the
		// old memory into the bin instead of freeing it."
		RetireBin bin(1000);   // long grace period: nothing should free itself mid-test
		std::vector<int, RetireAllocator<int>> v{ RetireAllocator<int>(&bin) };
		for (int i = 0; i < 5000; ++i) { v.push_back(i); }   // forces several reallocations

		EXPECT_GT(bin.pendingCount(), 0u) << "at least one grown-past buffer should have been retired";
		for (int i = 0; i < 5000; ++i) { EXPECT_EQ(v[static_cast<size_t>(i)], i); }
	}

} // namespace RetireAllocatorTests
