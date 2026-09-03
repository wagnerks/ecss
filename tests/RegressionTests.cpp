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
#include <set>
#include <cstdlib>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include <ecss/CommandBuffer.h>
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
template <int I> struct TypeN { int v{}; };

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

TEST(Regression_Concurrency, DestroyEntitiesDoesNotWaitForPinOnOtherSector) {
	auto* reg = new Registry<true>();
	reg->reserve<RPos>(16);
	for (EntityId e = 0; e < 8; ++e) {
		reg->takeEntity();
		reg->addComponent<RPos>(e, RPos{ float(e), 0, 0 });
	}

	std::atomic<bool> done{ false };
	std::thread writer;
	{
		auto pin = reg->pinComponent<RPos>(EntityId{ 0 });
		ASSERT_TRUE(static_cast<bool>(pin));

		writer = std::thread([&] {
			std::vector<EntityId> doomed{ 3, 4, 5 };
			reg->destroyEntities(doomed);
			done.store(true, std::memory_order_release);
		});

		const bool ok = waitFor([&] { return done.load(std::memory_order_acquire); });
		EXPECT_TRUE(ok) << "destroyEntities waited for a pin on a sector it was not destroying";
		if (!ok) { writer.detach(); return; }
	}
	writer.join();
	EXPECT_TRUE(reg->contains(EntityId{ 0 }));
	EXPECT_FALSE(reg->contains(EntityId{ 3 }));
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
	// SectorLayoutMeta::create hands over ownership, and the allocators below only borrow it.
	const std::unique_ptr<SectorLayoutMeta> meta(SectorLayoutMeta::create<RInt>());

	ChunksAllocator<8> dst; dst.init(meta.get()); dst.allocate(64);
	ChunksAllocator<8> src; src.init(meta.get()); src.allocate(16);
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

// ===========================================================================
// Pins are taken without any lock and validated against a structural epoch.
// The invariant: once a pin validates, the sector it points at is not relocated
// or destroyed until the pin is released. Writers hammer the array with middle
// inserts, defragments and destroys while readers hold pins and keep reading.
// ===========================================================================

TEST(Regression_Pinning, ValidatedPinSurvivesConcurrentStructuralChange) {
	struct Tag { uint64_t magic; uint64_t id; };
	constexpr uint64_t kMagic = 0x5ec70bad5ec70badull;
	constexpr EntityId kPinned = 2048, kChurn = 8000;

	Registry<true> reg;
	reg.reserve<Tag>(kChurn * 2);
	for (EntityId e = 0; e < kPinned; ++e) { reg.takeEntity(); reg.addComponent<Tag>(e, Tag{ kMagic ^ e, e }); }
	for (EntityId e = kPinned; e < kChurn; e += 2) { reg.addComponent<Tag>(e, Tag{ kMagic ^ e, e }); }

	std::atomic<bool> stop{ false };
	std::atomic<long long> corrupt{ 0 }, writes{ 0 };
	std::vector<std::thread> pool;

	for (int t = 0; t < 4; ++t) {
		pool.emplace_back([&, t] {
			std::mt19937 rng(1234u + unsigned(t));
			while (!stop.load(std::memory_order_relaxed)) {
				const EntityId id = rng() % kPinned;
				auto pin = reg.pinComponent<Tag>(id);
				if (!pin) { continue; }
				for (int k = 0; k < 32; ++k) {
					if (pin->magic != (kMagic ^ id) || pin->id != id) {
						corrupt.fetch_add(1, std::memory_order_relaxed);
						break;
					}
				}
			}
		});
	}
	for (int t = 0; t < 2; ++t) {
		pool.emplace_back([&, t] {
			std::mt19937 rng(999u + unsigned(t));
			while (!stop.load(std::memory_order_relaxed)) {
				const EntityId id = kPinned + 1 + (rng() % (kChurn - kPinned - 1));
				switch (rng() % 4) {
					case 0: reg.addComponent<Tag>(id, Tag{ kMagic ^ id, id }); break;
					case 1: reg.destroyComponent<Tag>(id); break;
					case 2: reg.getComponentContainer<Tag>()->defragment(); break;
					default: reg.destroyEntity(id); reg.takeEntity(); break;
				}
				writes.fetch_add(1, std::memory_order_relaxed);
			}
		});
	}

	std::this_thread::sleep_for(std::chrono::seconds(2));
	stop.store(true, std::memory_order_release);
	for (auto& th : pool) { th.join(); }

	EXPECT_EQ(corrupt.load(), 0) << "a validated pin observed relocated or destroyed storage";
	// Readers yield to waiting writers on purpose: without that, a few threads pinning in a
	// loop keep the array permanently non-quiescent and structural writes never run.
	EXPECT_GT(writes.load(), 1000) << "structural writes were starved by pinning readers";
}

// ===========================================================================
// Iteration needs "do not compact this array", not "leave this sector alone".
// Views used to express that by pinning the back sector, so every view on every
// thread contended on one sector counter; they take a thread-sharded structural
// hold instead. The guarantee has to be exactly the same.
// ===========================================================================

TEST(Regression_StructuralHold, LiveViewBlocksCompactionAndReleasesIt) {
	Registry<true> reg;
	constexpr EntityId kTotal = 1000;
	for (EntityId e = 0; e < kTotal; ++e) { reg.takeEntity(); reg.addComponent<RInt>(e, RInt{ int(e) }); }
	for (EntityId e = 0; e < kTotal; e += 2) { reg.destroyEntity(e); }

	auto* container = reg.getComponentContainer<RInt>();
	ASSERT_EQ(container->size(), size_t(kTotal)) << "dead sectors are still present before compaction";

	std::atomic<bool> compacted{ false };
	std::thread writer;
	{
		auto view = reg.view<RInt>();
		writer = std::thread([&] { container->defragment(); compacted.store(true, std::memory_order_release); });

		std::this_thread::sleep_for(std::chrono::milliseconds(200));
		EXPECT_FALSE(compacted.load()) << "compaction ran while a view was alive";
		EXPECT_EQ(container->size(), size_t(kTotal)) << "the array was compacted under a live view";
	}
	const bool finished = waitFor([&] { return compacted.load(); });
	writer.join();

	EXPECT_TRUE(finished) << "compaction never completed after the view was released";
	EXPECT_EQ(container->size(), size_t(kTotal / 2)) << "compaction did not reclaim the dead sectors";
}

TEST(Regression_StructuralHold, LongLivedViewsThrottleCompactionButDoNotBlockIt) {
	// Compaction needs every holder gone, so threads whose views overlap in time squeeze it
	// hard -- four continuous iterators leave very few zero-holder moments, and that was
	// equally true when views pinned the back sector. What must hold is that it is throttled
	// rather than blocked forever: the writer announces intent and holders yield to it, so it
	// keeps getting through. Do not tighten this into a rate: the rate is a property of how
	// long the views happen to live, not of the library.
	Registry<true> reg;
	for (EntityId e = 0; e < 4000; ++e) { reg.takeEntity(); reg.addComponent<RInt>(e, RInt{ int(e) }); }

	std::atomic<bool> stop{ false };
	std::atomic<long long> writes{ 0 }, iterations{ 0 };
	std::vector<std::thread> pool;
	for (int t = 0; t < 4; ++t) {
		pool.emplace_back([&] {
			while (!stop.load(std::memory_order_relaxed)) {
				auto v = reg.view<RInt>();
				for (auto tup : v) { (void)tup; }
				iterations.fetch_add(1, std::memory_order_relaxed);
			}
		});
	}
	pool.emplace_back([&] {
		while (!stop.load(std::memory_order_relaxed)) {
			reg.getComponentContainer<RInt>()->defragment();
			writes.fetch_add(1, std::memory_order_relaxed);
		}
	});

	std::this_thread::sleep_for(std::chrono::seconds(1));
	stop.store(true, std::memory_order_release);
	for (auto& th : pool) { th.join(); }

	EXPECT_GT(iterations.load(), 0) << "the readers never ran";
	EXPECT_GT(writes.load(), 0)
		<< "compaction never completed once in a second of continuous iteration";
}

// ===========================================================================
// insertBulk() used to require ids that were strictly ascending and above
// everything stored -- a comment, not a check. Anything else had to go through
// a loop of addComponent(), which shifts the tail and rewrites the sparse entry
// of every sector it passes, so M inserts cost O(M*N).
// ===========================================================================

TEST(Regression_BulkInsert, ArbitraryOrderMatchesOneAtATime) {
	std::mt19937 rng(4242);
	for (int round = 0; round < 200; ++round) {
		const uint32_t idSpace = 1 + rng() % 80;
		Registry<true> loop, bulk;

		std::vector<EntityId> pre;
		for (int i = 0, n = int(rng() % 40); i < n; ++i) { pre.push_back(rng() % idSpace); }
		std::sort(pre.begin(), pre.end());
		pre.erase(std::unique(pre.begin(), pre.end()), pre.end());
		for (auto e : pre) {
			loop.addComponent<RInt>(e, RInt{ int(e) });
			bulk.addComponent<RInt>(e, RInt{ int(e) });
		}
		// deferred erases leave dead dense entries whose sparse slot is cleared
		if (!pre.empty()) {
			for (int i = 0, n = int(rng() % 3); i < n; ++i) {
				const auto victim = pre[rng() % pre.size()];
				loop.destroyComponent<RInt>(victim);
				bulk.destroyComponent<RInt>(victim);
			}
		}

		std::vector<std::pair<EntityId, RInt>> batch;
		for (int i = 0, n = int(rng() % 40) + 1; i < n; ++i) {
			batch.emplace_back(rng() % idSpace, RInt{ int(rng() % 1000) });
		}

		for (auto& entry : batch) {
			RInt value = entry.second;
			loop.addComponent<RInt>(entry.first, value);
		}
		bulk.insertBulk<RInt>(batch.begin(), batch.end());

		auto* la = loop.getComponentContainer<RInt>();
		auto* ba = bulk.getComponentContainer<RInt>();
		ASSERT_EQ(la->size(), ba->size()) << "round " << round;

		for (uint32_t id = 0; id < idSpace; ++id) {
			ASSERT_EQ(loop.hasComponent<RInt>(id), bulk.hasComponent<RInt>(id))
				<< "presence differs for id " << id << " in round " << round;
			if (loop.hasComponent<RInt>(id)) {
				auto* lv = loop.pinComponent<RInt>(id).get();
				auto* bv = bulk.pinComponent<RInt>(id).get();
				ASSERT_TRUE(lv && bv);
				EXPECT_EQ(lv->v, bv->v) << "value differs for id " << id << " in round " << round;
			}
		}

		// the invariant every binary search depends on
		for (size_t i = 1; i < ba->size(); ++i) {
			ASSERT_LT(ba->getId(i - 1), ba->getId(i))
				<< "dense ids not strictly ascending at " << i << " in round " << round;
		}
	}
}

TEST(Regression_BulkInsert, StaysLinearInBatchSize) {
	// A wall-clock assertion, so it only means anything in an optimized build. In Debug
	// -- and far worse under a sanitizer -- it measures the compiler's bookkeeping
	// rather than the merge, takes minutes rather than seconds, and was the one test
	// that timed out once the ctest limit started being applied.
	#ifndef NDEBUG
	GTEST_SKIP() << "timing assertion; meaningless in a debug build";
	#endif
	// Per-element cost must not grow with the array size. The batch has to land *inside*
	// what is already stored, or the merge degenerates to "sort, then append" and the path
	// that used to be quadratic -- relocating the tail and rewriting its sparse entries --
	// is never taken.
	const auto timeMergeIntoPopulated = [](int half) {
		// even ids first, ascending, so they take the append path and just populate the array
		std::vector<std::pair<EntityId, RInt>> evens;
		evens.reserve(half);
		for (int i = 0; i < half; ++i) { evens.emplace_back(EntityId(2 * i), RInt{ i }); }

		// odd ids second, shuffled: every one of them belongs between two existing sectors
		std::vector<std::pair<EntityId, RInt>> odds;
		odds.reserve(half);
		for (int i = 0; i < half; ++i) { odds.emplace_back(EntityId(2 * i + 1), RInt{ i }); }
		std::shuffle(odds.begin(), odds.end(), std::mt19937(7));

		Registry<false> reg;
		reg.reserve<RInt>(2 * half);
		reg.insertBulk<RInt>(evens.begin(), evens.end());

		const auto t0 = std::chrono::steady_clock::now();
		reg.insertBulk<RInt>(odds.begin(), odds.end());
		const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

		// the merge is what is being timed, so make sure it actually happened
		EXPECT_EQ(reg.getComponentContainer<RInt>()->size(), size_t(2 * half));
		return ms;
	};

	// Best of several: a single sample of a few milliseconds is dominated by scheduling
	// noise, which is how an earlier version of this test managed to report 11x.
	const auto best = [&](int half) {
		double ms = 1e18;
		for (int rep = 0; rep < 5; ++rep) { ms = std::min(ms, timeMergeIntoPopulated(half)); }
		return ms;
	};

	const double small = best(100000);
	const double large = best(400000);
	const double ratio = large / (small > 0.0 ? small : 1.0);

	// four times the work; quadratic would be about sixteen
	EXPECT_LT(ratio, 8.0)
		<< "merging 4x the ids into a 4x array took " << ratio << "x as long (" << large
		<< " ms vs " << small << " ms) -- the merge has gone quadratic again";
}

TEST(Regression_BulkInsert, GeneratorBatchAcceptsUnorderedIds) {
	constexpr int N = 2000;
	Registry<true> reg;
	std::vector<EntityId> ids;
	for (int i = 0; i < N; ++i) { ids.push_back(EntityId(i)); }
	std::shuffle(ids.begin(), ids.end(), std::mt19937(11));

	size_t k = 0;
	reg.addComponents<RInt>([&]() -> std::pair<EntityId, RInt> {
		if (k >= ids.size()) { return { INVALID_ID, RInt{} }; }
		const auto id = ids[k++];
		return { id, RInt{ int(id) * 3 } };
	});

	auto* arr = reg.getComponentContainer<RInt>();
	ASSERT_EQ(arr->size(), size_t(N));
	for (int i = 0; i < N; ++i) {
		ASSERT_TRUE(reg.hasComponent<RInt>(EntityId(i))) << "id " << i << " missing";
		EXPECT_EQ(reg.pinComponent<RInt>(EntityId(i)).get()->v, i * 3);
	}
	for (size_t i = 1; i < arr->size(); ++i) {
		ASSERT_LT(arr->getId(i - 1), arr->getId(i)) << "dense ids not sorted at " << i;
	}
}

// ===========================================================================
// reserve() moves the dense id and liveness buffers and retires the old ones,
// but published nothing -- so the seqlock view kept naming buffers that were
// correct only until the next write, and freed once the grace period expired.
// ===========================================================================

TEST(Regression_Reclamation, ReserveRepublishesTheDenseView) {
	Registry<true> reg;
	for (EntityId e = 0; e < 4; ++e) { reg.addComponent<RInt>(e, RInt{ int(e) }); }

	reg.getComponentContainer<RInt>()->reserve(100000);   // forces the buffers to move

	// destroyComponent writes liveness through the live vector; hasComponent and iteration
	// read it through the published view. Asserting that they agree catches the stale
	// pointer directly, rather than waiting for the retired buffer to be freed and reused --
	// which happens eventually but reads as intact data often enough to hide the defect.
	reg.destroyComponent<RInt>(2);

	EXPECT_FALSE(reg.hasComponent<RInt>(2))
		<< "a destroyed component still reads as present -- the published dense view still "
		   "names the buffers reserve() moved away from";
	size_t seen = 0;
	for (auto it : reg.view<RInt>()) { (void)it; ++seen; }
	EXPECT_EQ(seen, 3u) << "iteration walked a stale dense view";
}

// ===========================================================================
// Structural changes are illegal while the calling thread holds a view or pin
// on the same array: the writer waits for holds and pins to drain, and its own
// are ones only it could release, so it hangs. Debug builds assert instead --
// which the stub harness cannot catch, so what is guarded here is the other
// direction: the operations that are legal must not start asserting.
// ===========================================================================

TEST(Regression_SelfWait, OperationsThatMoveNothingStayLegalUnderAView) {
	Registry<true> reg;
	for (EntityId e = 100; e < 200; ++e) { reg.addComponent<RInt>(e, RInt{ int(e) }); }

	{
		auto view = reg.view<RInt>();
		auto it = view.begin();
		(void)it;

		// appending above every stored id relocates nothing
		reg.addComponent<RInt>(500, RInt{ 500 });
		// destroying in place touches one sector and moves none
		reg.destroyComponent<RInt>(150);
	}
	EXPECT_TRUE(reg.hasComponent<RInt>(500));
	EXPECT_FALSE(reg.hasComponent<RInt>(150));

	// a pin blocks only the sector it names
	{
		auto pin = reg.pinComponent<RInt>(151);
		reg.destroyComponent<RInt>(152);
	}
	EXPECT_TRUE(reg.hasComponent<RInt>(151));
	EXPECT_FALSE(reg.hasComponent<RInt>(152));
}

TEST(Regression_SelfWait, AnotherArrayIsUnaffectedByAView) {
	Registry<true> reg;
	for (EntityId e = 0; e < 200; ++e) { reg.addComponent<RInt>(e, RInt{ int(e) }); }
	for (EntityId e = 100; e < 200; ++e) { reg.addComponent<RVel>(e, RVel{ float(e), 0.f, 0.f }); }

	auto view = reg.view<RInt>();
	auto it = view.begin();
	(void)it;

	// a middle insert into a different array shares nothing with this view
	reg.addComponent<RVel>(5, RVel{ 5.f, 0.f, 0.f });
	EXPECT_TRUE(reg.hasComponent<RVel>(5));
}

// ===========================================================================
// Each component's LayoutData is resolved once at registration and carried in
// the published snapshot, so a query no longer rescans the layout's type
// tokens. Registry::insert can still repoint an array at a different layout,
// which would leave that record describing the wrong sector shape, so the
// record is checked against the layout the array reports rather than trusted.
// ===========================================================================

namespace {
struct LA { int v{}; };
struct LB { double d{}; };
} // namespace

// Release-only: the debug build asserts on the mismatch rather than returning, and the stub
// harness has no death-test support to catch that. What is pinned here is the release
// fallback -- refusing the assignment instead of repointing the array at a foreign layout.
#ifdef NDEBUG
TEST(Regression_LayoutInvariant, InsertFromAForeignLayoutIsRefused) {
	Registry<true> dst, src;
	dst.registerArray<LA>();          // LA alone: index 0, first liveness bit
	src.registerArray<LB, LA>();      // LA second: different offset and different bit

	for (EntityId e = 0; e < 8; ++e) { src.addComponent<LA>(e, LA{ int(e) }); }
	dst.addComponent<LA>(100, LA{ 99 });

	// [LB, LA] is a different sector shape from [LA], so this cannot be honoured: it used to
	// repoint the destination at the source's layout, leaving the sector size and the
	// liveness bits describing bytes that were never laid out that way.
	dst.insert<LA>(*src.getComponentContainer<LA>());

	EXPECT_TRUE(dst.hasComponent<LA>(100))
		<< "the destination lost its own contents to an assignment that could not be honoured";
	for (EntityId e = 0; e < 8; ++e) {
		EXPECT_FALSE(dst.hasComponent<LA>(e))
			<< "id " << e << " arrived from an array with an incompatible layout";
	}
	EXPECT_EQ(dst.getComponentContainer<LA>()->getLayout()->getTotalSize(), sizeof(LA))
		<< "the destination was repointed at the foreign layout";
}
#endif

TEST(Regression_LayoutCache, EveryTypeInAGroupResolvesToItsOwnRecord) {
	// recordLayouts() fills one entry per pack member; a fold that wrote only the first or
	// the last would leave the others pointing at the wrong component's mask.
	Registry<false> reg;
	reg.registerArray<RPos, RVel, RInt>();

	for (EntityId e = 0; e < 32; ++e) {
		reg.addComponent<RPos>(e, RPos{ float(e), 0.f, 0.f });
		if (e % 2 == 0) { reg.addComponent<RVel>(e, RVel{ 1.f, 0.f, 0.f }); }
		if (e % 4 == 0) { reg.addComponent<RInt>(e, RInt{ int(e) }); }
	}

	for (EntityId e = 0; e < 32; ++e) {
		EXPECT_TRUE(reg.hasComponent<RPos>(e)) << "RPos missing at " << e;
		EXPECT_EQ(reg.hasComponent<RVel>(e), e % 2 == 0) << "RVel wrong at " << e;
		EXPECT_EQ(reg.hasComponent<RInt>(e), e % 4 == 0) << "RInt wrong at " << e;
	}
}

// ===========================================================================
// A thread-safe insert used to wait on pins and publish a structural epoch
// before it knew what kind of insert it was. An append past the end relocates
// nothing and names a sector that does not exist yet, so neither is needed --
// the sector is published with isAlive == 0 and the component's liveness bit
// goes out with a release store once it is built. Removing them took the
// per-call cost from 50.5 ns to 34.5.
// ===========================================================================

TEST(Regression_AppendFastPath, ReadersNeverSeeAHalfBuiltAppendedComponent) {
	struct Chk {
		uint64_t a{}, b{};
		static Chk of(uint64_t id) { return { id, ~id }; }
		bool valid(uint64_t id) const { return a == id && b == ~id; }
	};

	Registry<true> reg;
	reg.registerArray<Chk>(1 << 16);

	std::atomic<bool> stop{ false };
	std::atomic<uint64_t> frontier{ 0 };
	std::atomic<long long> whole{ 0 }, torn{ 0 };

	std::thread writer([&] {
		for (uint64_t id = 0; id < 60000 && !stop.load(std::memory_order_relaxed); ++id) {
			reg.addComponent<Chk>(EntityId(id), Chk::of(id));
			frontier.store(id, std::memory_order_release);
		}
		stop.store(true, std::memory_order_release);
	});

	std::vector<std::thread> readers;
	for (int t = 0; t < 3; ++t) {
		readers.emplace_back([&] {
			while (!stop.load(std::memory_order_relaxed)) {
				const uint64_t f = frontier.load(std::memory_order_acquire);
				for (uint64_t id = (f > 4 ? f - 4 : 0); id <= f + 4; ++id) {
					auto pin = reg.pinComponent<Chk>(EntityId(id));
					if (!pin) { continue; }              // not appended yet: fine
					if (pin.get()->valid(id)) { whole.fetch_add(1, std::memory_order_relaxed); }
					else { torn.fetch_add(1, std::memory_order_relaxed); }
				}
			}
		});
	}

	writer.join();
	stop.store(true, std::memory_order_release);
	for (auto& r : readers) { r.join(); }

	EXPECT_EQ(torn.load(), 0)
		<< "a component was observed between becoming visible and being constructed";
	EXPECT_GT(whole.load(), 0) << "the readers never caught up with the writer";
}

TEST(Regression_AppendFastPath, AppendingWriterIsNotStarvedByContinuousReaders) {
	// The fast path no longer announces writer intent, which is what made readers yield.
	// It does not have to: it never blocks on a pin. Guard against that changing.
	Registry<true> reg;
	reg.registerArray<RInt>(1 << 16);
	for (EntityId e = 0; e < 20000; ++e) { reg.addComponent<RInt>(e, RInt{ int(e) }); }

	std::atomic<bool> stop{ false };
	std::atomic<long long> appends{ 0 };
	std::vector<std::thread> readers;
	for (int t = 0; t < 4; ++t) {
		readers.emplace_back([&] {
			while (!stop.load(std::memory_order_relaxed)) {
				size_t n = 0;
				for (auto it : reg.view<RInt>()) { (void)it; if (++n > 2000) { break; } }
			}
		});
	}
	std::thread writer([&] {
		EntityId id = 100000;
		while (!stop.load(std::memory_order_relaxed)) {
			reg.addComponent<RInt>(id++, RInt{ 1 });
			appends.fetch_add(1, std::memory_order_relaxed);
		}
	});

	std::this_thread::sleep_for(std::chrono::milliseconds(700));
	stop.store(true, std::memory_order_release);
	writer.join();
	for (auto& r : readers) { r.join(); }

	EXPECT_GT(appends.load(), 100)
		<< "the appending writer made almost no progress against continuous readers";
}

// ===========================================================================
// Batch id allocation. A loop of takeEntity() re-reads the hint and rescans
// from it for every id; taking a run walks the bitmap once and, where a whole
// word is free, claims 64 ids with a single step. 7.3 -> 1.2 ns per id in the
// thread-safe build, which is where the loop cost the most.
// ===========================================================================

TEST(Regression_BatchIds, MatchesALoopOfTakeEntity) {
	std::mt19937 rng(31337);
	for (int round = 0; round < 100; ++round) {
		IdSet<EntityId, true> loop, batch;

		// identical starting state, including holes punched by erase()
		std::vector<EntityId> taken;
		for (int i = 0, n = int(rng() % 200); i < n; ++i) { taken.push_back(loop.take()); (void)batch.take(); }
		for (int i = 0, n = int(rng() % 80); i < n && !taken.empty(); ++i) {
			const auto victim = taken[rng() % taken.size()];
			loop.erase(victim);
			batch.erase(victim);
		}

		const size_t count = 1 + rng() % 300;
		std::vector<EntityId> a, b;
		for (size_t i = 0; i < count; ++i) { a.push_back(loop.take()); }
		batch.take(count, b);

		std::sort(a.begin(), a.end());
		std::sort(b.begin(), b.end());
		ASSERT_EQ(a.size(), b.size()) << "round " << round;
		EXPECT_TRUE(a == b) << "batch handed out a different id set than the loop, round " << round;

		const std::unordered_set<EntityId> distinct(b.begin(), b.end());
		EXPECT_EQ(distinct.size(), b.size()) << "batch repeated an id, round " << round;
		for (auto id : b) { EXPECT_TRUE(batch.contains(id)) << "id " << id << " not marked live"; }
	}
}

TEST(Regression_BatchIds, ConcurrentBatchesNeverOverlap) {
	IdSet<EntityId, true> set;
	constexpr int T = 6;
	std::vector<std::vector<EntityId>> got(T);
	std::atomic<bool> go{ false };

	std::vector<std::thread> pool;
	for (int t = 0; t < T; ++t) {
		pool.emplace_back([&, t] {
			while (!go.load(std::memory_order_acquire)) { std::this_thread::yield(); }
			std::mt19937 rng(unsigned(t) * 91 + 3);
			for (int round = 0; round < 120; ++round) { set.take(1 + rng() % 150, got[t]); }
		});
	}
	go.store(true, std::memory_order_release);
	for (auto& th : pool) { th.join(); }

	std::unordered_set<EntityId> all;
	size_t total = 0;
	for (auto& v : got) { total += v.size(); all.insert(v.begin(), v.end()); }
	EXPECT_EQ(all.size(), total)
		<< "the same id was handed to two threads (" << total << " taken, " << all.size() << " distinct)";
}

TEST(Regression_BatchIds, RegistryEntityIdsStayUsable) {
	Registry<true> reg;
	std::vector<EntityId> ids;
	reg.takeEntities(5000, ids);
	ASSERT_EQ(ids.size(), 5000u);

	for (auto e : ids) {
		EXPECT_TRUE(reg.contains(e)) << "id " << e << " was handed out but the registry disowns it";
		reg.addComponent<RInt>(e, RInt{ int(e) });
	}
	for (auto e : ids) { EXPECT_TRUE(reg.hasComponent<RInt>(e)) << "component lost for id " << e; }

	// and the watermark stays tied to how many are live, not to how many were ever taken
	for (auto e : ids) { reg.destroyEntity(e); }
	std::vector<EntityId> again;
	reg.takeEntities(5000, again);
	EXPECT_EQ(*std::max_element(again.begin(), again.end()),
	          *std::max_element(ids.begin(), ids.end()))
		<< "recycled ids drifted upward";
}

// ===========================================================================
// CommandBuffer: structural changes recorded now and applied at a chosen
// point. Two things it has to get right -- the same end state as the
// immediate calls, and never applying anything while a view is open.
// ===========================================================================

namespace {
struct CbA { int v{}; };
struct CbB { double d{}; };
} // namespace

TEST(Regression_CommandBuffer, EndsWhereImmediateCallsWould) {
	std::mt19937 rng(9001);
	for (int iter = 0; iter < 120; ++iter) {
		Registry<true> immediate, buffered;
		CommandBuffer<true> cb;

		const uint32_t space = 40 + rng() % 120;
		for (uint32_t i = 0; i < space; ++i) { immediate.takeEntity(); buffered.takeEntity(); }
		for (uint32_t i = 0; i < space; i += 3) {
			immediate.addComponent<CbA>(i, CbA{ int(i) });
			buffered.addComponent<CbA>(i, CbA{ int(i) });
		}

		// Recording against an entity destroyed earlier in the same buffer is a caller error
		// by contract, so the script does not do it.
		std::vector<bool> gone(space, false);
		for (int o = 0, n = 1 + int(rng() % 80); o < n; ++o) {
			const EntityId e = EntityId(rng() % space);
			if (gone[e]) { continue; }
			switch (rng() % 4) {
				case 0: immediate.addComponent<CbA>(e, CbA{ o }); cb.addComponent<CbA>(e, CbA{ o }); break;
				case 1: immediate.addComponent<CbB>(e, CbB{ double(o) }); cb.addComponent<CbB>(e, CbB{ double(o) }); break;
				case 2: immediate.destroyComponent<CbA>(e); cb.destroyComponent<CbA>(e); break;
				default: immediate.destroyEntity(e); cb.destroyEntity(e); gone[e] = true; break;
			}
		}
		cb.apply(buffered);
		EXPECT_TRUE(cb.empty()) << "buffer still holds work after apply, iteration " << iter;

		for (uint32_t i = 0; i < space; ++i) {
			ASSERT_EQ(immediate.contains(i), buffered.contains(i)) << "entity " << i << ", iter " << iter;
			ASSERT_EQ(immediate.hasComponent<CbA>(i), buffered.hasComponent<CbA>(i)) << "A on " << i << ", iter " << iter;
			ASSERT_EQ(immediate.hasComponent<CbB>(i), buffered.hasComponent<CbB>(i)) << "B on " << i << ", iter " << iter;
		}
	}
}

TEST(Regression_CommandBuffer, LastRecordedOperationWins) {
	Registry<true> reg;
	for (EntityId e = 0; e < 4; ++e) { reg.takeEntity(); reg.addComponent<CbA>(e, CbA{ int(e) }); }

	CommandBuffer<true> cb;
	cb.destroyComponent<CbA>(1);            // remove then add back: ends present
	cb.addComponent<CbA>(1, CbA{ 111 });
	cb.addComponent<CbA>(2, CbA{ 222 });    // add then remove: ends absent
	cb.destroyComponent<CbA>(2);
	cb.apply(reg);

	EXPECT_TRUE(reg.hasComponent<CbA>(1)) << "a component removed and re-added went missing";
	EXPECT_EQ(reg.pinComponent<CbA>(1).get()->v, 111);
	EXPECT_FALSE(reg.hasComponent<CbA>(2)) << "a component added and removed survived";
}

TEST(Regression_CommandBuffer, RecordingDuringIterationDoesNotHang) {
	// The immediate equivalent -- a middle insert into an array this thread is iterating --
	// waits for a hold only this thread could release, and never returns.
	Registry<true> reg;
	for (EntityId e = 100; e < 400; ++e) { reg.addComponent<CbA>(e, CbA{ int(e) }); }

	std::atomic<bool> done{ false };
	std::thread worker([&] {
		CommandBuffer<true> cb;
		{
			auto view = reg.view<CbA>();
			for (auto [e, a] : view) {
				if (a && a->v % 7 == 0) { cb.addComponent<CbB>(e, CbB{ 1.0 }); }
			}
		}
		cb.apply(reg);
		done.store(true, std::memory_order_release);
	});

	const bool finished = waitFor([&] { return done.load(std::memory_order_acquire); });
	ASSERT_TRUE(finished) << "recording during iteration hung; the buffer must defer, not apply";
	worker.join();

	size_t gained = 0;
	for (EntityId e = 100; e < 400; ++e) { gained += reg.hasComponent<CbB>(e); }
	EXPECT_GT(gained, 0u) << "the recorded work was never applied";
}

// ===========================================================================
// update() used to wait for the array to go quiet before compacting it, which
// meant a caller iterating that same array waited for something only it could
// release. That is what forced the call to be placed carefully in a frame.
// Compaction is attempted now, and skipped when the array is busy.
// ===========================================================================

TEST(Regression_Maintenance, UpdateFromInsideIterationDoesNotHang) {
	Registry<true> reg;
	for (EntityId e = 0; e < 2000; ++e) { reg.addComponent<RInt>(e, RInt{ int(e) }); }
	for (EntityId e = 0; e < 2000; e += 2) { reg.destroyComponent<RInt>(e); }   // queue work

	std::atomic<bool> done{ false };
	std::thread worker([&] {
		auto view = reg.view<RInt>();
		auto it = view.begin();
		(void)it;
		reg.update();          // the array this thread is holding open
		done.store(true, std::memory_order_release);
	});

	const bool finished = waitFor([&] { return done.load(std::memory_order_acquire); });
	ASSERT_TRUE(finished) << "update() blocked on a view held by the calling thread";
	worker.join();

	// and the deferred work is not lost -- a later call, with nothing in the way, does it
	for (int i = 0; i < 4; ++i) { reg.update(); }
	for (EntityId e = 1; e < 2000; e += 2) {
		ASSERT_TRUE(reg.hasComponent<RInt>(e)) << "surviving component " << e << " lost during compaction";
	}
	for (EntityId e = 0; e < 2000; e += 2) {
		ASSERT_FALSE(reg.hasComponent<RInt>(e)) << "destroyed component " << e << " came back";
	}
}

// ===========================================================================
// setAutoMaintenance: opening a view gives its arrays the pass update() would
// have. Only possible once update() stopped waiting -- before that, a view
// doing maintenance on an array the caller was already holding would hang.
// ===========================================================================

TEST(Regression_AutoMaintenance, OffByDefaultAndOpeningAViewChangesNothing) {
	Registry<true> reg;
	for (EntityId e = 0; e < 2000; ++e) { reg.addComponent<RInt>(e, RInt{ int(e) }); }
	for (EntityId e = 0; e < 2000; e += 2) { reg.destroyComponent<RInt>(e); }

	auto* arr = reg.getComponentContainer<RInt>();
	const size_t before = arr->size();
	EXPECT_FALSE(reg.autoMaintenance()) << "auto maintenance must be opt-in";

	for (auto it : reg.view<RInt>()) { (void)it; }
	EXPECT_EQ(arr->size(), before) << "a view compacted the array without being asked to";
}

TEST(Regression_AutoMaintenance, AViewCompactsWhatUpdateWouldHave) {
	Registry<true> reg;
	for (EntityId e = 0; e < 4000; ++e) { reg.addComponent<RInt>(e, RInt{ int(e) }); }
	for (EntityId e = 0; e < 4000; e += 2) { reg.destroyComponent<RInt>(e); }

	auto* arr = reg.getComponentContainer<RInt>();
	const size_t before = arr->size();
	reg.setAutoMaintenance(true);

	size_t seen = 0;
	for (auto [e, v] : reg.view<RInt>()) { (void)e; if (v) { ++seen; } }

	EXPECT_LT(arr->size(), before) << "the dead half was never compacted away";
	EXPECT_EQ(seen, 2000u) << "compaction lost or duplicated live components";
	for (EntityId e = 1; e < 4000; e += 2) {
		ASSERT_TRUE(reg.hasComponent<RInt>(e)) << "live component " << e << " went missing";
	}
}

TEST(Regression_AutoMaintenance, ANestedViewDoesNotHang) {
	// The inner view attempts maintenance on an array the outer one is holding open. It has
	// to skip rather than wait, or the thread blocks on its own hold.
	Registry<true> reg;
	for (EntityId e = 0; e < 2000; ++e) { reg.addComponent<RInt>(e, RInt{ int(e) }); }
	for (EntityId e = 0; e < 2000; e += 2) { reg.destroyComponent<RInt>(e); }
	reg.setAutoMaintenance(true);

	std::atomic<bool> done{ false };
	std::atomic<size_t> inner{ 0 };
	std::thread worker([&] {
		auto outer = reg.view<RInt>();
		auto it = outer.begin();
		(void)it;
		size_t n = 0;
		for (auto [e, v] : reg.view<RInt>()) { (void)e; if (v) { ++n; } }
		inner.store(n, std::memory_order_release);
		done.store(true, std::memory_order_release);
	});

	const bool finished = waitFor([&] { return done.load(std::memory_order_acquire); });
	ASSERT_TRUE(finished) << "a nested view blocked maintaining an array the outer view holds";
	worker.join();
	EXPECT_EQ(inner.load(), 1000u) << "the inner view did not see the live half";
}

TEST(Regression_AutoMaintenance, ReachesArraysNobodyIterates) {
	// Maintaining only the arrays a view names would leave a component type that is looked up
	// but never iterated holding its dead sectors forever, and update() would still be needed.
	// A rotation slot gives one other array a turn per few views.
	struct Iterated { int v{}; };
	struct LookedUpOnly { int v{}; };

	Registry<true> reg;
	reg.registerArray<Iterated>(8000);
	reg.registerArray<LookedUpOnly>(8000);
	reg.setAutoMaintenance(true);

	for (EntityId e = 0; e < 8000; ++e) {
		reg.addComponent<Iterated>(e, Iterated{ int(e) });
		reg.addComponent<LookedUpOnly>(e, LookedUpOnly{ int(e) });
	}
	for (EntityId e = 0; e < 8000; e += 2) {
		reg.destroyComponent<Iterated>(e);
		reg.destroyComponent<LookedUpOnly>(e);
	}

	auto* quiet = reg.getComponentContainer<LookedUpOnly>();
	const size_t before = quiet->size();

	// frames that only ever open a view over the other type, and never call update()
	for (int frame = 0; frame < 40; ++frame) {
		for (auto it : reg.view<Iterated>()) { (void)it; }
	}

	EXPECT_LT(quiet->size(), before)
		<< "an array nobody iterates was never compacted, so update() is still required";
	for (EntityId e = 1; e < 8000; e += 2) {
		ASSERT_TRUE(reg.hasComponent<LookedUpOnly>(e)) << "live component " << e << " lost";
	}
}

// ===========================================================================
// Every registerArray publishes a new snapshot of the registered arrays, and
// superseded ones used to be kept until the registry died, because a lock-free
// reader might still be inside one. That made a registry's snapshot memory
// quadratic in the number of component types: 400 types held about 4.9 MB.
// They go through a retire bin now, like every other buffer readers can hold.
// ===========================================================================

TEST(Regression_Snapshots, SupersededOnesAreFreedRatherThanKept) {
	Registry<true> reg;

	// each registration publishes one, superseding the last
	constexpr int kTypes = 64;
	[&]<int... I>(std::integer_sequence<int, I...>) {
		((reg.registerArray<TypeN<I>>(0)), ...);
	}(std::make_integer_sequence<int, kTypes>{});

	// The grace period has to pass first: a reader that loaded a snapshot before it was
	// superseded may still be walking it.
	size_t freed = 0;
	for (int t = 0; t < 6; ++t) { freed += reg.tick(); }

	EXPECT_GE(freed, size_t(kTypes - 1))
		<< "superseded snapshots were not reclaimed (" << freed << " blocks freed, expected at "
		   "least " << (kTypes - 1) << ")";

	// and the registry still works through the one that is still published
	for (EntityId e = 0; e < 100; ++e) { reg.addComponent<TypeN<0>>(e, TypeN<0>{ int(e) }); }
	for (EntityId e = 0; e < 100; ++e) { ASSERT_TRUE(reg.hasComponent<TypeN<0>>(e)); }
}

TEST(Regression_Snapshots, PlainBuildFreesThemImmediately) {
	// No lock-free readers there, so a superseded snapshot has nothing to outlive.
	Registry<false> reg;
	[&]<int... I>(std::integer_sequence<int, I...>) {
		((reg.registerArray<TypeN<100 + I>>(0)), ...);
	}(std::make_integer_sequence<int, 32>{});

	EXPECT_EQ(reg.tick(), 0u)
		<< "the plain build queued snapshots for later instead of freeing them on release";

	for (EntityId e = 0; e < 50; ++e) { reg.addComponent<TypeN<100>>(e, TypeN<100>{ int(e) }); }
	for (EntityId e = 0; e < 50; ++e) { ASSERT_TRUE(reg.hasComponent<TypeN<100>>(e)); }
}

// ===========================================================================
// The container keeps an array's shape safe, not a component's value: guarding
// that would mean locking or pinning per element. setAccessTracking makes the
// gap visible in debug builds instead of leaving it to a sanitizer.
// The conflict itself aborts, which the harness cannot catch, so what is
// checked here is the other half: that it stays quiet when it should.
// ===========================================================================

namespace {
struct TrkA { int v{}; };
struct TrkB { int v{}; };
} // namespace

TEST(Regression_AccessTracking, QuietWhenOneThreadReadsThenWritesTheSameType) {
	Registry<true> reg;
	reg.setAccessTracking(true);
	for (EntityId e = 0; e < 100; ++e) { reg.addComponent<TrkA>(e, TrkA{ int(e) }); }

	// A system routinely reads what it just wrote. Re-entering from the same thread must not
	// be mistaken for two threads colliding.
	size_t seen = 0;
	for (auto [e, a] : reg.view<TrkA>()) { (void)e; if (a) { ++seen; } }
	reg.addComponent<TrkA>(500, TrkA{ 1 });
	EXPECT_EQ(seen, 100u);
	EXPECT_TRUE(reg.hasComponent<TrkA>(500));

	reg.setAccessTracking(false);
}

TEST(Regression_AccessTracking, QuietWhenThreadsUseDifferentTypes) {
	Registry<true> reg;
	reg.setAccessTracking(true);
	for (EntityId e = 0; e < 200; ++e) { reg.addComponent<TrkA>(e, TrkA{ int(e) }); }

	std::atomic<bool> stop{ false };
	std::atomic<int> written{ 0 };
	std::thread writer([&] {
		EntityId e = 1000;
		while (!stop.load(std::memory_order_relaxed)) {
			reg.addComponent<TrkB>(e++, TrkB{ 1 });
			written.fetch_add(1, std::memory_order_release);
		}
	});
	// Keep reading until the writer has demonstrably been running alongside; the reader is
	// fast enough to finish before the thread is even scheduled otherwise.
	for (int i = 0; i < 50 || written.load(std::memory_order_acquire) < 100; ++i) {
		for (auto [e, a] : reg.view<TrkA>()) { (void)e; (void)a; }
	}
	stop.store(true, std::memory_order_release);
	writer.join();

	EXPECT_GT(written.load(), 0) << "the writer never ran, so nothing was overlapped";
	reg.setAccessTracking(false);
}

TEST(Regression_AccessTracking, OffByDefault) {
	Registry<true> reg;
	for (EntityId e = 0; e < 50; ++e) { reg.addComponent<TrkA>(e, TrkA{ int(e) }); }

	// Two threads on one type, which the tracker would abort on. It must not be watching
	// unless asked, or every existing program that does this would stop working in debug.
	std::atomic<bool> stop{ false };
	std::thread writer([&] {
		EntityId e = 0;
		while (!stop.load(std::memory_order_relaxed)) { reg.addComponent<TrkA>(e++ % 50, TrkA{ 2 }); }
	});
	for (int i = 0; i < 20; ++i) {
		for (auto [e, a] : reg.view<TrkA>()) { (void)e; (void)a; }
	}
	stop.store(true, std::memory_order_release);
	writer.join();
	SUCCEED() << "reached the end without the tracker firing";
}

// ===========================================================================
// access<> puts the value-level guarantee at the granularity systems work at:
// a reader-writer lock per component type, taken once per system rather than
// per element. Claims go in one call so every caller locks in the same order.
// ===========================================================================

namespace {
struct AccA { int v{}; };
struct AccB { int v{}; };
} // namespace

TEST(Regression_Access, KeepsAReaderAndAWriterOffOneType) {
	Registry<true> reg;
	reg.setAccessTracking(true);   // aborts the moment the two ever overlap
	for (EntityId e = 0; e < 400; ++e) { reg.addComponent<AccA>(e, AccA{ int(e) }); }

	std::atomic<bool> stop{ false };
	std::atomic<long long> reads{ 0 }, writes{ 0 };
	std::thread reader([&] {
		while (!stop.load(std::memory_order_relaxed)) {
			auto guard = reg.access<Read<AccA>>();
			for (auto [e, a] : reg.view<AccA>()) { (void)e; (void)a; }
			reads.fetch_add(1, std::memory_order_relaxed);
		}
	});
	std::thread writer([&] {
		EntityId e = 0;
		while (!stop.load(std::memory_order_relaxed)) {
			auto guard = reg.access<Write<AccA>>();
			reg.addComponent<AccA>(e++ % 400, AccA{ 7 });
			writes.fetch_add(1, std::memory_order_relaxed);
		}
	});
	std::this_thread::sleep_for(std::chrono::milliseconds(400));
	stop.store(true, std::memory_order_release);
	reader.join();
	writer.join();

	EXPECT_GT(reads.load(), 0) << "the reader never ran";
	EXPECT_GT(writes.load(), 0) << "the writer never ran";
	reg.setAccessTracking(false);
}

TEST(Regression_Access, OppositeClaimOrdersDoNotDeadlock) {
	// Taken one at a time these would be a textbook lock-order inversion. Asked for together,
	// the guard sorts them by type id, so both threads lock in the same sequence.
	Registry<true> reg;
	for (EntityId e = 0; e < 50; ++e) { reg.addComponent<AccA>(e, AccA{ 1 }); reg.addComponent<AccB>(e, AccB{ 1 }); }

	std::atomic<int> finished{ 0 };
	std::thread t1([&] {
		for (int i = 0; i < 20000; ++i) { auto g = reg.access<Read<AccA>, Write<AccB>>(); }
		finished.fetch_add(1, std::memory_order_release);
	});
	std::thread t2([&] {
		for (int i = 0; i < 20000; ++i) { auto g = reg.access<Write<AccB>, Read<AccA>>(); }
		finished.fetch_add(1, std::memory_order_release);
	});

	const bool done = waitFor([&] { return finished.load(std::memory_order_acquire) == 2; });
	ASSERT_TRUE(done) << "two systems claiming the same pair in opposite orders deadlocked";
	t1.join();
	t2.join();
}

TEST(Regression_Access, NestingOnOneThreadIsAllowed) {
	// std::shared_mutex is not recursive, and a second acquire deadlocks against a waiting
	// writer. A system calling a helper that claims the same type must not stop.
	Registry<true> reg;
	for (EntityId e = 0; e < 20; ++e) { reg.addComponent<AccA>(e, AccA{ int(e) }); }

	std::atomic<bool> done{ false };
	std::thread worker([&] {
		auto outer = reg.access<Write<AccA>>();
		{
			auto inner = reg.access<Write<AccA>>();
			reg.addComponent<AccA>(99, AccA{ 3 });
		}
		reg.addComponent<AccA>(98, AccA{ 4 });   // outer must still hold it
		done.store(true, std::memory_order_release);
	});

	ASSERT_TRUE(waitFor([&] { return done.load(std::memory_order_acquire); }))
		<< "a nested claim on the same type blocked against itself";
	worker.join();
	EXPECT_TRUE(reg.hasComponent<AccA>(99));
	EXPECT_TRUE(reg.hasComponent<AccA>(98));
}

// ===========================================================================
// eachSingle() takes a typed fast path when a sector holds one component, no
// padding, and nothing in the array is dead -- the chunk is then a plain T[].
// The guard is what makes it safe, so these check the cases that must NOT take
// it as much as the one that must.
// ===========================================================================

struct FpA { int v{}; };
struct FpB { int v{}; };

TEST(Regression_Iteration, TypedFastPathHonoursHolesAndGroups) {
	// holes: the array has dead sectors, so every slot must still be checked
	{
		Registry<false> reg;
		std::set<int> want;
		for (EntityId e = 0; e < 500; ++e) {
			reg.takeEntity();
			reg.addComponent<FpA>(e, FpA{ int(e) });
			want.insert(int(e));
		}
		for (EntityId e = 0; e < 500; e += 3) {
			reg.destroyComponent<FpA>(e);
			want.erase(int(e));
		}
		std::set<int> got;
		reg.view<FpA>().each([&](FpA& a) { got.insert(a.v); });
		EXPECT_EQ(got, want) << "iteration visited dead single-type sectors";

		// compacting removes the holes, so the fast path becomes legal again
		reg.getComponentContainer<FpA>()->defragment();
		std::set<int> after;
		reg.view<FpA>().each([&](FpA& a) { after.insert(a.v); });
		EXPECT_EQ(after, want) << "the fast path changed what iteration yields";
	}

	// grouped: a sector stays alive through its other component, so a cleared
	// FpA bit must still be respected even with no dead sectors at all
	{
		Registry<false> reg;
		reg.registerArray<FpA, FpB>();
		std::set<int> want;
		for (EntityId e = 0; e < 300; ++e) {
			reg.takeEntity();
			reg.addComponent<FpA>(e, FpA{ int(e) });
			reg.addComponent<FpB>(e, FpB{ int(e) });
			want.insert(int(e));
		}
		for (EntityId e = 0; e < 300; e += 10) {
			reg.destroyComponent<FpA>(e);
			want.erase(int(e));
		}
		std::set<int> got;
		reg.view<FpA>().each([&](FpA& a) { got.insert(a.v); });
		EXPECT_EQ(got, want) << "a grouped array took the single-type fast path";
	}

	// the case the fast path is for: many chunks, nothing dead
	{
		Registry<false> reg;
		size_t sum = 0, expected = 0;
		for (EntityId e = 0; e < 20000; ++e) {
			reg.takeEntity();
			reg.addComponent<FpA>(e, FpA{ int(e) });
			expected += e;
		}
		reg.view<FpA>().each([&](FpA& a) { sum += size_t(a.v); });
		EXPECT_EQ(sum, expected);
	}
}

namespace {

struct GrpA { int v{}; };
struct GrpB { int v{}; };

/// registerArray() returned without a word when every type it named already had an array.
/// That is right when they are already in one together -- the call is idempotent -- but not
/// when they are in separate arrays: a component's layout is fixed when its array is created,
/// so the grouping asked for can never happen. A registerArray() written one line too late,
/// after the addComponent() that registered the type implicitly, looked exactly like one that
/// worked. The README's own Quick Start had it in that order.
TEST(Regression_Grouping, LateRegisterArraySaysSoInsteadOfReturningQuietly) {
	{
		// Asking twice for the same group is idempotent and must stay silent.
		Registry<false> reg;
		reg.registerArray<GrpA, GrpB>();
		testing::internal::CaptureStderr();
		reg.registerArray<GrpA, GrpB>();
		EXPECT_EQ(testing::internal::GetCapturedStderr(), "")
			<< "re-registering the same group is not a mistake and must not warn";
		EXPECT_EQ(reg.getComponentContainer<GrpA>(), reg.getComponentContainer<GrpB>());
	}
	{
		// Both types already registered implicitly, each into its own array.
		Registry<false> reg;
		const auto e = reg.takeEntity();
		reg.addComponent<GrpA>(e, GrpA{ 1 });
		reg.addComponent<GrpB>(e, GrpB{ 2 });

		testing::internal::CaptureStderr();
		reg.registerArray<GrpA, GrpB>();
		const std::string err = testing::internal::GetCapturedStderr();

		EXPECT_NE(err.find("was ignored"), std::string::npos)
			<< "the grouping could not be honoured and the call said nothing";
		EXPECT_NE(reg.getComponentContainer<GrpA>(), reg.getComponentContainer<GrpB>())
			<< "the arrays are still separate -- which is the point of the warning";
	}
}

/// Every ranged-iterator test walked a handful of sectors inside one 8192-sector chunk, so
/// the branch that carries RangedIterator across a chunk boundary -- recomputing the chunk
/// base and the data pointer -- had never run in the suite. Counting is not enough to catch a
/// wrong base either, so this reads the values back.
TEST(Regression_RangedIterator, CrossesChunkBoundaries) {
	using Small = SectorsArray<false, ChunksAllocator<16>>;
	std::unique_ptr<Small> arr(Small::create<RInt>());

	constexpr SectorId kCount = 100;   // seven chunks of sixteen
	for (SectorId i = 0; i < kCount; ++i) { arr->emplace<RInt>(i, RInt{ int(i) * 3 }); }

	// Starts mid-chunk and ends mid-chunk, five boundaries apart.
	Ranges<SectorId> r(std::pair<SectorId, SectorId>{ 5, 90 });

	std::vector<int> seen;
	for (auto it = arr->beginRanged(r), en = arr->endRanged(); it != en; ++it) {
		const auto slot = *it;
		auto* c = Sector::getComponent<RInt>(slot.data, arr->getIsAlive(slot.id), arr->getLayout());
		ASSERT_NE(c, nullptr);
		seen.push_back(c->v);
	}

	ASSERT_EQ(seen.size(), 85u);
	for (size_t k = 0; k < seen.size(); ++k) {
		ASSERT_EQ(seen[k], int(5 + k) * 3) << "wrong sector at offset " << k
			<< " -- a chunk boundary was crossed with a stale base";
	}
}

/// takeEntities was only ever tested on Registry<true>; the plain build takes a different
/// path through IdSet, which had no direct test of its own either.
TEST(Regression_BatchIds, PlainBuildTakesEntitiesInBulk) {
	Registry<false> reg;
	std::vector<EntityId> ids;
	reg.takeEntities(5000, ids);
	ASSERT_EQ(ids.size(), 5000u);

	std::set<EntityId> distinct(ids.begin(), ids.end());
	EXPECT_EQ(distinct.size(), ids.size()) << "the same id was handed out twice";
	for (auto e : ids) { EXPECT_TRUE(reg.contains(e)); }

	// Freed ids come back, lowest first, and the batch form has to see them.
	std::vector<EntityId> half(ids.begin(), ids.begin() + 2000);
	reg.destroyEntities(half);

	std::vector<EntityId> again;
	reg.takeEntities(2000, again);
	EXPECT_EQ(again.size(), 2000u);
	std::set<EntityId> reused(again.begin(), again.end());
	EXPECT_EQ(reused.size(), again.size());
	for (auto e : again) { EXPECT_TRUE(reg.contains(e)); }
}

struct GrpC { int v{}; };

/// hasComponent<T>() went through getComponentAccess, which registers T on first use. So
/// asking whether an entity has a component nothing had ever added created an array to answer
/// "no" -- and left T registered on its own. The second half is the one that bites: a
/// registerArray<T, U>() afterwards can no longer group them, because T's layout is already
/// fixed. A question turned into a decision.
TEST(Regression_Queries, HasComponentDoesNotRegisterTheTypeItAsksAbout) {
	Registry<false> reg;
	const auto e = reg.takeEntity();

	EXPECT_FALSE(reg.hasComponent<GrpC>(e));

	// If the query registered GrpC, this grouping is no longer possible and warns about it.
	testing::internal::CaptureStderr();
	reg.registerArray<GrpC, GrpB>();
	EXPECT_EQ(testing::internal::GetCapturedStderr(), "")
		<< "the query registered the type, putting the grouping out of reach";
	EXPECT_EQ(reg.getComponentContainer<GrpC>(), reg.getComponentContainer<GrpB>());
}

/// addComponents(generator) was declared requires(ThreadSafe) although nothing in it needs
/// threads: it drains the generator into a vector and hands the result to insertBulk, which
/// the plain build has had all along. The documentation presented it as the general batch API.
///
/// It matters more than a missing overload usually would. A loop of addComponent() over ids
/// that land in the middle of a compacted array pays a full shift each time -- measured at
/// 39 ms for 200 inserts into 133k live sectors, against 0.5 ms for the same 200 handed over
/// as one batch.
TEST(Regression_BulkInsert, GeneratorFormWorksInThePlainBuild) {
	Registry<false> reg;
	// Deliberately out of order: this is the case the batch form exists for.
	const std::vector<EntityId> ids{ 7, 1, 4, 9, 2 };
	size_t next = 0;
	reg.addComponents<RInt>([&]() -> std::pair<EntityId, RInt> {
		if (next == ids.size()) { return { INVALID_ID, RInt{} }; }
		const auto id = ids[next++];
		return { id, RInt{ int(id) * 10 } };
	});

	EXPECT_EQ(reg.getComponentContainer<RInt>()->size(), ids.size());
	std::map<EntityId, int> got;
	for (auto [e, v] : reg.view<RInt>()) { if (v) { got[e] = v->v; } }
	EXPECT_EQ(got.size(), ids.size());
	for (auto id : ids) {
		ASSERT_TRUE(got.count(id)) << "id " << id << " did not survive the batch";
		EXPECT_EQ(got[id], int(id) * 10);
	}
}

/// mergeIntersections only ever compares neighbours, so it is right only on a sorted list --
/// and the constructor hands it whatever the caller passed. An unsorted list came back
/// unmerged and every later lookup trusted it. Folding also erased from the middle, shifting
/// the tail once per merge, so a contiguous block of ids cost O(n^2) to collapse.
/// mergeIntersections folds by comparing one range's end against the next one's start, which
/// a reversed pair does not merely fail to match -- it corrupts the running end. Empty and
/// reversed ranges are dropped before the fold rather than trusted to fall out of it.
TEST(Regression_Ranges, DegenerateRangesAreDroppedBeforeFolding) {
	{
		Ranges<EntityId> r(std::vector<std::pair<EntityId, EntityId>>{ {0, 5}, {9, 3}, {10, 15} });
		ASSERT_EQ(r.ranges.size(), 2u) << "a reversed range survived the fold";
		EXPECT_EQ(r.ranges[0].first, 0u);
		EXPECT_EQ(r.ranges[0].second, 5u);
		EXPECT_EQ(r.ranges[1].first, 10u);
		EXPECT_EQ(r.ranges[1].second, 15u);
	}
	{
		// Empty ranges say nothing and must not split the run they sit inside.
		Ranges<EntityId> r(std::vector<std::pair<EntityId, EntityId>>{ {0, 5}, {5, 5}, {5, 10} });
		ASSERT_EQ(r.ranges.size(), 1u);
		EXPECT_EQ(r.ranges[0].first, 0u);
		EXPECT_EQ(r.ranges[0].second, 10u);
	}
	{
		// Nothing but degenerate input leaves nothing behind.
		Ranges<EntityId> r(std::vector<std::pair<EntityId, EntityId>>{ {7, 7}, {9, 2} });
		EXPECT_TRUE(r.ranges.empty());
	}
}

/// take() returned takeBlock(1).first without looking at the count, so an exhausted id space
/// handed back whatever .first happened to hold as though it were a fresh id. And takeBlock
/// clamped a request for zero up to one, which is not what asking for none means.
TEST(Regression_Ranges, ExhaustionAndZeroRequestsAreReported) {
	{
		Ranges<uint8_t> r;
		// 255 is reserved: ranges carry an exclusive end, so it can never be handed out.
		size_t taken = 0;
		try {
			for (int i = 0; i < 300; ++i) { r.take(); ++taken; }
			FAIL() << "took " << taken << " ids from an 8-bit space without ever running out";
		}
		catch (const std::overflow_error&) {
			EXPECT_EQ(taken, 255u) << "stopped at the wrong point";
		}
	}
	{
		Ranges<EntityId> r;
		const auto [first, count] = r.takeBlock(0);
		EXPECT_EQ(count, 0u) << "asking for no ids handed out one";
		(void)first;
	}
}

TEST(Regression_Ranges, MergeSortsFirstAndFoldsInOnePass) {
	{
		// Same three ranges, out of order. They describe one contiguous span either way.
		Ranges<EntityId> r(std::vector<std::pair<EntityId, EntityId>>{ {20, 30}, {0, 10}, {10, 20} });
		ASSERT_EQ(r.ranges.size(), 1u) << "unsorted input was left unmerged";
		EXPECT_EQ(r.ranges[0].first, 0u);
		EXPECT_EQ(r.ranges[0].second, 30u);
	}
	{
		// Already sorted and disjoint: nothing may be merged away.
		Ranges<EntityId> r(std::vector<std::pair<EntityId, EntityId>>{ {0, 5}, {10, 15}, {20, 25} });
		ASSERT_EQ(r.ranges.size(), 3u);
		EXPECT_EQ(r.ranges[2].second, 25u);
	}
	{
		// The quadratic case: many ranges that fold into one. Correctness here, not timing --
		// the single pass is what makes it affordable, and the result must be identical.
		std::vector<std::pair<EntityId, EntityId>> many;
		for (EntityId i = 0; i < 20000; ++i) { many.emplace_back(i, i + 1); }
		Ranges<EntityId> r(many);
		ASSERT_EQ(r.ranges.size(), 1u);
		EXPECT_EQ(r.ranges[0].first, 0u);
		EXPECT_EQ(r.ranges[0].second, 20000u);
	}
}

/// Counts what actually happened to it. A byte-wise relocation runs no constructor and no
/// destructor and leaves any self-reference pointing at the old address, so all three of
/// these say plainly whether the sector was moved or just copied.
struct RTracked {
	static inline int moves = 0;
	static inline int dtors = 0;
	static void reset() { moves = 0; dtors = 0; }

	std::string s;
	const RTracked* self = this;

	RTracked() = default;
	explicit RTracked(std::string v) : s(std::move(v)) {}
	RTracked(const RTracked& o) : s(o.s) {}
	RTracked(RTracked&& o) noexcept : s(std::move(o.s)) { ++moves; }
	RTracked& operator=(const RTracked& o) { s = o.s; return *this; }
	RTracked& operator=(RTracked&& o) noexcept { s = std::move(o.s); ++moves; return *this; }
	~RTracked() { ++dtors; }
};

/// A move between arrays of different chunk capacity cannot adopt the chunks -- every sector
/// lands at a new address -- so it relocates them. That relocation used to be a memcpy of the
/// sector bytes followed by freeing the source chunks, which runs no constructor on the
/// destination and no destructor on the source: anything owning a resource ends up with two
/// owners, and anything pointing into itself keeps pointing at the address it left. The
/// liveness words saying which members are actually constructed live in the array, not the
/// allocator, so the relocation belongs at the level that has them.
TEST(Regression_Move, CrossCapacityMoveRelocatesNonTrivialComponents) {
	using Small = SectorsArray<false, ChunksAllocator<16>>;
	using Large = SectorsArray<false, ChunksAllocator<64>>;

	constexpr SectorId kCount = 40;
	const auto nameOf = [](SectorId i) {
		// Comfortably past any small-string buffer, so the string really owns a heap block.
		return "component-number-" + std::to_string(i) + "-with-enough-text";
	};

	std::unique_ptr<Small> src(Small::create<RTracked>());
	for (SectorId i = 0; i < kCount; ++i) { src->emplace<RTracked>(i, RTracked{ nameOf(i) }); }

	std::unique_ptr<Large> dst(Large::create<RTracked>());
	RTracked::reset();
	*dst = std::move(*src);

	EXPECT_EQ(RTracked::moves, int(kCount)) << "sectors were relocated without being moved";
	EXPECT_EQ(RTracked::dtors, int(kCount)) << "the source sectors were freed undestroyed";

	ASSERT_EQ(dst->size(), size_t(kCount));
	for (SectorId i = 0; i < kCount; ++i) {
		const auto slot = dst->findSlot(i);
		ASSERT_TRUE(slot) << "sector " << i << " was lost in the move";
		auto* n = Sector::getComponent<RTracked>(slot.data, dst->getIsAlive(i), dst->getLayout());
		ASSERT_NE(n, nullptr);
		EXPECT_EQ(n->s, nameOf(i)) << "sector " << i << " did not survive relocation intact";
		EXPECT_EQ(n->self, n) << "sector " << i << " still points at the address it left";
	}
	EXPECT_EQ(src->size(), 0u) << "the source still claims sectors it handed over";
}

/// clearAsync() used to release every entity id on the spot, while the arrays kept their
/// sectors until a later update(). takeEntity() then handed a live id straight back and the
/// new entity read the old one's components as its own.
TEST(Regression_EntityIds, ClearAsyncHoldsIdsUntilTheComponentsAreGone) {
	Registry<true> reg;
	reg.setAutoMaintenance(false);

	const auto a = reg.takeEntity();
	reg.addComponent<RPos>(a, RPos{ 1.f, 2.f, 3.f });
	reg.addComponent<RVel>(a, RVel{ 4.f, 5.f, 6.f });

	reg.clearAsync();

	// Nothing has actually been cleared yet -- no update() has run.
	const auto b = reg.takeEntity();
	EXPECT_FALSE(reg.hasComponent<RPos>(b))
		<< "a recycled id named a sector whose components were still there";
	EXPECT_FALSE(reg.hasComponent<RVel>(b));

	reg.update();
	EXPECT_FALSE(reg.hasComponent<RPos>(a)) << "the deferred clear never ran";
}

/// The other half of what reserve() documents: shrink_to_fit reallocates too, and the old
/// buffers go to the retire bin while the published view still names them. Only reserve()
/// republished, so a view opened after shrinkToFit() read memory that was correct until the
/// next write and freed once the grace period ran out.
TEST(Regression_Reclamation, ShrinkToFitRepublishesTheDenseView) {
	Registry<true> reg;
	auto* arr = reg.getComponentContainer<RInt>();
	arr->reserve(100000);
	for (EntityId e = 0; e < 4; ++e) { reg.addComponent<RInt>(e, RInt{ int(e) }); }

	arr->shrinkToFit();   // hands back the spare capacity, moving the buffers again

	reg.destroyComponent<RInt>(2);

	EXPECT_FALSE(reg.hasComponent<RInt>(2))
		<< "a destroyed component still reads as present -- the published dense view still "
		   "names the buffers shrinkToFit() moved away from";
	size_t seen = 0;
	for (auto it : reg.view<RInt>()) { (void)it; ++seen; }
	EXPECT_EQ(seen, 3u) << "iteration walked a stale dense view";
}

/// tick() bails out on mPending alone, so a move that carried the queued blocks without the
/// counter produced a bin that reported nothing to do while still holding them: they lived
/// until the destructor, whatever the grace period said.
TEST(Regression_Reclamation, RetireBinMoveCarriesThePendingCount) {
	{
		RetireBin src(1);
		src.retire(std::malloc(64));
		src.retire(std::malloc(64));

		RetireBin dst(std::move(src));
		EXPECT_EQ(dst.tick(), 2u) << "the moved-to bin never freed what it inherited";
		EXPECT_EQ(src.tick(), 0u) << "the moved-from bin kept blocks it no longer owns";
	}
	{
		// Move-assignment has the destination's own queue to deal with first: the assignment
		// overwrites the vector that owns those blocks, and nothing else names them.
		RetireBin src(1), dst(1);
		src.retire(std::malloc(32));
		dst.retire(std::malloc(32));

		dst = std::move(src);
		EXPECT_EQ(dst.tick(), 1u);
	}
}

/// A view's cached begin iterator points into the view's own mRanges, and its structural
/// holds are released against per-thread bookkeeping. Copying or moving one left the
/// iterator addressing the original and a second owner releasing holds it never took.
TEST(Regression_Iteration, ViewIsNeitherCopiedNorMoved) {
	using View = decltype(std::declval<Registry<true>&>().view<RPos>());
	static_assert(!std::is_copy_constructible_v<View>);
	static_assert(!std::is_move_constructible_v<View>);
	static_assert(!std::is_copy_assignable_v<View>);
	static_assert(!std::is_move_assignable_v<View>);
	SUCCEED();
}

/// There is no lock upgrade. Asking for Write while this thread holds Read used to assert
/// and then carry on, so a release build handed out a Write claim backed by nothing but a
/// shared lock. It now fails the same way in every configuration -- and before any lock is
/// taken, so the half-built guard leaves nothing held.
TEST(Regression_Access, WriteWhileHoldingReadFailsInEveryBuild) {
	Registry<true> reg;
	const auto e = reg.takeEntity();
	reg.addComponent<RPos>(e, RPos{});
	reg.addComponent<RVel>(e, RVel{});

	auto readGuard = reg.access<Read<RPos>>();
	EXPECT_THROW(reg.access<Write<RPos>>(), std::logic_error);

	// The failed attempt took nothing, so an unrelated type is still free to claim.
	EXPECT_NO_THROW(reg.access<Write<RVel>>());
}

} // namespace
