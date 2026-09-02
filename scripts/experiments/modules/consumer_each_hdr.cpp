// Byte-for-byte the work of consumer_each.cpp, through the header. Doubles as the PCH case:
// the first include is the header being precompiled, so /Yc"ecss/Registry.h" works directly
// without a separate pch.h.
#include <ecss/Registry.h>

struct ProbeA { float x, y, z; };
struct ProbeB { float dx, dy, dz; };

float probeEach(ecss::Registry<>& reg) {
	float sum = 0.f;

	reg.view<ProbeA>().each([&](ProbeA& a) { sum += a.x; });
	reg.view<ProbeA, ProbeB>().each([&](ProbeA& a, ProbeB& b) { sum += a.y + b.dy; });

	return sum;
}
