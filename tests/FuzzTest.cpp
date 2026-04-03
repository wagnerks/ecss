#include <gtest/gtest.h>
#include <random>
#include <vector>
#include <set>

#include <ecss/Registry.h>

using namespace ecss;

struct FuzzA { int x = 0; };
struct FuzzB { float y = 0.f; };
struct FuzzC { double z = 0.0; };

enum class Op {
    TakeEntity, DestroyEntity, AddA, AddB, AddC,
    DestroyA, DestroyB, DestroyC,
    ViewA, ViewAB, ViewABC,
    EachA, EachAB,
    Update, Defragment, Clear,
    COUNT
};

static void runFuzz(uint32_t seed, int ops, bool threadSafe) {
    std::mt19937 rng(seed);
    auto randOp = std::uniform_int_distribution<int>(0, static_cast<int>(Op::COUNT) - 1);
    auto randVal = std::uniform_int_distribution<int>(0, 9999);

    std::vector<EntityId> alive;
    alive.reserve(1024);

    auto pickAlive = [&]() -> EntityId {
        if (alive.empty()) return INVALID_ID;
        return alive[rng() % alive.size()];
    };

    auto removeFromAlive = [&](EntityId id) {
        alive.erase(std::remove(alive.begin(), alive.end(), id), alive.end());
    };

    if (threadSafe) {
        Registry<true> reg;
        reg.registerArray<FuzzA, FuzzB>();
        reg.reserve<FuzzA>(512);
        reg.reserve<FuzzC>(512);

        for (int i = 0; i < ops; ++i) {
            auto op = static_cast<Op>(randOp(rng));
            switch (op) {
            case Op::TakeEntity: {
                auto e = reg.takeEntity();
                alive.push_back(e);
                break;
            }
            case Op::DestroyEntity: {
                auto e = pickAlive();
                if (e != INVALID_ID) { reg.destroyEntity(e); removeFromAlive(e); }
                break;
            }
            case Op::AddA: { auto e = pickAlive(); if (e != INVALID_ID) reg.addComponent<FuzzA>(e, randVal(rng)); break; }
            case Op::AddB: { auto e = pickAlive(); if (e != INVALID_ID) reg.addComponent<FuzzB>(e, float(randVal(rng))); break; }
            case Op::AddC: { auto e = pickAlive(); if (e != INVALID_ID) reg.addComponent<FuzzC>(e, double(randVal(rng))); break; }
            case Op::DestroyA: { auto e = pickAlive(); if (e != INVALID_ID) reg.destroyComponent<FuzzA>(e); break; }
            case Op::DestroyB: { auto e = pickAlive(); if (e != INVALID_ID) reg.destroyComponent<FuzzB>(e); break; }
            case Op::DestroyC: { auto e = pickAlive(); if (e != INVALID_ID) reg.destroyComponent<FuzzC>(e); break; }
            case Op::ViewA: {
                for (auto [eid, a] : reg.view<FuzzA>()) { if (a) (void)a->x; }
                break;
            }
            case Op::ViewAB: {
                for (auto [eid, a, b] : reg.view<FuzzA, FuzzB>()) { if (a && b) (void)(a->x + b->y); }
                break;
            }
            case Op::ViewABC: {
                for (auto [eid, a, b, c] : reg.view<FuzzA, FuzzB, FuzzC>()) { if (a && b && c) (void)(a->x + b->y + c->z); }
                break;
            }
            case Op::EachA: {
                reg.view<FuzzA>().each([](FuzzA& a) { (void)a.x; });
                break;
            }
            case Op::EachAB: {
                reg.view<FuzzA, FuzzB>().each([](FuzzA& a, FuzzB& b) { (void)(a.x + b.y); });
                break;
            }
            case Op::Update: reg.update(true); break;
            case Op::Defragment: reg.defragment(); break;
            case Op::Clear: reg.clear(); alive.clear(); break;
            default: break;
            }
        }
    } else {
        Registry<false> reg;
        reg.registerArray<FuzzA, FuzzB>();
        reg.reserve<FuzzA>(512);
        reg.reserve<FuzzC>(512);

        for (int i = 0; i < ops; ++i) {
            auto op = static_cast<Op>(randOp(rng));
            switch (op) {
            case Op::TakeEntity: {
                auto e = reg.takeEntity();
                alive.push_back(e);
                break;
            }
            case Op::DestroyEntity: {
                auto e = pickAlive();
                if (e != INVALID_ID) { reg.destroyEntity(e); removeFromAlive(e); }
                break;
            }
            case Op::AddA: { auto e = pickAlive(); if (e != INVALID_ID) reg.addComponent<FuzzA>(e, randVal(rng)); break; }
            case Op::AddB: { auto e = pickAlive(); if (e != INVALID_ID) reg.addComponent<FuzzB>(e, float(randVal(rng))); break; }
            case Op::AddC: { auto e = pickAlive(); if (e != INVALID_ID) reg.addComponent<FuzzC>(e, double(randVal(rng))); break; }
            case Op::DestroyA: { auto e = pickAlive(); if (e != INVALID_ID) reg.destroyComponent<FuzzA>(e); break; }
            case Op::DestroyB: { auto e = pickAlive(); if (e != INVALID_ID) reg.destroyComponent<FuzzB>(e); break; }
            case Op::DestroyC: { auto e = pickAlive(); if (e != INVALID_ID) reg.destroyComponent<FuzzC>(e); break; }
            case Op::ViewA: {
                for (auto [eid, a] : reg.view<FuzzA>()) { if (a) (void)a->x; }
                break;
            }
            case Op::ViewAB: {
                for (auto [eid, a, b] : reg.view<FuzzA, FuzzB>()) { if (a && b) (void)(a->x + b->y); }
                break;
            }
            case Op::ViewABC: {
                for (auto [eid, a, b, c] : reg.view<FuzzA, FuzzB, FuzzC>()) { if (a && b && c) (void)(a->x + b->y + c->z); }
                break;
            }
            case Op::EachA: {
                reg.view<FuzzA>().each([](FuzzA& a) { (void)a.x; });
                break;
            }
            case Op::EachAB: {
                reg.view<FuzzA, FuzzB>().each([](FuzzA& a, FuzzB& b) { (void)(a.x + b.y); });
                break;
            }
            case Op::Update: reg.update(true); break;
            case Op::Defragment: reg.defragment(); break;
            case Op::Clear: reg.clear(); alive.clear(); break;
            default: break;
            }
        }
    }
}

TEST(Fuzz, NonTS_Seed0)    { runFuzz(0, 10000, false); }
TEST(Fuzz, NonTS_Seed1)    { runFuzz(1, 10000, false); }
TEST(Fuzz, NonTS_Seed42)   { runFuzz(42, 10000, false); }
TEST(Fuzz, NonTS_Seed123)  { runFuzz(123, 10000, false); }
TEST(Fuzz, NonTS_Seed999)  { runFuzz(999, 10000, false); }
TEST(Fuzz, NonTS_Seed7777) { runFuzz(7777, 10000, false); }
TEST(Fuzz, NonTS_Large)    { runFuzz(314159, 50000, false); }

TEST(Fuzz, TS_Seed0)    { runFuzz(0, 10000, true); }
TEST(Fuzz, TS_Seed1)    { runFuzz(1, 10000, true); }
TEST(Fuzz, TS_Seed42)   { runFuzz(42, 10000, true); }
TEST(Fuzz, TS_Seed123)  { runFuzz(123, 10000, true); }
TEST(Fuzz, TS_Seed999)  { runFuzz(999, 10000, true); }
TEST(Fuzz, TS_Seed7777) { runFuzz(7777, 10000, true); }
TEST(Fuzz, TS_Large)    { runFuzz(314159, 50000, true); }
