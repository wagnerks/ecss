#include <gtest/gtest.h>
#include <ecss/Registry.h>
#include <string>
#include <vector>
#include <set>
#include <thread>
#include <atomic>

using namespace ecss;

struct VA {
	int x = 0;
};
struct VB {
	float y = 0.f;
};
struct VC {
	double z = 0.0;
};
struct VD {
	uint8_t w = 0;
};
struct VStr {
	std::string s;
	VStr() = default;
	explicit VStr(std::string str) : s(std::move(str)) {}
	~VStr() = default;
	VStr(const VStr&) = default;
	VStr(VStr&&) = default;
	VStr& operator=(const VStr&) = default;
	VStr& operator=(VStr&&) = default;
};

namespace {

Ranges<EntityId> MakeRange(EntityId begin, EntityId end) {
	return Ranges<EntityId>(std::vector<Ranges<EntityId>::Range>{{begin, end}});
}

} // namespace

TEST(ViewFull, IteratorVsEach_SingleComp) {
	Registry<false> reg;
	for (int i = 0; i < 20; ++i) {
		EntityId e = reg.takeEntity();
		reg.addComponent<VA>(e)->x = i;
	}
	auto v = reg.view<VA>();
	size_t itCount = 0;
	for (auto it = v.begin(); it != v.end(); ++it) {
		auto [ent, pa] = *it;
		(void)ent;
		ASSERT_NE(pa, nullptr);
		++itCount;
	}
	size_t eachCount = 0;
	v.each([&](VA&) { ++eachCount; });
	EXPECT_EQ(itCount, 20u);
	EXPECT_EQ(eachCount, 20u);
}

TEST(ViewFull, IteratorVsEach_MultiComp) {
	Registry<false> reg;
	reg.registerArray<VA, VB>();
	for (int i = 0; i < 20; ++i) {
		EntityId e = reg.takeEntity();
		reg.addComponent<VA>(e)->x = i;
		reg.addComponent<VB>(e)->y = static_cast<float>(i);
	}
	auto v = reg.view<VA, VB>();
	size_t itCount = 0;
	for (auto it = v.begin(); it != v.end(); ++it) {
		auto [ent, pa, pb] = *it;
		(void)ent;
		ASSERT_NE(pa, nullptr);
		ASSERT_NE(pb, nullptr);
		++itCount;
	}
	size_t eachCount = 0;
	v.each([&](VA&, VB&) { ++eachCount; });
	EXPECT_EQ(itCount, 20u);
	EXPECT_EQ(eachCount, 20u);
}

TEST(ViewFull, View1Component) {
	Registry<false> reg;
	for (int i = 0; i < 10; ++i) {
		EntityId e = reg.takeEntity();
		reg.addComponent<VA>(e)->x = i;
	}
	size_t n = 0;
	reg.view<VA>().each([&](VA&) { ++n; });
	EXPECT_EQ(n, 10u);
}

TEST(ViewFull, View2Components_Grouped) {
	Registry<false> reg;
	reg.registerArray<VA, VB>();
	for (int i = 0; i < 10; ++i) {
		EntityId e = reg.takeEntity();
		reg.addComponent<VA>(e)->x = i;
		reg.addComponent<VB>(e)->y = static_cast<float>(i * 2);
	}
	size_t n = 0;
	reg.view<VA, VB>().each([&](VA&, VB&) { ++n; });
	EXPECT_EQ(n, 10u);
}

TEST(ViewFull, View2Components_Separate) {
	Registry<false> reg;
	std::vector<EntityId> withB;
	for (int i = 0; i < 10; ++i) {
		EntityId e = reg.takeEntity();
		reg.addComponent<VA>(e)->x = i;
		if (i < 5) {
			reg.addComponent<VB>(e)->y = static_cast<float>(i);
			withB.push_back(e);
		}
	}
	auto v = reg.view<VA, VB>();
	size_t n = 0;
	for (auto it = v.begin(); it != v.end(); ++it) {
		auto [ent, pa, pb] = *it;
		(void)ent;
		if (pa && pb) {
			++n;
		}
	}
	EXPECT_EQ(n, 5u);
}

TEST(ViewFull, View3Components_AllPresent) {
	Registry<false> reg;
	reg.registerArray<VA, VB, VC>();
	for (int i = 0; i < 10; ++i) {
		EntityId e = reg.takeEntity();
		reg.addComponent<VA>(e)->x = i;
		reg.addComponent<VB>(e)->y = static_cast<float>(i);
		reg.addComponent<VC>(e)->z = static_cast<double>(i);
	}
	size_t n = 0;
	reg.view<VA, VB, VC>().each([&](VA&, VB&, VC&) { ++n; });
	EXPECT_EQ(n, 10u);
}

TEST(ViewFull, View4Components) {
	Registry<false> reg;
	for (int i = 0; i < 10; ++i) {
		EntityId e = reg.takeEntity();
		reg.addComponent<VA>(e)->x = i;
		reg.addComponent<VB>(e)->y = static_cast<float>(i);
		reg.addComponent<VC>(e)->z = static_cast<double>(i);
		reg.addComponent<VD>(e)->w = static_cast<uint8_t>(i);
	}
	auto v = reg.view<VA, VB, VC, VD>();
	size_t n = 0;
	for (auto it = v.begin(); it != v.end(); ++it) {
		auto [ent, pa, pb, pc, pd] = *it;
		(void)ent;
		ASSERT_NE(pa, nullptr);
		ASSERT_NE(pb, nullptr);
		ASSERT_NE(pc, nullptr);
		ASSERT_NE(pd, nullptr);
		++n;
	}
	EXPECT_EQ(n, 10u);
}

TEST(ViewFull, RangedGroupedPartialAlive) {
	Registry<false> reg;
	reg.registerArray<VA, VB>();
	for (int i = 0; i < 20; ++i) {
		EntityId e = reg.takeEntity();
		reg.addComponent<VA>(e)->x = i;
	}
	Ranges<EntityId> rg = MakeRange(0, 20);
	size_t n = 0;
	reg.view<VA, VB>(rg).each([&](VA&, VB&) { ++n; });
	EXPECT_EQ(n, 0u);
}

// Sibling of RangedGroupedPartialAlive above, but for range-for instead of each(): where
// each() is a join (skip an entity missing a secondary), a range-for is main-driven and hands
// back nullptr for a secondary the entity lacks -- documented on ArraysView::each() ("A
// range-for yields (EntityId, T*, CompTypes*...) for every entity with the main component,
// and a secondary it lacks arrives as nullptr for you to test"). This is the grouped case
// specifically: VA and VB share one physical sector, so the secondary going dead clears only
// its own bit in that shared sector's mask, not the main's.
TEST(ViewFull, RangeForGroupedPartialAlive_SecondaryComesBackNull) {
	Registry<false> reg;
	reg.registerArray<VA, VB>();

	EntityId onlyA = reg.takeEntity();
	reg.addComponent<VA>(onlyA)->x = 1;

	EntityId both = reg.takeEntity();
	reg.addComponent<VA>(both)->x = 2;
	reg.addComponent<VB>(both)->y = 3.f;

	int visited = 0;
	int sawSecondaryNull = 0;
	for (auto [id, a, b] : reg.view<VA, VB>()) {
		++visited;
		ASSERT_NE(a, nullptr) << "the main component must always be non-null when the entity is visited at all";
		if (id == onlyA) {
			EXPECT_EQ(b, nullptr) << "VB was never added to this entity: must arrive as nullptr, not skip it";
			++sawSecondaryNull;
		}
		else if (id == both) {
			ASSERT_NE(b, nullptr);
			EXPECT_FLOAT_EQ(b->y, 3.f);
		}
	}
	EXPECT_EQ(visited, 2) << "range-for must visit every entity with the main component, regardless of secondary liveness";
	EXPECT_EQ(sawSecondaryNull, 1);
}

// Same shape, but the secondary was added and then explicitly destroyed rather than never
// added -- proving the shared sector's bit gets cleared correctly, not just "never set".
TEST(ViewFull, RangeForGroupedPartialAlive_AfterDestroyComponent) {
	Registry<false> reg;
	reg.registerArray<VA, VB>();

	EntityId e = reg.takeEntity();
	reg.addComponent<VA>(e)->x = 5;
	reg.addComponent<VB>(e)->y = 6.f;
	reg.destroyComponent<VB>(e);   // VA's bit in the shared sector must survive this

	int visited = 0;
	for (auto [id, a, b] : reg.view<VA, VB>()) {
		++visited;
		ASSERT_NE(a, nullptr);
		EXPECT_EQ(a->x, 5);
		EXPECT_EQ(b, nullptr) << "VB was destroyed: must arrive as nullptr, and VA's own bit must be unaffected";
	}
	EXPECT_EQ(visited, 1);
}

TEST(ViewFull, ViewEmpty_NoEntities) {
	Registry<false> reg;
	auto v = reg.view<VA>();
	EXPECT_TRUE(v.empty());
	size_t n = 0;
	v.each([&](VA&) { ++n; });
	EXPECT_EQ(n, 0u);
}

TEST(ViewFull, ViewEmpty_WrongComponent) {
	Registry<false> reg;
	for (int i = 0; i < 10; ++i) {
		EntityId e = reg.takeEntity();
		reg.addComponent<VA>(e)->x = i;
	}
	EXPECT_TRUE(reg.view<VB>().empty());
}

TEST(ViewFull, EachGrouped_DifferentOrder_2) {
	Registry<false> reg;
	reg.registerArray<VA, VB>();
	for (int i = 0; i < 10; ++i) {
		EntityId e = reg.takeEntity();
		reg.addComponent<VA>(e)->x = i * 3;
		reg.addComponent<VB>(e)->y = static_cast<float>(i * 7);
	}
	reg.view<VB, VA>().each([&](VB& b, VA& a) {
		EXPECT_FLOAT_EQ(b.y, static_cast<float>((a.x / 3) * 7));
	});
}

TEST(ViewFull, EachGrouped_DifferentOrder_3) {
	Registry<false> reg;
	reg.registerArray<VA, VB, VC>();
	for (int i = 0; i < 10; ++i) {
		EntityId e = reg.takeEntity();
		reg.addComponent<VA>(e)->x = i;
		reg.addComponent<VB>(e)->y = static_cast<float>(i + 1);
		reg.addComponent<VC>(e)->z = static_cast<double>(i + 2);
	}
	reg.view<VC, VA, VB>().each([&](VC& c, VA& a, VB& b) {
		EXPECT_EQ(a.x + 1.0, static_cast<double>(b.y));
		EXPECT_EQ(static_cast<double>(a.x) + 2.0, c.z);
	});
}

TEST(ViewFull, EachAfterDestroyComponent) {
	Registry<false> reg;
	std::vector<EntityId> ids;
	for (int i = 0; i < 10; ++i) {
		EntityId e = reg.takeEntity();
		reg.addComponent<VA>(e)->x = i;
		ids.push_back(e);
	}
	for (int i = 0; i < 5; ++i) {
		reg.destroyComponent<VA>(ids[static_cast<size_t>(i)]);
	}
	size_t n = 0;
	reg.view<VA>().each([&](VA&) { ++n; });
	EXPECT_EQ(n, 5u);
}

TEST(ViewFull, EachAllDead) {
	Registry<false> reg;
	std::vector<EntityId> ids;
	for (int i = 0; i < 10; ++i) {
		EntityId e = reg.takeEntity();
		reg.addComponent<VA>(e)->x = i;
		ids.push_back(e);
	}
	for (EntityId e : ids) {
		reg.destroyComponent<VA>(e);
	}
	size_t n = 0;
	reg.view<VA>().each([&](VA&) { ++n; });
	EXPECT_EQ(n, 0u);
}

TEST(ViewFull, ForEachAsync_HappyPath) {
	Registry<true> reg;
	std::vector<EntityId> ids;
	for (int i = 0; i < 10; ++i) {
		EntityId e = reg.takeEntity();
		reg.addComponent<VA>(e)->x = i;
		ids.push_back(e);
	}
	std::atomic<int> count{0};
	reg.forEachAsync<VA>(ids, [&](EntityId, VA* p) {
		ASSERT_NE(p, nullptr);
		count.fetch_add(1, std::memory_order_relaxed);
	});
	EXPECT_EQ(count.load(), 10);
}

TEST(ViewFull, ForEachAsync_EmptyIds) {
	Registry<true> reg;
	std::atomic<bool> called{false};
	const std::vector<EntityId> empty;
	reg.forEachAsync<VA>(empty, [&](EntityId, VA*) { called.store(true); });
	EXPECT_FALSE(called.load());
}

TEST(ViewFull, ForEachAsync_MixAliveDestroyed) {
	Registry<true> reg;
	std::vector<EntityId> ids;
	for (int i = 0; i < 10; ++i) {
		EntityId e = reg.takeEntity();
		reg.addComponent<VA>(e)->x = i;
		ids.push_back(e);
	}
	for (int i = 0; i < 5; ++i) {
		reg.destroyEntity(ids[static_cast<size_t>(i)]);
	}
	std::atomic<int> count{0};
	reg.forEachAsync<VA>(ids, [&](EntityId, VA* p) {
		if (p) {
			count.fetch_add(1, std::memory_order_relaxed);
		}
	});
	EXPECT_EQ(count.load(), 5);
}

TEST(ViewFull, ForEachAsync_TwoComponents) {
	Registry<true> reg;
	reg.registerArray<VA, VB>();
	std::vector<EntityId> ids;
	for (int i = 0; i < 5; ++i) {
		EntityId e = reg.takeEntity();
		reg.addComponent<VA>(e)->x = i;
		reg.addComponent<VB>(e)->y = static_cast<float>(i);
		ids.push_back(e);
	}
	std::atomic<int> count{0};
	reg.forEachAsync<VA, VB>(ids, [&](EntityId, VA* a, VB* b) {
		ASSERT_NE(a, nullptr);
		ASSERT_NE(b, nullptr);
		count.fetch_add(1, std::memory_order_relaxed);
	});
	EXPECT_EQ(count.load(), 5);
}

TEST(ViewFull, ViewOnNonTrivial) {
	Registry<false> reg;
	for (int i = 0; i < 10; ++i) {
		EntityId e = reg.takeEntity();
		reg.addComponent<VStr>(e, VStr{"s" + std::to_string(i)});
	}
	std::set<std::string> seen;
	reg.view<VStr>().each([&](VStr& s) { seen.insert(s.s); });
	EXPECT_EQ(seen.size(), 10u);
	for (int i = 0; i < 10; ++i) {
		EXPECT_EQ(seen.count("s" + std::to_string(i)), 1u);
	}
}

TEST(ViewFull, MultipleViewsSimultaneous) {
	Registry<false> reg;
	for (int i = 0; i < 10; ++i) {
		EntityId e = reg.takeEntity();
		reg.addComponent<VA>(e)->x = i;
	}
	auto v1 = reg.view<VA>();
	auto v2 = reg.view<VA>();
	size_t c1 = 0;
	v1.each([&](VA&) { ++c1; });
	size_t c2 = 0;
	v2.each([&](VA&) { ++c2; });
	EXPECT_EQ(c1, 10u);
	EXPECT_EQ(c2, 10u);
}

TEST(ViewFull, ViewAfterUpdate) {
	Registry<true> reg;
	std::vector<EntityId> ids;
	for (int i = 0; i < 10; ++i) {
		EntityId e = reg.takeEntity();
		reg.addComponent<VA>(e)->x = i;
		ids.push_back(e);
	}
	for (int i = 0; i < 5; ++i) {
		reg.destroyEntity(ids[static_cast<size_t>(i)]);
	}
	reg.update(true);
	size_t n = 0;
	reg.view<VA>().each([&](VA&) { ++n; });
	EXPECT_EQ(n, 5u);
}

TEST(ViewFull, ViewIterator_EntityIdCorrect) {
	Registry<false> reg;
	std::vector<EntityId> expected;
	for (int i = 0; i < 3; ++i) {
		EntityId e = reg.takeEntity();
		expected.push_back(e);
		reg.addComponent<VA>(e)->x = i;
	}
	size_t idx = 0;
	for (auto it = reg.view<VA>().begin(); it != reg.view<VA>().end(); ++it) {
		auto [ent, p] = *it;
		ASSERT_NE(p, nullptr);
		EXPECT_EQ(ent, expected[idx]);
		++idx;
	}
	EXPECT_EQ(idx, 3u);
}

TEST(ViewFull, EachSingle_LargeCount) {
	Registry<false> reg;
	for (int i = 0; i < 100000; ++i) {
		EntityId e = reg.takeEntity();
		reg.addComponent<VA>(e)->x = i;
	}
	size_t n = 0;
	reg.view<VA>().each([&](VA&) { ++n; });
	EXPECT_EQ(n, 100000u);
}

TEST(ViewFull, RangedView_PartialRange) {
	Registry<false> reg;
	for (int i = 0; i < 20; ++i) {
		EntityId e = reg.takeEntity();
		reg.addComponent<VA>(e)->x = i;
	}
	Ranges<EntityId> rg = MakeRange(5, 15);
	size_t n = 0;
	reg.view<VA>(rg).each([&](VA&) { ++n; });
	EXPECT_EQ(n, 10u);
}

TEST(ViewFull, RangedView_NoOverlap) {
	Registry<false> reg;
	for (int i = 0; i < 10; ++i) {
		EntityId e = reg.takeEntity();
		reg.addComponent<VA>(e)->x = i;
	}
	Ranges<EntityId> rg = MakeRange(100, 200);
	size_t n = 0;
	reg.view<VA>(rg).each([&](VA&) { ++n; });
	EXPECT_EQ(n, 0u);
}

TEST(ViewFull, RangedView_MultiComponent) {
	Registry<false> reg;
	reg.registerArray<VA, VB>();
	for (int i = 0; i < 20; ++i) {
		EntityId e = reg.takeEntity();
		reg.addComponent<VA>(e)->x = i;
		if ((i % 2) == 0) {
			reg.addComponent<VB>(e)->y = static_cast<float>(i);
		}
	}
	Ranges<EntityId> rg = MakeRange(0, 10);
	size_t n = 0;
	reg.view<VA, VB>(rg).each([&](VA&, VB&) { ++n; });
	EXPECT_EQ(n, 5u);
}

TEST(ViewFull, ConcurrentViewAndMutation_TS) {
	Registry<true> reg;
	for (int i = 0; i < 10; ++i) {
		EntityId e = reg.takeEntity();
		reg.addComponent<VA>(e)->x = i;
	}
	auto v = reg.view<VA>();
	size_t countBefore = 0;
	for (auto it = v.begin(); it != v.end(); ++it) {
		++countBefore;
	}
	std::thread worker([&]() {
		for (int i = 0; i < 100; ++i) {
			EntityId e = reg.takeEntity();
			reg.addComponent<VA>(e)->x = 1000 + i;
		}
	});
	worker.join();
	size_t countAfter = 0;
	for (auto it = v.begin(); it != v.end(); ++it) {
		++countAfter;
	}
	EXPECT_LE(countAfter, countBefore);
}

TEST(ViewFull, ViewEach_GroupedThreeTypes_LargeScale) {
	Registry<false> reg;
	reg.registerArray<VA, VB, VC>();
	for (int i = 0; i < 10000; ++i) {
		EntityId e = reg.takeEntity();
		reg.addComponent<VA>(e)->x = i;
		reg.addComponent<VB>(e)->y = static_cast<float>(i);
		reg.addComponent<VC>(e)->z = static_cast<double>(i);
	}
	size_t n = 0;
	reg.view<VC, VA, VB>().each([&](VC&, VA&, VB&) { ++n; });
	EXPECT_EQ(n, 10000u);
}

TEST(ViewFull, RangedViewTS) {
	Registry<true> reg;
	for (int i = 0; i < 30; ++i) {
		EntityId e = reg.takeEntity();
		reg.addComponent<VA>(e)->x = i;
	}
	Ranges<EntityId> rg = MakeRange(10, 20);
	size_t n = 0;
	reg.view<VA>(rg).each([&](VA&) { ++n; });
	EXPECT_EQ(n, 10u);
}

TEST(ViewFull, RangedView_Holes) {
	Registry<false> reg;
	for (int i = 0; i < 200; ++i) {
		EntityId e = reg.takeEntity();
		reg.addComponent<VA>(e)->x = i;
	}
	Ranges<EntityId> rg(std::vector<Ranges<EntityId>::Range>{{0, 3}, {50, 52}, {190, 200}});
	std::vector<int> seen;
	reg.view<VA>(rg).each([&](VA& a) { seen.push_back(a.x); });
	const std::vector<int> expected{0, 1, 2, 50, 51, 190, 191, 192, 193, 194, 195, 196, 197, 198, 199};
	EXPECT_EQ(seen, expected);
}

TEST(ViewFull, ViewAfterClear) {
	Registry<false> reg;
	for (int i = 0; i < 10; ++i) {
		EntityId e = reg.takeEntity();
		reg.addComponent<VA>(e)->x = i;
	}
	reg.clear();
	size_t n = 0;
	reg.view<VA>().each([&](VA&) { ++n; });
	EXPECT_EQ(n, 0u);
}
