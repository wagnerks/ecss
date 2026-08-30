// Regression tests for defects found and fixed while auditing the concurrency and
// storage layers. Each test names the failure it reproduces, so a future change that
// reintroduces one fails here with the reason attached rather than as a mystery hang.
//
// The deadlock tests deliberately leak their Registry when they time out: the stuck
// threads still hold locks on it, and destroying it underneath them would turn a clean
// test failure into a crash that hides the diagnosis.

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <random>
#include <thread>
#include <unordered_set>
#include <vector>

#include <ecss/IdSet.h>
#include <ecss/Registry.h>
#include <ecss/memory/ChunksAllocator.h>
#include <ecss/threads/PinCounters.h>

using namespace ecss;
using namespace ecss::Memory;

namespace {

struct RPos { float x{}, y{}, z{}; };
struct RVel { float dx{}, dy{}, dz{}; };
struct RInt { int v{}; };

/// @brief Spin until @p done or the deadline passes. Returns false on timeout.
template <class Pred>
bool waitFor(Pred done, std::chrono::milliseconds limit = std::chrono::seconds(5)) {
	const auto deadline = std::chrono::steady_clock::now() + limit;
	while (std::chrono::steady_clock::now() < deadline) {
		if (done()) { return true; }
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	return done();
}

} // namespace

// ===========================================================================
// Deferred erase clears the sparse slot but leaves the dense entry in place.
// Re-inserting the same id used to append a *second* dense entry: the sorted-id
// invariant broke, and the next defragment cleared the sparse slot on behalf of
// the dead twin, orphaning the live sector (iterable, but absent from lookups).
// ===========================================================================

TEST(Regression_DuplicateId, DeferredEraseThenReinsert_NonThreadSafe) {
	std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<RInt>());
	for (SectorId i = 0; i < 4; ++i) { arr->emplace<RInt>(i, RInt{ int(i) }); }

	arr->erase(arr->findLinearIdx(1), 1, /*defragment*/ false);
	arr->emplace<RInt>(1, RInt{ 111 });

	EXPECT_EQ(arr->size(), 4u) << "re-inserting an erased id must reuse its dense entry";
	for (size_t i = 1; i < arr->size(); ++i) {
		EXPECT_LT(arr->getId(i - 1), arr->getId(i)) << "dense ids must stay strictly ascending";
	}

	arr->defragment();
	EXPECT_TRUE(arr->findSlot(1)) << "defragment must not orphan a live sector";
}

TEST(Regression_DuplicateId, DeferredEraseThenReinsert_ThreadSafe) {
	std::unique_ptr<SectorsArray<true>> arr(SectorsArray<true>::create<RInt>());
	for (SectorId i = 0; i < 4; ++i) { arr->emplace<RInt>(i, RInt{ int(i) }); }

	arr->eraseAsync(1);
	arr->emplace<RInt>(1, RInt{ 111 });
	EXPECT_EQ(arr->size(), 4u);

	arr->processPendingErases(true);
	arr->defragment();
	ASSERT_TRUE(arr->findSlot(1));
	auto* c = Sector::getComponent<RInt>(arr->findSectorData(1), arr->getIsAlive(1), arr->getLayout());
	ASSERT_NE(c, nullptr);
	EXPECT_EQ(c->v, 111);
}

TEST(Regression_DuplicateId, ViewAndLookupNeverDisagree) {
	Registry<true> reg;
	for (EntityId e = 0; e <= 4; ++e) { reg.takeEntity(); reg.addComponent<RInt>(e, RInt{ int(e) }); }

	reg.getComponentContainer<RInt>()->eraseAsync(1);
	reg.addComponent<RInt>(1, RInt{ 111 });
	reg.update();
	reg.getComponentContainer<RInt>()->defragment();

	std::vector<EntityId> iterated;
	for (auto [id, c] : reg.view<RInt>()) { iterated.push_back(id); }
	for (auto id : iterated) {
		EXPECT_TRUE(reg.hasComponent<RInt>(id))
			<< "entity " << id << " is yielded by the view but invisible to hasComponent";
	}
}

// findInsertPositionImpl narrowed until right-left==1 and so never inspected index 0,
// returning 1 instead of 0 when the id equalled ids[0] -- which placed a resurrected id
// after its own dead entry and produced the duplicate above.
TEST(Regression_DuplicateId, ReinsertOfTheFirstId) {
	std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<RInt>());
	for (SectorId i = 5; i < 10; ++i) { arr->emplace<RInt>(i, RInt{ int(i) }); }

	arr->erase(arr->findLinearIdx(5), 1, /*defragment*/ false);
	arr->emplace<RInt>(5, RInt{ 55 });

	EXPECT_EQ(arr->size(), 5u);
	for (size_t i = 1; i < arr->size(); ++i) {
		EXPECT_LT(arr->getId(i - 1), arr->getId(i));
	}
}

TEST(Regression_DuplicateId, RandomizedModelCheck) {
	std::mt19937 rng(4242);
	for (int round = 0; round < 8; ++round) {
		std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<RInt>());
		std::map<SectorId, int> model;

		for (int step = 0; step < 300; ++step) {
			const SectorId id = rng() % 60;
			const int op = rng() % 100;
			if (op < 55) {
				arr->emplace<RInt>(id, RInt{ step });
				model[id] = step;
			}
			else if (op < 80) {
				const auto idx = arr->findLinearIdx(id);
				if (idx != INVALID_IDX) {
					arr->erase(idx, 1, (rng() % 2) == 0);
					model.erase(id);
				}
			}
			else {
				arr->defragment();
			}

			for (size_t i = 1; i < arr->size(); ++i) {
				ASSERT_LT(arr->getId(i - 1), arr->getId(i))
					<< "dense ids unsorted at round " << round << " step " << step;
			}
			for (const auto& [id2, want] : model) {
				const auto slot = arr->findSlot(id2);
				ASSERT_TRUE(slot) << "id " << id2 << " lost at round " << round << " step " << step;
				auto* c = Sector::getComponent<RInt>(slot.data, arr->getIsAlive(id2), arr->getLayout());
				ASSERT_NE(c, nullptr);
				ASSERT_EQ(c->v, want);
			}
		}
	}
}

// ===========================================================================
// A moved-from SectorsArray kept its sparse map, so it still claimed to own
// sectors and handed out pointers into the memory the destination now owned.
// ===========================================================================

TEST(Regression_Move, MovedFromArrayOwnsNothing) {
	std::unique_ptr<SectorsArray<false>> src(SectorsArray<false>::create<RInt>());
	for (SectorId i = 0; i < 5; ++i) { src->emplace<RInt>(i, RInt{ int(i) }); }
	std::unique_ptr<SectorsArray<false>> dst(SectorsArray<false>::create<RInt>());
	*dst = std::move(*src);

	EXPECT_EQ(src->size(), 0u);
	EXPECT_FALSE(src->containsSector(3)) << "moved-from array still claims a sector";
	EXPECT_EQ(src->findSectorData(3), nullptr) << "moved-from array aliases the destination memory";
	EXPECT_NE(dst->findSectorData(3), nullptr);
}

// ===========================================================================
// Ranges was templated on Type but hardcoded EntityId internally, truncating
// anything wider than 32 bits; and take() stranded ids freed below the first block.
// ===========================================================================

TEST(Regression_Ranges, WideTypeIsNotTruncated) {
	Ranges<uint64_t> r;
	const uint64_t big = (1ull << 40) + 7;
	r.insert(big);
	ASSERT_EQ(r.size(), 1u);
	EXPECT_EQ(r.front().first, big) << "value truncated to 32 bits";
	EXPECT_TRUE(r.contains(big));
	EXPECT_FALSE(r.contains(7));
}

TEST(Regression_Ranges, IdFreedAtTheFrontIsRecycled) {
	Ranges<EntityId> r;
	for (int i = 0; i < 5; ++i) { (void)r.take(); }
	r.erase(0);
	EXPECT_EQ(r.take(), 0u) << "an id freed below the first block must not be stranded";
	EXPECT_TRUE(r.contains(0));
}

TEST(Regression_Ranges, TakeBlockNeverStraddlesAllocatedIds) {
	Ranges<EntityId> r;
	for (int i = 0; i < 10; ++i) { (void)r.take(); }
	r.erase(4); // a hole of exactly one

	const auto [first, count] = r.takeBlock(8);
	ASSERT_GE(count, 1u);
	for (EntityId i = first; i < first + count; ++i) {
		EXPECT_TRUE(r.contains(i)) << "block id " << i << " reported free";
	}
}

// ===========================================================================
// Entity id allocation: two threads must never receive the same id, and clear()
// must reset the id space. Ranges::takeBlock() exists for a future batched
// allocator; it must never hand out a span that straddles allocated ids.
// ===========================================================================

TEST(Regression_EntityIds, ConcurrentTakeNeverHandsOutDuplicates) {
	Registry<true> reg;
	constexpr int T = 8, PER = 2000;
	std::vector<std::vector<EntityId>> got(T);
	std::vector<std::thread> pool;
	for (int t = 0; t < T; ++t) {
		pool.emplace_back([&, t] {
			got[t].reserve(PER);
			for (int i = 0; i < PER; ++i) { got[t].push_back(reg.takeEntity()); }
		});
	}
	for (auto& th : pool) { th.join(); }

	std::unordered_set<EntityId> all;
	size_t total = 0;
	for (const auto& v : got) { for (auto id : v) { ++total; all.insert(id); } }
	EXPECT_EQ(total, size_t(T) * PER);
	EXPECT_EQ(all.size(), total) << "an id was handed to more than one thread";
	for (auto id : all) { EXPECT_TRUE(reg.contains(id)); }
}

TEST(Regression_EntityIds, ClearResetsTheIdSpace) {
	Registry<true> reg;
	const auto a = reg.takeEntity();
	ASSERT_TRUE(reg.contains(a));
	reg.clear();
	EXPECT_FALSE(reg.contains(a));
	const auto b = reg.takeEntity();
	EXPECT_EQ(b, 0u) << "clear() must restart id allocation at 0";
	EXPECT_TRUE(reg.contains(b));
}

// ===========================================================================
// The pin predicates must never report "safe" while a pin is live. The previous
// implementation derived a highest-pinned-id from a hierarchical bit mask that
// could under-report while a clear propagated upward.
// ===========================================================================

TEST(Regression_PinContract, NeverReportsSafeWhilePinned) {
	Threads::PinCounters pins;
	pins.reserve(4096);

	std::atomic<bool> stop{ false };
	std::atomic<long long> relocationGateLies{ 0 }, ownSectorLies{ 0 }, checks{ 0 };

	std::vector<std::thread> churn;
	for (int t = 0; t < 4; ++t) {
		churn.emplace_back([&, t] {
			unsigned x = 1u + unsigned(t) * 7919u;
			while (!stop.load(std::memory_order_relaxed)) {
				x = x * 1664525u + 1013904223u;
				const SectorId id = (x >> 8) % 4000;
				pins.pin(id);
				std::this_thread::yield();
				pins.unpin(id);
			}
		});
	}

	constexpr SectorId kHigh = 3999;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (std::chrono::steady_clock::now() < deadline) {
		pins.pin(kHigh);
		for (int k = 0; k < 200; ++k) {
			++checks;
			if (!pins.hasAnyPins()) { ++relocationGateLies; }
			if (pins.canMoveSector(kHigh)) { ++ownSectorLies; }
		}
		pins.unpin(kHigh);
	}
	stop.store(true, std::memory_order_release);
	for (auto& th : churn) { th.join(); }

	EXPECT_GT(checks.load(), 0);
	EXPECT_EQ(relocationGateLies.load(), 0) << "hasAnyPins() was false while a sector was pinned";
	EXPECT_EQ(ownSectorLies.load(), 0) << "canMoveSector() was true for a pinned sector";
}

// ===========================================================================
// Waiting on pins while holding the array write lock deadlocks any pin holder
// that needs the shared lock again (a second view, pinComponent, hasComponent).
// Every writer must wait outside the lock, then take it and re-verify.
// ===========================================================================

TEST(Regression_Deadlock, PinHeldWhileAnotherThreadDestroysComponent) {
	auto* reg = new Registry<true>();
	for (EntityId e = 0; e < 10; ++e) { reg->takeEntity(); reg->addComponent<RPos>(e, RPos{ float(e), 0, 0 }); }

	std::atomic<bool> pinned{ false }, writerEntered{ false }, writerDone{ false }, readerDone{ false };

	std::thread reader([&] {
		auto pin = reg->pinComponent<RPos>(5);
		pinned.store(true, std::memory_order_release);
		while (!writerEntered.load(std::memory_order_acquire)) { std::this_thread::yield(); }
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		(void)reg->hasComponent<RPos>(7); // needs the shared lock while still pinning
		readerDone.store(true, std::memory_order_release);
	});
	while (!pinned.load(std::memory_order_acquire)) { std::this_thread::yield(); }

	std::thread writer([&] {
		writerEntered.store(true, std::memory_order_release);
		reg->destroyComponent<RPos>(EntityId{ 0 });
		writerDone.store(true, std::memory_order_release);
	});

	const bool ok = waitFor([&] { return writerDone.load() && readerDone.load(); });
	EXPECT_TRUE(ok) << "destroyComponent deadlocked against a live pin";
	if (!ok) { reader.detach(); writer.detach(); return; } // leak reg: threads still hold its locks
	reader.join(); writer.join();
	delete reg;
}

TEST(Regression_Deadlock, MiddleInsertWhileALowerSectorIsPinned) {
	auto* reg = new Registry<true>();
	for (EntityId e = 0; e <= 10; ++e) { reg->takeEntity(); if (e != 5) { reg->addComponent<RPos>(e, RPos{ float(e), 0, 0 }); } }

	std::atomic<bool> pinned{ false }, writerEntered{ false }, writerDone{ false }, readerDone{ false };

	std::thread reader([&] {
		auto pin = reg->pinComponent<RPos>(2); // lower id than the insert position
		pinned.store(true, std::memory_order_release);
		while (!writerEntered.load(std::memory_order_acquire)) { std::this_thread::yield(); }
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		(void)reg->hasComponent<RPos>(7);
		readerDone.store(true, std::memory_order_release);
	});
	while (!pinned.load(std::memory_order_acquire)) { std::this_thread::yield(); }

	std::thread writer([&] {
		writerEntered.store(true, std::memory_order_release);
		reg->addComponent<RPos>(EntityId{ 5 }, RPos{ 55, 0, 0 }); // middle insert -> shift
		writerDone.store(true, std::memory_order_release);
	});

	const bool ok = waitFor([&] { return writerDone.load() && readerDone.load(); });
	EXPECT_TRUE(ok) << "a middle insert deadlocked against a pin on a lower sector";
	if (!ok) { reader.detach(); writer.detach(); return; }
	reader.join(); writer.join();
	delete reg;
}

TEST(Regression_Deadlock, TwoLiveViewsWhileAnotherThreadDestroysEntity) {
	auto* reg = new Registry<true>();
	for (EntityId e = 0; e < 10; ++e) {
		reg->takeEntity();
		reg->addComponent<RPos>(e, RPos{ float(e), 0, 0 });
		reg->addComponent<RVel>(e, RVel{ 1, 1, 1 });
	}

	std::atomic<bool> viewAlive{ false }, writerEntered{ false }, writerDone{ false }, readerDone{ false };

	std::thread reader([&] {
		auto first = reg->view<RPos>(); // pins the back sector for its whole lifetime
		viewAlive.store(true, std::memory_order_release);
		while (!writerEntered.load(std::memory_order_acquire)) { std::this_thread::yield(); }
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		auto second = reg->view<RPos>(); // needs the shared lock while the first pin is held
		int n = 0;
		for (auto t : second) { (void)t; ++n; }
		readerDone.store(true, std::memory_order_release);
	});
	while (!viewAlive.load(std::memory_order_acquire)) { std::this_thread::yield(); }

	std::thread writer([&] {
		writerEntered.store(true, std::memory_order_release);
		reg->destroyEntity(EntityId{ 0 });
		writerDone.store(true, std::memory_order_release);
	});

	const bool ok = waitFor([&] { return writerDone.load() && readerDone.load(); });
	EXPECT_TRUE(ok) << "destroyEntity deadlocked against two live views";
	if (!ok) { reader.detach(); writer.detach(); return; }
	reader.join(); writer.join();
	delete reg;
}

// ===========================================================================
// Whole-registry churn: readers holding views and pins, writers inserting,
// overwriting and destroying, plus maintenance. Watches for both a stall
// (deadlock) and a broken dense-array invariant.
// ===========================================================================

TEST(Regression_Concurrency, RegistryChurnStaysLiveAndConsistent) {
	auto* reg = new Registry<true>();
	reg->reserve<RPos>(20000);
	reg->reserve<RVel>(20000);
	for (EntityId e = 0; e < 4000; ++e) { reg->takeEntity(); reg->addComponent<RPos>(e, RPos{ float(e), 0, 0 }); }

	std::atomic<bool> stop{ false };
	std::atomic<long long> ops{ 0 }, ordering{ 0 };
	std::atomic<int> alive{ 0 };
	std::vector<std::thread> pool;

	auto spawn = [&](auto body) {
		pool.emplace_back([&, body] { alive.fetch_add(1); body(); alive.fetch_sub(1); });
	};

	for (int t = 0; t < 3; ++t) {
		spawn([&, t] {
			std::mt19937 rng(unsigned(t) * 977u + 1u);
			while (!stop.load(std::memory_order_relaxed)) {
				auto v = reg->view<RPos>();
				for (auto [e, p] : v) { (void)e; (void)p; }
				auto pin = reg->pinComponent<RPos>(EntityId(rng() % 4000));
				(void)reg->hasComponent<RVel>(EntityId(rng() % 4000));
				auto nested = reg->view<RPos, RVel>();
				for (auto tup : nested) { (void)tup; }
				ops.fetch_add(1, std::memory_order_relaxed);
			}
		});
	}
	for (int t = 0; t < 2; ++t) {
		spawn([&, t] {
			std::mt19937 rng(unsigned(t) * 131u + 7u);
			while (!stop.load(std::memory_order_relaxed)) {
				const auto id = EntityId(rng() % 4000);
				switch (rng() % 5) {
					case 0: reg->addComponent<RPos>(id, RPos{ 1, 2, 3 }); break;
					case 1: reg->addComponent<RVel>(id, RVel{ 1, 1, 1 }); break;
					case 2: reg->destroyComponent<RVel>(id); break;
					case 3: reg->destroyEntity(id); reg->takeEntity(); break;
					default: reg->addComponent<RPos>(EntityId(4000 + rng() % 2000), RPos{}); break;
				}
				ops.fetch_add(1, std::memory_order_relaxed);
			}
		});
	}
	spawn([&] {
		while (!stop.load(std::memory_order_relaxed)) {
			reg->update();
			auto* c = reg->getComponentContainer<RPos>();
			auto lock = c->writeLock();
			for (size_t i = 1; i < c->template size<false>(); ++i) {
				if (c->getId(i - 1) >= c->getId(i)) { ordering.fetch_add(1, std::memory_order_relaxed); break; }
			}
		}
	});

	bool stalled = false;
	long long last = -1;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
	while (std::chrono::steady_clock::now() < deadline) {
		std::this_thread::sleep_for(std::chrono::milliseconds(400));
		const auto now = ops.load(std::memory_order_relaxed);
		if (now == last) { stalled = true; break; }
		last = now;
	}
	stop.store(true, std::memory_order_release);

	const bool joined = waitFor([&] { return alive.load() == 0; });
	EXPECT_FALSE(stalled) << "registry churn stopped making progress (deadlock)";
	EXPECT_EQ(ordering.load(), 0) << "dense ids went unsorted under concurrent mutation";
	EXPECT_TRUE(joined) << "threads never finished";
	if (!joined) { for (auto& th : pool) { th.detach(); } return; }
	for (auto& th : pool) { th.join(); }
	delete reg;
}

// ===========================================================================
// ChunksAllocator::move cleared the destination chunk vector without freeing or
// retiring the chunks it already owned.
// ===========================================================================

TEST(Regression_Allocator, MoveAssignRetiresTheChunksItOwned) {
	auto* meta = SectorLayoutMeta::create<RInt>();

	ChunksAllocator<8> dst; dst.init(meta); dst.allocate(64);
	ChunksAllocator<8> src; src.init(meta); src.allocate(16);
	ASSERT_EQ(dst.mBin.pendingCount(), 0u);

	dst = std::move(src);
	EXPECT_GT(dst.mBin.pendingCount(), 0u) << "chunks were dropped without being freed or retired";
}

// ===========================================================================
// RangedIterator indexed its range vector without a bound check, so incrementing
// past the end read out of bounds (caught by AddressSanitizer).
// ===========================================================================

TEST(Regression_RangedIterator, IncrementPastEndIsInBounds) {
	std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<RInt>());
	for (SectorId i = 0; i < 10; ++i) { arr->emplace<RInt>(i, RInt{ int(i) }); }

	Ranges<SectorId> r(std::pair<SectorId, SectorId>{ 0, 10 });
	auto it = arr->beginRanged(r);
	auto en = arr->endRanged();
	size_t n = 0;
	for (; it != en; ++it) { ++n; }
	EXPECT_EQ(n, 10u);

	++it; // must not read past the end of the internal range vector
	++it;
	SUCCEED();
}

// ===========================================================================
// The live-entity set used to be an interval list, so every erase inside a run
// split it with a vector insert and the run count grew towards N/2. Destroying
// entities in arbitrary order -- the normal case -- was quadratic: 171 ms for
// 200k, against 1.5 ms with the bitmap.
// ===========================================================================

TEST(Regression_IdSet, MatchesAReferenceSet) {
	IdSet<EntityId> set;
	std::set<EntityId> model;
	std::mt19937 rng(99);

	for (int step = 0; step < 20000; ++step) {
		if (model.empty() || (rng() % 100) < 60) {
			const auto id = set.take();
			ASSERT_TRUE(model.insert(id).second) << "take() handed out a live id: " << id;
		}
		else {
			auto it = model.begin();
			std::advance(it, rng() % model.size());
			const auto id = *it;
			set.erase(id);
			model.erase(it);
			ASSERT_FALSE(set.contains(id));
		}
	}

	EXPECT_EQ(set.size(), model.size());
	const auto all = set.getAll();
	EXPECT_TRUE(std::is_sorted(all.begin(), all.end())) << "getAll() must be ascending";
	EXPECT_EQ(std::vector<EntityId>(model.begin(), model.end()), all);
	for (auto id : model) { EXPECT_TRUE(set.contains(id)); }
}

TEST(Regression_IdSet, TakeReturnsTheLowestFreeId) {
	IdSet<EntityId> set;
	for (int i = 0; i < 10; ++i) { EXPECT_EQ(set.take(), EntityId(i)); }

	set.erase(7);
	set.erase(2);
	set.erase(5);
	EXPECT_EQ(set.take(), 2u);
	EXPECT_EQ(set.take(), 5u);
	EXPECT_EQ(set.take(), 7u);
	EXPECT_EQ(set.take(), 10u) << "with no holes left, allocation continues past the watermark";
}

TEST(Regression_IdSet, ClearRestartsAllocation) {
	IdSet<EntityId> set;
	for (int i = 0; i < 100; ++i) { (void)set.take(); }
	set.clear();
	EXPECT_TRUE(set.empty());
	EXPECT_FALSE(set.contains(50));
	EXPECT_EQ(set.take(), 0u);
}

TEST(Regression_IdSet, FootprintTracksPeakNotChurn) {
	IdSet<EntityId> set;
	std::vector<EntityId> live;
	for (int i = 0; i < 1000; ++i) { live.push_back(set.take()); }
	const auto peakBytes = set.byteSize();

	// churn hard: the same 1000 slots recycled many times over
	for (int round = 0; round < 200; ++round) {
		for (auto id : live) { set.erase(id); }
		for (auto& id : live) { id = set.take(); }
	}
	EXPECT_EQ(set.byteSize(), peakBytes)
		<< "footprint grew with churn; take() should be reusing the lowest free id";
	EXPECT_EQ(set.size(), live.size());
}

// Timing-based, but self-normalising: it compares random-order destruction against
// sequential destruction of the same population, so it does not depend on machine speed.
// Quadratic behaviour showed as x30 (20k) rising to x234 (200k); linear sits near x1.5.
TEST(Regression_IdSet, RandomOrderDestructionStaysLinear) {
	constexpr int N = 100000;

	const auto timeDestroy = [](bool shuffle) {
		Registry<false> reg;
		reg.reserve<RInt>(N);
		std::vector<EntityId> order;
		order.reserve(N);
		for (int i = 0; i < N; ++i) {
			const auto e = reg.takeEntity();
			reg.addComponent<RInt>(e, RInt{ i });
			order.push_back(e);
		}
		if (shuffle) { std::shuffle(order.begin(), order.end(), std::mt19937(7)); }

		const auto t0 = std::chrono::steady_clock::now();
		for (auto e : order) { reg.destroyEntity(e); }
		return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	};

	const double sequential = timeDestroy(false);
	const double random = timeDestroy(true);
	const double ratio = random / (sequential > 0.0 ? sequential : 1.0);

	EXPECT_LT(ratio, 10.0)
		<< "random-order destroyEntity is " << ratio << "x sequential (" << random << " ms vs "
		<< sequential << " ms) -- the live-id set is quadratic again";
}

// ===========================================================================
// Registry::update() skips the per-array write lock when an array has nothing
// queued and nothing to compact -- taking it just to find no work cost ~49 ns
// per array per frame. The skip must not swallow real work.
// ===========================================================================

TEST(Regression_Maintenance, IdleUpdateStillReclaimsDeferredErases) {
	Registry<true> reg;
	constexpr EntityId kTotal = 1000, kErased = 400;
	for (EntityId e = 0; e < kTotal; ++e) { reg.takeEntity(); reg.addComponent<RInt>(e, RInt{ int(e) }); }

	auto* container = reg.getComponentContainer<RInt>();
	ASSERT_EQ(container->size(), size_t(kTotal));

	for (EntityId e = 0; e < kErased; ++e) { container->eraseAsync(e); }
	EXPECT_GT(container->getDefragmentationSize(), 0u) << "deferred erases must register as fragmentation";

	for (int frame = 0; frame < 8; ++frame) { reg.update(); }

	EXPECT_EQ(container->size(), size_t(kTotal - kErased)) << "compaction never ran";
	EXPECT_EQ(container->getDefragmentationSize(), 0u);
	for (EntityId e = kErased; e < kTotal; ++e) { EXPECT_TRUE(reg.hasComponent<RInt>(e)) << "survivor " << e << " lost"; }
	for (EntityId e = 0; e < kErased; ++e) { EXPECT_FALSE(reg.hasComponent<RInt>(e)) << "erased " << e << " still present"; }

	const auto settled = container->size();
	for (int frame = 0; frame < 50; ++frame) { reg.update(); }
	EXPECT_EQ(container->size(), settled) << "idle update() must not change anything";
}
