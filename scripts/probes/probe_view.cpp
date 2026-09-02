// What a TU pays to iterate: plain view, grouped each(), and a ranged view.
#if __has_include(<ecss/View.h>)
#  include <ecss/View.h>
#else
#  include <ecss/Registry.h>
#endif

struct ProbeA { float x, y, z; };
struct ProbeB { float dx, dy, dz; };

float probeView(ecss::Registry<>& reg, const ecss::Ranges<ecss::EntityId>& ranges) {
	float sum = 0.f;

	for (auto [e, a, b] : reg.view<ProbeA, ProbeB>()) {
		if (a && b) { sum += a->x + b->dx; }
	}

	reg.view<ProbeA, ProbeB>().each([&](ProbeA& a, ProbeB& b) {
		sum += a.y + b.dy;
	});

	for (auto [e, a, b] : reg.view<ProbeA, ProbeB>(ranges)) {
		if (a && b) { sum += a->z + b->dz; }
	}

	return sum;
}
