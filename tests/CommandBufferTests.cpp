// Black-box, per-method coverage of ecss::CommandBuffer, driven by its own doc comments
// and docs/batching.md rather than by how Store/IStore happen to implement it. Every
// assertion here traces back to a documented sentence; where the documentation is silent
// or the behavior is explicitly a caller error (recording against an entity this buffer
// already destroyed), no test asserts an outcome for it.
//
// RegressionTests.cpp already carries CommandBuffer coverage for specific past defects
// (fuzz equivalence against immediate calls, last-recorded-wins, recording-during-iteration).
// This file is the standalone spec for the class: every public method, on its own.
//
// Two boundary cases were deliberately left out rather than asserted on:
//  - A component type whose constructor throws while being recorded. Nothing in
//    CommandBuffer.h, Registry.h or docs/batching.md says what state the buffer is left
//    in, so any assertion here would be testing this file's guess, not the class's contract.
//  - Recording against an EntityId never obtained from the target registry's takeEntity().
//    apply() forwards straight to Registry::insertBulk / destroyComponent / destroyEntities,
//    and "takes on the contract of those operations" per apply()'s own doc comment -- so
//    that boundary belongs to Registry's own id-validity contract, not to CommandBuffer's.

#include <gtest/gtest.h>

#include <type_traits>

#include <ecss/CommandBuffer.h>
#include <ecss/Registry.h>

namespace CommandBufferTests {
	using namespace ecss;

	struct CbInt { int v{}; };
	struct CbPoint { int x{}; int y{}; };
	struct CbDouble { double v{}; };
	struct CbDefault { int v = -7; };

	// ===================================================================== construction

	TEST(CommandBuffer_Construction, DefaultConstructedBufferIsEmpty) {
		CommandBuffer<true> cb;
		EXPECT_TRUE(cb.empty());
		EXPECT_EQ(cb.size(), 0u);
	}

	TEST(CommandBuffer_Construction, MovableNotCopyable) {
		using Cb = CommandBuffer<true>;
		static_assert(!std::is_copy_constructible_v<Cb>, "CommandBuffer(const CommandBuffer&) is documented deleted");
		static_assert(!std::is_copy_assignable_v<Cb>, "CommandBuffer::operator=(const CommandBuffer&) is documented deleted");
		static_assert(std::is_move_constructible_v<Cb>, "CommandBuffer(CommandBuffer&&) is documented default");
		static_assert(std::is_move_assignable_v<Cb>, "CommandBuffer::operator=(CommandBuffer&&) is documented default");
		SUCCEED();
	}

	TEST(CommandBuffer_Construction, MoveConstructedBufferPreservesRecordedWork) {
		// The move constructor is documented "= default", which member-wise moves the
		// per-type stores and the destroy list -- so a moved-to buffer must apply exactly
		// what was recorded into the moved-from one.
		Registry<true> reg;
		const auto e = reg.takeEntity();

		CommandBuffer<true> cb1;
		cb1.addComponent<CbInt>(e, 5);
		ASSERT_EQ(cb1.size(), 1u);

		CommandBuffer<true> cb2(std::move(cb1));
		EXPECT_EQ(cb2.size(), 1u);
		EXPECT_FALSE(cb2.empty());

		cb2.apply(reg);
		ASSERT_TRUE(reg.hasComponent<CbInt>(e));
		EXPECT_EQ(reg.pinComponent<CbInt>(e).get()->v, 5);
	}

	TEST(CommandBuffer_Construction, MoveAssignedBufferPreservesRecordedWork) {
		Registry<true> reg;
		const auto e = reg.takeEntity();

		CommandBuffer<true> cb1;
		cb1.addComponent<CbInt>(e, 6);
		cb1.destroyEntity(reg.takeEntity());
		ASSERT_EQ(cb1.size(), 2u);

		CommandBuffer<true> cb2;
		cb2.addComponent<CbDouble>(reg.takeEntity(), 1.0);   // pre-existing content, must be replaced
		cb2 = std::move(cb1);
		EXPECT_EQ(cb2.size(), 2u);

		cb2.apply(reg);
		ASSERT_TRUE(reg.hasComponent<CbInt>(e));
		EXPECT_EQ(reg.pinComponent<CbInt>(e).get()->v, 6);
	}

	TEST(CommandBuffer_Construction, WorksWithNonThreadSafeRegistry) {
		// ThreadSafe is a template parameter with no default divergent from Registry's own,
		// so the non-thread-safe pairing is as much the documented contract as the default.
		// pinComponent() is a thread-safe-build-only API, so this reads back through view<>
		// instead, which is available on both builds.
		Registry<false> reg;
		const auto e = reg.takeEntity();

		CommandBuffer<false> cb;
		cb.addComponent<CbInt>(e, 42);
		cb.apply(reg);

		ASSERT_TRUE(reg.hasComponent<CbInt>(e));
		CbInt* found = nullptr;
		for (auto [id, c] : reg.view<CbInt>()) { if (id == e) { found = c; break; } }
		ASSERT_NE(found, nullptr);
		EXPECT_EQ(found->v, 42);
	}

	// ===================================================================== addComponent

	TEST(CommandBuffer_AddComponent, NotVisibleUntilApply) {
		Registry<true> reg;
		const auto e = reg.takeEntity();

		CommandBuffer<true> cb;
		cb.addComponent<CbInt>(e, 7);
		EXPECT_FALSE(reg.hasComponent<CbInt>(e)) << "a recorded add must not be visible before apply()";

		cb.apply(reg);
		ASSERT_TRUE(reg.hasComponent<CbInt>(e));
		EXPECT_EQ(reg.pinComponent<CbInt>(e).get()->v, 7);
	}

	TEST(CommandBuffer_AddComponent, ForwardsConstructorArgumentsToTheComponent) {
		Registry<true> reg;
		const auto e = reg.takeEntity();

		CommandBuffer<true> cb;
		cb.addComponent<CbPoint>(e, 3, 4);
		cb.apply(reg);

		ASSERT_TRUE(reg.hasComponent<CbPoint>(e));
		auto* p = reg.pinComponent<CbPoint>(e).get();
		EXPECT_EQ(p->x, 3);
		EXPECT_EQ(p->y, 4);
	}

	TEST(CommandBuffer_AddComponent, ZeroConstructorArgumentsUsesTheComponentsDefaultConstruction) {
		// addComponent<T>(entity, args...) accepts Args of any count, including none.
		Registry<true> reg;
		const auto e = reg.takeEntity();

		CommandBuffer<true> cb;
		cb.addComponent<CbDefault>(e);
		cb.apply(reg);

		ASSERT_TRUE(reg.hasComponent<CbDefault>(e));
		EXPECT_EQ(reg.pinComponent<CbDefault>(e).get()->v, -7);
	}

	TEST(CommandBuffer_AddComponent, EntityAllocatedJustBeforeRecordingIsUsableRightAway) {
		// "Ids come from the registry, not the buffer, so an entity created for a recorded
		// add is usable as an id right away."
		Registry<true> reg;
		CommandBuffer<true> cb;

		const auto e = reg.takeEntity();   // immediate: ids are Registry's job
		cb.addComponent<CbInt>(e, 5);      // deferred: the component value is the buffer's job
		cb.apply(reg);

		ASSERT_TRUE(reg.contains(e));
		ASSERT_TRUE(reg.hasComponent<CbInt>(e));
		EXPECT_EQ(reg.pinComponent<CbInt>(e).get()->v, 5);
	}

	TEST(CommandBuffer_AddComponent, RepeatedAddForSameEntityAndTypeKeepsTheLaterValue) {
		// "Recording the same entity twice for one type keeps the later value, matching what
		// a pair of immediate addComponent calls would have left behind."
		Registry<true> reg;
		const auto e = reg.takeEntity();

		CommandBuffer<true> cb;
		cb.addComponent<CbInt>(e, 1);
		cb.addComponent<CbInt>(e, 2);
		cb.apply(reg);

		ASSERT_TRUE(reg.hasComponent<CbInt>(e));
		EXPECT_EQ(reg.pinComponent<CbInt>(e).get()->v, 2);
	}

	TEST(CommandBuffer_AddComponent, EachCallIncreasesSizeByOne) {
		Registry<true> reg;
		CommandBuffer<true> cb;
		const auto e1 = reg.takeEntity();
		const auto e2 = reg.takeEntity();

		EXPECT_EQ(cb.size(), 0u);
		cb.addComponent<CbInt>(e1, 1);
		EXPECT_EQ(cb.size(), 1u);
		cb.addComponent<CbDouble>(e2, 2.0);
		EXPECT_EQ(cb.size(), 2u);
	}

	// ===================================================================== destroyComponent

	TEST(CommandBuffer_DestroyComponent, NotAppliedUntilApply) {
		Registry<true> reg;
		const auto e = reg.takeEntity();
		reg.addComponent<CbInt>(e, 1);   // immediate: present before recording

		CommandBuffer<true> cb;
		cb.destroyComponent<CbInt>(e);
		EXPECT_TRUE(reg.hasComponent<CbInt>(e)) << "a recorded removal must not apply before apply()";

		cb.apply(reg);
		EXPECT_FALSE(reg.hasComponent<CbInt>(e));
	}

	TEST(CommandBuffer_DestroyComponent, OnEntityWithoutTheComponentIsHarmless) {
		Registry<true> reg;
		const auto e = reg.takeEntity();   // never given CbInt

		CommandBuffer<true> cb;
		cb.destroyComponent<CbInt>(e);
		EXPECT_NO_THROW(cb.apply(reg));

		EXPECT_FALSE(reg.hasComponent<CbInt>(e));
		EXPECT_TRUE(reg.contains(e)) << "destroying an absent component must not touch the entity itself";
	}

	// ===================================================================== destroyEntity

	TEST(CommandBuffer_DestroyEntity, NotAppliedUntilApply) {
		Registry<true> reg;
		const auto e = reg.takeEntity();

		CommandBuffer<true> cb;
		cb.destroyEntity(e);
		EXPECT_TRUE(reg.contains(e)) << "a recorded destroy must not apply before apply()";

		cb.apply(reg);
		EXPECT_FALSE(reg.contains(e));
	}

	TEST(CommandBuffer_DestroyEntity, RemovesEveryComponentTypeItHad) {
		Registry<true> reg;
		const auto e = reg.takeEntity();
		reg.addComponent<CbInt>(e, 1);
		reg.addComponent<CbDouble>(e, 2.0);

		CommandBuffer<true> cb;
		cb.destroyEntity(e);
		cb.apply(reg);

		EXPECT_FALSE(reg.contains(e));
		EXPECT_FALSE(reg.hasComponent<CbInt>(e));
		EXPECT_FALSE(reg.hasComponent<CbDouble>(e));
	}

	TEST(CommandBuffer_DestroyEntity, EachCallIncreasesSizeByOne) {
		Registry<true> reg;
		CommandBuffer<true> cb;
		const auto e = reg.takeEntity();

		EXPECT_EQ(cb.size(), 0u);
		cb.destroyEntity(e);
		EXPECT_EQ(cb.size(), 1u);
	}

	// A write and a destroy recorded for the same entity in the same buffer: destruction is
	// applied after everything else and is terminal, so the entity ends up destroyed no
	// matter which order the two were recorded in.
	TEST(CommandBuffer_DestroyEntity, WinsOverAnAddRecordedBeforeIt) {
		Registry<true> reg;
		const auto e = reg.takeEntity();

		CommandBuffer<true> cb;
		cb.addComponent<CbInt>(e, 9);
		cb.destroyEntity(e);
		cb.apply(reg);

		EXPECT_FALSE(reg.contains(e)) << "destruction recorded after the add must still win";
	}

	TEST(CommandBuffer_DestroyEntity, WinsOverAnAddRecordedAfterIt) {
		Registry<true> reg;
		const auto e = reg.takeEntity();

		CommandBuffer<true> cb;
		cb.destroyEntity(e);
		cb.addComponent<CbInt>(e, 9);
		cb.apply(reg);

		EXPECT_FALSE(reg.contains(e)) << "destruction recorded before the add must still win: it is terminal";
	}

	// ===================================================================== ordering (last recorded wins)

	TEST(CommandBuffer_Ordering, RemoveThenAddEndsPresentWithTheAddedValue) {
		Registry<true> reg;
		const auto e = reg.takeEntity();
		reg.addComponent<CbInt>(e, 1);

		CommandBuffer<true> cb;
		cb.destroyComponent<CbInt>(e);
		cb.addComponent<CbInt>(e, 111);
		cb.apply(reg);

		ASSERT_TRUE(reg.hasComponent<CbInt>(e));
		EXPECT_EQ(reg.pinComponent<CbInt>(e).get()->v, 111);
	}

	TEST(CommandBuffer_Ordering, AddThenRemoveEndsAbsent) {
		Registry<true> reg;
		const auto e = reg.takeEntity();

		CommandBuffer<true> cb;
		cb.addComponent<CbInt>(e, 222);
		cb.destroyComponent<CbInt>(e);
		cb.apply(reg);

		EXPECT_FALSE(reg.hasComponent<CbInt>(e));
	}

	// ===================================================================== apply

	TEST(CommandBuffer_Apply, OnAnEmptyBufferIsANoOp) {
		Registry<true> reg;
		const auto e = reg.takeEntity();
		reg.addComponent<CbInt>(e, 5);

		CommandBuffer<true> cb;
		EXPECT_NO_THROW(cb.apply(reg));

		EXPECT_TRUE(reg.contains(e));
		ASSERT_TRUE(reg.hasComponent<CbInt>(e));
		EXPECT_EQ(reg.pinComponent<CbInt>(e).get()->v, 5);
	}

	TEST(CommandBuffer_Apply, EmptiesTheBuffer) {
		Registry<true> reg;
		const auto e = reg.takeEntity();

		CommandBuffer<true> cb;
		cb.addComponent<CbInt>(e, 1);
		cb.destroyEntity(reg.takeEntity());
		ASSERT_FALSE(cb.empty());

		cb.apply(reg);
		EXPECT_TRUE(cb.empty());
		EXPECT_EQ(cb.size(), 0u);
	}

	TEST(CommandBuffer_Apply, CallingItTwiceAppliesTheRecordedWorkOnlyOnce) {
		Registry<true> reg;
		const auto e = reg.takeEntity();

		CommandBuffer<true> cb;
		cb.addComponent<CbInt>(e, 1);
		cb.apply(reg);
		reg.pinComponent<CbInt>(e).get()->v = 99;   // mutate directly, outside the buffer

		cb.apply(reg);   // buffer is empty now: must not redo the add and clobber the value
		EXPECT_EQ(reg.pinComponent<CbInt>(e).get()->v, 99);
	}

	TEST(CommandBuffer_Apply, MultipleComponentTypesAreEachAppliedCorrectly) {
		Registry<true> reg;
		const auto keep = reg.takeEntity();
		const auto drop = reg.takeEntity();
		reg.addComponent<CbDouble>(drop, 1.0);

		CommandBuffer<true> cb;
		cb.addComponent<CbInt>(keep, 10);
		cb.addComponent<CbDouble>(keep, 2.5);
		cb.destroyComponent<CbDouble>(drop);
		cb.apply(reg);

		ASSERT_TRUE(reg.hasComponent<CbInt>(keep));
		EXPECT_EQ(reg.pinComponent<CbInt>(keep).get()->v, 10);
		ASSERT_TRUE(reg.hasComponent<CbDouble>(keep));
		EXPECT_DOUBLE_EQ(reg.pinComponent<CbDouble>(keep).get()->v, 2.5);
		EXPECT_FALSE(reg.hasComponent<CbDouble>(drop));
	}

	// ===================================================================== clear

	TEST(CommandBuffer_Clear, DropsARecordedAddWithoutApplyingIt) {
		Registry<true> reg;
		const auto e = reg.takeEntity();

		CommandBuffer<true> cb;
		cb.addComponent<CbInt>(e, 1);
		cb.clear();
		cb.apply(reg);

		EXPECT_FALSE(reg.hasComponent<CbInt>(e));
	}

	TEST(CommandBuffer_Clear, DropsARecordedRemoval) {
		Registry<true> reg;
		const auto e = reg.takeEntity();
		reg.addComponent<CbInt>(e, 1);

		CommandBuffer<true> cb;
		cb.destroyComponent<CbInt>(e);
		cb.clear();
		cb.apply(reg);

		EXPECT_TRUE(reg.hasComponent<CbInt>(e)) << "the cleared removal must not have applied";
	}

	TEST(CommandBuffer_Clear, DropsARecordedEntityDestruction) {
		Registry<true> reg;
		const auto e = reg.takeEntity();

		CommandBuffer<true> cb;
		cb.destroyEntity(e);
		cb.clear();
		cb.apply(reg);

		EXPECT_TRUE(reg.contains(e)) << "the cleared destroy must not have applied";
	}

	TEST(CommandBuffer_Clear, LeavesTheBufferEmpty) {
		Registry<true> reg;
		const auto e = reg.takeEntity();

		CommandBuffer<true> cb;
		cb.addComponent<CbInt>(e, 1);
		cb.destroyEntity(reg.takeEntity());
		cb.clear();

		EXPECT_TRUE(cb.empty());
		EXPECT_EQ(cb.size(), 0u);
	}

	TEST(CommandBuffer_Clear, OnAnEmptyBufferIsANoOp) {
		CommandBuffer<true> cb;
		EXPECT_NO_THROW(cb.clear());
		EXPECT_TRUE(cb.empty());
		EXPECT_EQ(cb.size(), 0u);
	}

	// ===================================================================== empty / size

	TEST(CommandBuffer_EmptyAndSize, EmptyBecomesFalseAfterAnAdd) {
		Registry<true> reg;
		CommandBuffer<true> cb;
		ASSERT_TRUE(cb.empty());
		cb.addComponent<CbInt>(reg.takeEntity(), 1);
		EXPECT_FALSE(cb.empty());
	}

	TEST(CommandBuffer_EmptyAndSize, EmptyBecomesFalseAfterARemoval) {
		Registry<true> reg;
		CommandBuffer<true> cb;
		cb.destroyComponent<CbInt>(reg.takeEntity());
		EXPECT_FALSE(cb.empty());
	}

	TEST(CommandBuffer_EmptyAndSize, EmptyBecomesFalseAfterAnEntityDestroy) {
		Registry<true> reg;
		CommandBuffer<true> cb;
		cb.destroyEntity(reg.takeEntity());
		EXPECT_FALSE(cb.empty());
	}

	TEST(CommandBuffer_EmptyAndSize, SizeCountsEveryRecordedOperationAcrossKinds) {
		Registry<true> reg;
		CommandBuffer<true> cb;
		const auto e1 = reg.takeEntity();
		const auto e2 = reg.takeEntity();
		const auto e3 = reg.takeEntity();

		cb.addComponent<CbInt>(e1, 1);
		cb.addComponent<CbDouble>(e1, 1.0);
		cb.destroyComponent<CbInt>(e2);
		cb.destroyEntity(e3);

		EXPECT_EQ(cb.size(), 4u);
	}

} // namespace CommandBufferTests
