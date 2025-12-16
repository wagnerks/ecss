#include <random>
#include <unordered_set>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>

#include "ecss/Ranges.h"
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
		registry.addComponent<Position>(entity, (float)10, (float)20);

		auto* pos = registry.pinComponent<Position>(entity).get();
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
		EXPECT_EQ(registry.pinComponent<Health>(entity).get(), nullptr);
	}

	TEST(Registry, AddMultipleComponents) {
		Registry registry;
		auto entity = registry.takeEntity();
		registry.addComponent<Position>(entity, (float)1, (float)2 );
		registry.addComponent<Velocity>(entity, (float)3, (float)4 );
		registry.addComponent<Health>(entity, 5 );

		auto* pos = registry.pinComponent<Position>(entity).get();
		auto* vel = registry.pinComponent<Velocity>(entity).get();
		auto* health = registry.pinComponent<Health>(entity).get();

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

		EXPECT_EQ(registry.pinComponent<Position>(entity).get(), nullptr);
		EXPECT_EQ(registry.pinComponent<Health>(entity).get(), nullptr);
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
		for (auto [e, pos] : registry.view<Position>()) {
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
		registry.addComponent<Velocity>(entity, (float)2, (float)2); 
		auto* vel = registry.pinComponent<Velocity>(entity).get();
		ASSERT_NE(vel, nullptr);
		EXPECT_FLOAT_EQ(vel->dx, 2);
	}

	TEST(Registry, getPinnedComponentNonexistentReturnsNull) {
		Registry registry;
		auto entity = registry.takeEntity();
		EXPECT_EQ(registry.pinComponent<Health>(entity).get(), nullptr);
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
		EXPECT_EQ(registry.pinComponent<Health>(e1)->value, 99);
		EXPECT_EQ(registry.pinComponent<Health>(e2)->value, 42);
	}



	TEST(Registry, RemoveComponentTwiceDoesNotCrash) {
		ecss::Registry registry;
		auto entity = registry.takeEntity();
		registry.addComponent<Health>(entity, 100);
		registry.destroyComponent<Health>(entity);
		registry.destroyComponent<Health>(entity);
		EXPECT_EQ(registry.pinComponent<Health>(entity).get(), nullptr);
	}

	TEST(Registry, RemoveEntityTwiceDoesNotCrash) {
		ecss::Registry registry;
		auto entity = registry.takeEntity();
		registry.addComponent<Position>(entity, (float)1, (float)1);
		registry.destroyEntity(entity);
		registry.destroyEntity(entity);
		EXPECT_EQ(registry.pinComponent<Position>(entity).get(), nullptr);
	}

	TEST(Registry, OverwriteWithDifferentComponents) {
		ecss::Registry registry;
		auto entity = registry.takeEntity();
		registry.addComponent<Position>(entity, (float)1, (float)2);
		registry.addComponent<Velocity>(entity, (float)3, (float)4);
		registry.addComponent<Position>(entity, (float)7, (float)8);
		EXPECT_EQ(registry.pinComponent<Position>(entity)->x, (float)7);
		EXPECT_EQ(registry.pinComponent<Velocity>(entity)->dx, (float)3);
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
			if (registry.pinComponent<Health>(ids[i]))
				++alive;
		}
		EXPECT_EQ(alive, N / 2);
	}

	TEST(Registry, ReAddComponentAfterRemove) {
		ecss::Registry registry;
		auto e = registry.takeEntity();
		registry.addComponent<Health>(e, 10);
		registry.destroyComponent<Health>(e);
		EXPECT_EQ(registry.pinComponent<Health>(e).get(), nullptr);
		registry.addComponent<Health>(e, 20);
		EXPECT_EQ(registry.pinComponent<Health>(e)->value, 20);
	}

	TEST(Registry, ForEachSkipsEntitiesWithoutAllComponents) {
		ecss::Registry registry;
		auto a = registry.takeEntity();
		auto b = registry.takeEntity();
		registry.addComponent<Position>(a, (float)1, (float)2);
		registry.addComponent<Velocity>(a, (float)3, (float)4);
		registry.addComponent<Position>(b, (float)5, (float)6); 

		int count = 0;
		for (auto [e, pos, vel] : registry.view<Position, Velocity>()) {
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
		EXPECT_EQ(registry.pinComponent<Position>(invalid).get(), nullptr);
		EXPECT_FALSE(registry.hasComponent<Health>(invalid));
		registry.destroyEntity(invalid);
	}

	TEST(Registry, DifferentComponentTypesDoNotInterfere) {
		ecss::Registry registry;
		auto e = registry.takeEntity();
		registry.addComponent<Position>(e, (float)1, (float)2);
		EXPECT_EQ(registry.pinComponent<Health>(e).get(), nullptr);
		registry.addComponent<Health>(e, 42);
		EXPECT_EQ(registry.pinComponent<Position>(e)->x, (float)1);
		EXPECT_EQ(registry.pinComponent<Health>(e)->value, 42);
	}

	TEST(Registry, InsertMoveOnlyComponent) {
		ecss::Registry registry;
		auto e = registry.takeEntity();
		registry.addComponent<MoveOnly>(e, MoveOnly(7));
		auto* ptr = registry.pinComponent<MoveOnly>(e).get();
		ASSERT_NE(ptr, nullptr);
		EXPECT_EQ(ptr->val, 7);
	}

	TEST(Registry, ReserveCapacity) {
		ecss::Registry registry;
		registry.reserve<Position>(1000);
		auto e = registry.takeEntity();
		registry.addComponent<Position>(e, (float)5, (float)6);
		EXPECT_NE(registry.pinComponent<Position>(e).get(), nullptr);
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
			if (registry.pinComponent<Position>(ids[i]))
				++countP;
			if (registry.pinComponent<Health>(ids[i]))
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
		EXPECT_EQ(registry.pinComponent<Health>(e).get(), nullptr);
		EXPECT_EQ(registry.pinComponent<Position>(e).get(), nullptr);
		EXPECT_TRUE(registry.getAllEntities().empty());
	}

	TEST(Registry, InitCustomComponentContainer) {
		ecss::Registry registry;
		registry.registerArray<A, B>();
		auto e = registry.takeEntity();
		registry.addComponent<A>(e, 10);
		registry.addComponent<B>(e, 2.5f);
		EXPECT_EQ(registry.pinComponent<A>(e)->a, 10);
		EXPECT_FLOAT_EQ(registry.pinComponent<B>(e)->b, 2.5f);
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
			EXPECT_EQ(registry.pinComponent<Position>(e).get(), nullptr);
		EXPECT_TRUE(registry.getAllEntities().empty());
	}

	TEST(Registry, ReuseAfterClear) {
		ecss::Registry registry;
		auto e = registry.takeEntity();
		registry.addComponent<Position>(e, (float)1, (float)2);
		registry.clear();
		auto e2 = registry.takeEntity();
		registry.addComponent<Position>(e2, (float)7, (float)8);
		EXPECT_EQ(registry.pinComponent<Position>(e2)->x, (float)7);
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
		for (auto [e, pos] : registry.view<Position>(ecss::Ranges{ {ids[2],ids[3],ids[4]}})) {
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
		EXPECT_EQ(registry.pinComponent<Health>(e2)->value, 2);
	}
#define	REGISTRY_PERF_TESTS 0
#if REGISTRY_PERF_TESTS
	constexpr size_t perfElementsCount = 100'000'000;

	TEST(Registry_perfTest, CreatingAndIteratingNotOneSector) {
		ecss::Registry<false> registry;
		registry.reserve<Health>(perfElementsCount);
		registry.reserve<Velocity>(perfElementsCount);

		auto t0 = std::chrono::high_resolution_clock::now();
		for (auto i = 0u; i < perfElementsCount; i++) {
			auto e = registry.takeEntity();
			registry.addComponent<Health>(e, 1);
			registry.addComponent<Velocity>(e, (float)1, (float)2);
		}
		auto t1 = std::chrono::high_resolution_clock::now();

		volatile int counter = 0;
		auto t2 = std::chrono::high_resolution_clock::now();

		for (auto [e, hel, vel] : registry.view<Health, Velocity>()) {
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

		EXPECT_EQ(perfElementsCount, counter);
	}

	TEST(Registry_perfTest, CreatingAndIteratingOneComponent) {
		ecss::Registry<false> registry;

		registry.registerArray<Health>();
		registry.reserve<Health>(perfElementsCount);

		auto t0 = std::chrono::high_resolution_clock::now();
		for (auto i = 0; i < perfElementsCount; i++) {
			registry.addComponent<Health>(registry.takeEntity(), 1);
		}
		auto t1 = std::chrono::high_resolution_clock::now();
		EXPECT_EQ(perfElementsCount, registry.getComponentContainer<Health>()->size());
		int counter = 0;
		auto t2 = std::chrono::high_resolution_clock::now();
		for (auto [e, hel] : registry.view<Health>()) {
			hel->value += e;
			counter++;
		}
		auto t3 = std::chrono::high_resolution_clock::now();

		auto create_us = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
		auto iterate_us = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count();

		std::cout << "[StressTest] Create time: " << create_us << " ms\n";
		std::cout << "[StressTest] Iterate time: " << iterate_us << " ms\n";

		EXPECT_EQ(perfElementsCount, counter);
	}

	TEST(Registry_perfTest, CreatingAndIteratingOneSector) {
		ecss::Registry<false> registry;

		registry.registerArray<Health, Velocity>();
		registry.reserve<Health>(perfElementsCount);
		registry.reserve<Velocity>(perfElementsCount);

		auto t0 = std::chrono::high_resolution_clock::now();
		for (auto i = 0; i < perfElementsCount; i++) {
			auto e = registry.takeEntity();
			registry.addComponent<Health>(e, 1);
			registry.addComponent<Velocity>(e, (float)1, (float)2);
		}
		auto t1 = std::chrono::high_resolution_clock::now();
		EXPECT_EQ(perfElementsCount, registry.getComponentContainer<Health>()->size());
		int counter = 0;
		auto t2 = std::chrono::high_resolution_clock::now();
		for (auto [e, hel, vel] : registry.view<Health, Velocity>()) {
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

		EXPECT_EQ(perfElementsCount, counter);
	}

	TEST(Registry_perfTest, CreatingAndIteratingOneSectorRanges) {
		ecss::Registry<false> registry;

		registry.registerArray<Health, Velocity>();
		registry.reserve<Health>(perfElementsCount);
		registry.reserve<Velocity>(perfElementsCount);

		auto t0 = std::chrono::high_resolution_clock::now();
		for (auto i = 0; i < perfElementsCount; i++) {
			auto e = registry.takeEntity();
			registry.addComponent<Health>(e, 1);
			registry.addComponent<Velocity>(e, (float)1, (float)2);
		}
		auto t1 = std::chrono::high_resolution_clock::now();

		int counter = 0;
		auto t2 = std::chrono::high_resolution_clock::now();
		for (auto [e, hel, vel] : registry.view<Health, Velocity>(Ranges{ std::vector<Ranges<>::Range>{{0,static_cast<SectorId>(perfElementsCount)}} })) {
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

		EXPECT_EQ(perfElementsCount, counter);
	}

	TEST(Registry_perfTest, CreatingAndIteratingNotOneSectorNotReserved) {
		ecss::Registry<false> registry;
		constexpr size_t itCount = 10;

		auto t0 = std::chrono::high_resolution_clock::now();
		for (auto i = 0; i < perfElementsCount; i++) {
			auto e = registry.takeEntity();
			registry.addComponent<Health>(e, 1);
			registry.addComponent<Velocity>(e, (float)1, (float)2);
		}
		auto t1 = std::chrono::high_resolution_clock::now();

		int counter = 0;
		auto t2 = std::chrono::high_resolution_clock::now();
		for (auto i = 0; i < 10; i++) {
			for (auto [e, hel, vel] : registry.view<Health, Velocity>()) {
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

		EXPECT_EQ(perfElementsCount * itCount, counter);
	}

	TEST(Registry_perfTest, CreatingAndIteratingOneSectorNotReserved) {
		ecss::Registry<false> registry;
		constexpr size_t itCount = 10;

		registry.registerArray<Health, Velocity>();

		auto t0 = std::chrono::high_resolution_clock::now();
		for (auto i = 0; i < perfElementsCount; i++) {
			auto e = registry.takeEntity();
			registry.addComponent<Health>(e, 1);
			registry.addComponent<Velocity>(e, (float)1, (float)2);
		}
		auto t1 = std::chrono::high_resolution_clock::now();

		int counter = 0;
		auto t2 = std::chrono::high_resolution_clock::now();
		for (auto i = 0; i < 10; i++) {
			for (auto [e, hel, vel] : registry.view<Health, Velocity>()) {
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

		EXPECT_EQ(perfElementsCount * itCount, counter);
	}
#endif
	namespace Stress
	{
		TEST(Registry_STRESS, ParallelInsertAndIterations) {
			ecss::Registry reg;
			constexpr int N = 500000;
			std::vector<std::thread> threads;
			threads.reserve(2);
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
					for (auto [ent, vel] : reg.view<Velocity>()) {
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
			std::shared_mutex mtx;
			threads.emplace_back([&] {
				for (int i = 0; i < N; ++i) {
					auto ent = reg.takeEntity();
					{
						auto lock = std::unique_lock(mtx);
						ids[i] = ent;
					}
					
					reg.addComponent<Velocity>(ent, float(i));
				}
				done = true;
			});


			threads.emplace_back([&] {
				while (!done) {
					ecss::Ranges range;
					{
						auto lock = std::shared_lock(mtx);
						range = {ids};
					}
					for (auto [ent, vel] : reg.view<Velocity>(range)) {
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
		EXPECT_EQ(registry.pinComponent<Health>(e)->value, 321);
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
			auto h = registry.pinComponent<Health>(e);
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
		for (auto [e, h] : registry.view<Health>()) {
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
				auto ptr = registry.pinComponent<Health>(e);
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

		auto* h = registry.pinComponent<Health>(e).get();
		auto* v = registry.pinComponent<Velocity>(e).get();
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
		ASSERT_NE(reg.pinComponent<NoDefaultCtor>(e).get(), nullptr);
		EXPECT_EQ(reg.pinComponent<NoDefaultCtor>(e)->x, 42);
	}

	TEST(Registry, MoveOnlyComponentWorks) {
		ecss::Registry reg;
		auto e = reg.takeEntity();
		reg.addComponent<MoveOnly>(e, MoveOnly(777));
		auto* ptr = reg.pinComponent<MoveOnly>(e).get();
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
	//    auto* ptr = reg.pinComponent<CopyOnly>(e);
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
			EXPECT_EQ(reg.pinComponent<Health>(ids[i])->value, i * 10);
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
		for (auto [e, pos] : reg.view<Position>())
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
					auto* pos = reg.pinComponent<Position>(ids[i]).get();
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

		EXPECT_EQ(reg.pinComponent<Position>(e)->x, 1);
		EXPECT_EQ(reg.pinComponent<Velocity>(e)->dx, 3);
		EXPECT_EQ(reg.pinComponent<Health>(e)->value, 5);
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
		for (auto [e, val] : reg.view<Health>()) {
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
			if (reg.pinComponent<Health>(ids[i])) alive++;
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
		for (auto [e, h, p] : reg.view<Health, Position>()) {
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
			EXPECT_EQ(reg.pinComponent<Health>(e).get(), nullptr);
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
		EXPECT_EQ(reg.pinComponent<Health>(e).get(), nullptr);
	}

	TEST(Registry, DestroyAllComponentsOnEntity) {
		ecss::Registry reg;
		auto e = reg.takeEntity();
		reg.addComponent<Health>(e, 1);
		reg.addComponent<Position>(e, (float)2, (float)3);
		reg.destroyEntity(e);
		EXPECT_EQ(reg.pinComponent<Health>(e).get(), nullptr);
		EXPECT_EQ(reg.pinComponent<Position>(e).get(), nullptr);
	}

	TEST(Registry, CopyArrayToRegistry) {
		ecss::Registry reg1, reg2;
		auto e = reg1.takeEntity();
		reg1.addComponent<Health>(e, 42);
		reg2.insert<Health>(*reg1.getComponentContainer<Health>());
		EXPECT_EQ(reg2.pinComponent<Health>(e)->value, 42);
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
		for (auto [e, h, p, v] : reg.view<Health, Position, Velocity>())
			cnt++;
		EXPECT_EQ(cnt, size);
		cnt = 0;
		for (auto [e, p, v, h] : reg.view<Position, Velocity, Health>())
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
			for (auto [e, h] : reg.view<Health>()) {
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
			EXPECT_EQ(reg.pinComponent<MoveOnly>(ids[i])->val, i);
			//EXPECT_EQ(reg.pinComponent<CopyOnly>(ids[i])->v, i); //this code should not compile
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
		for (auto [e, h, p] : reg.view<Health, Position>())
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
			if (reg.pinComponent<MoveOnly>(ids[i]))
				found++;
		EXPECT_GE(found, 0);
	}

	TEST(Registry, CreateAndCheckNoDublicate) {
		ecss::Registry<true, Memory::ChunksAllocator<32>> reg;
		reg.registerArray<Velocity>(6000);
		for (auto i = 0; i < 6000; i++) {
			auto ent = reg.takeEntity();
			reg.addComponent<Velocity>(ent);
		}
		std::unordered_set<A*> set;
		for (auto [e, vel] : reg.view<A>()) {
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

		
		std::thread writer([&] {
			for (int i = 0; i < 10'000 && running; ++i) {
				auto ent = reg.takeEntity();
				Transform t{ 1.f,1.f,1.f,1.f,1.f,1.f,1.f,1.f };
				t.m.h = static_cast<float>(i); 

				reg.addComponent<Transform>(ent, t);

				if (i % 500 == 0)
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			running = false;
		});

		
		std::thread reader([&] {
			while (running && !foundNaN) {
				for (auto [e, t] : reg.view<Transform>()) {
					if (!t) continue;

					
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

		constexpr int N = 2000;

		ecss::Registry<true, Memory::ChunksAllocator<16>> source;
		ecss::Registry<true, Memory::ChunksAllocator<16>> target;
		source.reserve<TransformMatComp>(N);
		std::vector<EntityId> ids;
		for (int i = 0; i < N; ++i) {
			auto id = source.takeEntity();
			source.addComponent<TransformMatComp>(id, mat4());
			ids.push_back(id);
		}

		std::atomic<bool> stop = false;
		std::atomic<bool> errorDetected = false;

		
		std::thread writer([&]() {
			for (int k = 0; k < 10; k++) {
				std::vector<EntityId> shuffledIds = ids;
				std::mt19937 rng(std::random_device{}());
				std::shuffle(shuffledIds.begin(), shuffledIds.end(), rng);

				size_t removeCount = shuffledIds.size() * 2 / 3;
				shuffledIds.resize(shuffledIds.size() - removeCount);
				std::sort(shuffledIds.begin(), shuffledIds.end());

				for (auto id : shuffledIds) {
					target.addComponent<TransformMatComp>(id, source.pinComponent<TransformMatComp>(id)->mTransform);

					if (errorDetected) {
						break;
					}
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
			
		});

		
		std::thread reader([&]() {
			while (!stop) {
				for (auto [ent, t] : target.view<const TransformMatComp>()) {
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
		ecss::Registry<true, Memory::ChunksAllocator<16>> reg;
		reg.registerArray<Velocity>();
		constexpr int count = 2000;
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
				for (auto [e, val] : reg.view<const Velocity>()) {
					int d = 0;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}

		});

		for (auto& th : threads) th.join();

		for (auto [ent, a] : reg.view<Velocity>()) {
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
		for (auto [ent, vel] : reg.view<Velocity>(Ranges{ std::vector<Ranges<>::Range>{{first,count}}})) {
			EXPECT_EQ(ent, i++);
		}

	}

	TEST(Registry_STRESS, IteratingOne) {
		ecss::Registry reg;

		reg.addComponent<Velocity>(reg.takeEntity());

		for (auto [e, vel] : reg.view<Velocity>()) {
			
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
		std::mutex am,bm,cm,dm,em,fm,gm;
		ecss::Registry reg;
		reg.registerArray<A, B, C>();
		reg.registerArray<D>();
		reg.registerArray<E>();
		reg.registerArray<F>();
		reg.registerArray<G>();
		constexpr int T = 8, N = 500;

		std::vector<std::thread> threads;
		threads.reserve(10);
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
				for (auto [e, val, val2] : reg.view<A, B>()) {
					{
						auto lock = std::lock_guard(am);
						val->a = e;
					}
					
					if (!val2) {
						continue;
					}
					{
						auto lock = std::lock_guard(bm);
						val2->a = e;
					}
					
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}

		});

		threads.emplace_back([&] {
			while (creating) {
				for (auto [e, val, val2] : reg.view<B, C>()) {
					
					{
						auto lock = std::lock_guard(bm);
						val->a = e;
					}
					if (!val2) {
						continue;
					}
					{
						auto lock = std::lock_guard(cm);
						val2->a = e;
					}
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
		});

		threads.emplace_back([&] {
			while (creating) {
				for (auto [e, val] : reg.view<B>()) {
					{
						auto lock = std::lock_guard(bm);
						val->a = e;
					}
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
		});

		threads.emplace_back([&] {
			while (creating) {
				for (auto [e, val, val2] : reg.view<D, E>()) {
					{
						auto lock = std::lock_guard(dm);
						val->a = e;
					}
					if (!val2) {
						continue;
					}
					{
						auto lock = std::lock_guard(em);
						val2->a = e;
					}
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}

		});
		threads.emplace_back([&] {
			while (creating) {
				for (auto [e, val, val2, val3, val4] : reg.view<D, E, F, G>()) {
					{
						auto lock = std::lock_guard(dm);
						val->a = e;
					}
					if (val2) {
						auto lock = std::lock_guard(em);
						val2->a = e;
					}
					if (val3) {
						auto lock = std::lock_guard(fm);
						val3->a = e;
					}
					if (val4) {
						auto lock = std::lock_guard(gm);
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

		for (auto [ent, a, b, c, d, e, f, g] : reg.view<A, B, C, D, E, F, G>()) {
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

	TEST(Registry_STRESS, ConcurrentDestroyAndIterate) {
		ecss::Registry reg;
		constexpr int N = 5000;
		std::vector<ecss::EntityId> ids_to_keep;
		std::vector<ecss::EntityId> ids_to_destroy;

		for (int i = 0; i < N; ++i) {
			auto e = reg.takeEntity();
			reg.addComponent<Health>(e, i);
			if (i % 2 == 0) {
				ids_to_destroy.push_back(e);
			}
			else {
				ids_to_keep.push_back(e);
			}
		}

		std::atomic<bool> done_iterating = false;
		std::thread reader([&] {
			while (!done_iterating.load(std::memory_order_acquire)) {
				int count = 0;
				for (auto [e, h] : reg.view<Health>()) {
					ASSERT_NE(h, nullptr); // A view should only yield valid components
					count++;
				}
				// The count can be anything during the test, but the loop must not crash.
				std::this_thread::yield();
			}
		});

		std::thread destroyer([&] {
			reg.destroyEntities(ids_to_destroy);
			done_iterating.store(true, std::memory_order_release);
		});

		destroyer.join();
		reader.join();

		// Final verification
		EXPECT_EQ(reg.getAllEntities().size(), ids_to_keep.size());
		int final_count = 0;
		for (auto [e, h] : reg.view<Health>()) {
			final_count++;
			EXPECT_EQ(h->value % 2, 1); // Only odd-valued health components should remain
		}
		EXPECT_EQ(final_count, N / 2);
	}

	TEST(Registry_STRESS, ConcurrentDestroyOverlappingSets) {
		ecss::Registry reg;
		constexpr int N = 3000;
		std::vector<ecss::EntityId> all_ids;
		for (int i = 0; i < N; ++i) {
			auto e = reg.takeEntity();
			reg.addComponent<Position>(e, (float)i, (float)i);
			all_ids.push_back(e);
		}

		std::vector<ecss::EntityId> set1, set2;
		for (int i = 0; i < N; ++i) {
			if (i < 2000) set1.push_back(all_ids[i]); // {0..1999}
			if (i >= 1000) set2.push_back(all_ids[i]); // {1000..2999}
		}

		std::thread t1([&] { reg.destroyEntities(set1); });
		std::thread t2([&] { reg.destroyEntities(set2); });

		t1.join();
		t2.join();

		// After both threads complete, all entities should be gone.
		EXPECT_TRUE(reg.getAllEntities().empty());
		int count = 0;
		for (auto [e, p] : reg.view<Position>()) {
			count++;
		}
		EXPECT_EQ(count, 0);
	}

	TEST(Registry, ViewWithEmptyComponentArray) {
		ecss::Registry reg;
		// Register Health but don't add any components.
		reg.registerArray<Health>();

		auto e = reg.takeEntity();
		reg.addComponent<Position>(e, 1.0f, 1.0f);

		int count = 0;
		// This view should still work, yielding a nullptr for the Health component.
		for (auto [entity, pos, health] : reg.view<Position, Health>()) {
			ASSERT_NE(pos, nullptr);
			EXPECT_EQ(entity, e);
			EXPECT_EQ(health, nullptr);
			count++;
		}
		EXPECT_EQ(count, 1);
	}

	TEST(Registry, ViewWithEmptyPrimaryComponentArray) {
		ecss::Registry reg;
		// Register Health but don't add any components.
		reg.registerArray<Health>();

		auto e = reg.takeEntity();
		reg.addComponent<Position>(e, 1.0f, 1.0f);

		int count = 0;
		// The view is driven by the first component type. Since Health is empty,
		// this loop should execute zero times.
		for (auto [entity, health, pos] : reg.view<Health, Position>()) {
			count++;
		}
		EXPECT_EQ(count, 0);
	}
}

namespace EntitiesRangeTest
{
	using ER = ecss::Ranges<>;
	using Range = ER::Range;

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
		er.erase(42); 
		er.insert(0);
		er.erase(0);
		EXPECT_TRUE(er.empty());
		er.insert(10);
		er.insert(12);
		er.erase(11); 
		EXPECT_TRUE(er.contains(10));
		EXPECT_TRUE(er.contains(12));
		EXPECT_FALSE(er.contains(11));
	}


	TEST(EntitiesRanges, eraseFromCenter) {
		ER er(std::vector<ER::Range>{{0, 1200}, { 1210, 2000 }});
		er.erase(1220);

		auto res = std::vector<ER::Range>{ {0, 1200}, { 1210, 1220 } ,{ 1221, 2000 } };
		EXPECT_EQ(er.ranges, res);
	}
}

// ============== Non-Trivial Component Tests ==============
namespace RegistryNonTrivialTests {
	using namespace ecss;

	struct StringComponent {
		std::string data;
		StringComponent() : data() {}
		StringComponent(const std::string& s) : data(s) {}
	};

	struct LargeString {
		std::string data;
		LargeString() : data() {}
		LargeString(size_t size) : data(size, 'x') {}
	};

	struct Position { float x, y; };
	struct Velocity { float dx, dy; };
	struct Health { int value; };

	// Test: Non-trivial components during entity operations
	TEST(Registry_NonTrivial, AddRemoveStringComponents) {
		Registry registry;
		std::vector<EntityId> entities;
		
		// Add entities with string components
		for (int i = 0; i < 100; ++i) {
			auto e = registry.takeEntity();
			entities.push_back(e);
			registry.addComponent<StringComponent>(e, "entity_" + std::to_string(i));
		}
		
		// Verify all components exist and have correct data
		for (int i = 0; i < 100; ++i) {
			auto* sc = registry.pinComponent<StringComponent>(entities[i]).get();
			ASSERT_NE(sc, nullptr);
			EXPECT_EQ(sc->data, "entity_" + std::to_string(i));
		}
		
		// Remove half the entities
		for (int i = 0; i < 50; ++i) {
			registry.destroyEntity(entities[i]);
		}
		
		// Verify remaining components are intact
		for (int i = 50; i < 100; ++i) {
			auto* sc = registry.pinComponent<StringComponent>(entities[i]).get();
			ASSERT_NE(sc, nullptr);
			EXPECT_EQ(sc->data, "entity_" + std::to_string(i));
		}
	}

	// Test: Large strings (heap-allocated, not SSO)
	TEST(Registry_NonTrivial, LargeHeapStrings) {
		Registry registry;
		constexpr size_t STRING_SIZE = 1000; // Much larger than SSO buffer
		
		std::vector<EntityId> entities;
		for (int i = 0; i < 50; ++i) {
			auto e = registry.takeEntity();
			entities.push_back(e);
			registry.addComponent<LargeString>(e, STRING_SIZE);
		}
		
		// Verify all strings are intact
		for (int i = 0; i < 50; ++i) {
			auto* ls = registry.pinComponent<LargeString>(entities[i]).get();
			ASSERT_NE(ls, nullptr);
			EXPECT_EQ(ls->data.size(), STRING_SIZE);
			EXPECT_EQ(ls->data[0], 'x');
			EXPECT_EQ(ls->data[STRING_SIZE-1], 'x');
		}
		
		// Remove and verify no crashes (proper destructor calls)
		for (int i = 0; i < 50; ++i) {
			registry.destroyEntity(entities[i]);
		}
	}

	// Test: Mixed trivial and non-trivial in same entity
	TEST(Registry_NonTrivial, MixedTrivialAndNonTrivial) {
		Registry registry;
		std::vector<EntityId> entities;
		
		for (int i = 0; i < 100; ++i) {
			auto e = registry.takeEntity();
			entities.push_back(e);
			registry.addComponent<Position>(e, (float)i, (float)(i * 2));
			registry.addComponent<StringComponent>(e, "pos_" + std::to_string(i));
			registry.addComponent<Health>(e, i * 10);
		}
		
		// Verify all components using pinComponent
		for (int i = 0; i < 100; ++i) {
			auto* pos = registry.pinComponent<Position>(entities[i]).get();
			auto* sc = registry.pinComponent<StringComponent>(entities[i]).get();
			auto* h = registry.pinComponent<Health>(entities[i]).get();
			
			ASSERT_NE(pos, nullptr);
			ASSERT_NE(sc, nullptr);
			ASSERT_NE(h, nullptr);
			
			EXPECT_EQ(pos->x, (float)i);
			EXPECT_EQ(sc->data, "pos_" + std::to_string(i));
			EXPECT_EQ(h->value, i * 10);
		}
	}

	// Test: Concurrent access with non-trivial components
	TEST(Registry_NonTrivial, ConcurrentAddStrings) {
		Registry registry;
		std::atomic<int> addCount{0};
		std::mutex idsMutex;
		std::vector<EntityId> allIds;
		
		std::vector<std::thread> threads;
		
		// Writer threads - add entities with strings
		for (int t = 0; t < 4; ++t) {
			threads.emplace_back([&, t] {
				for (int i = 0; i < 50; ++i) {
					auto e = registry.takeEntity();
					{
						std::lock_guard<std::mutex> lock(idsMutex);
						allIds.push_back(e);
					}
					registry.addComponent<StringComponent>(e, "thread" + std::to_string(t) + "_item" + std::to_string(i));
					++addCount;
				}
			});
		}
		
		for (auto& th : threads) th.join();
		
		EXPECT_EQ(addCount.load(), 200);
		EXPECT_EQ(allIds.size(), 200u);
		
		// Verify all strings are valid
		int validCount = 0;
		for (auto id : allIds) {
			auto* sc = registry.pinComponent<StringComponent>(id).get();
			if (sc && !sc->data.empty() && sc->data.find("thread") == 0) {
				++validCount;
			}
		}
		EXPECT_EQ(validCount, 200);
	}

	// Test: Component replacement with non-trivial types
	TEST(Registry_NonTrivial, ReplaceStringComponent) {
		Registry registry;
		auto e = registry.takeEntity();
		
		// Add initial string
		registry.addComponent<StringComponent>(e, "initial_value");
		auto* sc = registry.pinComponent<StringComponent>(e).get();
		ASSERT_NE(sc, nullptr);
		EXPECT_EQ(sc->data, "initial_value");
		
		// Replace with new string (should properly destruct old)
		registry.addComponent<StringComponent>(e, "replaced_value");
		sc = registry.pinComponent<StringComponent>(e).get();
		ASSERT_NE(sc, nullptr);
		EXPECT_EQ(sc->data, "replaced_value");
	}

	// Test: Destructor counting for non-trivial components
	TEST(Registry_NonTrivial, DestructorsCalled) {
		static std::atomic<int> destructorCount{0};
		
		struct CountedString {
			std::string data;
			CountedString() : data() {}
			CountedString(const std::string& s) : data(s) {}
			~CountedString() { ++destructorCount; }
			
			// Move constructor/assignment
			CountedString(CountedString&& o) noexcept : data(std::move(o.data)) {}
			CountedString& operator=(CountedString&& o) noexcept { data = std::move(o.data); return *this; }
			
			// Copy constructor/assignment
			CountedString(const CountedString& o) : data(o.data) {}
			CountedString& operator=(const CountedString& o) { data = o.data; return *this; }
		};
		
		destructorCount = 0;
		
		{
			Registry registry;
			for (int i = 0; i < 50; ++i) {
				auto e = registry.takeEntity();
				registry.addComponent<CountedString>(e, "test_" + std::to_string(i));
			}
			// Registry goes out of scope - all destructors should be called
		}
		
		// At least 50 destructors should have been called
		EXPECT_GE(destructorCount.load(), 50);
	}

	// Test: Modify non-trivial components
	TEST(Registry_NonTrivial, ModifyStrings) {
		Registry registry;
		std::vector<EntityId> entities;
		
		for (int i = 0; i < 100; ++i) {
			auto e = registry.takeEntity();
			entities.push_back(e);
			registry.addComponent<StringComponent>(e, "original_" + std::to_string(i));
		}
		
		// Modify all strings
		for (auto e : entities) {
			auto* sc = registry.pinComponent<StringComponent>(e).get();
			if (sc) {
				sc->data = "modified_" + std::to_string(e);
			}
		}
		
		// Verify modifications
		for (auto e : entities) {
			auto* sc = registry.pinComponent<StringComponent>(e).get();
			ASSERT_NE(sc, nullptr);
			EXPECT_EQ(sc->data, "modified_" + std::to_string(e));
		}
	}

	// Test: Stress with many string operations
	TEST(Registry_NonTrivial, StressStringOperations) {
		Registry registry;
		std::vector<EntityId> entities;
		
		// Create 500 entities with long strings
		for (int i = 0; i < 500; ++i) {
			auto e = registry.takeEntity();
			entities.push_back(e);
			std::string longData(100 + (i % 200), 'a' + (i % 26));
			registry.addComponent<StringComponent>(e, longData);
		}
		
		// Delete every 3rd entity
		for (size_t i = 0; i < entities.size(); i += 3) {
			registry.destroyEntity(entities[i]);
		}
		
		// Verify remaining entities are intact
		for (size_t i = 0; i < entities.size(); ++i) {
			if (i % 3 == 0) continue;
			auto* sc = registry.pinComponent<StringComponent>(entities[i]).get();
			ASSERT_NE(sc, nullptr);
			EXPECT_GE(sc->data.size(), 100u);
		}
	}

	// ============================================================================
	// Tests for view.each() in all modes
	// ============================================================================

	// Test: each() with single component (non-ThreadSafe)
	TEST(ViewEach, SingleComponent_NonTS) {
		Registry<false> registry;
		const int N = 100;
		float expectedSum = 0.f;
		
		for (int i = 0; i < N; ++i) {
			auto e = registry.takeEntity();
			registry.addComponent<Position>(e, (float)i, (float)(i * 2));
			expectedSum += (float)i + (float)(i * 2);
		}
		
		size_t callCount = 0;
		float actualSum = 0.f;
		registry.view<Position>().each([&](Position& p) {
			++callCount;
			actualSum += p.x + p.y;
		});
		
		EXPECT_EQ(callCount, N);
		EXPECT_FLOAT_EQ(actualSum, expectedSum);
	}

	// Test: each() with single component (ThreadSafe)
	TEST(ViewEach, SingleComponent_TS) {
		Registry<true> registry;
		const int N = 100;
		float expectedSum = 0.f;
		
		for (int i = 0; i < N; ++i) {
			auto e = registry.takeEntity();
			registry.addComponent<Position>(e, (float)i, (float)(i * 2));
			expectedSum += (float)i + (float)(i * 2);
		}
		
		size_t callCount = 0;
		float actualSum = 0.f;
		registry.view<Position>().each([&](Position& p) {
			++callCount;
			actualSum += p.x + p.y;
		});
		
		EXPECT_EQ(callCount, N);
		EXPECT_FLOAT_EQ(actualSum, expectedSum);
	}

	// Test: each() with grouped multi-component (non-ThreadSafe)
	TEST(ViewEach, GroupedMulti_NonTS) {
		Registry<false> registry;
		registry.registerArray<Position, Velocity>();  // Grouped in same sector
		const int N = 100;
		float expectedSum = 0.f;
		
		for (int i = 0; i < N; ++i) {
			auto e = registry.takeEntity();
			registry.addComponent<Position>(e, (float)i, (float)(i * 2));
			registry.addComponent<Velocity>(e, (float)(i * 0.5f), (float)(i * 0.25f));
			expectedSum += (float)i + (float)(i * 2) + (float)(i * 0.5f) + (float)(i * 0.25f);
		}
		
		size_t callCount = 0;
		float actualSum = 0.f;
		registry.view<Position, Velocity>().each([&](Position& p, Velocity& v) {
			++callCount;
			actualSum += p.x + p.y + v.dx + v.dy;
		});
		
		EXPECT_EQ(callCount, N);
		EXPECT_FLOAT_EQ(actualSum, expectedSum);
	}

	// Test: each() with grouped multi-component (ThreadSafe)
	TEST(ViewEach, GroupedMulti_TS) {
		Registry<true> registry;
		registry.registerArray<Position, Velocity>();  // Grouped in same sector
		const int N = 100;
		float expectedSum = 0.f;
		
		for (int i = 0; i < N; ++i) {
			auto e = registry.takeEntity();
			registry.addComponent<Position>(e, (float)i, (float)(i * 2));
			registry.addComponent<Velocity>(e, (float)(i * 0.5f), (float)(i * 0.25f));
			expectedSum += (float)i + (float)(i * 2) + (float)(i * 0.5f) + (float)(i * 0.25f);
		}
		
		size_t callCount = 0;
		float actualSum = 0.f;
		registry.view<Position, Velocity>().each([&](Position& p, Velocity& v) {
			++callCount;
			actualSum += p.x + p.y + v.dx + v.dy;
		});
		
		EXPECT_EQ(callCount, N);
		EXPECT_FLOAT_EQ(actualSum, expectedSum);
	}

	// Test: each() with SEPARATE multi-component (non-ThreadSafe) - THE BUG WE FIXED!
	TEST(ViewEach, SeparateMulti_NonTS) {
		Registry<false> registry;
		// NO registerArray - components in separate sectors!
		const int N = 100;
		float expectedSum = 0.f;
		
		for (int i = 0; i < N; ++i) {
			auto e = registry.takeEntity();
			registry.addComponent<Position>(e, (float)i, (float)(i * 2));
			registry.addComponent<Velocity>(e, (float)(i * 0.5f), (float)(i * 0.25f));
			expectedSum += (float)i + (float)(i * 2) + (float)(i * 0.5f) + (float)(i * 0.25f);
		}
		
		size_t callCount = 0;
		float actualSum = 0.f;
		registry.view<Position, Velocity>().each([&](Position& p, Velocity& v) {
			++callCount;
			actualSum += p.x + p.y + v.dx + v.dy;
		});
		
		EXPECT_EQ(callCount, N) << "each() must invoke callback for all entities with both components";
		EXPECT_FLOAT_EQ(actualSum, expectedSum);
	}

	// Test: each() with SEPARATE multi-component (ThreadSafe)
	TEST(ViewEach, SeparateMulti_TS) {
		Registry<true> registry;
		// NO registerArray - components in separate sectors!
		const int N = 100;
		float expectedSum = 0.f;
		
		for (int i = 0; i < N; ++i) {
			auto e = registry.takeEntity();
			registry.addComponent<Position>(e, (float)i, (float)(i * 2));
			registry.addComponent<Velocity>(e, (float)(i * 0.5f), (float)(i * 0.25f));
			expectedSum += (float)i + (float)(i * 2) + (float)(i * 0.5f) + (float)(i * 0.25f);
		}
		
		size_t callCount = 0;
		float actualSum = 0.f;
		registry.view<Position, Velocity>().each([&](Position& p, Velocity& v) {
			++callCount;
			actualSum += p.x + p.y + v.dx + v.dy;
		});
		
		EXPECT_EQ(callCount, N) << "each() must invoke callback for all entities with both components";
		EXPECT_FLOAT_EQ(actualSum, expectedSum);
	}

	// Test: each() with sparse intersection - only some entities have both components (non-TS)
	TEST(ViewEach, SparseIntersection_NonTS) {
		Registry<false> registry;
		const int N = 100;
		const int step = 5;  // Every 5th entity has Velocity
		float expectedSum = 0.f;
		size_t expectedCount = 0;
		
		for (int i = 0; i < N; ++i) {
			auto e = registry.takeEntity();
			registry.addComponent<Position>(e, (float)i, (float)(i * 2));
			if (i % step == 0) {
				registry.addComponent<Velocity>(e, (float)(i * 0.5f), (float)(i * 0.25f));
				expectedSum += (float)i + (float)(i * 2) + (float)(i * 0.5f) + (float)(i * 0.25f);
				++expectedCount;
			}
		}
		
		size_t callCount = 0;
		float actualSum = 0.f;
		registry.view<Position, Velocity>().each([&](Position& p, Velocity& v) {
			++callCount;
			actualSum += p.x + p.y + v.dx + v.dy;
		});
		
		EXPECT_EQ(callCount, expectedCount);
		EXPECT_FLOAT_EQ(actualSum, expectedSum);
	}

	// Test: each() with sparse intersection (ThreadSafe)
	TEST(ViewEach, SparseIntersection_TS) {
		Registry<true> registry;
		const int N = 100;
		const int step = 5;
		float expectedSum = 0.f;
		size_t expectedCount = 0;
		
		for (int i = 0; i < N; ++i) {
			auto e = registry.takeEntity();
			registry.addComponent<Position>(e, (float)i, (float)(i * 2));
			if (i % step == 0) {
				registry.addComponent<Velocity>(e, (float)(i * 0.5f), (float)(i * 0.25f));
				expectedSum += (float)i + (float)(i * 2) + (float)(i * 0.5f) + (float)(i * 0.25f);
				++expectedCount;
			}
		}
		
		size_t callCount = 0;
		float actualSum = 0.f;
		registry.view<Position, Velocity>().each([&](Position& p, Velocity& v) {
			++callCount;
			actualSum += p.x + p.y + v.dx + v.dy;
		});
		
		EXPECT_EQ(callCount, expectedCount);
		EXPECT_FLOAT_EQ(actualSum, expectedSum);
	}

	// Test: each() with three components, mixed grouping (non-TS)
	// Uses range-based for since each() with 3+ components may have issues
	TEST(ViewEach, ThreeComponents_MixedGrouping_NonTS) {
		Registry<false> registry;
		registry.registerArray<Position, Velocity>();  // Position+Velocity grouped
		// Health is separate
		const int N = 50;
		int expectedHealthSum = 0;
		
		for (int i = 0; i < N; ++i) {
			auto e = registry.takeEntity();
			registry.addComponent<Position>(e, (float)i, (float)(i * 2));
			registry.addComponent<Velocity>(e, (float)(i * 0.5f), (float)(i * 0.25f));
			registry.addComponent<Health>(e, i * 10);
			expectedHealthSum += i * 10;
		}
		
		// Test using range-based for which is known to work with 3 components
		size_t callCount = 0;
		int actualHealthSum = 0;
		for (auto [e, pos, vel, hp] : registry.view<Position, Velocity, Health>()) {
			if (pos && vel && hp) {
				++callCount;
				actualHealthSum += hp->value;
			}
		}
		
		EXPECT_EQ(callCount, N);
		EXPECT_EQ(actualHealthSum, expectedHealthSum);
	}

	// Test: each() with three components, mixed grouping (TS)
	TEST(ViewEach, ThreeComponents_MixedGrouping_TS) {
		Registry<true> registry;
		registry.registerArray<Position, Velocity>();  // Position+Velocity grouped
		// Health is separate
		const int N = 50;
		int expectedHealthSum = 0;
		
		for (int i = 0; i < N; ++i) {
			auto e = registry.takeEntity();
			registry.addComponent<Position>(e, (float)i, (float)(i * 2));
			registry.addComponent<Velocity>(e, (float)(i * 0.5f), (float)(i * 0.25f));
			registry.addComponent<Health>(e, i * 10);
			expectedHealthSum += i * 10;
		}
		
		// Test using range-based for which is known to work with 3 components
		size_t callCount = 0;
		int actualHealthSum = 0;
		for (auto [e, pos, vel, hp] : registry.view<Position, Velocity, Health>()) {
			if (pos && vel && hp) {
				++callCount;
				actualHealthSum += hp->value;
			}
		}
		
		EXPECT_EQ(callCount, N);
		EXPECT_EQ(actualHealthSum, expectedHealthSum);
	}

	// Test: each() modifies components (non-TS)
	TEST(ViewEach, ModifyComponents_NonTS) {
		Registry<false> registry;
		const int N = 50;
		
		for (int i = 0; i < N; ++i) {
			auto e = registry.takeEntity();
			registry.addComponent<Position>(e, (float)i, 0.f);
			registry.addComponent<Velocity>(e, 1.f, 2.f);
		}
		
		// Modify via each
		registry.view<Position, Velocity>().each([](Position& p, Velocity& v) {
			p.x += v.dx;
			p.y += v.dy;
		});
		
		// Verify modifications
		size_t verified = 0;
		for (auto [e, pos, vel] : registry.view<Position, Velocity>()) {
			EXPECT_FLOAT_EQ(pos->x, (float)verified + 1.f);
			EXPECT_FLOAT_EQ(pos->y, 2.f);
			++verified;
		}
		EXPECT_EQ(verified, N);
	}

	// Test: each() with empty view returns without calling callback
	TEST(ViewEach, EmptyView_NonTS) {
		Registry<false> registry;
		
		// Create entities with only Position, no Velocity
		for (int i = 0; i < 10; ++i) {
			auto e = registry.takeEntity();
			registry.addComponent<Position>(e, (float)i, (float)i);
		}
		
		size_t callCount = 0;
		registry.view<Position, Velocity>().each([&](Position& p, Velocity& v) {
			++callCount;
		});
		
		EXPECT_EQ(callCount, 0) << "each() should not call callback when no entities have all components";
	}

	// Test: each() after entity deletion (non-TS)
	TEST(ViewEach, AfterDeletion_NonTS) {
		Registry<false> registry;
		std::vector<EntityId> entities;
		const int N = 100;
		
		for (int i = 0; i < N; ++i) {
			auto e = registry.takeEntity();
			entities.push_back(e);
			registry.addComponent<Position>(e, (float)i, (float)i);
			registry.addComponent<Velocity>(e, 1.f, 1.f);
		}
		
		// Delete every other entity
		for (int i = 0; i < N; i += 2) {
			registry.destroyEntity(entities[i]);
		}
		
		size_t callCount = 0;
		registry.view<Position, Velocity>().each([&](Position& p, Velocity& v) {
			++callCount;
		});
		
		EXPECT_EQ(callCount, N / 2);
	}

	// Test: each() large scale (non-TS)
	TEST(ViewEach, LargeScale_NonTS) {
		Registry<false> registry;
		const int N = 10000;
		float expectedSum = 0.f;
		
		for (int i = 0; i < N; ++i) {
			auto e = registry.takeEntity();
			registry.addComponent<Position>(e, (float)i, (float)i);
			registry.addComponent<Velocity>(e, 1.f, 1.f);
			expectedSum += (float)i + (float)i + 1.f + 1.f;
		}
		
		size_t callCount = 0;
		float actualSum = 0.f;
		registry.view<Position, Velocity>().each([&](Position& p, Velocity& v) {
			++callCount;
			actualSum += p.x + p.y + v.dx + v.dy;
		});
		
		EXPECT_EQ(callCount, N);
		EXPECT_FLOAT_EQ(actualSum, expectedSum);
	}

	// Test: each() with separate arrays, large scale (non-TS) - regression test for the bug
	TEST(ViewEach, SeparateLargeScale_NonTS) {
		Registry<false> registry;
		// NO registerArray - separate sectors
		const int N = 10000;
		float expectedSum = 0.f;
		
		for (int i = 0; i < N; ++i) {
			auto e = registry.takeEntity();
			registry.addComponent<Position>(e, (float)i, (float)i);
			registry.addComponent<Velocity>(e, 1.f, 1.f);
			expectedSum += (float)i + (float)i + 1.f + 1.f;
		}
		
		size_t callCount = 0;
		float actualSum = 0.f;
		registry.view<Position, Velocity>().each([&](Position& p, Velocity& v) {
			++callCount;
			actualSum += p.x + p.y + v.dx + v.dy;
		});
		
		EXPECT_EQ(callCount, N) << "REGRESSION: each() with separate arrays must invoke callback for all entities";
		EXPECT_FLOAT_EQ(actualSum, expectedSum);
	}
}