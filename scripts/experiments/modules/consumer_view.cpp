// The gate. This is ordinary ecss iteration, and on cl 19.38 it does not compile through a
// module: C3643 "cannot decompose type with non-static data-members in both ...". Structured
// bindings over a std::tuple that reached the importer through a module boundary fail.
//
// Compile this after any toolchain upgrade. Until it passes, a module build of ecss would
// force every consumer to give up range-for iteration, and the migration is not worth
// discussing.
import ecss;

struct ProbeA { float x, y, z; };
struct ProbeB { float dx, dy, dz; };

float probeView(ecss::Registry<>& reg) {
	float sum = 0.f;

	for (auto [e, a, b] : reg.view<ProbeA, ProbeB>()) {
		if (a && b) { sum += a->x + b->dx; }
	}

	return sum;
}
