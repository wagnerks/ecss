#include <gtest/gtest.h>
#include <vector>
#include <set>
#include <string>
#include <memory>

#include <ecss/Registry.h>
#include <ecss/Ranges.h>
#include <ecss/memory/SectorsArray.h>
#include <ecss/memory/Sector.h>
#include <ecss/memory/RetireAllocator.h>
#include <ecss/memory/ChunksAllocator.h>

using namespace ecss;
using namespace ecss::Memory;

// Test components
struct Trivial1 { int x = 0; };
struct Trivial2 { float y = 0.f; };
struct Trivial3 { double z = 0.0; };
struct Big { double a = 0, b = 0, c = 0, d = 0; };
struct Tiny { uint8_t v = 0; };

struct NonTrivial {
    std::string s;
    NonTrivial() = default;
    explicit NonTrivial(std::string str) : s(std::move(str)) {}
    NonTrivial(const NonTrivial&) = default;
    NonTrivial(NonTrivial&&) = default;
    NonTrivial& operator=(const NonTrivial&) = default;
    NonTrivial& operator=(NonTrivial&&) = default;
    ~NonTrivial() = default;
};

struct MoveOnlyComp {
    int val;
    explicit MoveOnlyComp(int v = 0) : val(v) {}
    MoveOnlyComp(MoveOnlyComp&& o) noexcept : val(o.val) { o.val = -1; }
    MoveOnlyComp& operator=(MoveOnlyComp&& o) noexcept { val = o.val; o.val = -1; return *this; }
    MoveOnlyComp(const MoveOnlyComp&) = delete;
    MoveOnlyComp& operator=(const MoveOnlyComp&) = delete;
};

// ============================================================================
// Ranges.h (1-20)
// ============================================================================

TEST(Cov_Ranges, TakeFromEmpty) {
    Ranges<EntityId> r;
    auto id = r.take();
    EXPECT_EQ(id, 0u);
    EXPECT_TRUE(r.contains(0));
}

TEST(Cov_Ranges, TakeSequential) {
    Ranges<EntityId> r;
    for (uint32_t i = 0; i < 100; ++i) {
        EXPECT_EQ(r.take(), i);
    }
    EXPECT_EQ(r.size(), 1u);
    EXPECT_TRUE(r.contains(50));
}

TEST(Cov_Ranges, EraseAndTakeRecycles) {
    Ranges<EntityId> r;
    for (int i = 0; i < 5; ++i) r.take();
    r.erase(4);
    auto id = r.take();
    EXPECT_EQ(id, 4u);
}

TEST(Cov_Ranges, EraseMidSplitsRange) {
    Ranges<EntityId> r;
    for (int i = 0; i < 10; ++i) r.take();
    r.erase(5);
    EXPECT_FALSE(r.contains(5));
    EXPECT_TRUE(r.contains(4));
    EXPECT_TRUE(r.contains(6));
    EXPECT_EQ(r.size(), 2u);
}

TEST(Cov_Ranges, EraseFirstElement) {
    Ranges<EntityId> r;
    for (int i = 0; i < 5; ++i) r.take();
    r.erase(0);
    EXPECT_FALSE(r.contains(0));
    EXPECT_TRUE(r.contains(1));
}

TEST(Cov_Ranges, EraseLastElement) {
    Ranges<EntityId> r;
    for (int i = 0; i < 5; ++i) r.take();
    r.erase(4);
    EXPECT_FALSE(r.contains(4));
    EXPECT_TRUE(r.contains(3));
}

TEST(Cov_Ranges, EraseNonExistent) {
    Ranges<EntityId> r;
    for (int i = 0; i < 5; ++i) r.take();
    r.erase(999);
    EXPECT_EQ(r.size(), 1u);
}

TEST(Cov_Ranges, InsertCreatesNewRange) {
    Ranges<EntityId> r;
    r.insert(10);
    EXPECT_TRUE(r.contains(10));
    EXPECT_FALSE(r.contains(9));
    EXPECT_FALSE(r.contains(11));
}

TEST(Cov_Ranges, InsertAdjacentMerges) {
    Ranges<EntityId> r;
    r.insert(5);
    r.insert(6);
    r.insert(4);
    EXPECT_TRUE(r.contains(4));
    EXPECT_TRUE(r.contains(5));
    EXPECT_TRUE(r.contains(6));
    EXPECT_EQ(r.size(), 1u);
}

TEST(Cov_Ranges, InsertDuplicate) {
    Ranges<EntityId> r;
    r.insert(5);
    r.insert(5);
    EXPECT_TRUE(r.contains(5));
    EXPECT_EQ(r.size(), 1u);
}

TEST(Cov_Ranges, GetAllReturnsAllIds) {
    Ranges<EntityId> r;
    for (int i = 0; i < 10; ++i) r.take();
    r.erase(3);
    r.erase(7);
    auto all = r.getAll();
    EXPECT_EQ(all.size(), 8u);
    EXPECT_TRUE(std::find(all.begin(), all.end(), 3) == all.end());
    EXPECT_TRUE(std::find(all.begin(), all.end(), 7) == all.end());
}

TEST(Cov_Ranges, ContainsOutOfRange) {
    Ranges<EntityId> r;
    for (int i = 0; i < 5; ++i) r.take();
    EXPECT_FALSE(r.contains(5));
    EXPECT_FALSE(r.contains(1000));
}

TEST(Cov_Ranges, SortedVectorCtor_Duplicates) {
    std::vector<EntityId> ids = {1, 1, 2, 2, 3};
    Ranges<EntityId> r(ids);
    EXPECT_TRUE(r.contains(1));
    EXPECT_TRUE(r.contains(2));
    EXPECT_TRUE(r.contains(3));
    EXPECT_EQ(r.size(), 1u);
}

TEST(Cov_Ranges, SortedVectorCtor_Gaps) {
    std::vector<EntityId> ids = {0, 1, 5, 6, 10};
    Ranges<EntityId> r(ids);
    EXPECT_TRUE(r.contains(0));
    EXPECT_TRUE(r.contains(5));
    EXPECT_TRUE(r.contains(10));
    EXPECT_FALSE(r.contains(3));
    EXPECT_FALSE(r.contains(8));
}

TEST(Cov_Ranges, SortedVectorCtor_Single) {
    std::vector<EntityId> ids = {42};
    Ranges<EntityId> r(ids);
    EXPECT_TRUE(r.contains(42));
    EXPECT_EQ(r.size(), 1u);
}

TEST(Cov_Ranges, MergeIntersections) {
    Ranges<EntityId> r(std::vector<Ranges<EntityId>::Range>{{0, 5}, {3, 8}, {10, 15}});
    EXPECT_TRUE(r.contains(0));
    EXPECT_TRUE(r.contains(7));
    EXPECT_FALSE(r.contains(8));
    EXPECT_TRUE(r.contains(10));
}

TEST(Cov_Ranges, ClearAndEmpty) {
    Ranges<EntityId> r;
    for (int i = 0; i < 10; ++i) r.take();
    EXPECT_FALSE(r.empty());
    r.clear();
    EXPECT_TRUE(r.empty());
    EXPECT_EQ(r.size(), 0u);
}

TEST(Cov_Ranges, EraseAllOneByOne) {
    Ranges<EntityId> r;
    for (int i = 0; i < 5; ++i) r.take();
    for (int i = 0; i < 5; ++i) r.erase(i);
    EXPECT_TRUE(r.empty());
}

TEST(Cov_Ranges, InsertAfterClear) {
    Ranges<EntityId> r;
    r.take(); r.take();
    r.clear();
    r.insert(100);
    EXPECT_TRUE(r.contains(100));
    EXPECT_FALSE(r.contains(0));
}

TEST(Cov_Ranges, TakeMergesWithSecondRange) {
    Ranges<EntityId> r;
    for (int i = 0; i < 10; ++i) r.take();
    r.erase(5);
    // ranges: [0,5) [6,10)
    // take() returns 5 (front().second), merges with [6,10) -> [0,10)
    auto id = r.take();
    EXPECT_EQ(id, 5u);
    EXPECT_EQ(r.size(), 1u);
}

// ============================================================================
// RetireBin (21-30)
// ============================================================================

TEST(Cov_Retire, RetireAndTick) {
    RetireBin bin(2);
    void* p = std::malloc(64);
    bin.retire(p);
    EXPECT_EQ(bin.pendingCount(), 1u);
    bin.tick();
    EXPECT_EQ(bin.pendingCount(), 1u);
    bin.tick();
    EXPECT_EQ(bin.pendingCount(), 0u);
}

TEST(Cov_Retire, DrainAll) {
    RetireBin bin;
    for (int i = 0; i < 10; ++i) bin.retire(std::malloc(32));
    EXPECT_EQ(bin.pendingCount(), 10u);
    bin.drainAll();
    EXPECT_EQ(bin.pendingCount(), 0u);
}

TEST(Cov_Retire, GracePeriodZero) {
    RetireBin bin(0);
    bin.retire(std::malloc(16));
    auto freed = bin.tick();
    EXPECT_EQ(freed, 1u);
    EXPECT_EQ(bin.pendingCount(), 0u);
}

TEST(Cov_Retire, GracePeriodChange) {
    RetireBin bin(10);
    EXPECT_EQ(bin.getGracePeriod(), 10u);
    bin.setGracePeriod(1);
    EXPECT_EQ(bin.getGracePeriod(), 1u);
    bin.retire(std::malloc(16));
    bin.tick();
    EXPECT_EQ(bin.pendingCount(), 0u);
}

TEST(Cov_Retire, RetireNull) {
    RetireBin bin;
    bin.retire(nullptr);
    EXPECT_EQ(bin.pendingCount(), 0u);
}

TEST(Cov_Retire, MultipleTicks) {
    RetireBin bin(3);
    bin.retire(std::malloc(16));
    bin.retire(std::malloc(16));
    EXPECT_EQ(bin.tick(), 0u);
    EXPECT_EQ(bin.tick(), 0u);
    EXPECT_EQ(bin.tick(), 2u);
}

TEST(Cov_Retire, StaggeredRetire) {
    RetireBin bin(2);
    bin.retire(std::malloc(16));
    bin.tick();
    bin.retire(std::malloc(16));
    bin.tick();
    EXPECT_EQ(bin.pendingCount(), 1u);
    bin.tick();
    EXPECT_EQ(bin.pendingCount(), 0u);
}

TEST(Cov_Retire, AllocatorDeallocateRetires) {
    RetireBin bin(5);
    RetireAllocator<int> alloc(&bin);
    int* p = alloc.allocate(10);
    alloc.deallocate(p, 10);
    EXPECT_EQ(bin.pendingCount(), 1u);
}

TEST(Cov_Retire, AllocatorNullBin) {
    RetireAllocator<int> alloc(nullptr);
    int* p = alloc.allocate(5);
    ASSERT_NE(p, nullptr);
    alloc.deallocate(p, 5);
}

TEST(Cov_Retire, AllocatorEquality) {
    RetireBin b1, b2;
    RetireAllocator<int> a1(&b1), a2(&b2), a3(&b1);
    EXPECT_NE(a1, a2);
    EXPECT_EQ(a1, a3);
}

// ============================================================================
// SectorsArray basics -- non-TS (31-50)
// ============================================================================

TEST(Cov_SA, CreateEmpty) {
    std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<Trivial1>());
    EXPECT_EQ(arr->size(), 0u);
    EXPECT_TRUE(arr->empty());
}

TEST(Cov_SA, InsertAndFind) {
    std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<Trivial1>());
    arr->emplace<Trivial1>(10, Trivial1{42});
    EXPECT_EQ(arr->size(), 1u);
    EXPECT_TRUE(arr->containsSector(10));
    EXPECT_FALSE(arr->containsSector(11));
}

TEST(Cov_SA, InsertMultipleSorted) {
    std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<Trivial1>());
    arr->emplace<Trivial1>(5, Trivial1{5});
    arr->emplace<Trivial1>(3, Trivial1{3});
    arr->emplace<Trivial1>(8, Trivial1{8});
    EXPECT_EQ(arr->size(), 3u);
    EXPECT_EQ(arr->getId<false>(0), 3u);
    EXPECT_EQ(arr->getId<false>(1), 5u);
    EXPECT_EQ(arr->getId<false>(2), 8u);
}

TEST(Cov_SA, OverwriteExisting) {
    std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<Trivial1>());
    arr->emplace<Trivial1>(5, Trivial1{10});
    arr->emplace<Trivial1>(5, Trivial1{20});
    EXPECT_EQ(arr->size(), 1u);
    auto* data = arr->findSectorData(5);
    ASSERT_NE(data, nullptr);
    auto* comp = Sector::getComponent<Trivial1>(data, arr->getIsAlive(5), arr->getLayout());
    ASSERT_NE(comp, nullptr);
    EXPECT_EQ(comp->x, 20);
}

TEST(Cov_SA, EraseWithDefragment) {
    std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<Trivial1>());
    for (SectorId i = 0; i < 10; ++i) arr->emplace<Trivial1>(i, Trivial1{int(i)});
    arr->erase(3, 1, true);
    EXPECT_EQ(arr->size(), 9u);
    EXPECT_FALSE(arr->containsSector(3));
}

TEST(Cov_SA, EraseWithoutDefragment) {
    std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<Trivial1>());
    for (SectorId i = 0; i < 5; ++i) arr->emplace<Trivial1>(i, Trivial1{int(i)});
    arr->erase(2, 1, false);
    EXPECT_EQ(arr->size(), 5u);
    EXPECT_GT(arr->getDefragmentationSize(), 0u);
}

TEST(Cov_SA, ClearResetsSize) {
    std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<Trivial1>());
    for (SectorId i = 0; i < 20; ++i) arr->emplace<Trivial1>(i, Trivial1{int(i)});
    arr->clear();
    EXPECT_EQ(arr->size(), 0u);
    EXPECT_TRUE(arr->empty());
}

TEST(Cov_SA, ReserveIncreasesCap) {
    std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<Trivial1>());
    arr->reserve(1000);
    EXPECT_GE(arr->capacity(), 1000u);
}

TEST(Cov_SA, DefragmentCompacts) {
    std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<Trivial1>());
    for (SectorId i = 0; i < 10; ++i) arr->emplace<Trivial1>(i, Trivial1{int(i)});
    arr->erase(3, 1, false);
    arr->erase(7, 1, false);
    EXPECT_EQ(arr->getDefragmentationSize(), 2u);
    arr->defragment();
    EXPECT_EQ(arr->size(), 8u);
    EXPECT_EQ(arr->getDefragmentationSize(), 0u);
}

TEST(Cov_SA, NonTrivialInsertDestroy) {
    std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<NonTrivial>());
    arr->emplace<NonTrivial>(0, std::string("hello"));
    auto slot = arr->findSlot(0);
    ASSERT_TRUE(slot);
    auto* comp = Sector::getComponent<NonTrivial>(slot.data, arr->getIsAlive(0), arr->getLayout());
    ASSERT_NE(comp, nullptr);
    EXPECT_EQ(comp->s, "hello");
    arr->clear();
}

TEST(Cov_SA, CopySameType) {
    std::unique_ptr<SectorsArray<false>> a(SectorsArray<false>::create<Trivial1>());
    a->emplace<Trivial1>(0, Trivial1{42});
    a->emplace<Trivial1>(1, Trivial1{99});

    std::unique_ptr<SectorsArray<false>> b(SectorsArray<false>::create<Trivial1>());
    *b = *a;
    EXPECT_EQ(b->size(), 2u);
    auto* data = b->findSectorData(0);
    auto* comp = Sector::getComponent<Trivial1>(data, b->getIsAlive(0), b->getLayout());
    EXPECT_EQ(comp->x, 42);
}

TEST(Cov_SA, MoveSameType) {
    auto* raw = SectorsArray<false>::create<Trivial1>();
    raw->emplace<Trivial1>(0, Trivial1{42});

    std::unique_ptr<SectorsArray<false>> b(SectorsArray<false>::create<Trivial1>());
    *b = std::move(*raw);
    EXPECT_EQ(b->size(), 1u);
    EXPECT_EQ(raw->size(), 0u);
    delete raw;
}

TEST(Cov_SA, IteratorBasic) {
    std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<Trivial1>());
    for (SectorId i = 0; i < 5; ++i) arr->emplace<Trivial1>(i, Trivial1{int(i)});
    int count = 0;
    for (auto it = arr->begin(); it != arr->end(); ++it) ++count;
    EXPECT_EQ(count, 5);
}

TEST(Cov_SA, IteratorAliveSkipsDead) {
    std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<Trivial1>());
    for (SectorId i = 0; i < 5; ++i) arr->emplace<Trivial1>(i, Trivial1{int(i)});
    arr->erase(2, 1, false);
    int count = 0;
    auto it = arr->beginAlive<Trivial1>();
    auto e = arr->endAlive();
    for (; it != e; ++it) ++count;
    EXPECT_EQ(count, 4);
}

TEST(Cov_SA, ShrinkToFitReducesCap) {
    std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<Trivial1>());
    arr->reserve(10000);
    auto bigCap = arr->capacity();
    for (SectorId i = 0; i < 10; ++i) arr->emplace<Trivial1>(i, Trivial1{int(i)});
    arr->shrinkToFit();
    EXPECT_LE(arr->capacity(), bigCap);
}

TEST(Cov_SA, GroupedTwoTypes) {
    std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<Trivial1, Trivial2>());
    arr->emplace<Trivial1>(0, Trivial1{1});
    arr->emplace<Trivial2>(0, Trivial2{2.f});
    auto isAlive = arr->getIsAlive(0);
    auto* t1 = Sector::getComponent<Trivial1>(arr->findSectorData(0), isAlive, arr->getLayout());
    auto* t2 = Sector::getComponent<Trivial2>(arr->findSectorData(0), isAlive, arr->getLayout());
    ASSERT_NE(t1, nullptr);
    ASSERT_NE(t2, nullptr);
    EXPECT_EQ(t1->x, 1);
    EXPECT_FLOAT_EQ(t2->y, 2.f);
}

TEST(Cov_SA, DestroyMemberPartial) {
    std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<Trivial1, Trivial2>());
    arr->emplace<Trivial1>(0, Trivial1{1});
    arr->emplace<Trivial2>(0, Trivial2{2.f});
    auto& isAlive = arr->getIsAliveRef<false>(arr->findSlot(0).linearIdx);
    Sector::destroyMember(arr->findSectorData(0), isAlive, arr->getLayoutData<Trivial1>());
    auto* t1 = Sector::getComponent<Trivial1>(arr->findSectorData(0), isAlive, arr->getLayout());
    auto* t2 = Sector::getComponent<Trivial2>(arr->findSectorData(0), isAlive, arr->getLayout());
    EXPECT_EQ(t1, nullptr);
    ASSERT_NE(t2, nullptr);
    EXPECT_FLOAT_EQ(t2->y, 2.f);
}

TEST(Cov_SA, NonTrivialCopy) {
    std::unique_ptr<SectorsArray<false>> a(SectorsArray<false>::create<NonTrivial>());
    a->emplace<NonTrivial>(0, std::string("test_string_long_enough_no_sso"));
    std::unique_ptr<SectorsArray<false>> b(SectorsArray<false>::create<NonTrivial>());
    *b = *a;
    auto* comp = Sector::getComponent<NonTrivial>(b->findSectorData(0), b->getIsAlive(0), b->getLayout());
    ASSERT_NE(comp, nullptr);
    EXPECT_EQ(comp->s, "test_string_long_enough_no_sso");
}

TEST(Cov_SA, NonTrivialDefragment) {
    std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<NonTrivial>());
    for (SectorId i = 0; i < 10; ++i) arr->emplace<NonTrivial>(i, "str_" + std::to_string(i));
    arr->erase(3, 1, false);
    arr->erase(7, 1, false);
    arr->defragment();
    EXPECT_EQ(arr->size(), 8u);
    for (auto it = arr->beginAlive<NonTrivial>(); it != arr->endAlive(); ++it) {
        auto slot = *it;
        auto* comp = Sector::getComponent<NonTrivial>(slot.data, slot.isAlive, arr->getLayout());
        ASSERT_NE(comp, nullptr);
        EXPECT_FALSE(comp->s.empty());
    }
}

TEST(Cov_SA, MoveOnlyInsert) {
    std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<MoveOnlyComp>());
    arr->emplace<MoveOnlyComp>(0, 42);
    auto* data = arr->findSectorData(0);
    auto* comp = Sector::getComponent<MoveOnlyComp>(data, arr->getIsAlive(0), arr->getLayout());
    ASSERT_NE(comp, nullptr);
    EXPECT_EQ(comp->val, 42);
}

// ============================================================================
// Registry (51-80)
// ============================================================================

TEST(Cov_Reg, ViewEmptyRegistry) {
    Registry<false> reg;
    int count = 0;
    reg.view<Trivial1>().each([&](Trivial1&) { ++count; });
    EXPECT_EQ(count, 0);
}

TEST(Cov_Reg, ViewAfterClear) {
    Registry<false> reg;
    for (int i = 0; i < 10; ++i) {
        auto e = reg.takeEntity();
        reg.addComponent<Trivial1>(e, i);
    }
    reg.clear();
    int count = 0;
    reg.view<Trivial1>().each([&](Trivial1&) { ++count; });
    EXPECT_EQ(count, 0);
}

TEST(Cov_Reg, ViewWrongComponent) {
    Registry<false> reg;
    auto e = reg.takeEntity();
    reg.addComponent<Trivial1>(e, 1);
    int count = 0;
    for (auto [eid, p] : reg.view<Trivial2>()) { if (p) ++count; }
    EXPECT_EQ(count, 0);
}

TEST(Cov_Reg, DestroyEntitiesEmpty) {
    Registry<false> reg;
    std::vector<EntityId> empty;
    reg.destroyEntities(empty);
}

TEST(Cov_Reg, DestroyEntitiesDuplicates) {
    Registry<false> reg;
    auto e = reg.takeEntity();
    reg.addComponent<Trivial1>(e, 42);
    std::vector<EntityId> ids = {e, e, e};
    reg.destroyEntities(ids);
    EXPECT_FALSE(reg.contains(e));
}

TEST(Cov_Reg, ContainsAfterDestroy) {
    Registry<false> reg;
    auto e = reg.takeEntity();
    EXPECT_TRUE(reg.contains(e));
    reg.destroyEntity(e);
    EXPECT_FALSE(reg.contains(e));
}

TEST(Cov_Reg, HasComponentFalseForWrongType) {
    Registry<false> reg;
    auto e = reg.takeEntity();
    reg.addComponent<Trivial1>(e, 1);
    EXPECT_FALSE(reg.hasComponent<Trivial2>(e));
}

TEST(Cov_Reg, AddComponentOverwrites) {
    Registry<false> reg;
    auto e = reg.takeEntity();
    reg.addComponent<Trivial1>(e, 10);
    reg.addComponent<Trivial1>(e, 20);
    auto container = reg.getComponentContainer<Trivial1>();
    auto slot = container->findSlot(e);
    ASSERT_TRUE(slot);
    auto* comp = Sector::getComponent<Trivial1>(slot.data, container->getIsAlive(e), container->getLayout());
    EXPECT_EQ(comp->x, 20);
}

TEST(Cov_Reg, RegisterArrayGrouped) {
    Registry<false> reg;
    reg.registerArray<Trivial1, Trivial2>();
    auto* c1 = reg.getComponentContainer<Trivial1>();
    auto* c2 = reg.getComponentContainer<Trivial2>();
    EXPECT_EQ(c1, c2);
}

TEST(Cov_Reg, RegisterArrayTwiceIdempotent) {
    Registry<false> reg;
    reg.registerArray<Trivial1, Trivial2>();
    reg.registerArray<Trivial1, Trivial2>();
    EXPECT_EQ(reg.getComponentContainer<Trivial1>(), reg.getComponentContainer<Trivial2>());
}

TEST(Cov_Reg, DestroyComponentPartial) {
    Registry<false> reg;
    reg.registerArray<Trivial1, Trivial2>();
    auto e = reg.takeEntity();
    reg.addComponent<Trivial1>(e, 1);
    reg.addComponent<Trivial2>(e, 2.f);
    reg.destroyComponent<Trivial1>(e);
    EXPECT_FALSE(reg.hasComponent<Trivial1>(e));
    EXPECT_TRUE(reg.hasComponent<Trivial2>(e));
}

TEST(Cov_Reg, ViewIteratorTuple) {
    Registry<false> reg;
    auto e1 = reg.takeEntity();
    reg.addComponent<Trivial1>(e1, 10);
    auto e2 = reg.takeEntity();
    reg.addComponent<Trivial1>(e2, 20);

    std::vector<int> vals;
    for (auto [eid, t] : reg.view<Trivial1>()) {
        if (t) vals.push_back(t->x);
    }
    ASSERT_EQ(vals.size(), 2u);
    EXPECT_EQ(vals[0], 10);
    EXPECT_EQ(vals[1], 20);
}

TEST(Cov_Reg, ViewTwoComponentsDifferentArrays) {
    Registry<false> reg;
    auto e = reg.takeEntity();
    reg.addComponent<Trivial1>(e, 1);
    reg.addComponent<Trivial2>(e, 2.f);
    int count = 0;
    for (auto [eid, t1, t2] : reg.view<Trivial1, Trivial2>()) {
        if (t1 && t2) {
            EXPECT_EQ(t1->x, 1);
            EXPECT_FLOAT_EQ(t2->y, 2.f);
            ++count;
        }
    }
    EXPECT_EQ(count, 1);
}

TEST(Cov_Reg, ViewMissingSecondary) {
    Registry<false> reg;
    auto e1 = reg.takeEntity();
    reg.addComponent<Trivial1>(e1, 1);
    auto e2 = reg.takeEntity();
    reg.addComponent<Trivial1>(e2, 2);
    reg.addComponent<Trivial2>(e2, 3.f);

    int countBoth = 0;
    for (auto [eid, t1, t2] : reg.view<Trivial1, Trivial2>()) {
        if (t1 && t2) ++countBoth;
    }
    EXPECT_EQ(countBoth, 1);
}

TEST(Cov_Reg, DefragmentAfterDestroys) {
    Registry<false> reg;
    std::vector<EntityId> ids;
    for (int i = 0; i < 100; ++i) {
        auto e = reg.takeEntity();
        reg.addComponent<Trivial1>(e, i);
        ids.push_back(e);
    }
    for (int i = 0; i < 50; i += 2) reg.destroyComponent<Trivial1>(ids[i]);
    reg.update(true);
    int count = 0;
    reg.view<Trivial1>().each([&](Trivial1&) { ++count; });
    EXPECT_EQ(count, 75);
}

TEST(Cov_Reg, GetAllEntities) {
    Registry<false> reg;
    for (int i = 0; i < 5; ++i) reg.takeEntity();
    auto all = reg.getAllEntities();
    EXPECT_EQ(all.size(), 5u);
}

TEST(Cov_Reg, NonTrivialComponentLifecycle) {
    Registry<false> reg;
    auto e = reg.takeEntity();
    reg.addComponent<NonTrivial>(e, std::string("lifecycle_test"));
    reg.destroyEntity(e);
}

TEST(Cov_Reg, ManyEntitiesCreateDestroy) {
    Registry<false> reg;
    reg.reserve<Trivial1>(10000);
    for (int i = 0; i < 10000; ++i) {
        auto e = reg.takeEntity();
        reg.addComponent<Trivial1>(e, i);
    }
    int count = 0;
    reg.view<Trivial1>().each([&](Trivial1&) { ++count; });
    EXPECT_EQ(count, 10000);
}

TEST(Cov_Reg, ReserveDoesNotAffectSize) {
    Registry<false> reg;
    reg.reserve<Trivial1>(10000);
    EXPECT_EQ(reg.getComponentContainer<Trivial1>()->size(), 0u);
}

TEST(Cov_Reg, DestroyEntityTwice) {
    Registry<false> reg;
    auto e = reg.takeEntity();
    reg.addComponent<Trivial1>(e, 1);
    reg.destroyEntity(e);
    reg.destroyEntity(e);
    EXPECT_FALSE(reg.contains(e));
}

TEST(Cov_Reg, ViewEachSingleCount) {
    Registry<false> reg;
    for (int i = 0; i < 50; ++i) {
        auto e = reg.takeEntity();
        reg.addComponent<Trivial1>(e, i);
    }
    int count = 0;
    reg.view<Trivial1>().each([&](Trivial1&) { ++count; });
    EXPECT_EQ(count, 50);
}

TEST(Cov_Reg, ViewEachGroupedCount) {
    Registry<false> reg;
    reg.registerArray<Trivial1, Trivial2>();
    for (int i = 0; i < 50; ++i) {
        auto e = reg.takeEntity();
        reg.addComponent<Trivial1>(e, i);
        reg.addComponent<Trivial2>(e, float(i));
    }
    int count = 0;
    reg.view<Trivial1, Trivial2>().each([&](Trivial1&, Trivial2&) { ++count; });
    EXPECT_EQ(count, 50);
}

TEST(Cov_Reg, DestroyComponentBatch) {
    Registry<false> reg;
    std::vector<EntityId> ids;
    for (int i = 0; i < 20; ++i) {
        auto e = reg.takeEntity();
        reg.addComponent<Trivial1>(e, i);
        ids.push_back(e);
    }
    std::vector<EntityId> toDestroy(ids.begin(), ids.begin() + 10);
    reg.destroyComponent<Trivial1>(toDestroy);
    int count = 0;
    reg.view<Trivial1>().each([&](Trivial1&) { ++count; });
    EXPECT_EQ(count, 10);
}

TEST(Cov_Reg, ViewEmptyCheck) {
    Registry<false> reg;
    EXPECT_TRUE(reg.view<Trivial1>().empty());
    auto e = reg.takeEntity();
    reg.addComponent<Trivial1>(e, 1);
    EXPECT_FALSE(reg.view<Trivial1>().empty());
}

TEST(Cov_Reg, ThreeComponentView) {
    Registry<false> reg;
    auto e = reg.takeEntity();
    reg.addComponent<Trivial1>(e, 1);
    reg.addComponent<Trivial2>(e, 2.f);
    reg.addComponent<Trivial3>(e, 3.0);
    int count = 0;
    for (auto [eid, t1, t2, t3] : reg.view<Trivial1, Trivial2, Trivial3>()) {
        if (t1 && t2 && t3) {
            EXPECT_EQ(t1->x, 1);
            EXPECT_FLOAT_EQ(t2->y, 2.f);
            EXPECT_DOUBLE_EQ(t3->z, 3.0);
            ++count;
        }
    }
    EXPECT_EQ(count, 1);
}

// ============================================================================
// Registry ThreadSafe=true (81-90)
// ============================================================================

TEST(Cov_RegTS, BasicCreateAndView) {
    Registry<true> reg;
    auto e = reg.takeEntity();
    reg.addComponent<Trivial1>(e, 42);
    auto pin = reg.pinComponent<Trivial1>(e);
    ASSERT_TRUE(pin);
    EXPECT_EQ(pin->x, 42);
}

TEST(Cov_RegTS, PinNonExistent) {
    Registry<true> reg;
    auto e = reg.takeEntity();
    auto pin = reg.pinComponent<Trivial1>(e);
    EXPECT_FALSE(pin);
}

TEST(Cov_RegTS, ViewEach) {
    Registry<true> reg;
    for (int i = 0; i < 20; ++i) {
        auto e = reg.takeEntity();
        reg.addComponent<Trivial1>(e, i);
    }
    int sum = 0;
    reg.view<Trivial1>().each([&](Trivial1& t) { sum += t.x; });
    EXPECT_EQ(sum, 190);
}

TEST(Cov_RegTS, UpdateProcessesPending) {
    Registry<true> reg;
    auto e = reg.takeEntity();
    reg.addComponent<Trivial1>(e, 1);
    {
        auto pin = reg.pinComponent<Trivial1>(e);
        reg.getComponentContainer<Trivial1>()->eraseAsync(e);
    }
    reg.update(true);
    auto pin2 = reg.pinComponent<Trivial1>(e);
    EXPECT_FALSE(pin2);
}

TEST(Cov_RegTS, TickFreesMemory) {
    Registry<true> reg;
    reg.reserve<Trivial1>(100);
    for (int i = 0; i < 100; ++i) {
        auto e = reg.takeEntity();
        reg.addComponent<Trivial1>(e, i);
    }
    reg.tick();
}

TEST(Cov_RegTS, ClearAndReuse) {
    Registry<true> reg;
    for (int i = 0; i < 10; ++i) {
        auto e = reg.takeEntity();
        reg.addComponent<Trivial1>(e, i);
    }
    reg.clear();
    auto e = reg.takeEntity();
    reg.addComponent<Trivial1>(e, 999);
    auto pin = reg.pinComponent<Trivial1>(e);
    ASSERT_TRUE(pin);
    EXPECT_EQ(pin->x, 999);
}

TEST(Cov_RegTS, ForEachAsyncEmpty) {
    Registry<true> reg;
    std::vector<EntityId> empty;
    reg.forEachAsync<Trivial1>(empty, [](EntityId, Trivial1*) {
        FAIL() << "Should not be called";
    });
}

TEST(Cov_RegTS, ForEachAsyncDestroyedEntities) {
    Registry<true> reg;
    auto e = reg.takeEntity();
    reg.addComponent<Trivial1>(e, 42);
    reg.destroyEntity(e);
    std::vector<EntityId> ids = {e};
    int count = 0;
    reg.forEachAsync<Trivial1>(ids, [&](EntityId, Trivial1* t) {
        if (t) ++count;
    });
    EXPECT_EQ(count, 0);
}

TEST(Cov_RegTS, AddComponentsBulk) {
    Registry<true> reg;
    std::vector<EntityId> ids;
    for (int i = 0; i < 10; ++i) ids.push_back(reg.takeEntity());
    int idx = 0;
    reg.addComponents<Trivial1>([&]() -> std::pair<EntityId, Trivial1> {
        if (idx >= 10) return {INVALID_ID, {}};
        return {ids[idx++], Trivial1{idx}};
    });
    int count = 0;
    reg.view<Trivial1>().each([&](Trivial1&) { ++count; });
    EXPECT_EQ(count, 10);
}

TEST(Cov_RegTS, SetDefragmentThreshold) {
    Registry<true> reg;
    auto e = reg.takeEntity();
    reg.addComponent<Trivial1>(e, 1);
    reg.setDefragmentThreshold<Trivial1>(0.5f);
}

// ============================================================================
// Ranged view edge cases (91-97)
// ============================================================================

TEST(Cov_RangedView, RangeNoOverlapWithEntities) {
    Registry<false> reg;
    for (int i = 0; i < 10; ++i) {
        auto e = reg.takeEntity();
        reg.addComponent<Trivial1>(e, i);
    }
    Ranges<EntityId> ranges(std::vector<Ranges<EntityId>::Range>{{100, 200}});
    int count = 0;
    reg.view<Trivial1>(ranges).each([&](Trivial1&) { ++count; });
    EXPECT_EQ(count, 0);
}

TEST(Cov_RangedView, RangeCoversAll) {
    Registry<false> reg;
    for (int i = 0; i < 10; ++i) {
        auto e = reg.takeEntity();
        reg.addComponent<Trivial1>(e, i);
    }
    Ranges<EntityId> ranges(std::vector<Ranges<EntityId>::Range>{{0, 10}});
    int count = 0;
    reg.view<Trivial1>(ranges).each([&](Trivial1&) { ++count; });
    EXPECT_EQ(count, 10);
}

TEST(Cov_RangedView, RangePartial) {
    Registry<false> reg;
    for (int i = 0; i < 20; ++i) {
        auto e = reg.takeEntity();
        reg.addComponent<Trivial1>(e, i);
    }
    Ranges<EntityId> ranges(std::vector<Ranges<EntityId>::Range>{{5, 15}});
    int count = 0;
    reg.view<Trivial1>(ranges).each([&](Trivial1& t) {
        EXPECT_GE(t.x, 5);
        EXPECT_LT(t.x, 15);
        ++count;
    });
    EXPECT_EQ(count, 10);
}

TEST(Cov_RangedView, RangeWithDeadEntities) {
    Registry<false> reg;
    std::vector<EntityId> ids;
    for (int i = 0; i < 20; ++i) {
        auto e = reg.takeEntity();
        reg.addComponent<Trivial1>(e, i);
        ids.push_back(e);
    }
    reg.destroyComponent<Trivial1>(ids[7]);
    reg.destroyComponent<Trivial1>(ids[12]);

    Ranges<EntityId> ranges(std::vector<Ranges<EntityId>::Range>{{5, 15}});
    int count = 0;
    reg.view<Trivial1>(ranges).each([&](Trivial1&) { ++count; });
    EXPECT_EQ(count, 8);
}

TEST(Cov_RangedView, MultiComponentRanged) {
    Registry<false> reg;
    for (int i = 0; i < 20; ++i) {
        auto e = reg.takeEntity();
        reg.addComponent<Trivial1>(e, i);
        if (i % 2 == 0) reg.addComponent<Trivial2>(e, float(i));
    }
    Ranges<EntityId> ranges(std::vector<Ranges<EntityId>::Range>{{0, 10}});
    int count = 0;
    reg.view<Trivial1, Trivial2>(ranges).each([&](Trivial1&, Trivial2&) { ++count; });
    EXPECT_EQ(count, 5);
}

TEST(Cov_RangedView, RangedViewEmpty) {
    Registry<false> reg;
    Ranges<EntityId> ranges(std::vector<Ranges<EntityId>::Range>{{0, 100}});
    EXPECT_TRUE(reg.view<Trivial1>(ranges).empty());
}

TEST(Cov_RangedView, RangedViewThreadSafe) {
    Registry<true> reg;
    for (int i = 0; i < 30; ++i) {
        auto e = reg.takeEntity();
        reg.addComponent<Trivial1>(e, i);
    }
    Ranges<EntityId> ranges(std::vector<Ranges<EntityId>::Range>{{10, 20}});
    int count = 0;
    reg.view<Trivial1>(ranges).each([&](Trivial1& t) {
        EXPECT_GE(t.x, 10);
        EXPECT_LT(t.x, 20);
        ++count;
    });
    EXPECT_EQ(count, 10);
}

// ============================================================================
// Misc edge cases (98-100)
// ============================================================================

TEST(Cov_Misc, DestroyNonExistentEntity) {
    Registry<false> reg;
    reg.destroyEntity(999);
}

TEST(Cov_Misc, DestroyComponentNonExistentEntity) {
    Registry<false> reg;
    reg.destroyComponent<Trivial1>(999);
}

TEST(Cov_Misc, HasComponentNonExistentEntity) {
    Registry<false> reg;
    EXPECT_FALSE(reg.hasComponent<Trivial1>(999));
}

TEST(Cov_Misc, ViewAfterDestroyAllEntities) {
    Registry<false> reg;
    std::vector<EntityId> ids;
    for (int i = 0; i < 10; ++i) {
        auto e = reg.takeEntity();
        reg.addComponent<Trivial1>(e, i);
        ids.push_back(e);
    }
    for (auto id : ids) reg.destroyEntity(id);
    reg.update(true);
    int count = 0;
    reg.view<Trivial1>().each([&](Trivial1&) { ++count; });
    EXPECT_EQ(count, 0);
}

TEST(Cov_Misc, InsertMoveComponent) {
    Registry<false> reg;
    auto e = reg.takeEntity();
    reg.addComponent<MoveOnlyComp>(e, 42);
    auto container = reg.getComponentContainer<MoveOnlyComp>();
    auto slot = container->findSlot(e);
    ASSERT_TRUE(slot);
    auto* comp = Sector::getComponent<MoveOnlyComp>(slot.data, container->getIsAlive(e), container->getLayout());
    ASSERT_NE(comp, nullptr);
    EXPECT_EQ(comp->val, 42);
}

TEST(Cov_Misc, MultipleRegistriesIndependent) {
    Registry<false> r1, r2;
    auto e1 = r1.takeEntity();
    auto e2 = r2.takeEntity();
    r1.addComponent<Trivial1>(e1, 1);
    r2.addComponent<Trivial1>(e2, 2);
    int sum1 = 0, sum2 = 0;
    r1.view<Trivial1>().each([&](Trivial1& t) { sum1 += t.x; });
    r2.view<Trivial1>().each([&](Trivial1& t) { sum2 += t.x; });
    EXPECT_EQ(sum1, 1);
    EXPECT_EQ(sum2, 2);
}

TEST(Cov_Misc, LargeEntityIds) {
    Registry<false> reg;
    reg.reserve<Trivial1>(100);
    for (int i = 0; i < 100; ++i) reg.takeEntity();
    for (int i = 0; i < 50; ++i) reg.destroyEntity(i);
    // ids 50-99 alive
    int count = 0;
    for (auto [eid, t] : reg.view<Trivial1>()) { (void)eid; (void)t; ++count; }
    EXPECT_EQ(count, 0);
    // add components to remaining
    for (int i = 50; i < 100; ++i) reg.addComponent<Trivial1>(i, i);
    count = 0;
    reg.view<Trivial1>().each([&](Trivial1&) { ++count; });
    EXPECT_EQ(count, 50);
}

TEST(Cov_Misc, ViewEachGroupedThreeTypes) {
    Registry<false> reg;
    reg.registerArray<Trivial1, Trivial2, Trivial3>();
    for (int i = 0; i < 20; ++i) {
        auto e = reg.takeEntity();
        reg.addComponent<Trivial1>(e, i);
        reg.addComponent<Trivial2>(e, float(i));
        reg.addComponent<Trivial3>(e, double(i));
    }
    int count = 0;
    reg.view<Trivial3, Trivial1, Trivial2>().each([&](Trivial3& t3, Trivial1& t1, Trivial2& t2) {
        EXPECT_DOUBLE_EQ(t3.z, double(t1.x));
        EXPECT_FLOAT_EQ(t2.y, float(t1.x));
        ++count;
    });
    EXPECT_EQ(count, 20);
}
