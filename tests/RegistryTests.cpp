#include <random>
#include <unordered_set>

#include <ecss/Registry.h>

#include <gtest/gtest.h>

namespace RegistryTests {
	using namespace ecss;

	struct Position { float x, y; };
	struct Velocity { float dx, dy; };
	struct Health { int value; };

	struct A { int a; };
	struct B { float b; };

	struct MoveOnly {
	    int val;
	    MoveOnly(int v) : val(v) {}
	    MoveOnly(MoveOnly&& o) noexcept : val(o.val) { o.val = -1; }
	    MoveOnly& operator=(MoveOnly&& o) noexcept { val = o.val; o.val = -1; return *this; }
	    MoveOnly(const MoveOnly&) = delete;
	    MoveOnly& operator=(const MoveOnly&) = delete;
	};

	struct NoDefaultCtor {
	    int x;
	    explicit NoDefaultCtor(int v) : x(v) {}
	    NoDefaultCtor(const NoDefaultCtor&) = default;
	};

	TEST(Registry, AddAndgetPinnedComponent) {
	    Registry registry;
	    auto entity = registry.takeEntity();
	    registry.addComponent<Position>(entity, false, (float)10, (float)20);

	    auto* pos = registry.getPinnedComponent<Position>(entity).get();
	    ASSERT_NE(pos, nullptr);
	    EXPECT_FLOAT_EQ(pos->x, 10);
	    EXPECT_FLOAT_EQ(pos->y, 20);
	}

	TEST(Registry, HasComponentWorks) {
	    Registry registry;
	    auto entity = registry.takeEntity();
	    EXPECT_FALSE(registry.hasComponent<Velocity>(entity));
	    registry.addComponent<Velocity>(entity, 1.f, 2.f );
	    EXPECT_TRUE(registry.hasComponent<Velocity>(entity));
	}

	TEST(Registry, destroyComponent) {
	    Registry registry;
	    auto entity = registry.takeEntity();
	    registry.addComponent<Health>(entity,  100 );
	    registry.destroyComponent<Health>(entity);
	    EXPECT_FALSE(registry.hasComponent<Health>(entity));
	    EXPECT_EQ(registry.getPinnedComponent<Health>(entity).get(), nullptr);
	}

	TEST(Registry, AddMultipleComponents) {
	    Registry registry;
	    auto entity = registry.takeEntity();
	    registry.addComponent<Position>(entity, (float)1, (float)2 );
	    registry.addComponent<Velocity>(entity, (float)3, (float)4 );
	    registry.addComponent<Health>(entity, 5 );

	    auto* pos = registry.getPinnedComponent<Position>(entity).get();
	    auto* vel = registry.getPinnedComponent<Velocity>(entity).get();
	    auto* health = registry.getPinnedComponent<Health>(entity).get();

	    ASSERT_NE(pos, nullptr);
	    ASSERT_NE(vel, nullptr);
	    ASSERT_NE(health, nullptr);

	    EXPECT_FLOAT_EQ(pos->x, 1);
	    EXPECT_EQ(health->value, 5);
	}

	TEST(Registry, RemoveEntity) {
	    Registry registry;
	    auto entity = registry.takeEntity();
	    registry.addComponent<Position>(entity, (float)10, (float)20);
	    registry.addComponent<Health>(entity, 50);
	    registry.destroyEntity(entity);

	    EXPECT_EQ(registry.getPinnedComponent<Position>(entity).get(), nullptr);
	    EXPECT_EQ(registry.getPinnedComponent<Health>(entity).get(), nullptr);
	}

	TEST(Registry, IterateEntitiesWithComponent) {
	    Registry registry;
	    std::vector<EntityId> ids;
	    for (int i = 0; i < 5; ++i) {
	        auto e = registry.takeEntity();
	        registry.addComponent<Position>(e, float(i), float(i * 10));
	        ids.push_back(e);
	    }
	    int count = 0;
	    for (auto [e, pos] : registry.forEach<Position>()) {
	        EXPECT_EQ(e, ids[count]);
	        EXPECT_FLOAT_EQ(pos->x, float(count));
	        ++count;
	    }
	    EXPECT_EQ(count, 5);
	}

	TEST(Registry, ComponentOverwrite) {
	    Registry registry;
	    auto entity = registry.takeEntity();
	    registry.addComponent<Velocity>(entity, (float)1, (float)1);
	    registry.addComponent<Velocity>(entity, (float)2, (float)2); // Overwrite
	    auto* vel = registry.getPinnedComponent<Velocity>(entity).get();
	    ASSERT_NE(vel, nullptr);
	    EXPECT_FLOAT_EQ(vel->dx, 2);
	}

	TEST(Registry, getPinnedComponentNonexistentReturnsNull) {
	    Registry registry;
	    auto entity = registry.takeEntity();
	    EXPECT_EQ(registry.getPinnedComponent<Health>(entity).get(), nullptr);
	}

	TEST(Registry, HasComponentAfterEntityRemove) {
	    Registry registry;
	    auto entity = registry.takeEntity();
	    registry.addComponent<Position>(entity, (float)0, (float)0);
	    registry.destroyEntity(entity);
	    EXPECT_FALSE(registry.hasComponent<Position>(entity));
	}

	TEST(Registry, ComponentStorageIsIsolatedPerEntity) {
	    Registry registry;
	    auto e1 = registry.takeEntity();
	    auto e2 = registry.takeEntity();
	    registry.addComponent<Health>(e1, 99);
	    registry.addComponent<Health>(e2, 42);
	    EXPECT_EQ(registry.getPinnedComponent<Health>(e1)->value, 99);
	    EXPECT_EQ(registry.getPinnedComponent<Health>(e2)->value, 42);
	}



	TEST(Registry, RemoveComponentTwiceDoesNotCrash) {
	    ecss::Registry registry;
	    auto entity = registry.takeEntity();
	    registry.addComponent<Health>(entity, 100);
	    registry.destroyComponent<Health>(entity);
	    registry.destroyComponent<Health>(entity);
	    EXPECT_EQ(registry.getPinnedComponent<Health>(entity).get(), nullptr);
	}

	TEST(Registry, RemoveEntityTwiceDoesNotCrash) {
	    ecss::Registry registry;
	    auto entity = registry.takeEntity();
	    registry.addComponent<Position>(entity, (float)1, (float)1);
	    registry.destroyEntity(entity);
	    registry.destroyEntity(entity);
	    EXPECT_EQ(registry.getPinnedComponent<Position>(entity).get(), nullptr);
	}

	TEST(Registry, OverwriteWithDifferentComponents) {
	    ecss::Registry registry;
	    auto entity = registry.takeEntity();
	    registry.addComponent<Position>(entity, (float)1, (float)2);
	    registry.addComponent<Velocity>(entity, (float)3, (float)4);
	    registry.addComponent<Position>(entity, (float)7, (float)8);
	    EXPECT_EQ(registry.getPinnedComponent<Position>(entity)->x, (float)7);
	    EXPECT_EQ(registry.getPinnedComponent<Velocity>(entity)->dx, (float)3);
	}

	TEST(Registry, MassEntityAndComponentAddRemove) {
	    ecss::Registry registry;
	    std::vector<ecss::EntityId> ids;
	    constexpr int N = 10000;
	    for (int i = 0; i < N; ++i) {
	        auto id = registry.takeEntity();
	        registry.addComponent<Health>(id, i);
	        registry.addComponent<Position>(id, float(i), float(-i));
	        ids.push_back(id);
	    }
	    for (int i = 0; i < N; i += 2) {
	        registry.destroyEntity(ids[i]);
	    }
	    int alive = 0;
	    for (int i = 0; i < N; ++i) {
	        if (registry.getPinnedComponent<Health>(ids[i]))
	            ++alive;
	    }
	    EXPECT_EQ(alive, N / 2);
	}

	TEST(Registry, ReAddComponentAfterRemove) {
	    ecss::Registry registry;
	    auto e = registry.takeEntity();
	    registry.addComponent<Health>(e, 10);
	    registry.destroyComponent<Health>(e);
	    EXPECT_EQ(registry.getPinnedComponent<Health>(e).get(), nullptr);
	    registry.addComponent<Health>(e, 20);
	    EXPECT_EQ(registry.getPinnedComponent<Health>(e)->value, 20);
	}

	TEST(Registry, ForEachSkipsEntitiesWithoutAllComponents) {
	    ecss::Registry registry;
	    auto a = registry.takeEntity();
	    auto b = registry.takeEntity();
	    registry.addComponent<Position>(a, (float)1, (float)2);
	    registry.addComponent<Velocity>(a, (float)3, (float)4);
	    registry.addComponent<Position>(b, (float)5, (float)6); // b без Velocity

	    int count = 0;
	    for (auto [e, pos, vel] : registry.forEach<Position, Velocity>()) {
	        if (e == a) {
	            EXPECT_EQ(pos->x, (float)1);
	            EXPECT_EQ(vel->dx, (float)3);
	        }
	        else {
	            EXPECT_EQ(pos->x, (float)5);
	            EXPECT_EQ(vel, nullptr);
	        }
	       
	        ++count;
	    }
	    EXPECT_EQ(count, 2);
	}

	TEST(Registry, InvalidEntityDoesNothing) {
	    ecss::Registry registry;
	    ecss::EntityId invalid = 0;
	    EXPECT_EQ(registry.getPinnedComponent<Position>(invalid).get(), nullptr);
	    EXPECT_FALSE(registry.hasComponent<Health>(invalid));
	    registry.destroyEntity(invalid);
	}

	TEST(Registry, DifferentComponentTypesDoNotInterfere) {
	    ecss::Registry registry;
	    auto e = registry.takeEntity();
	    registry.addComponent<Position>(e, (float)1, (float)2);
	    EXPECT_EQ(registry.getPinnedComponent<Health>(e).get(), nullptr);
	    registry.addComponent<Health>(e, 42);
	    EXPECT_EQ(registry.getPinnedComponent<Position>(e)->x, (float)1);
	    EXPECT_EQ(registry.getPinnedComponent<Health>(e)->value, 42);
	}

	TEST(Registry, InsertMoveOnlyComponent) {
	    ecss::Registry registry;
	    auto e = registry.takeEntity();
	    registry.addComponent<MoveOnly>(e, MoveOnly(7));
	    auto* ptr = registry.getPinnedComponent<MoveOnly>(e).get();
	    ASSERT_NE(ptr, nullptr);
	    EXPECT_EQ(ptr->val, 7);
	}

	TEST(Registry, ReserveCapacity) {
	    ecss::Registry registry;
	    registry.reserve<Position>(1000);
	    auto e = registry.takeEntity();
	    registry.addComponent<Position>(e, (float)5, (float)6);
	    EXPECT_NE(registry.getPinnedComponent<Position>(e).get(), nullptr);
	}

	TEST(Registry, ThreadedAddRemoveStress) {
	    ecss::Registry registry;
	    constexpr int threadCount = 8, N = 5000;
	    std::vector<ecss::EntityId> ids(threadCount * N);
	    std::vector<std::thread> threads;
	    for (int t = 0; t < threadCount; ++t) {
	        threads.emplace_back([&, t] {
	            for (int i = 0; i < N; ++i) {
	                int idx = t * N + i;
	                auto e = registry.takeEntity();
	                ids[idx] = e;
	                registry.addComponent<Health>(e, idx);
	                registry.addComponent<Position>(e, (float)idx, (float)-idx);
	            }
	        });
	    }
	    for (auto& th : threads) th.join();

	    int countP = 0;
	    int countH = 0;
	    std::unordered_map<EntityId, size_t> ents;
	    for (int i = 0; i < threadCount * N; ++i) {
	        EXPECT_EQ(++ents[ids[i]], 1);
	        if (registry.getPinnedComponent<Position>(ids[i]))
	            ++countP;
	        if (registry.getPinnedComponent<Health>(ids[i]))
	            ++countH;
	    }

	    EXPECT_EQ(countP, threadCount * N);
	    EXPECT_EQ(countH, threadCount * N);
	    EXPECT_EQ(countP, countH);
	}

	TEST(Registry, ClearRemovesAllEntitiesAndComponents) {
	    ecss::Registry registry;
	    auto e = registry.takeEntity();
	    registry.addComponent<Health>(e, 5);
	    registry.addComponent<Position>(e, (float)1, (float)2);
	    registry.clear();
	    EXPECT_EQ(registry.getPinnedComponent<Health>(e).get(), nullptr);
	    EXPECT_EQ(registry.getPinnedComponent<Position>(e).get(), nullptr);
	    EXPECT_TRUE(registry.getAllEntities().empty());
	}

	TEST(Registry, InitCustomComponentContainer) {
	    ecss::Registry registry;
	    registry.registerArray<A, B>();
	    auto e = registry.takeEntity();
	    registry.addComponent<A>(e, 10);
	    registry.addComponent<B>(e, 2.5f);
	    EXPECT_EQ(registry.getPinnedComponent<A>(e)->a, 10);
	    EXPECT_FLOAT_EQ(registry.getPinnedComponent<B>(e)->b, 2.5f);
	}

	TEST(Registry, DestroyMultipleComponents) {
	    ecss::Registry registry;
	    std::vector<ecss::EntityId> ids;
	    for (int i = 0; i < 50; ++i) {
	        auto e = registry.takeEntity();
	        registry.addComponent<Health>(e, i);
	        ids.push_back(e);
	    }
	    registry.destroyComponent<Health>(ids);
	    for (auto e : ids)
	        EXPECT_FALSE(registry.hasComponent<Health>(e));
	}

	TEST(Registry, DestroyEntitiesBulk) {
	    ecss::Registry registry;
	    std::vector<ecss::EntityId> ids;
	    for (int i = 0; i < 25; ++i) {
	        auto e = registry.takeEntity();
	        registry.addComponent<Position>(e, float(i), float(-i));
	        ids.push_back(e);
	    }
	    registry.destroyEntities(ids);
	    for (auto e : ids)
	        EXPECT_EQ(registry.getPinnedComponent<Position>(e).get(), nullptr);
	    EXPECT_TRUE(registry.getAllEntities().empty());
	}

	TEST(Registry, ReuseAfterClear) {
	    ecss::Registry registry;
	    auto e = registry.takeEntity();
	    registry.addComponent<Position>(e, (float)1, (float)2);
	    registry.clear();
	    auto e2 = registry.takeEntity();
	    registry.addComponent<Position>(e2, (float)7, (float)8);
	    EXPECT_EQ(registry.getPinnedComponent<Position>(e2)->x, (float)7);
	}

	TEST(Registry, ForEachAsyncWorks) {
	    ecss::Registry registry;
	    std::vector<ecss::EntityId> ids;
	    for (int i = 0; i < 5; ++i) {
	        auto e = registry.takeEntity();
	        registry.addComponent<Position>(e, float(i), float(i * 2));
	        ids.push_back(e);
	    }
	    int counter = 0;
	    registry.forEachAsync<Position>(ids, [&](ecss::EntityId e, Position* pos) {
	        EXPECT_EQ(pos->x, float(counter));
	        ++counter;
	    });
	    EXPECT_EQ(counter, 5);
	}

	TEST(Registry, ForEachWithRanges) {
	    ecss::Registry registry;
	    std::vector<ecss::EntityId> ids;
	    for (int i = 0; i < 10; ++i) {
	        auto e = registry.takeEntity();
	        registry.addComponent<Position>(e, float(i), float(-i));
	        ids.push_back(e);
	    }

	    int count = 0;
	    for (auto [e, pos] : registry.forEach<Position>(ecss::EntitiesRanges{ {ids[2],ids[3],ids[4]}})) {
	        EXPECT_GE(e, ids[2]);
	        EXPECT_LT(e, ids[5]);
	        ++count;
	    }
	    EXPECT_EQ(count, ids[5] - ids[2]);
	}

	TEST(Registry, ReserveAfterDeleteAndAdd) {
	    ecss::Registry registry;
	    registry.reserve<Health>(10);
	    auto e = registry.takeEntity();
	    registry.addComponent<Health>(e, 1);
	    registry.destroyComponent<Health>(e);
	    registry.reserve<Health>(20);
	    auto e2 = registry.takeEntity();
	    registry.addComponent<Health>(e2, 2);
	    EXPECT_EQ(registry.getPinnedComponent<Health>(e2)->value, 2);
	}

#define REGISTRY_PERF_TESTS  1
#if REGISTRY_PERF_TESTS
	TEST(Registry_perfTest, CreatingAndIteratingNotOneSector) {
		ecss::Registry registry;
		constexpr size_t size = 100'000'000;
		registry.reserve<Health>(size);
		registry.reserve<Velocity>(size);

		auto t0 = std::chrono::high_resolution_clock::now();
		for (auto i = 0; i < size; i++) {
			auto e = registry.takeEntity();
			registry.addComponent<Health>(e, false, 1);
			registry.addComponent<Velocity>(e, false, (float)1, (float)2);
		}
		auto t1 = std::chrono::high_resolution_clock::now();

		int counter = 0;
		auto t2 = std::chrono::high_resolution_clock::now();

		for (auto [e, hel, vel] : registry.forEach<Health, Velocity>()) {
			hel->value += e;
			vel->dx += e;
			vel->dy += hel->value;
			counter++;
		}

		auto t3 = std::chrono::high_resolution_clock::now();

		auto create_us = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
		auto iterate_us = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count();

		std::cout << "[StressTest] Create time: " << create_us << " ms\n";
		std::cout << "[StressTest] Iterate time: " << iterate_us << " ms\n";

		EXPECT_EQ(size, counter);
	}

	TEST(Registry_perfTest, CreatingAndIteratingOneComponent) {
		ecss::Registry<false> registry;
		constexpr size_t size = 100'000'000;

		registry.registerArray<Health>();
		registry.reserve<Health>(size);

		auto t0 = std::chrono::high_resolution_clock::now();
		for (auto i = 0; i < size; i++) {
			registry.addComponent<Health>(registry.takeEntity(), false, 1);
		}
		auto t1 = std::chrono::high_resolution_clock::now();
		EXPECT_EQ(size, registry.getComponentContainer<Health>()->size());
		int counter = 0;
		auto t2 = std::chrono::high_resolution_clock::now();
		for (auto [e, hel] : registry.forEach<Health>()) {
			hel->value += e;
			counter++;
		}
		auto t3 = std::chrono::high_resolution_clock::now();

		auto create_us = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
		auto iterate_us = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count();

		std::cout << "[StressTest] Create time: " << create_us << " ms\n";
		std::cout << "[StressTest] Iterate time: " << iterate_us << " ms\n";

		EXPECT_EQ(size, counter);
	}

	TEST(Registry_perfTest, CreatingAndIteratingOneSector) {
		ecss::Registry registry;
		constexpr size_t size = 100'000'000;

		registry.registerArray<Health, Velocity>();
		registry.reserve<Health>(size);
		registry.reserve<Velocity>(size);

		auto t0 = std::chrono::high_resolution_clock::now();
		for (auto i = 0; i < size; i++) {
			auto e = registry.takeEntity();
			registry.addComponent<Health>(e, false, 1);
			registry.addComponent<Velocity>(e, false, (float)1, (float)2);
		}
		auto t1 = std::chrono::high_resolution_clock::now();
		EXPECT_EQ(size, registry.getComponentContainer<Health>()->size());
		int counter = 0;
		auto t2 = std::chrono::high_resolution_clock::now();
		for (auto [e, hel, vel] : registry.forEach<Health, Velocity>()) {
			hel->value += e;
			vel->dx += e;
			vel->dy += hel->value;
			counter++;
		}
		auto t3 = std::chrono::high_resolution_clock::now();

		auto create_us = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
		auto iterate_us = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count();

		std::cout << "[StressTest] Create time: " << create_us << " ms\n";
		std::cout << "[StressTest] Iterate time: " << iterate_us << " ms\n";

		EXPECT_EQ(size, counter);
	}

	TEST(Registry_perfTest, CreatingAndIteratingOneSectorRanges) {
		ecss::Registry registry;
		constexpr size_t size = 100'000'000;

		registry.registerArray<Health, Velocity>();
		registry.reserve<Health>(size);
		registry.reserve<Velocity>(size);

		auto t0 = std::chrono::high_resolution_clock::now();
		for (auto i = 0; i < size; i++) {
			auto e = registry.takeEntity();
			registry.addComponent<Health>(e, false, 1);
			registry.addComponent<Velocity>(e, false, (float)1, (float)2);
		}
		auto t1 = std::chrono::high_resolution_clock::now();

		int counter = 0;
		auto t2 = std::chrono::high_resolution_clock::now();
		for (auto [e, hel, vel] : registry.forEach<Health, Velocity>(EntitiesRanges{ std::vector<EntitiesRanges::range>{{0,static_cast<SectorId>(size)}} })) {
			hel->value += e;
			vel->dx += e;
			vel->dy += hel->value;
			counter++;
		}
		auto t3 = std::chrono::high_resolution_clock::now();

		auto create_us = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
		auto iterate_us = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count();

		std::cout << "[StressTest] Create time: " << create_us << " ms\n";
		std::cout << "[StressTest] Iterate time: " << iterate_us << " ms\n";

		EXPECT_EQ(size, counter);
	}

	TEST(Registry_perfTest, CreatingAndIteratingNotOneSectorNotReserved) {
		ecss::Registry registry;
		constexpr size_t size = 100'000'000;
		constexpr size_t itCount = 10;

		auto t0 = std::chrono::high_resolution_clock::now();
		for (auto i = 0; i < size; i++) {
			auto e = registry.takeEntity();
			registry.addComponent<Health>(e, false, 1);
			registry.addComponent<Velocity>(e, false, (float)1, (float)2);
		}
		auto t1 = std::chrono::high_resolution_clock::now();

		int counter = 0;
		auto t2 = std::chrono::high_resolution_clock::now();
		for (auto i = 0; i < 10; i++) {
			for (auto [e, hel, vel] : registry.forEach<Health, Velocity>()) {
				hel->value += e;
				vel->dx += e;
				vel->dy += hel->value;
				counter++;
			}
		}
		auto t3 = std::chrono::high_resolution_clock::now();

		auto create_us = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
		auto iterate_us = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count();

		std::cout << "[StressTest] Create time: " << create_us << " ms\n";
		std::cout << "[StressTest] Iterate time: " << iterate_us << " ms\n";

		EXPECT_EQ(size * itCount, counter);
	}

	TEST(Registry_perfTest, CreatingAndIteratingOneSectorNotReserved) {
		ecss::Registry registry;
		constexpr size_t size = 100'000'000;
		constexpr size_t itCount = 10;

		registry.registerArray<Health, Velocity>();

		auto t0 = std::chrono::high_resolution_clock::now();
		for (auto i = 0; i < size; i++) {
			auto e = registry.takeEntity();
			registry.addComponent<Health>(e, false, 1);
			registry.addComponent<Velocity>(e, false, (float)1, (float)2);
		}
		auto t1 = std::chrono::high_resolution_clock::now();

		int counter = 0;
		auto t2 = std::chrono::high_resolution_clock::now();
		for (auto i = 0; i < 10; i++) {
			for (auto [e, hel, vel] : registry.forEach<Health, Velocity>()) {
				hel->value += e;
				vel->dx += e;
				vel->dy += hel->value;
				counter++;
			}
		}
		auto t3 = std::chrono::high_resolution_clock::now();

		auto create_us = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
		auto iterate_us = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count();

		std::cout << "[StressTest] Create time: " << create_us << " ms\n";
		std::cout << "[StressTest] Iterate time: " << iterate_us << " ms\n";

		EXPECT_EQ(size * itCount, counter);
	}
#endif
	namespace Stress
	{
		TEST(Registry_STRESS, ParallelInsertAndIterations) {
			ecss::Registry reg;
			constexpr int N = 500000;
			std::vector<std::thread> threads;
			std::vector<ecss::EntityId> ids(N);
			std::atomic<bool> done = false;
			threads.emplace_back([&] {
				for (int i = 0; i < N; ++i) {
					ids[i] = reg.takeEntity();
					reg.addComponent<Velocity>(ids[i], float(i));
				}
				done = true;
			});


			threads.emplace_back([&] {
				while (!done) {
					for (auto [ent, vel] : reg.forEach<Velocity>()) {
						vel->dx++;
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(16));
				}
			});


			for (auto& th : threads) th.join();
			//this test passed if no crashes or aborts
		}

		TEST(Registry_STRESS, ParallelInsertAndIterationsRanges) {
			ecss::Registry reg;
			constexpr int N = 500000;
			std::vector<std::thread> threads;
			std::vector<ecss::EntityId> ids(N);
			std::atomic<bool> done = false;
			threads.emplace_back([&] {
				for (int i = 0; i < N; ++i) {
					ids[i] = reg.takeEntity();
					reg.addComponent<Velocity>(ids[i], float(i));
				}
				done = true;
			});


			threads.emplace_back([&] {
				while (!done) {
					for (auto [ent, vel] : reg.forEach<Velocity>({ ids })) {
						vel->dx++;
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(16));
				}
			});


			for (auto& th : threads) th.join();
			//this test passed if no crashes or aborts
		}
	}
	

	TEST(Registry, AddRemoveComponentMultipleTimes) {
	    ecss::Registry registry;
	    auto e = registry.takeEntity();
	    EXPECT_FALSE(registry.hasComponent<Health>(e));
	    registry.addComponent<Health>(e, 123);
	    EXPECT_TRUE(registry.hasComponent<Health>(e));
	    registry.destroyComponent<Health>(e);
	    EXPECT_FALSE(registry.hasComponent<Health>(e));
	    registry.addComponent<Health>(e, 321);
	    EXPECT_TRUE(registry.hasComponent<Health>(e));
	    EXPECT_EQ(registry.getPinnedComponent<Health>(e)->value, 321);
	}

	TEST(Registry, IterationWithComponentRemoval) {
	    ecss::Registry registry;
	    std::vector<ecss::EntityId> entities;
	    for (int i = 0; i < 1000; ++i) {
	        auto e = registry.takeEntity();
	        registry.addComponent<Health>(e, i);
	        entities.push_back(e);
	    }

	    int count = 0;
		for (auto e : entities) {
			auto h = registry.getPinnedComponent<Health>(e);
			if (h->value % 2 == 0) {
				h.release();
				registry.destroyComponent<Health>(e);
			}
			else {
				++count;
			}
		}
	 
	    EXPECT_EQ(count, 500);

	    int check = 0;
	    for (auto [e, h] : registry.forEach<Health>()) {
	        EXPECT_TRUE(h->value % 2 == 1);
	        ++check;
	    }
	    EXPECT_EQ(check, 500);
	}

	TEST(Registry, ThreadSafeGetComponent) {
	    ecss::Registry registry;
	    auto e = registry.takeEntity();
	    registry.addComponent<Health>(e, 777);

	    std::atomic<bool> stop = false;
	    std::thread t1([&]() {
	        while (!stop.load()) {
	            auto ptr = registry.getPinnedComponent<Health>(e);
	            if (ptr) EXPECT_EQ(ptr->value, 777);
	        }
	    });
	    std::thread t2([&]() {
	        for (int i = 0; i < 10000; ++i) {
	            registry.hasComponent<Health>(e);
	        }
	        stop = true;
	    });
	    t1.join();
	    t2.join();
	}

	TEST(Registry, CompositeComponentSector) {
	    ecss::Registry registry;
	    registry.registerArray<Health, Velocity>();

	    auto e = registry.takeEntity();
	    registry.addComponent<Health>(e, 100);
	    registry.addComponent<Velocity>(e, (float)1, (float)2);

	    auto* h = registry.getPinnedComponent<Health>(e).get();
	    auto* v = registry.getPinnedComponent<Velocity>(e).get();
	    EXPECT_TRUE(h);
	    EXPECT_TRUE(v);
	    EXPECT_EQ(h->value, 100);
	    EXPECT_EQ(v->dx, 1);
	    EXPECT_EQ(v->dy, 2);
	}

	TEST(Registry, AddNoDefaultCtor) {
	    ecss::Registry reg;
	    auto e = reg.takeEntity();
	    reg.addComponent<NoDefaultCtor>(e, 42);
	    ASSERT_NE(reg.getPinnedComponent<NoDefaultCtor>(e).get(), nullptr);
	    EXPECT_EQ(reg.getPinnedComponent<NoDefaultCtor>(e)->x, 42);
	}

	TEST(Registry, MoveOnlyComponentWorks) {
	    ecss::Registry reg;
	    auto e = reg.takeEntity();
	    reg.addComponent<MoveOnly>(e, MoveOnly(777));
	    auto* ptr = reg.getPinnedComponent<MoveOnly>(e).get();
	    ASSERT_NE(ptr, nullptr);
	    EXPECT_EQ(ptr->val, 777);
	}


	struct CopyOnly {
	    int v;
	    CopyOnly(int x) : v(x) {}
	    CopyOnly(const CopyOnly& o) : v(o.v) {}
	    CopyOnly(CopyOnly&&) = delete;
	    CopyOnly& operator=(const CopyOnly&) = delete;
	    CopyOnly& operator=(CopyOnly&&) = delete;
	};

	// this code should not compile
	//TEST(Registry, CopyOnlyComponentWorks) {
	//    ecss::Registry reg;
	//    auto e = reg.takeEntity();
	//    reg.addComponent<CopyOnly>(e, CopyOnly(555));
	//    auto* ptr = reg.getPinnedComponent<CopyOnly>(e);
	//    ASSERT_NE(ptr, nullptr);
	//    EXPECT_EQ(ptr->v, 555);
	//}

	TEST(Registry, MultipleEntitiesSameComponentType) {
	    ecss::Registry reg;
	    std::vector<ecss::EntityId> ids;
	    for (int i = 0; i < 10; ++i) {
	        auto e = reg.takeEntity();
	        reg.addComponent<Health>(e, i * 10);
	        ids.push_back(e);
	    }
	    for (int i = 0; i < 10; ++i) {
	        EXPECT_EQ(reg.getPinnedComponent<Health>(ids[i])->value, i * 10);
	    }
	}

	TEST(Registry, MassAddRemoveAndIterate) {
	    ecss::Registry reg;
	    std::vector<ecss::EntityId> ids;
	    constexpr int N = 2000;
	    for (int i = 0; i < N; ++i) {
	        auto e = reg.takeEntity();
	        reg.addComponent<Position>(e, (float)i, (float) - i);
	        ids.push_back(e);
	    }
	    for (int i = 0; i < N; i += 2)
	        reg.destroyEntity(ids[i]);
	    int cnt = 0;
	    for (auto [e, pos] : reg.forEach<Position>())
	        cnt++;
	    EXPECT_EQ(cnt, N / 2);
	}

	TEST(Registry, ParallelAddAndGet) {
	    ecss::Registry reg;
	    constexpr int threads = 8, N = 2000;
	    std::vector<ecss::EntityId> ids(threads * N);
	    std::vector<std::thread> thrs;
	    for (int t = 0; t < threads; ++t) {
	        thrs.emplace_back([&, t] {
	            for (int i = 0; i < N; ++i) {
	                int idx = t * N + i;
	                auto e = reg.takeEntity();
	                ids[idx] = e;
	                reg.addComponent<Position>(e, (float)idx, (float)-idx);
	            }
	        });
	    }
	    for (auto& th : thrs) th.join();

	    std::atomic<int> found{ 0 };
	    std::vector<std::thread> reads;
	    for (int t = 0; t < threads; ++t) {
	        reads.emplace_back([&, t] {
	            for (int i = t * N; i < (t + 1) * N; ++i) {
	                auto* pos = reg.getPinnedComponent<Position>(ids[i]).get();
	                if (pos) found++;
	            }
	        });
	    }
	    for (auto& th : reads) th.join();
	    EXPECT_EQ(found.load(), threads * N);
	}

	TEST(Registry, HugeReserveDoesNotCrash) {
	    ecss::Registry reg;
	    EXPECT_NO_THROW(reg.reserve<Health>(1 << 20));
	}

	TEST(Registry, RegisterArrayMultipleTypes) {
	    ecss::Registry reg;
	    reg.registerArray<Position, Velocity, Health>();
	    auto e = reg.takeEntity();
	    reg.addComponent<Position>(e, (float)1, (float)2);
	    reg.addComponent<Velocity>(e, (float)3, (float)4);
	    reg.addComponent<Health>(e, 5);

	    EXPECT_EQ(reg.getPinnedComponent<Position>(e)->x, 1);
	    EXPECT_EQ(reg.getPinnedComponent<Velocity>(e)->dx, 3);
	    EXPECT_EQ(reg.getPinnedComponent<Health>(e)->value, 5);
	}

	TEST(Registry, ParallelAddRemove) {
	    ecss::Registry reg;
	    constexpr int threads = 8, N = 1000;
	    std::vector<ecss::EntityId> ids(threads * N);
	    std::vector<std::thread> thrs;
	    for (int t = 0; t < threads; ++t) {
	        thrs.emplace_back([&, t] {
	            for (int i = 0; i < N; ++i) {
	                int idx = t * N + i;
	                auto e = reg.takeEntity();
	                ids[idx] = e;
	                reg.addComponent<Health>(e, idx);
	            }
	        });
	    }
	    for (auto& th : thrs) th.join();

	    int count = 0;
	    for (auto [e, val] : reg.forEach<Health>()) {
	        count++;
	    }

	    EXPECT_EQ(count, threads * N);

	    std::vector<std::thread> dels;
	    for (int t = 0; t < threads; ++t) {
	        dels.emplace_back([&, t] {
	            for (int i = t * N; i < (t + 1) * N; i += 2)
	                reg.destroyEntity(ids[i]);
	        });
	    }
	    for (auto& th : dels) th.join();

	    int alive = 0;
	    for (int i = 0; i < threads * N; ++i)
	        if (reg.getPinnedComponent<Health>(ids[i])) alive++;
	    EXPECT_EQ(alive, threads * N / 2);
	}

	TEST(Registry, StressForEach) {
		constexpr size_t size = 100000;
		ecss::Registry reg;
		for (int i = 0; i < size; ++i) {
			auto e = reg.takeEntity();
			reg.addComponent<Health>(e, i);
			reg.addComponent<Position>(e, (float)i, (float)-i);
		}
		size_t cnt = 0, sum = 0;
		for (auto [e, h, p] : reg.forEach<Health, Position>()) {
			cnt++;
			sum += static_cast<size_t>(h->value) + static_cast<size_t>(p->x);
		}
		EXPECT_EQ(cnt, size);
		EXPECT_EQ(sum, 2 * size * (size - 1) / 2);
	}

	TEST(Registry, StressDestroyAll) {
	    ecss::Registry reg;
	    std::vector<ecss::EntityId> ids;
	    constexpr int N = 10000;
	    for (int i = 0; i < N; ++i) {
	        auto e = reg.takeEntity();
	        reg.addComponent<Health>(e, i);
	        ids.push_back(e);
	    }
	    reg.destroyEntities(ids);
	    for (auto e : ids)
	        EXPECT_EQ(reg.getPinnedComponent<Health>(e).get(), nullptr);
	}

	TEST(Registry, RemoveNonExistent) {
	    ecss::Registry reg;
	    EXPECT_NO_THROW(reg.destroyEntity(123456));
	}

	TEST(Registry, DestroyComponentOnly) {
	    ecss::Registry reg;
	    auto e = reg.takeEntity();
	    reg.addComponent<Health>(e, 1);
	    reg.destroyComponent<Health>(e);
	    EXPECT_EQ(reg.getPinnedComponent<Health>(e).get(), nullptr);
	}

	TEST(Registry, DestroyAllComponentsOnEntity) {
	    ecss::Registry reg;
	    auto e = reg.takeEntity();
	    reg.addComponent<Health>(e, 1);
	    reg.addComponent<Position>(e, (float)2, (float)3);
	    reg.destroyComponents(e);
	    EXPECT_EQ(reg.getPinnedComponent<Health>(e).get(), nullptr);
	    EXPECT_EQ(reg.getPinnedComponent<Position>(e).get(), nullptr);
	}

	TEST(Registry, CopyArrayToRegistry) {
	    ecss::Registry reg1, reg2;
	    auto e = reg1.takeEntity();
	    reg1.addComponent<Health>(e, 42);
	    reg2.copyComponentsArrayToRegistry<Health>(*reg1.getComponentContainer<Health>());
	    EXPECT_EQ(reg2.getPinnedComponent<Health>(e)->value, 42);
	}

	TEST(Registry, ForEachOrderIndependence) {
	    ecss::Registry reg;
	    constexpr int size = 1000;
	    for (int i = 0; i < size; ++i) {
	        auto e = reg.takeEntity();
	        reg.addComponent<Health>(e, i);
	        reg.addComponent<Position>(e, (float)i, (float)-i);
	        reg.addComponent<Velocity>(e, (float)10, (float)-10);
	    }
	    int cnt = 0;
	    for (auto [e, h, p, v] : reg.forEach<Health, Position, Velocity>())
	        cnt++;
	    EXPECT_EQ(cnt, size);
	    cnt = 0;
	    for (auto [e, p, v, h] : reg.forEach<Position, Velocity, Health>())
	        cnt++;
	    EXPECT_EQ(cnt, size);
	}

	TEST(Registry, ForEachAsync) {
	    ecss::Registry reg;
	    constexpr int size = 1000;
	    std::vector<ecss::EntityId> ids;
	    for (int i = 0; i < size; ++i) {
	        auto e = reg.takeEntity();
	        reg.addComponent<Health>(e, i);
	        ids.push_back(e);
	    }
	    std::vector<int> vals(size);
	    reg.forEachAsync<Health>(ids, [&](ecss::EntityId e, Health* h) {
	        vals[h->value] = h->value;
	    });
	    for (int i = 0; i < size; ++i) EXPECT_EQ(vals[i], i);
	}

	TEST(Registry, MultithreadedAddDestroyIterate) {
	    ecss::Registry reg;
	    constexpr int T = 8, N = 2000;
	    std::vector<std::thread> threads;
	    std::vector<ecss::EntityId> ids(T * N);

	    for (int t = 0; t < T; ++t) {
	        threads.emplace_back([&, t] {
	            for (int i = 0; i < N; ++i) {
	                int idx = t * N + i;
	                auto e = reg.takeEntity();
	                ids[idx] = e;
	                reg.addComponent<Health>(e, idx);
	            }
	        });
	    }
	    for (auto& th : threads) th.join();

	    std::thread iter([&] {
	        int cnt = 0;
	        for (auto [e, h] : reg.forEach<Health>()) {
	            EXPECT_NE(h, nullptr);
	            cnt++;
	        }
	        EXPECT_GE(cnt, T * N / 2);
	    });

	    std::vector<std::thread> dels;
	    for (int t = 0; t < T; ++t) {
	        dels.emplace_back([&, t] {
	            for (int i = t * N; i < (t + 1) * N; i += 2)
	                reg.destroyEntity(ids[i]);
	        });
	    }
	    for (auto& th : dels) th.join();
	    iter.join();
	}

	TEST(Registry, MoveCopyStress) {
	    ecss::Registry reg;
	    constexpr int N = 2000;
	    std::vector<ecss::EntityId> ids(N);
	    for (int i = 0; i < N; ++i) {
	        auto e = reg.takeEntity();
	        reg.addComponent<MoveOnly>(e, MoveOnly(i));
	        //reg.addComponent<CopyOnly>(e, CopyOnly(i));  //this code should not compile
	        ids[i] = e;
	    }
	    for (int i = 0; i < N; ++i) {
	        EXPECT_EQ(reg.getPinnedComponent<MoveOnly>(ids[i])->val, i);
	        //EXPECT_EQ(reg.getPinnedComponent<CopyOnly>(ids[i])->v, i); //this code should not compile
	    }
	}

	TEST(Registry, ParallelMixedOps) {
	    ecss::Registry reg;
	    constexpr int T = 8, N = 1500;
	    std::vector<std::thread> threads;
	    std::vector<ecss::EntityId> ids(T * N);
	    for (int t = 0; t < T; ++t) {
	        threads.emplace_back([&, t] {
	            for (int i = 0; i < N; ++i) {
	                int idx = t * N + i;
	                auto e = reg.takeEntity();
	                ids[idx] = e;
	                reg.addComponent<Health>(e, idx);
	                reg.addComponent<Position>(e, (float)idx, (float)-idx);
	                if (idx % 10 == 0)
	                    reg.destroyEntity(e);
	            }
	        });
	    }
	    for (auto& th : threads) th.join();
	    int cnt = 0;
	    for (auto [e, h, p] : reg.forEach<Health, Position>())
	        cnt++;
	    EXPECT_LE(cnt, T * N);
	}

	TEST(Registry, RepeatedAddRemoveRace) {
	    ecss::Registry reg;
	    constexpr int N = 2000, T = 8;
	    std::vector<ecss::EntityId> ids(N);
	    for (int i = 0; i < N; ++i)
	        ids[i] = reg.takeEntity();

	    std::vector<std::thread> threads;
	    for (int t = 0; t < T; ++t) {
	        threads.emplace_back([&, t] {
	            for (int i = 0; i < N; ++i) {
	                reg.addComponent<Health>(ids[i], t * N + i);
	                reg.destroyComponent<Health>(ids[i]);
	            }
	        });
	    }
	    for (auto& th : threads) th.join();
	}

	TEST(Registry, ParallelMoveOnlyInsertGetErase) {
	    ecss::Registry reg;
	    constexpr int T = 8, N = 500;
	    std::vector<std::thread> threads;
	    std::vector<ecss::EntityId> ids(T * N);
	    for (int t = 0; t < T; ++t) {
	        threads.emplace_back([&, t] {
	            for (int i = 0; i < N; ++i) {
	                int idx = t * N + i;
	                auto e = reg.takeEntity();
	                ids[idx] = e;
	                reg.addComponent<MoveOnly>(e, MoveOnly(idx));
	            }
	        });
	    }
	    for (auto& th : threads) th.join();

	    std::vector<std::thread> dels;
	    for (int t = 0; t < T; ++t) {
	        dels.emplace_back([&, t] {
	            for (int i = t * N; i < (t + 1) * N; i += 3)
	                reg.destroyEntity(ids[i]);
	        });
	    }
	    for (auto& th : dels) th.join();

	    int found = 0;
	    for (int i = 0; i < T * N; ++i)
	        if (reg.getPinnedComponent<MoveOnly>(ids[i]))
	            found++;
	    EXPECT_GE(found, 0);
	}

	TEST(Registry, CreateAndCheckNoDublicate) {
		ecss::Registry reg;

		for (auto i = 0; i < 6000000; i++) {
			auto ent = reg.takeEntity();
			reg.addComponent<Velocity>(ent);
		}
		std::unordered_set<A*> set;
		for (auto [e, vel] : reg.forEach<A>()) {
			EXPECT_TRUE(!set.contains(vel));
			set.insert(vel);
		}
	}

	TEST(ChunksManager, ConcurrentAccessBugRepro) {
		using namespace ecss;
		struct matrix {
			float a, b, c, d, e, f, g, h;
		};

		struct Transform {
			matrix m;
		};

		Registry reg;
		std::atomic_bool running = true;
		std::atomic_bool foundNaN = false;

		// Писатель: создаёт много сущностей с компонентом Transform
		std::thread writer([&] {
			for (int i = 0; i < 10'000 && running; ++i) {
				auto ent = reg.takeEntity();
				Transform t{ 1.f,1.f,1.f,1.f,1.f,1.f,1.f,1.f };
				t.m.h = static_cast<float>(i); // для уникальности

				reg.addComponent<Transform>(ent, t);

				if (i % 500 == 0)
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			running = false;
		});

		// Читатель: параллельно читает Transform и проверяет корректность
		std::thread reader([&] {
			while (running && !foundNaN) {
				for (auto [e, t] : reg.forEach<Transform>()) {
					if (!t) continue;

					// Проверка матрицы на NaN/Inf (сломана память?)
					if (!std::isfinite(t->m.a) || std::isnan(t->m.a)) {
						std::cerr << "Corrupted matrix found! Entity: " << e << "\n";
						foundNaN = true;
						break;
					}
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		});

		std::thread reaper([&] {
			while (running) {
				auto ents = reg.getAllEntities();
				std::shuffle(ents.begin(), ents.end(), std::mt19937{ std::random_device{}() });
				ents.resize(ents.size() / 2);
				reg.destroyEntities(ents);
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		});

		writer.join();
		reader.join();
		reaper.join();
		EXPECT_FALSE(foundNaN);
	}

	TEST(Registry_STRESS, ConcurrentAddAndIterate_TransformMatComp_RawMat4)
	{
		struct mat4 {
			float m[4][4];
		};

		struct TransformMatComp {
			mat4 mTransform;
		};

		static_assert(std::is_trivial_v<TransformMatComp>);

		ecss::Registry source;
		ecss::Registry target;

		constexpr int N = 50000;
		std::vector<EntityId> ids;
		for (int i = 0; i < N; ++i) {
			auto id = source.takeEntity();
			source.addComponent<TransformMatComp>(id, mat4());
			ids.push_back(id);
		}

		std::atomic<bool> stop = false;
		std::atomic<bool> errorDetected = false;

		// thread: copy comp from source to target
		std::thread writer([&]() {
			for (int k = 0; k < 10; k++) {
				std::vector<EntityId> shuffledIds = ids;
				std::mt19937 rng(std::random_device{}());
				std::shuffle(shuffledIds.begin(), shuffledIds.end(), rng);

				size_t removeCount = shuffledIds.size() * 2 / 3;
				shuffledIds.resize(shuffledIds.size() - removeCount);
				std::sort(shuffledIds.begin(), shuffledIds.end());

				for (auto id : shuffledIds) {
					target.addComponent<TransformMatComp>(id, source.getPinnedComponent<TransformMatComp>(id)->mTransform);

					if (errorDetected) {
						break;
					}
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
			
		});

		// thread: iterating and check if there is any matrix corrupted
		std::thread reader([&]() {
			while (!stop) {
				for (auto [ent, t] : target.forEach<const TransformMatComp>()) {
					for (int i = 0; i < 4; ++i)
						for (int j = 0; j < 4; ++j) {
							float v = t->mTransform.m[i][j];
							if (!std::isfinite(v) || std::fpclassify(v) == FP_SUBNORMAL) {
								std::cerr << "Corrupt matrix! ent: " << ent << " value: " << v << "\n";
								errorDetected = true;
								stop = true;
								break;
							}
						}
					if (stop) break;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
		});

		writer.join();
		stop = true;
		reader.join();

		EXPECT_FALSE(errorDetected);
	}

	TEST(Registry, ParallelAddingToTheRandomPlacesAndIteration) {
		ecss::Registry reg;
		reg.registerArray<Velocity>();
		constexpr int count = 60000;
		std::vector<std::thread> threads;
		std::atomic_bool creating = true;
		threads.emplace_back([&]()
		{
			std::mt19937 rng(std::random_device{}());
			std::uniform_int_distribution<int> dist(0, count);

			for (auto i = 0; i < count; i++) {
				int randomValue = dist(rng);
				reg.addComponent<Velocity>(randomValue, (float)randomValue);

			}
			creating = false;
		});

		threads.emplace_back([&] {
			while (creating) {
				for (auto [e, val] : reg.forEach<const Velocity>()) {
					int d = 0;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}

		});

		for (auto& th : threads) th.join();

		for (auto [ent, a] : reg.forEach<Velocity>()) {
			EXPECT_EQ(a->dx, (float)ent);
		}
	}

	TEST(Registry, IteratinNotFromStart) {
		ecss::Registry reg;
		constexpr SectorId count = 100;
		constexpr SectorId first = 98;
		reg.registerArray<Velocity>();

		for (auto i = 0; i < count; i++) {
			reg.addComponent<Velocity>(reg.takeEntity());
		}

		int i = first;
		for (auto [ent, vel] : reg.forEach<Velocity>(EntitiesRanges{ std::vector<EntitiesRanges::range>{{first,count}}})) {
			EXPECT_EQ(ent, i++);
		}

	}

	TEST(Registry_STRESS, IteratingOne) {
		ecss::Registry reg;

		reg.addComponent<Velocity>(reg.takeEntity());

		for (auto [e, vel] : reg.forEach<Velocity>()) {
			
		}
	}

	TEST(Registry_STRESS, ParallelCreatingDestroing) {
		struct A
		{
			int a, b;
		};
		struct B
		{
			int a, b;
		};
		struct C
		{
			int a, b;
		};
		struct D
		{
			int a, b;
		};
		struct E
		{
			int a, b;
		};
		struct F
		{
			int a, b;
		};
		struct G
		{
			int a, b;
		};
		ecss::Registry reg;
		reg.registerArray<A, B, C>();
		reg.registerArray<D>();
		reg.registerArray<E>();
		reg.registerArray<F>();
		reg.registerArray<G>();
		for (int i = 0 ; i < 200; i++) {
			std::cout << std::to_string(i) << std::endl;
			constexpr int T = 8, N = 500;
			std::vector<std::thread> threads;
			std::atomic_bool creating = true;
			threads.emplace_back([&]()
			{
				for (auto i = 0; i < 60000; i++) {
					auto ent = reg.takeEntity();
					reg.addComponent<A>(ent);
					reg.addComponent<B>(ent);
					reg.addComponent<C>(ent);
					reg.addComponent<D>(ent);
					reg.addComponent<E>(ent);
					reg.addComponent<F>(ent);
					reg.addComponent<G>(ent);
				}
				creating = false;
			});


			threads.emplace_back([&] {
				while (creating) {
					for (auto [e, val, val2] : reg.forEach<A, B>()) {
						val->a = e;
						if (!val2) {
							continue;
						}
						val2->a = e;
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(50));
				}

			});

			threads.emplace_back([&] {
				while (creating) {
					for (auto [e, val, val2] : reg.forEach<B, C>()) {
						val->a = e;
						if (!val2) {
							continue;
						}
						val2->a = e;
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(50));
				}
			});

			threads.emplace_back([&] {
				while (creating) {
					for (auto [e, val] : reg.forEach<B>()) {
						val->a = e;
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(50));
				}
			});

			threads.emplace_back([&] {
				while (creating) {
					for (auto [e, val, val2] : reg.forEach<D, E>()) {
						val->a = e;
						if (!val2) {
							continue;
						}
						val2->a = e;
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(50));
				}

			});
			threads.emplace_back([&] {
				while (creating) {
					for (auto [e, val, val2, val3, val4] : reg.forEach<D, E, F, G>()) {
						val->a = e;
						if (val2) {
							val2->a = e;
						}
						if (val3) {
							val3->a = e;
						}
						if (val4) {
							val4->a = e;
						}

					}
					std::this_thread::sleep_for(std::chrono::milliseconds(50));
				}

			});

			threads.emplace_back([&] {
				while (creating) {
					auto componentsToDelete = reg.getAllEntities();

					std::mt19937 rng(static_cast<unsigned int>(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
					std::shuffle(componentsToDelete.begin(), componentsToDelete.end(), rng);
					auto N = componentsToDelete.size() / 3;
					componentsToDelete.resize(componentsToDelete.size() - N);

					reg.destroyEntities(componentsToDelete);
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
				}

			});

			for (auto& th : threads) th.join();

			for (auto [ent, a, b, c, d, e, f, g] : reg.forEach<A, B, C, D, E, F, G>()) {
				EXPECT_EQ(true, a->a == ent || a->a == 0);
				if (b) {
					EXPECT_EQ(true, b->a == ent || b->a == 0);
				}
				if (c) {
					EXPECT_EQ(true, c->a == ent || c->a == 0);
				}
				if (d) {
					EXPECT_EQ(true, d->a == ent || d->a == 0);
				}
				if (e) {
					EXPECT_EQ(true, e->a == ent || e->a == 0);
				}
				if (f) {
					EXPECT_EQ(true, f->a == ent || f->a == 0);
				}
				if (g) {
					EXPECT_EQ(true, g->a == ent || g->a == 0);
				}
			}


			reg.clear();
		}
		
	}
}

#include "ecss/EntitiesRanges.h"

namespace EntitiesRangeTest
{
	using ER = ecss::EntitiesRanges;
	using Range = ER::range;

	TEST(EntitiesRanges, EmptyInit) {
		ER er;
		EXPECT_TRUE(er.empty());
		EXPECT_EQ(er.size(), 0u);
		EXPECT_FALSE(er.contains(0));
		EXPECT_EQ(er.getAll().size(), 0u);
	}

	TEST(EntitiesRanges, InitFromSortedVector) {
		std::vector<ecss::EntityId> v{ 1, 2, 3, 5, 7, 8, 9, 10 };
		ER er(v);
		EXPECT_EQ(er.size(), 3u);
		EXPECT_EQ(er.ranges[0], Range(1, 4));
		EXPECT_EQ(er.ranges[1], Range(5, 6));
		EXPECT_EQ(er.ranges[2], Range(7, 11));
	}

	TEST(EntitiesRanges, TakeAndInsert) {
		ER er;
		auto id0 = er.take();
		auto id1 = er.take();
		EXPECT_EQ(id0, 0u);
		EXPECT_EQ(id1, 1u);
		er.insert(1);
		er.insert(5);
		er.insert(3);
		EXPECT_TRUE(er.contains(5));
		EXPECT_TRUE(er.contains(3));
		EXPECT_FALSE(er.contains(2));
	}

	TEST(EntitiesRanges, EraseElement) {
		ER er;
		for (int i = 0; i < 5; ++i) er.insert(i);
		er.erase(2);
		EXPECT_FALSE(er.contains(2));
		er.erase(0);
		EXPECT_FALSE(er.contains(0));
		er.erase(4);
		EXPECT_FALSE(er.contains(4));
		EXPECT_TRUE(er.contains(1));
		EXPECT_TRUE(er.contains(3));
	}

	TEST(EntitiesRanges, InsertAndMergeRanges) {
		ER er;
		er.insert(0);
		er.insert(2);
		er.insert(4);
		er.insert(1);
		er.insert(3);
		er.mergeIntersections();
		ASSERT_EQ(er.size(), 1u);
		EXPECT_EQ(er.ranges[0], Range(0, 5));
	}

	TEST(EntitiesRanges, ClearAndReuse) {
		ER er;
		for (int i = 0; i < 10; ++i) er.insert(i);
		er.clear();
		EXPECT_TRUE(er.empty());
		er.insert(42);
		EXPECT_TRUE(er.contains(42));
	}

	TEST(EntitiesRanges, ContainsAndGetAll) {
		ER er;
		for (int i = 0; i < 10; ++i) er.insert(i * 2);
		for (int i = 0; i < 20; ++i) {
			if (i % 2 == 0)
				EXPECT_TRUE(er.contains(i));
			else
				EXPECT_FALSE(er.contains(i));
		}
		auto all = er.getAll();
		EXPECT_EQ(all.size(), 10u);
		for (int i = 0; i < 10; ++i)
			EXPECT_EQ(all[i], i * 2);
	}

	TEST(EntitiesRanges, TakeInsertEraseMix) {
		ER er;
		for (int i = 0; i < 100; ++i) er.insert(i);
		for (int i = 0; i < 50; ++i) er.erase(i);
		for (int i = 50; i < 100; ++i) EXPECT_TRUE(er.contains(i));
		for (int i = 0; i < 50; ++i) EXPECT_FALSE(er.contains(i));
		for (int i = 0; i < 50; ++i) er.insert(i);
		er.mergeIntersections();
		EXPECT_EQ(er.size(), 1u);
		EXPECT_EQ(er.ranges[0], Range(0, 100));
	}

	TEST(EntitiesRanges, PopFrontBack) {
		ER er;
		er.insert(0);
		er.insert(2);
		er.insert(4);
		EXPECT_EQ(er.front(), Range(0, 1));
		
		er.pop_back();
		EXPECT_EQ(er.back(), Range(2, 3));
	}

	TEST(EntitiesRanges, StressRandomOps) {
		ER er;
		std::set<ecss::EntityId> groundTruth;
		for (int i = 0; i < 10000; ++i) {
			int val = rand() % 20000;
			if (rand() % 2) {
				er.insert(val);
				groundTruth.insert(val);
			}
			else if (!groundTruth.empty()) {
				auto it = groundTruth.begin();
				std::advance(it, rand() % groundTruth.size());
				er.erase(*it);
				groundTruth.erase(*it);
			}
		}
		auto all = er.getAll();
		std::vector<ecss::EntityId> expected(groundTruth.begin(), groundTruth.end());
		EXPECT_EQ(all, expected);
	}

	TEST(EntitiesRanges, InsertAdjacentMerges) {
		ER er;
		er.insert(1);
		er.insert(2);
		er.insert(3);
		EXPECT_EQ(er.size(), 1u);
		er.insert(5);
		EXPECT_EQ(er.size(), 2u);
		er.insert(4);
		er.mergeIntersections();
		EXPECT_EQ(er.size(), 1u);
		EXPECT_EQ(er.ranges[0], Range(1, 6));
	}

	TEST(EntitiesRanges, EdgeCases) {
		ER er;
		er.erase(42); // erasing from empty
		er.insert(0);
		er.erase(0);
		EXPECT_TRUE(er.empty());
		er.insert(10);
		er.insert(12);
		er.erase(11); // erasing missing element
		EXPECT_TRUE(er.contains(10));
		EXPECT_TRUE(er.contains(12));
		EXPECT_FALSE(er.contains(11));
	}


	TEST(EntitiesRanges, eraseFromCenter) {
		ER er(std::vector<ER::range>{{0, 1200}, { 1210, 2000 }});
		er.erase(1220);

		auto res = std::vector<ER::range>{ {0, 1200}, { 1210, 1220 } ,{ 1221, 2000 } };
		EXPECT_EQ(er.ranges, res);
	}
}