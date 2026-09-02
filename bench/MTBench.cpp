// Fixed-work multi-threaded benchmarks.
//
// The suite's existing MTChurn runs for a fixed two seconds and reports how much it managed.
// Three runs of the same binary have been seen at 2.8M, 6.2M and 8.2M writes -- the split
// between readers and writers is whatever the scheduler decided -- so it cannot detect a
// regression of less than about a factor of three, let alone an improvement.
//
// Here every thread performs exactly the same number of operations and the clock measures how
// long the slowest one takes. That makes two runs comparable, and it makes the thread count
// mean something: the interesting number is not ns/op at 32 threads, it is how ns/op moves
// between 1 and 32. A per-operation cost that stays flat is a path that scales; one that
// climbs is a shared cache line, and the scaling column is there to say which.
//
// Not part of the test suite on purpose -- it would run in every CI lane, sanitizers included.
//   ./scripts/mtbench.ps1

#include <algorithm>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include <ecss/Registry.h>

using namespace ecss;

namespace {

struct MPos { float x, y, z; };
struct MVel { float dx, dy, dz; };

constexpr size_t kEntities = 200'000;
constexpr int    kRepeats  = 5;

using Clock = std::chrono::steady_clock;

/// Keep a value the optimizer cannot argue away. A local, so threads share nothing.
template <class T>
FORCE_INLINE void sink(const T& value) noexcept {
	volatile T hole = value;
	(void)hole;
}

void fill(Registry<true>& reg) {
	reg.setAutoMaintenance(false);
	reg.registerArray<MPos>();
	reg.registerArray<MVel>();
	for (EntityId e = 0; e < static_cast<EntityId>(kEntities); ++e) {
		reg.takeEntity();
		reg.addComponent<MPos>(e, MPos{ float(e), 1.f, 2.f });
		if (e % 2 == 0) { reg.addComponent<MVel>(e, MVel{ .5f, .25f, .125f }); }
	}
}

/// @return nanoseconds per operation, best of kRepeats.
///
/// Best-of rather than mean: a slow run means the scheduler took the thread away, which is
/// noise added to the thing being measured and never subtracted from it.
template <class Body>
double measure(unsigned threads, size_t opsPerThread, Body&& body) {
	double best = 1e30;
	for (int rep = 0; rep < kRepeats; ++rep) {
		std::barrier ready(static_cast<std::ptrdiff_t>(threads) + 1);
		std::vector<std::thread> pool;
		pool.reserve(threads);

		for (unsigned t = 0; t < threads; ++t) {
			pool.emplace_back([&, t] {
				ready.arrive_and_wait();          // every thread starts at once
				body(t, opsPerThread);
				ready.arrive_and_wait();          // and the clock stops at the last one
			});
		}

		ready.arrive_and_wait();
		const auto t0 = Clock::now();
		ready.arrive_and_wait();
		const auto elapsed = std::chrono::duration<double, std::nano>(Clock::now() - t0).count();

		for (auto& th : pool) { th.join(); }
		best = std::min(best, elapsed / double(opsPerThread * threads));
	}
	return best;
}

struct Row { unsigned threads; double nsPerOp; };

void report(const char* name, const std::vector<Row>& rows) {
	std::printf("\n  %s\n", name);
	std::printf("  %8s %12s %11s %12s\n", "threads", "ns/op", "speedup", "efficiency");
	const double one = rows.empty() ? 1.0 : rows.front().nsPerOp;
	for (const auto& r : rows) {
		// ns/op is wall time divided by every operation every thread performed, so it
		// already carries the parallelism: the speedup is one/ns, and the efficiency is
		// that over the thread count. 1.00 is perfect; the shortfall went into contention.
		const double speedup = one / r.nsPerOp;
		std::printf("  %8u %12.2f %10.2fx %12.2f\n",
			r.threads, r.nsPerOp, speedup, speedup / double(r.threads));
	}
}

} // namespace

int main() {
	const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
	std::printf("ecss multi-threaded benchmarks -- fixed work per thread\n");
	std::printf("  %zu entities, best of %d, %u hardware threads\n", kEntities, kRepeats, hw);

	std::vector<unsigned> counts;
	for (unsigned t = 1; t <= hw; t *= 2) { counts.push_back(t); }
	if (counts.back() != hw) { counts.push_back(hw); }

	{   // Opening and closing a view: one structural hold per array taken and released.
		// This is the path that bumps the shared hold generation on every close.
		Registry<true> reg; fill(reg);
		std::vector<Row> rows;
		for (auto t : counts) {
			rows.push_back({ t, measure(t, 200'000, [&](unsigned, size_t n) {
				for (size_t i = 0; i < n; ++i) {
					auto view = reg.view<MPos>();
					sink(view.empty());
				}
			}) });
		}
		report("view open + close (structural hold traffic)", rows);
	}

	{   // Pin and unpin one sector: two atomics on the array-wide aggregate per pair.
		Registry<true> reg; fill(reg);
		std::vector<Row> rows;
		for (auto t : counts) {
			rows.push_back({ t, measure(t, 200'000, [&](unsigned tid, size_t n) {
				// Distinct sectors per thread, so this measures the aggregate rather than
				// contention on one sector's own counter.
				EntityId id = static_cast<EntityId>(tid * 997 % kEntities);
				for (size_t i = 0; i < n; ++i) {
					auto pin = reg.pinComponent<MPos>(id);
					sink(pin ? pin->x : 0.f);
					id = static_cast<EntityId>((id + 1) % kEntities);
				}
			}) });
		}
		report("pin + unpin (array-wide pin aggregate)", rows);
	}

	{   // Pure lock-free reads: sparse table, then the liveness word.
		Registry<true> reg; fill(reg);
		std::vector<Row> rows;
		for (auto t : counts) {
			rows.push_back({ t, measure(t, 1'000'000, [&](unsigned tid, size_t n) {
				EntityId id = static_cast<EntityId>(tid * 997 % kEntities);
				for (size_t i = 0; i < n; ++i) {
					sink(reg.hasComponent<MPos>(id));
					id = static_cast<EntityId>((id + 1) % kEntities);
				}
			}) });
		}
		report("hasComponent (lock-free sparse read)", rows);
	}

	{   // A whole view walked: hold, iteration, and the secondary lookups of a join.
		Registry<true> reg; fill(reg);
		std::vector<Row> rows;
		for (auto t : counts) {
			rows.push_back({ t, measure(t, 20, [&](unsigned, size_t n) {
				for (size_t i = 0; i < n; ++i) {
					float acc = 0;
					reg.view<MPos, MVel>().each([&](MPos& p, MVel& v) { acc += p.x + v.dx; });
					sink(acc);
				}
			}) });
		}
		report("view<MPos, MVel>().each(), whole array per op", rows);
	}

	std::printf("\n  ns/op counts the work of every thread, so a path that scales keeps it"
	            " falling and holds efficiency near 1.00.\n  Efficiency well below 1.00 is a"
	            " shared cache line. Compare two builds at the same thread count.\n");
	return 0;
}
