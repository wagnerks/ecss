# ❓ FAQ

### Q: What is the core idea behind ecss?
A: A minimal sector (chunk) based ECS: each entity's grouped components live contiguously in a fixed layout "sector" to maximize cache locality and keep iteration branch‑lean, while leaving unrelated components in their own arrays.

---

### Q: Why not a full archetype system?
A: Archetypes implicitly explode with many unique component combinations and force entity moves on composition changes. Here you explicitly opt into grouping only hot sets; adding an unrelated component never relocates existing grouped data.

---

### Q: When should I group components?
A: Group only components frequently accessed together in the same tight loop (e.g., `Position + Velocity`). Avoid grouping large, rarely touched, or write‑heavy types with small hot ones to prevent cache pollution.

---

### Q: What happens if I group too many types?
A: Larger sectors increase memory touched per iteration and slow moves/defrag. Keep groups lean—prefer multiple small arrays to one bloated layout.

---

### Q: Is it thread‑safe?
A: Yes when using `Registry<true>`. Reads take shared locks; structural writes take a unique lock and wait on pin counters for only the impacted sectors.

---

### Q: Do I need to pin manually?
A: Typical high‑level iteration APIs handle safety implicitly. Manual pinning is only needed for custom low‑level pointer retention across potential defrag / erase windows.

---

### Q: How expensive is a view iteration?
A: Very low: liveness mask bit test + pointer arithmetic; foreign components fetched by O(1) id→sector map lookup. No dynamic dispatch or variant visitation.

---

### Q: How are component type ids generated?
A: A lightweight reflection helper assigns a dense `ECSType` per component type per registry instance (no RTTI / strings in hot loops).

---

### Q: What about random (non‑tail) insert performance?
A: Local to the affected `SectorsArray`. If all grouped component types are trivial, shifts / defrag become raw `memmove`. Keep grouped components trivially movable for best random insert and compaction speed.

---

### Q: How do I remove entities?
A: Call `destroyEntity(id)` (marks dead) then `update()` later to reclaim and maybe defragment. The delay amortizes structural costs.

---

### Q: How often should I call `update()`?
A: Typically once per frame or simulation tick. It processes deferred erases and may trigger opportunistic compaction based on per‑array thresholds.

---

### Q: Can I force defragmentation?
A: Yes—call the defrag method on a specific array (or use a helper if exposed). You can also adjust the fragmentation threshold per component/grouped set.

---

### Q: Memory overhead per entity?
A: One sector header (id + liveness mask) plus tightly packed component payloads in grouped arrays. Unused grouped components = no padding inside each sector beyond natural alignment.

---

### Q: Does ecss support multiple worlds?
A: Yes—each `Registry` instance is independent with its own type id mapping and component arrays.

---

### Q: How does it compare to entt / flecs?
A: Benchmarks (see the dashboard) show faster or competitive iteration for grouped hot sets and predictable structural cost since only targeted arrays mutate. Trade‑off: fewer high‑level utilities out of the box.

---

### Q: What's the recommended component design?
A: Prefer small POD / trivially movable structs. Put large blobs (e.g., std::vector heavy data) behind handles or separate arrays to avoid slowing relocation and polluting cache lines for hot loops.

---

### Q: What if a component is non‑trivial?
A: Moves during compaction call its move constructor / destructor via a small function table. Works, but heavier. Keep such types ungrouped unless locality gain outweighs cost.

---

### Q: Are there global singletons or hidden systems?
A: No. The library stays explicit: you manage registries, choose grouping, and drive maintenance.

---

### Q: License & usage?
A: MIT. Free for commercial and open‑source projects.

---

### Q: Stability / maturity?
A: Actively evolving; core layout & iteration model are stable, APIs intentionally small. Expect additive utilities rather than disruptive rewrites.

---

### Q: Where can I see more real usage?
A: Browse the test suite in this repository and my pet project using ecss: https://github.com/wagnerks/StelForge . Both contain practical integration patterns.

---

### Q: How do I contribute?
A: Open an issue / PR with focused changes (performance trace, bug reproduction, or documentation improvements). Keep additions minimal and justified by measurable wins.

---

If a question is missing, open an issue or extend this file with a concise Q/A entry.
