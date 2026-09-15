// Tests for typed directed relations: what relate/unrelate/views promise, what happens on
// entity destruction, and that the linked lists stay consistent under random and threaded use.

#include <algorithm>
#include <atomic>
#include <concepts>
#include <random>
#include <thread>
#include <vector>

#include <ecss/Registry.h>

#include <gtest/gtest.h>

using namespace ecss;

namespace RelationTests {

	// ---------------------------------------------------------------- relation types

	struct Holds {};
	struct Targets {};
	struct Owns {};

	struct Equipped {
		enum class Slot { LeftHand, RightHand };
		Slot slot{ Slot::LeftHand };
	};

	/// Nothrow-move-constructible, nothrow-destructible, but NOT move-assignable: exercises
	/// the destroy-and-rebuild branch of overwrite.
	struct NonAssignable {
		int value = 0;
		NonAssignable() = default;
		explicit NonAssignable(int v) noexcept : value(v) {}
		NonAssignable(NonAssignable&&) noexcept = default;
		NonAssignable(const NonAssignable&) noexcept = default;
		NonAssignable& operator=(NonAssignable&&) = delete;
		NonAssignable& operator=(const NonAssignable&) = delete;
		~NonAssignable() = default;
	};

	// ---------------------------------------------------------------- helpers

	template<class Reg, class R>
	static std::vector<EntityId> CollectTargets(Reg& r, EntityId source) {
		std::vector<EntityId> out;
		for (EntityId t : r.template targets<R>(source)) { out.push_back(t); }
		std::sort(out.begin(), out.end());
		return out;
	}

	template<class Reg, class R>
	static std::vector<EntityId> CollectSources(Reg& r, EntityId target) {
		std::vector<EntityId> out;
		for (EntityId s : r.template sources<R>(target)) { out.push_back(s); }
		std::sort(out.begin(), out.end());
		return out;
	}

	/// Allocate `n` ids one at a time so the ids are deterministic in the single-threaded build.
	template<class Reg>
	static std::vector<EntityId> Take(Reg& r, size_t n) {
		std::vector<EntityId> out;
		out.reserve(n);
		for (size_t i = 0; i < n; ++i) { out.push_back(r.takeEntity()); }
		return out;
	}

	// ================================================================ compile-time contract

	// Which types are accepted as relations. Each message says which rule is checked, so a
	// failure tells you what broke.

	struct NotEmpty { int x; };
	struct NotDefaultInit { NotDefaultInit() = delete; };
	struct ThrowingDefaultCtor { ThrowingDefaultCtor() noexcept(false) { throw 42; } };
	struct ThrowingMoveCtor { ThrowingMoveCtor() = default; ThrowingMoveCtor(ThrowingMoveCtor&&) noexcept(false) {} };
	struct ThrowingDtor { ~ThrowingDtor() noexcept(false) {} };

	static_assert(RelationPayload<Holds>, "an empty tag is a valid payload");
	static_assert(RelationPayload<Equipped>, "a trivially movable struct is a valid payload");
	static_assert(RelationPayload<NonAssignable>, "move-assignment is not required");

	static_assert(RelationTag<Holds>, "empty, default-initializable, nothrow");
	static_assert(!RelationTag<Equipped>, "not empty: the tag form must not accept it");
	static_assert(!RelationTag<NotEmpty>, "not empty");
	static_assert(!RelationTag<NotDefaultInit>, "empty but not default-initializable");
	static_assert(!RelationTag<ThrowingDefaultCtor>, "empty and constructible, but construction can throw");

	static_assert(!RelationPayload<ThrowingMoveCtor>, "move constructor can throw");
	static_assert(!RelationPayload<ThrowingDtor>, "destructor can throw");

	// The checks are phrased as concepts over a dependent R rather than as bare
	// requires-expressions naming a concrete type: with the type fixed, an overload set that
	// resolves to nothing is a hard error on some compilers instead of an unsatisfied
	// requirement, and the assertion would fail to build rather than fail to hold.

	/// Is the two-argument (tag) form of relate callable for R?
	template<class R>
	concept TagRelatable = requires(Registry<false>& r, EntityId a, EntityId b) { r.template relate<R>(a, b); };

	/// Does a payload view over a non-const registry hand out a mutable R&?
	template<class R>
	concept MutablePayloadFrom = requires(Registry<false>& r, EntityId a) {
		{ (*r.template relationsFrom<R>(a).begin()).data() } -> std::same_as<R&>;
	};

	/// ... and over a const one?
	template<class R>
	concept MutablePayloadFromConst = requires(const Registry<false>& r, EntityId a) {
		{ (*r.template relationsFrom<R>(a).begin()).data() } -> std::same_as<R&>;
	};

	// relate<Equipped>(a, b) silently producing an Equipped with an unset slot is the bug the
	// tag constraint removes.
	static_assert(!TagRelatable<Equipped>, "relate<R>(a, b) must require RelationTag<R>");
	static_assert(TagRelatable<Holds>, "relate<R>(a, b) must accept a tag");

	// A const registry must not hand out a mutable payload.
	static_assert(MutablePayloadFrom<Equipped>, "a non-const Registry yields R&");
	static_assert(!MutablePayloadFromConst<Equipped>, "a const Registry must not hand out R&");

	// ================================================================ basics

	TEST(Relations_Basics, RelateThenHasRelation) {
		Registry<false> reg;
		auto e = Take(reg, 2);

		reg.relate<Holds>(e[0], e[1]);

		EXPECT_TRUE(reg.hasRelation<Holds>(e[0], e[1]));
	}

	TEST(Relations_Basics, EdgesAreDirected) {
		Registry<false> reg;
		auto e = Take(reg, 2);

		reg.relate<Holds>(e[0], e[1]);

		EXPECT_TRUE(reg.hasRelation<Holds>(e[0], e[1]));
		EXPECT_FALSE(reg.hasRelation<Holds>(e[1], e[0]));  // edges have a direction
	}

	TEST(Relations_Basics, UnrelateRemovesExactlyThatEdge) {
		Registry<false> reg;
		auto e = Take(reg, 3);

		reg.relate<Holds>(e[0], e[1]);
		reg.relate<Holds>(e[0], e[2]);

		reg.unrelate<Holds>(e[0], e[1]);

		EXPECT_FALSE(reg.hasRelation<Holds>(e[0], e[1]));
		EXPECT_TRUE(reg.hasRelation<Holds>(e[0], e[2]));
		EXPECT_EQ((CollectTargets<Registry<false>, Holds>(reg, e[0])), (std::vector<EntityId>{ e[2] }));
	}

	TEST(Relations_Basics, DuplicateRelateIsOneLogicalEdge) {
		Registry<false> reg;
		auto e = Take(reg, 2);

		reg.relate<Holds>(e[0], e[1]);
		reg.relate<Holds>(e[0], e[1]);
		reg.relate<Holds>(e[0], e[1]);

		// Same type, same pair: still one edge.
		EXPECT_EQ((CollectTargets<Registry<false>, Holds>(reg, e[0]).size()), 1u);
		EXPECT_EQ((CollectSources<Registry<false>, Holds>(reg, e[1]).size()), 1u);

		// One unrelate is enough to remove it: there is no second edge hiding behind it.
		reg.unrelate<Holds>(e[0], e[1]);
		EXPECT_FALSE(reg.hasRelation<Holds>(e[0], e[1]));
	}

	TEST(Relations_Basics, ManyTargetsFromOneSource) {
		Registry<false> reg;
		auto e = Take(reg, 4);

		reg.relate<Holds>(e[0], e[1]);
		reg.relate<Holds>(e[0], e[2]);
		reg.relate<Holds>(e[0], e[3]);

		EXPECT_EQ((CollectTargets<Registry<false>, Holds>(reg, e[0])), (std::vector<EntityId>{ e[1], e[2], e[3] }));
	}

	TEST(Relations_Basics, ManySourcesToOneTarget) {
		Registry<false> reg;
		auto e = Take(reg, 4);

		reg.relate<Holds>(e[0], e[3]);
		reg.relate<Holds>(e[1], e[3]);
		reg.relate<Holds>(e[2], e[3]);

		EXPECT_EQ((CollectSources<Registry<false>, Holds>(reg, e[3])), (std::vector<EntityId>{ e[0], e[1], e[2] }));
	}

	TEST(Relations_Basics, ForwardAndReverseAreOneSetOfEdges) {
		Registry<false> reg;
		auto e = Take(reg, 3);

		reg.relate<Holds>(e[0], e[2]);
		reg.relate<Holds>(e[1], e[2]);

		// Every edge must be visible from both ends.
		for (EntityId s : CollectSources<Registry<false>, Holds>(reg, e[2])) {
			EXPECT_TRUE(reg.hasRelation<Holds>(s, e[2]));
			auto t = CollectTargets<Registry<false>, Holds>(reg, s);
			EXPECT_NE(std::find(t.begin(), t.end(), e[2]), t.end());
		}
	}

	TEST(Relations_Basics, RelationTypesAreIndependent) {
		Registry<false> reg;
		auto e = Take(reg, 2);

		reg.relate<Holds>(e[0], e[1]);
		reg.relate<Targets>(e[0], e[1]);

		reg.unrelate<Holds>(e[0], e[1]);

		EXPECT_FALSE(reg.hasRelation<Holds>(e[0], e[1]));
		EXPECT_TRUE(reg.hasRelation<Targets>(e[0], e[1]));
	}

	// ================================================================ self-edges

	TEST(Relations_SelfEdge, IsAWellFormedEdge) {
		Registry<false> reg;
		auto e = Take(reg, 1);

		reg.relate<Targets>(e[0], e[0]);

		EXPECT_TRUE(reg.hasRelation<Targets>(e[0], e[0]));
		EXPECT_EQ((CollectTargets<Registry<false>, Targets>(reg, e[0])), (std::vector<EntityId>{ e[0] }));
		EXPECT_EQ((CollectSources<Registry<false>, Targets>(reg, e[0])), (std::vector<EntityId>{ e[0] }));
	}

	TEST(Relations_SelfEdge, UnrelatesCleanly) {
		Registry<false> reg;
		auto e = Take(reg, 1);

		reg.relate<Targets>(e[0], e[0]);
		reg.unrelate<Targets>(e[0], e[0]);

		EXPECT_FALSE(reg.hasRelation<Targets>(e[0], e[0]));
		EXPECT_TRUE(reg.targets<Targets>(e[0]).empty());
		EXPECT_TRUE(reg.sources<Targets>(e[0]).empty());
		EXPECT_TRUE(reg.validateRelationStructure());
	}

	// ================================================================ payload

	TEST(Relations_Payload, StoreAndRetrieve) {
		Registry<false> reg;
		auto e = Take(reg, 3);

		reg.relate<Equipped>(e[0], e[1], Equipped{ Equipped::Slot::RightHand });
		reg.relate<Equipped>(e[0], e[2], Equipped{ Equipped::Slot::LeftHand });

		bool sawRight = false, sawLeft = false;
		for (auto edge : reg.relationsFrom<Equipped>(e[0])) {
			if (edge.target() == e[1]) { sawRight = edge.data().slot == Equipped::Slot::RightHand; }
			if (edge.target() == e[2]) { sawLeft  = edge.data().slot == Equipped::Slot::LeftHand; }
		}
		EXPECT_TRUE(sawRight);
		EXPECT_TRUE(sawLeft);
	}

	TEST(Relations_Payload, IncomingViewCarriesTheSamePayload) {
		Registry<false> reg;
		auto e = Take(reg, 2);

		reg.relate<Equipped>(e[0], e[1], Equipped{ Equipped::Slot::RightHand });

		size_t seen = 0;
		for (auto edge : reg.relationsTo<Equipped>(e[1])) {
			EXPECT_EQ(edge.source(), e[0]);
			EXPECT_EQ(edge.data().slot, Equipped::Slot::RightHand);
			++seen;
		}
		EXPECT_EQ(seen, 1u);
	}

	TEST(Relations_Payload, RerelateOverwritesAndDoesNotDuplicate) {
		Registry<false> reg;
		auto e = Take(reg, 2);

		reg.relate<Equipped>(e[0], e[1], Equipped{ Equipped::Slot::LeftHand });
		reg.relate<Equipped>(e[0], e[1], Equipped{ Equipped::Slot::RightHand });

		// Relating the same pair again replaces the payload and keeps one edge.
		size_t count = 0;
		for (auto edge : reg.relationsFrom<Equipped>(e[0])) {
			EXPECT_EQ(edge.data().slot, Equipped::Slot::RightHand);
			++count;
		}
		EXPECT_EQ(count, 1u);
	}

	TEST(Relations_Payload, NonAssignableGoesThroughDestroyAndReconstruct) {
		Registry<false> reg;
		auto e = Take(reg, 2);

		reg.relate<NonAssignable>(e[0], e[1], NonAssignable{ 10 });
		reg.relate<NonAssignable>(e[0], e[1], NonAssignable{ 20 });

		size_t count = 0;
		for (auto edge : reg.relationsFrom<NonAssignable>(e[0])) {
			EXPECT_EQ(edge.data().value, 20);
			++count;
		}
		EXPECT_EQ(count, 1u);
	}

	TEST(Relations_Payload, MutableAccessThroughNonConstRegistry) {
		Registry<false> reg;
		auto e = Take(reg, 2);

		reg.relate<Equipped>(e[0], e[1], Equipped{ Equipped::Slot::LeftHand });

		// The view blocks writers, so R& points at the real payload and edits stick.
		for (auto edge : reg.relationsFrom<Equipped>(e[0])) {
			edge.data().slot = Equipped::Slot::RightHand;
		}
		for (auto edge : reg.relationsFrom<Equipped>(e[0])) {
			EXPECT_EQ(edge.data().slot, Equipped::Slot::RightHand);
		}
	}

	// ================================================================ unregistered type

	TEST(Relations_Unregistered, QueriesAreEmptyAndCreateNoStorage) {
		Registry<false> reg;
		auto e = Take(reg, 2);

		const size_t before = reg.relationTypeCount();

		EXPECT_FALSE(reg.hasRelation<Holds>(e[0], e[1]));
		EXPECT_TRUE(reg.targets<Holds>(e[0]).empty());
		EXPECT_TRUE(reg.sources<Holds>(e[1]).empty());
		EXPECT_TRUE(reg.relationsFrom<Holds>(e[0]).empty());
		EXPECT_TRUE(reg.relationsTo<Holds>(e[1]).empty());
		reg.unrelate<Holds>(e[0], e[1]);            // no-op, not an error
		reg.removeRelationsFrom<Holds>(e[0]);
		reg.removeRelationsTo<Holds>(e[1]);
		reg.removeAllRelations(e[0]);

		// This is what the test is really about: answering "no" must not create a storage.
		// Checking only the return values would miss that. Components work the same way,
		// see hasComponent().
		EXPECT_EQ(reg.relationTypeCount(), before);
	}

	TEST(Relations_Unregistered, RelateIsWhatCreatesStorage) {
		Registry<false> reg;
		auto e = Take(reg, 2);

		const size_t before = reg.relationTypeCount();
		reg.relate<Holds>(e[0], e[1]);
		EXPECT_EQ(reg.relationTypeCount(), before + 1);

		// A second type is a second storage; a second edge in the same type is not.
		reg.relate<Holds>(e[1], e[0]);
		EXPECT_EQ(reg.relationTypeCount(), before + 1);
		reg.relate<Targets>(e[0], e[1]);
		EXPECT_EQ(reg.relationTypeCount(), before + 2);
	}

	// ================================================================ bulk removal

	TEST(Relations_BulkRemoval, RemoveRelationsFromClearsOutgoingOnly) {
		Registry<false> reg;
		auto e = Take(reg, 4);

		reg.relate<Holds>(e[0], e[1]);
		reg.relate<Holds>(e[0], e[2]);
		reg.relate<Holds>(e[3], e[0]);      // incoming, must survive

		reg.removeRelationsFrom<Holds>(e[0]);

		EXPECT_TRUE(reg.targets<Holds>(e[0]).empty());
		EXPECT_EQ((CollectSources<Registry<false>, Holds>(reg, e[0])), (std::vector<EntityId>{ e[3] }));
		EXPECT_TRUE(reg.validateRelationStructure());
	}

	TEST(Relations_BulkRemoval, RemoveRelationsToClearsIncomingOnly) {
		Registry<false> reg;
		auto e = Take(reg, 4);

		reg.relate<Holds>(e[1], e[0]);
		reg.relate<Holds>(e[2], e[0]);
		reg.relate<Holds>(e[0], e[3]);      // outgoing, must survive

		reg.removeRelationsTo<Holds>(e[0]);

		EXPECT_TRUE(reg.sources<Holds>(e[0]).empty());
		EXPECT_EQ((CollectTargets<Registry<false>, Holds>(reg, e[0])), (std::vector<EntityId>{ e[3] }));
		EXPECT_TRUE(reg.validateRelationStructure());
	}

	TEST(Relations_BulkRemoval, RemoveAllRelationsSpansEveryType) {
		Registry<false> reg;
		auto e = Take(reg, 3);

		reg.relate<Holds>(e[0], e[1]);
		reg.relate<Targets>(e[2], e[0]);
		reg.relate<Owns>(e[0], e[2]);

		reg.removeAllRelations(e[0]);

		EXPECT_FALSE(reg.hasRelation<Holds>(e[0], e[1]));
		EXPECT_FALSE(reg.hasRelation<Targets>(e[2], e[0]));
		EXPECT_FALSE(reg.hasRelation<Owns>(e[0], e[2]));
		EXPECT_TRUE(reg.validateRelationStructure());
	}

	// ================================================================ destruction

	TEST(Relations_Destruction, DestroyingTheSourceRemovesTheEdge) {
		Registry<false> reg;
		auto e = Take(reg, 2);

		reg.relate<Holds>(e[0], e[1]);
		reg.destroyEntity(e[0]);

		EXPECT_TRUE(reg.sources<Holds>(e[1]).empty());
		EXPECT_TRUE(reg.validateRelationStructure());
	}

	TEST(Relations_Destruction, DestroyingTheTargetRemovesTheEdge) {
		Registry<false> reg;
		auto e = Take(reg, 2);

		reg.relate<Holds>(e[0], e[1]);
		reg.destroyEntity(e[1]);

		EXPECT_TRUE(reg.targets<Holds>(e[0]).empty());
		EXPECT_TRUE(reg.validateRelationStructure());
	}

	TEST(Relations_Destruction, EveryEdgeTouchingTheEntityGoes) {
		Registry<false> reg;
		auto e = Take(reg, 5);
		const EntityId A = e[0], B = e[1], C = e[2], D = e[3], E = e[4];

		reg.relate<Holds>(A, B);
		reg.relate<Targets>(C, A);
		reg.relate<Holds>(A, D);
		reg.relate<Owns>(E, A);

		reg.destroyEntity(A);

		EXPECT_TRUE(reg.sources<Holds>(B).empty());
		EXPECT_TRUE(reg.targets<Targets>(C).empty());
		EXPECT_TRUE(reg.sources<Holds>(D).empty());
		EXPECT_TRUE(reg.targets<Owns>(E).empty());
		EXPECT_TRUE(reg.validateRelationStructure());
	}

	TEST(Relations_Destruction, NoIdReuseWindow) {
		Registry<false> reg;
		auto e = Take(reg, 2);
		const EntityId player = e[0];
		const EntityId sword  = e[1];

		reg.relate<Holds>(player, sword);
		reg.destroyEntity(sword);

		const EntityId reused = reg.takeEntity();
		ASSERT_EQ(reused, sword) << "the allocator did not recycle the id; the test proves nothing";

		// The edge must be gone before the id is reused, or the new entity would inherit it.
		EXPECT_FALSE(reg.hasRelation<Holds>(player, reused));
		EXPECT_TRUE(reg.targets<Holds>(player).empty());
	}

	TEST(Relations_Destruction, SelfEdgeIsUnlinkedExactlyOnce) {
		Registry<false> reg;
		auto e = Take(reg, 1);

		// The edge is in both of A's lists. Simplest case where a sloppy sweep would unlink
		// it twice.
		reg.relate<Targets>(e[0], e[0]);
		reg.destroyEntity(e[0]);

		EXPECT_TRUE(reg.validateRelationStructure());
		EXPECT_EQ(reg.relationEdgeCount<Targets>(), 0u);
	}

	TEST(Relations_Destruction, BothEndpointsInOneBatch) {
		Registry<false> reg;
		auto e = Take(reg, 2);

		reg.relate<Holds>(e[0], e[1]);

		std::vector<EntityId> batch{ e[0], e[1] };
		reg.destroyEntities(batch);

		// Both ends are in the batch. The edge must be removed and its slot freed only once.
		EXPECT_TRUE(reg.validateRelationStructure());
		EXPECT_EQ(reg.relationEdgeCount<Holds>(), 0u);
	}

	TEST(Relations_Destruction, BatchLeavesUnrelatedEdgesAlone) {
		Registry<false> reg;
		auto e = Take(reg, 4);

		reg.relate<Holds>(e[0], e[1]);
		reg.relate<Holds>(e[2], e[3]);

		std::vector<EntityId> batch{ e[0], e[1] };
		reg.destroyEntities(batch);

		EXPECT_TRUE(reg.hasRelation<Holds>(e[2], e[3]));
		EXPECT_EQ(reg.relationEdgeCount<Holds>(), 1u);
		EXPECT_TRUE(reg.validateRelationStructure());
	}

	// ================================================================ structural validator

	/// Linked-list bugs usually show up only after slots get freed and reused, so run a long
	/// random mix of operations and check the lists after every step.
	TEST(Relations_Structure, RandomisedSequenceKeepsTheInvariants) {
		Registry<false> reg;
		auto e = Take(reg, 32);

		std::mt19937 rng{ 20260915u };
		auto pick = [&] { return e[rng() % e.size()]; };

		for (int step = 0; step < 4000; ++step) {
			switch (rng() % 6) {
			case 0: reg.relate<Holds>(pick(), pick()); break;
			case 1: reg.relate<Targets>(pick(), pick()); break;
			case 2: reg.unrelate<Holds>(pick(), pick()); break;
			case 3: reg.removeRelationsFrom<Holds>(pick()); break;
			case 4: reg.removeRelationsTo<Targets>(pick()); break;
			case 5: reg.removeAllRelations(pick()); break;
			}
			ASSERT_TRUE(reg.validateRelationStructure()) << "broke at step " << step;
		}
	}

	TEST(Relations_Structure, DestructionInterleavedWithMutation) {
		Registry<false> reg;
		auto e = Take(reg, 24);

		std::mt19937 rng{ 987654321u };

		for (int step = 0; step < 2000; ++step) {
			const EntityId a = e[rng() % e.size()];
			const EntityId b = e[rng() % e.size()];

			switch (rng() % 4) {
			case 0: reg.relate<Holds>(a, b); break;
			case 1: reg.relate<Owns>(a, b); break;
			case 2: reg.unrelate<Holds>(a, b); break;
			case 3:
				reg.destroyEntity(a);
				// Recycle so the id space churns rather than draining.
				e[rng() % e.size()] = reg.takeEntity();
				break;
			}
			ASSERT_TRUE(reg.validateRelationStructure()) << "broke at step " << step;
		}
	}

	// ================================================================ views

	TEST(Relations_Views, AreEmptyForAnEntityWithNoEdges) {
		Registry<false> reg;
		auto e = Take(reg, 2);

		reg.relate<Holds>(e[0], e[1]);

		EXPECT_TRUE(reg.targets<Holds>(e[1]).empty());
		EXPECT_FALSE(reg.targets<Holds>(e[0]).empty());
		EXPECT_TRUE(reg.sources<Holds>(e[0]).empty());
		EXPECT_FALSE(reg.sources<Holds>(e[1]).empty());
	}

	TEST(Relations_Views, CollectThenMutateIsTheSupportedPattern) {
		Registry<false> reg;
		auto e = Take(reg, 4);

		reg.relate<Holds>(e[0], e[1]);
		reg.relate<Holds>(e[0], e[2]);
		reg.relate<Holds>(e[0], e[3]);

		// Changing a relation while holding its view deadlocks. Collect first, then change:
		// that is the way to do it, so make sure it works.
		std::vector<EntityId> doomed;
		for (EntityId t : reg.targets<Holds>(e[0])) { doomed.push_back(t); }
		for (EntityId t : doomed) { reg.unrelate<Holds>(e[0], t); }

		EXPECT_TRUE(reg.targets<Holds>(e[0]).empty());
		EXPECT_TRUE(reg.validateRelationStructure());
	}

	TEST(Relations_Views, ClearDropsEveryEdgeButKeepsTheStorages) {
		Registry<false> reg;
		auto e = Take(reg, 3);

		reg.relate<Holds>(e[0], e[1]);
		reg.relate<Targets>(e[2], e[0]);
		const size_t types = reg.relationTypeCount();

		reg.clear();

		// clear() removes the entities, so the edges naming them must go with them. The
		// storages stay registered: nothing unregisters a relation type.
		EXPECT_EQ(reg.relationEdgeCount<Holds>(), 0u);
		EXPECT_EQ(reg.relationEdgeCount<Targets>(), 0u);
		EXPECT_EQ(reg.relationTypeCount(), types);
		EXPECT_TRUE(reg.validateRelationStructure());

		// And the storage is usable again afterwards.
		auto f = Take(reg, 2);
		reg.relate<Holds>(f[0], f[1]);
		EXPECT_TRUE(reg.hasRelation<Holds>(f[0], f[1]));
		EXPECT_TRUE(reg.validateRelationStructure());
	}

	// ================================================================ thread safety

	TEST(Relations_Threading, ConcurrentReadersOfOneRelation) {
		Registry<true> reg;
		auto e = Take(reg, 16);
		for (size_t i = 1; i < e.size(); ++i) { reg.relate<Holds>(e[0], e[i]); }

		std::atomic<bool> bad{ false };
		std::vector<std::thread> threads;
		for (int t = 0; t < 4; ++t) {
			threads.emplace_back([&] {
				for (int i = 0; i < 2000; ++i) {
					size_t n = 0;
					for (EntityId _ : reg.targets<Holds>(e[0])) { (void)_; ++n; }
					if (n != e.size() - 1) { bad = true; }
				}
			});
		}
		for (auto& th : threads) { th.join(); }
		EXPECT_FALSE(bad);
	}

	TEST(Relations_Threading, ReadersOfOneRelationAgainstAWriterOfAnother) {
		Registry<true> reg;
		auto e = Take(reg, 16);
		for (size_t i = 1; i < e.size(); ++i) { reg.relate<Holds>(e[0], e[i]); }
		reg.relate<Targets>(e[0], e[1]);   // register the second storage up front

		std::atomic<bool> stop{ false };
		std::atomic<bool> bad{ false };

		std::thread writer([&] {
			std::mt19937 rng{ 4242u };
			while (!stop) {
				const EntityId a = e[rng() % e.size()];
				const EntityId b = e[rng() % e.size()];
				reg.relate<Targets>(a, b);
				reg.unrelate<Targets>(a, b);
			}
		});

		for (int i = 0; i < 5000; ++i) {
			size_t n = 0;
			for (EntityId _ : reg.targets<Holds>(e[0])) { (void)_; ++n; }
			if (n != e.size() - 1) { bad = true; }
		}
		stop = true;
		writer.join();

		EXPECT_FALSE(bad);
		EXPECT_TRUE(reg.validateRelationStructure());
	}

	TEST(Relations_Threading, ConcurrentWritersOfOneRelationStayConsistent) {
		Registry<true> reg;
		auto e = Take(reg, 24);

		std::vector<std::thread> threads;
		for (int t = 0; t < 4; ++t) {
			threads.emplace_back([&, t] {
				std::mt19937 rng{ static_cast<uint32_t>(1000 + t) };
				for (int i = 0; i < 3000; ++i) {
					const EntityId a = e[rng() % e.size()];
					const EntityId b = e[rng() % e.size()];
					if (rng() % 2) { reg.relate<Holds>(a, b); }
					else           { reg.unrelate<Holds>(a, b); }
				}
			});
		}
		for (auto& th : threads) { th.join(); }

		EXPECT_TRUE(reg.validateRelationStructure());
	}

	TEST(Relations_Threading, FirstUseFromSeveralThreadsProducesOneStorage) {
		Registry<true> reg;
		auto e = Take(reg, 8);

		const size_t before = reg.relationTypeCount();

		std::vector<std::thread> threads;
		for (int t = 0; t < 4; ++t) {
			threads.emplace_back([&] { reg.relate<Owns>(e[0], e[1]); });
		}
		for (auto& th : threads) { th.join(); }

		EXPECT_EQ(reg.relationTypeCount(), before + 1);
		EXPECT_EQ(reg.relationEdgeCount<Owns>(), 1u);   // still one edge, even from four threads
	}

} // namespace RelationTests
