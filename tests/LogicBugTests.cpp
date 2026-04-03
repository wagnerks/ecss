#include <gtest/gtest.h>
#include <vector>
#include <set>
#include <algorithm>

#include <ecss/Registry.h>

using namespace ecss;

// Components with different sizes/alignment to expose offset bugs
struct CompA { float x = -1.f; };
struct CompB { double y = -2.0; };
struct CompC { int z = -3; };
struct SmallComp { uint8_t v = 0; };
struct BigComp { double a = 0, b = 0, c = 0, d = 0; };

// ============================================================================
// Bug #1: eachGrouped packed -- OffsetArray order mismatch
//
// registerArray<A, B>() lays out A then B in memory.
// view<B, A>().each() must still get correct pointers, not swapped.
// ============================================================================

TEST(LogicBug, EachGrouped_OffsetOrder_SameAsRegister) {
    Registry<false> reg;
    reg.registerArray<CompA, CompB>();

    auto e = reg.takeEntity();
    reg.addComponent<CompA>(e, 42.f);
    reg.addComponent<CompB>(e, 99.0);

    // view in SAME order as register
    int count = 0;
    reg.view<CompA, CompB>().each([&](CompA& a, CompB& b) {
        EXPECT_FLOAT_EQ(a.x, 42.f);
        EXPECT_DOUBLE_EQ(b.y, 99.0);
        ++count;
    });
    EXPECT_EQ(count, 1);
}

TEST(LogicBug, EachGrouped_OffsetOrder_ReversedFromRegister) {
    Registry<false> reg;
    reg.registerArray<CompA, CompB>();

    auto e = reg.takeEntity();
    reg.addComponent<CompA>(e, 42.f);
    reg.addComponent<CompB>(e, 99.0);

    // view in REVERSE order from register -- this was the bug
    int count = 0;
    reg.view<CompB, CompA>().each([&](CompB& b, CompA& a) {
        EXPECT_FLOAT_EQ(a.x, 42.f) << "CompA has wrong value -- likely offset mismatch";
        EXPECT_DOUBLE_EQ(b.y, 99.0) << "CompB has wrong value -- likely offset mismatch";
        ++count;
    });
    EXPECT_EQ(count, 1);
}

TEST(LogicBug, EachGrouped_OffsetOrder_ThreeComponents) {
    Registry<false> reg;
    reg.registerArray<CompA, CompB, CompC>();

    auto e = reg.takeEntity();
    reg.addComponent<CompA>(e, 1.f);
    reg.addComponent<CompB>(e, 2.0);
    reg.addComponent<CompC>(e, 3);

    // view in completely different order
    int count = 0;
    reg.view<CompC, CompB, CompA>().each([&](CompC& c, CompB& b, CompA& a) {
        EXPECT_FLOAT_EQ(a.x, 1.f);
        EXPECT_DOUBLE_EQ(b.y, 2.0);
        EXPECT_EQ(c.z, 3);
        ++count;
    });
    EXPECT_EQ(count, 1);
}

TEST(LogicBug, EachGrouped_DifferentSizes) {
    Registry<false> reg;
    reg.registerArray<SmallComp, BigComp>();

    auto e = reg.takeEntity();
    reg.addComponent<SmallComp>(e, SmallComp{77});
    reg.addComponent<BigComp>(e, BigComp{1.0, 2.0, 3.0, 4.0});

    // Reverse order -- sizeof(SmallComp)=1, sizeof(BigComp)=32, very different offsets
    int count = 0;
    reg.view<BigComp, SmallComp>().each([&](BigComp& big, SmallComp& sm) {
        EXPECT_EQ(sm.v, 77);
        EXPECT_DOUBLE_EQ(big.a, 1.0);
        EXPECT_DOUBLE_EQ(big.d, 4.0);
        ++count;
    });
    EXPECT_EQ(count, 1);
}

// ============================================================================
// Bug #2: eachSingle/eachGrouped packed -- skip alive check
//
// Grouped sector exists but not all components added.
// each() must NOT call func for dead components.
// ============================================================================

TEST(LogicBug, EachSingle_SkipsDeadComponent) {
    Registry<false> reg;
    reg.registerArray<CompA, CompB>();

    auto e1 = reg.takeEntity();
    reg.addComponent<CompB>(e1, 10.0);  // only B, NOT A

    auto e2 = reg.takeEntity();
    reg.addComponent<CompA>(e2, 20.f);  // only A
    reg.addComponent<CompB>(e2, 30.0);

    // view<CompA> should see only e2 (e1 has no A)
    int count = 0;
    reg.view<CompA>().each([&](CompA& a) {
        EXPECT_FLOAT_EQ(a.x, 20.f);
        ++count;
    });
    EXPECT_EQ(count, 1) << "eachSingle visited entity without CompA alive";
}

TEST(LogicBug, EachGrouped_SkipsPartiallyAlive) {
    Registry<false> reg;
    reg.registerArray<CompA, CompB>();

    auto e1 = reg.takeEntity();
    reg.addComponent<CompA>(e1, 10.f);  // only A, NOT B

    auto e2 = reg.takeEntity();
    reg.addComponent<CompA>(e2, 20.f);
    reg.addComponent<CompB>(e2, 30.0);  // both A and B

    // view<CompA, CompB>.each should see only e2
    int count = 0;
    reg.view<CompA, CompB>().each([&](CompA& a, CompB& b) {
        EXPECT_FLOAT_EQ(a.x, 20.f);
        EXPECT_DOUBLE_EQ(b.y, 30.0);
        ++count;
    });
    EXPECT_EQ(count, 1) << "eachGrouped visited entity with dead CompB";
}

TEST(LogicBug, EachSingle_AfterDestroyComponent) {
    Registry<false> reg;

    auto e = reg.takeEntity();
    reg.addComponent<CompA>(e, 42.f);
    reg.destroyComponent<CompA>(e);

    // defragment hasn't run -> slot exists but CompA dead
    int count = 0;
    reg.view<CompA>().each([&](CompA&) { ++count; });
    EXPECT_EQ(count, 0) << "eachSingle visited destroyed component";
}

TEST(LogicBug, EachGrouped_ManyEntities_MixedAlive) {
    Registry<false> reg;
    reg.registerArray<CompA, CompB>();

    std::vector<EntityId> withBoth;
    for (int i = 0; i < 100; ++i) {
        auto e = reg.takeEntity();
        reg.addComponent<CompA>(e, float(i));
        if (i % 3 == 0) {
            reg.addComponent<CompB>(e, double(i));
            withBoth.push_back(e);
        }
    }

    int count = 0;
    reg.view<CompA, CompB>().each([&](CompA& a, CompB& b) {
        EXPECT_DOUBLE_EQ(b.y, double(a.x));
        ++count;
    });
    EXPECT_EQ(count, static_cast<int>(withBoth.size()));
}

// ============================================================================
// Bug #3: Ranged view with disjoint ranges visits entities in gaps
// ============================================================================

TEST(LogicBug, RangedView_DisjointRanges_NoGapEntities) {
    Registry<false> reg;

    // Create entities with known ids (0..29)
    for (int i = 0; i < 30; ++i) {
        auto e = reg.takeEntity();
        ASSERT_EQ(e, static_cast<EntityId>(i));
        reg.addComponent<CompA>(e, float(i));
    }

    // Request only [0,10) and [20,30) -- gap is [10,20)
    Ranges<EntityId> ranges(std::vector<Ranges<EntityId>::Range>{{0, 10}, {20, 30}});

    std::set<EntityId> visited;
    reg.view<CompA>(ranges).each([&](CompA& a) {
        auto id = static_cast<EntityId>(a.x);
        visited.insert(id);
    });

    // Should NOT contain any id from [10,20)
    for (EntityId id = 10; id < 20; ++id) {
        EXPECT_EQ(visited.count(id), 0u)
            << "Ranged view visited entity " << id << " which is in the gap between ranges";
    }

    // Should contain ids from [0,10) and [20,30)
    for (EntityId id = 0; id < 10; ++id) {
        EXPECT_EQ(visited.count(id), 1u) << "Missing entity " << id << " from first range";
    }
    for (EntityId id = 20; id < 30; ++id) {
        EXPECT_EQ(visited.count(id), 1u) << "Missing entity " << id << " from second range";
    }
}

TEST(LogicBug, RangedView_SingleRange_Correct) {
    Registry<false> reg;

    for (int i = 0; i < 20; ++i) {
        auto e = reg.takeEntity();
        reg.addComponent<CompA>(e, float(i));
    }

    Ranges<EntityId> ranges(std::vector<Ranges<EntityId>::Range>{{5, 15}});

    int count = 0;
    reg.view<CompA>(ranges).each([&](CompA& a) {
        auto id = static_cast<int>(a.x);
        EXPECT_GE(id, 5);
        EXPECT_LT(id, 15);
        ++count;
    });
    EXPECT_EQ(count, 10);
}

TEST(LogicBug, RangedView_EmptyRange) {
    Registry<false> reg;

    for (int i = 0; i < 10; ++i) {
        auto e = reg.takeEntity();
        reg.addComponent<CompA>(e, float(i));
    }

    Ranges<EntityId> ranges;  // empty
    int count = 0;
    reg.view<CompA>(ranges).each([&](CompA&) { ++count; });
    EXPECT_EQ(count, 0);
}

TEST(LogicBug, RangedView_ThreeDisjointRanges) {
    Registry<false> reg;

    for (int i = 0; i < 50; ++i) {
        auto e = reg.takeEntity();
        reg.addComponent<CompA>(e, float(i));
    }

    // Three ranges with gaps
    Ranges<EntityId> ranges(std::vector<Ranges<EntityId>::Range>{{0, 5}, {20, 25}, {40, 45}});

    std::vector<int> visited;
    reg.view<CompA>(ranges).each([&](CompA& a) {
        visited.push_back(static_cast<int>(a.x));
    });

    EXPECT_EQ(visited.size(), 15u);
    for (int id : visited) {
        bool inRange = (id >= 0 && id < 5) || (id >= 20 && id < 25) || (id >= 40 && id < 45);
        EXPECT_TRUE(inRange) << "Entity " << id << " is not in any requested range";
    }
}

// ============================================================================
// Bug #10: erase non-TS -- no bounds check
// ============================================================================

TEST(LogicBug, Erase_OutOfBounds_NonTS) {
    auto* arr = Memory::SectorsArray<false>::create<CompA>();
    arr->emplace<CompA>(0, CompA{1.f});
    arr->emplace<CompA>(1, CompA{2.f});
    arr->emplace<CompA>(2, CompA{3.f});
    ASSERT_EQ(arr->size(), 3u);

    // erase at index way past size -- should not crash
    arr->erase(999, 1, false);
    EXPECT_EQ(arr->size(), 3u) << "Size changed after out-of-bounds erase";

    arr->erase(3, 1, false);
    EXPECT_EQ(arr->size(), 3u);

    // valid erase still works
    arr->erase(0, 1, true);
    EXPECT_EQ(arr->size(), 2u);

    delete arr;
}

// ============================================================================
// Combined: ThreadSafe=true versions of the same tests
// ============================================================================

TEST(LogicBug, EachGrouped_OffsetOrder_ThreadSafe) {
    Registry<true> reg;
    reg.registerArray<CompA, CompB>();

    auto e = reg.takeEntity();
    reg.addComponent<CompA>(e, 42.f);
    reg.addComponent<CompB>(e, 99.0);

    int count = 0;
    reg.view<CompB, CompA>().each([&](CompB& b, CompA& a) {
        EXPECT_FLOAT_EQ(a.x, 42.f);
        EXPECT_DOUBLE_EQ(b.y, 99.0);
        ++count;
    });
    EXPECT_EQ(count, 1);
}

TEST(LogicBug, EachSingle_SkipsDeadComponent_ThreadSafe) {
    Registry<true> reg;
    reg.registerArray<CompA, CompB>();

    auto e1 = reg.takeEntity();
    reg.addComponent<CompB>(e1, 10.0);

    auto e2 = reg.takeEntity();
    reg.addComponent<CompA>(e2, 20.f);
    reg.addComponent<CompB>(e2, 30.0);

    int count = 0;
    reg.view<CompA>().each([&](CompA& a) {
        EXPECT_FLOAT_EQ(a.x, 20.f);
        ++count;
    });
    EXPECT_EQ(count, 1);
}

TEST(LogicBug, RangedView_DisjointRanges_ThreadSafe) {
    Registry<true> reg;

    for (int i = 0; i < 30; ++i) {
        auto e = reg.takeEntity();
        reg.addComponent<CompA>(e, float(i));
    }

    Ranges<EntityId> ranges(std::vector<Ranges<EntityId>::Range>{{0, 10}, {20, 30}});

    std::set<EntityId> visited;
    reg.view<CompA>(ranges).each([&](CompA& a) {
        visited.insert(static_cast<EntityId>(a.x));
    });

    for (EntityId id = 10; id < 20; ++id) {
        EXPECT_EQ(visited.count(id), 0u) << "Gap entity " << id << " visited";
    }
    EXPECT_EQ(visited.size(), 20u);
}

TEST(LogicBug, RangedView_DisjointRanges_IteratorPath) {
    Registry<false> reg;

    for (int i = 0; i < 30; ++i) {
        auto e = reg.takeEntity();
        reg.addComponent<CompA>(e, float(i));
    }

    Ranges<EntityId> ranges(std::vector<Ranges<EntityId>::Range>{{0, 10}, {20, 30}});

    std::set<EntityId> visited;
    for (auto [eid, p] : reg.view<CompA>(ranges)) {
        if (p) visited.insert(static_cast<EntityId>(p->x));
    }

    for (EntityId id = 10; id < 20; ++id) {
        EXPECT_EQ(visited.count(id), 0u)
            << "Range-for iterator visited gap entity " << id;
    }
    EXPECT_EQ(visited.size(), 20u);
}
