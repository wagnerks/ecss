// Iteration through the module. Pairs with consumer_each_hdr.cpp, which is the same code
// through the header; the two together are the payoff measurement.
import ecss;

struct ProbeA { float x, y, z; };
struct ProbeB { float dx, dy, dz; };

float probeEach(ecss::Registry<>& reg) {
	float sum = 0.f;

	reg.view<ProbeA>().each([&](ProbeA& a) { sum += a.x; });
	reg.view<ProbeA, ProbeB>().each([&](ProbeA& a, ProbeB& b) { sum += a.y + b.dy; });

	return sum;
}
