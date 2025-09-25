---
hide:
  - navigation
---

# Examples

Practical patterns for using **ECSS**. Each snippet is self‑contained and focuses on one aspect of the API.

> More real‑world usage: explore the test suite in this repository (covers edge cases, iteration modes, defrag, threading) and my pet engine using ECSS: https://github.com/wagnerks/StelForge  

---

## 1. Basic Movement View
```cpp
struct Position { float x,y; };
struct Velocity { float dx,dy; };

ecss::Registry<false> reg; // non thread-safe
auto e = reg.takeEntity();
reg.addComponent<Position>(e, {0,0});
reg.addComponent<Velocity>(e, {1,0.5f});

for (auto [id, p, v] : reg.view<Position, Velocity>()) {
    if (p && v) { p->x += v->dx; p->y += v->dy; }
}
```

---

## 2. Grouping Hot Components
Group components that are always accessed together for better locality.
```cpp
reg.registerArray<Position, Velocity>(); // future entities sharing both go into same sector

auto e2 = reg.takeEntity();
reg.addComponent<Position>(e2, {5,5});
reg.addComponent<Velocity>(e2, {0.2f, 0});
```

---

## 3. Deferred Erase & Maintenance
```cpp
struct Health { int hp; };

auto e3 = reg.takeEntity();
reg.addComponent<Health>(e3, {10});

// Mark entity for removal when hp <= 0
for (auto [id, h] : reg.view<Health>()) {
    if (h && h->hp <= 0) reg.destroyEntity(id); // deferred
}

reg.update(); // processes deferred destroys + optional defrag
```

---

## 4. Ranged Iteration (Sparse Subset)
```cpp
// Build two disjoint id ranges
ecss::Ranges<ecss::EntityId> subset({ {100, 200}, {400, 450} });
for (auto [id, pos] : reg.view<Position>(subset)) {
    if (pos) {/* operate only on those ids */}
}
```

---

## 5. Foreign Component Projection
Iterate main array (Position) while optionally reading unrelated components (Velocity, Health) stored elsewhere.
```cpp
for (auto [id, p, v, h] : reg.view<Position, Velocity, Health>()) {
    if (v) { p->x += v->dx; }
    if (h && h->hp <= 0) { reg.destroyEntity(id); }
}
```

---

## 6. Manual Defragmentation
```cpp
// Force defrag of all arrays you know about (after heavy churn)
reg.defragment();
```

---

## 7. Per-Array Defrag Threshold
```cpp
// Trigger compaction sooner for frequently destroyed transient entities
reg.setDefragmentThreshold<Velocity>(0.10f); // 10% dead triggers
```

---

## 8. Thread-Safe Variant
```cpp
ecss::Registry<true> tsReg; // uses shared/unique locks + pins
auto e = tsReg.takeEntity();
tsReg.addComponent<Position>(e, {0,0});

// Reader thread
auto reader = std::jthread([&]{
    for (int i = 0; i < 1000; ++i) {
        for (auto [id, p] : tsReg.view<Position>()) {
            if (p) (void)p->x; // read only
        }
    }
});

// Writer thread
auto writer = std::jthread([&]{
    for (int i = 0; i < 100; ++i) {
        auto w = tsReg.takeEntity();
        tsReg.addComponent<Position>(w, {float(i),0});
        if (i % 10 == 0) tsReg.update();
    }
});
```

---

## 9. Trivial vs Non-Trivial Components
Prefer trivial types for fast raw moves during defrag / insertion.
```cpp
struct TrivialTag { int value; };          // trivially movable
struct Heavy { std::string name; };        // non-trivial (move invokes code)

reg.addComponent<TrivialTag>(reg.takeEntity(), {1});
reg.addComponent<Heavy>(reg.takeEntity(), {"npc_01"});
```
If a `SectorsArray` contains only trivial types, random insertion / compaction uses `memmove` for best speed.

---

## 10. Recycling Entity IDs
```cpp
std::vector<ecss::EntityId> temp;
for (int i = 0; i < 50; ++i) temp.push_back(reg.takeEntity());
for (auto id : temp) reg.destroyEntity(id);
reg.update(); // ids become recyclable
auto reused = reg.takeEntity(); // may reuse a prior id
```

---

## 11. Conditional Component Addition
Add a component later without disturbing grouped arrays of other types.
```cpp
auto eLate = reg.takeEntity();
reg.addComponent<Position>(eLate, {0,0});
// Later decision:
if (/* needs velocity */) reg.addComponent<Velocity>(eLate, {0.5f, 0});
```
Only the `Velocity` array changes; existing `Position` storage untouched.

---

## 12. Minimal System Dispatch Pattern
```cpp
void integrate(ecss::Registry<false>& r, float dt) {
    for (auto [id, p, v] : r.view<Position, Velocity>()) {
        if (p && v) { p->x += v->dx * dt; p->y += v->dy * dt; }
    }
}
```

---

## 13. Custom Component Grouping Strategy
Group only the hot pairs; leave rarely co-accessed types ungrouped.
```cpp
reg.registerArray<Position, Velocity>();      // hot pair
// PhysicsMass kept separate (queried sparsely)
reg.registerArray<PhysicsMass>();
```
This avoids creating archetype explosions while still gaining locality where it matters.

---

## 14. Simple Health Cleanup Pass
```cpp
for (auto [id, h] : reg.view<Health>()) {
    if (h && h->hp <= 0) reg.destroyEntity(id);
}
reg.update(); // finalize removals
```

---

## 15. Partial Component Access in a View
```cpp
for (auto [id, p, v, h] : reg.view<Position, Velocity, Health>()) {
    // You may ignore unused projected pointers safely
    if (p) {/* only need position this frame */}
}
```

---

## 16. Manual Opportunistic Defrag After Burst
```cpp
// After large spawn / destroy cycle:
reg.update(); // will internally decide
// Force anyway for a specific hot array:
reg.defragment<Position>(); // if such helper exists; else keep array pointer & call
```

---

## 17. Range-Constrained System (Chunked Work)
```cpp
auto all = reg.entityRanges(); // assume helper returning full range set
// Process in windowed slices (pseudo)
for (auto window : partition(all, 4096)) { // user-defined helper
    for (auto [id, pos] : reg.view<Position>(window)) {
        if (pos) {/* ... */}
    }
}
```

---

## 18. Checking for Component Presence Cheaply
```cpp
if (reg.hasComponent<Velocity>(someEntity)) {
    // safe to assume view<Velocity> would yield pointer
}
```

---

## 19. Adding Many Entities (Bulk)
```cpp
reg.reserve<Position>(100'000);
for (int i = 0; i < 100'000; ++i) {
    auto e = reg.takeEntity();
    reg.addComponent<Position>(e, {float(i), 0});
}
```
---

## 20. Simple Debug Print of Alive Positions
```cpp
for (auto [id, p] : reg.view<Position>()) {
    if (p) std::cout << id << ": (" << p->x << "," << p->y << ")\n";
}
```

---

These patterns can be combined. Favor:
- Trivial component types for fastest structural edits.
- Group only high‑coherence sets.
- Call `update()` once per frame (or per simulation tick) to amortize cleanup.
- Study tests + StelForge for deeper, real integration scenarios.
