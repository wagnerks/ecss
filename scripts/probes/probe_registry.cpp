// What a TU pays to create and destroy components without ever iterating.
#include <ecss/Registry.h>

struct ProbePos { float x, y, z; };
struct ProbeHp { int v; };

void probeRegistry(ecss::Registry<>& reg, ecss::EntityId e) {
	reg.addComponent<ProbePos>(e, 1.f, 2.f, 3.f);
	reg.addComponent<ProbeHp>(e, 100);

	if (reg.hasComponent<ProbePos>(e)) {
		reg.destroyComponent<ProbeHp>(e);
	}
	if (auto pinned = reg.pinComponent<const ProbePos>(e)) {
		reg.addComponent<ProbeHp>(e, static_cast<int>(pinned->x));
	}
}
