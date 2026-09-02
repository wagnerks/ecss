---
hide:
  - navigation
---

# FAQ

### Q: What is the core idea behind ECSS?
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
A: Yes when using `Registry<true>`. Reads take **no lock**: lookups and iteration go through
seqlock-published snapshots, so `hasComponent` and `each` cost the same as in the plain build
and scale with threads (6.8x on eight). Structural writes take a unique lock; only the ones
that relocate sectors wait on pin counters, and appends do not wait at all.

---

### Q: Do I need to pin manually?
A: Typical high‑level iteration APIs handle safety implicitly. Manual pinning is only needed for custom low‑level pointer retention across potential defrag / erase windows.

---

### Q: Is a component's *value* safe to read while another thread writes it?
A: No, and that is deliberate. The container guarantees an array's *shape*: it will not be
relocated under an iterator, and a pinned sector will not move or die. Guarding a value would
mean a lock or a pin per element -- 27 ns against 0.5 for an iteration step.

It is also a scheduling bug before it is a memory one: a system reading Position while another
writes it sees a mix of old and new, so the frame's answer depends on which thread won.

Three ways to deal with it, from cheapest to most explicit:

- arrange systems not to overlap on a type, which is what a scheduler would do for you;
- `reg.access<Read<Position>, Write<Velocity>>()` around a system: a reader-writer lock per
  component type, 29.5 ns for one, taken once per system rather than per element;
- `reg.pinComponent<T>(entity)` for a single value you need to hold still.

And `reg.setAccessTracking(true)` in development finds the places you missed: the first overlap
aborts and names the type and both threads. It compiles out when `NDEBUG` is set.

---

### Q: What does thread safety actually cost me?
A: Reading, essentially nothing — 1.0x to 1.3x. Batched structural changes, 1.0x to 1.4x.
The one expensive case is structural changes made one call at a time: `addComponent` is 4.8x
and `destroyEntity` 4.0x. Use the batch entry points or a `CommandBuffer` and the gap closes
to about 1.1x. See [Batching & Deferral](batching.md).

---

### Q: Can I add or remove components while iterating?
A: Not on the array you are iterating, on the same thread. Relocating a sector would invalidate
the iterator reading it, so the writer waits for the iteration to end — and it is waiting for
you. Debug builds assert; release builds block.

Appending an id above every id stored, destroying a component in place, and anything on a
different array are all fine. So is any of it from another thread. To change the array you are
walking, record into a `CommandBuffer` and apply it after the loop.

---

### Q: How expensive is a view iteration?
A: Very low: liveness mask bit test + pointer arithmetic; foreign components fetched by O(1) id→sector map lookup. No dynamic dispatch or variant visitation.

---

### Q: How are component type ids generated?
A: A lightweight reflection helper assigns a dense `ECSType` per component type, from one
counter for the whole process (no RTTI / strings in hot loops). The same type therefore has the
same id in every `Registry`, which is what makes a lookup a plain array index.

---

### Q: What about random (non‑tail) insert performance?
A: Local to the affected `SectorsArray`, and cheap per insert only if the ids arrive ascending.
A middle insert shifts the tail and rewrites the sparse entry of every sector it passes, so
adding M components at scattered ids one at a time costs `O(M·N)` — 892 ms for 50k adds into a
200k array.

Hand the batch to `insertBulk` instead and it is sorted once and merged in a single pass: 2.6 ms
for the same work, scaling linearly. Keeping grouped components trivially movable helps on top
of that, since shifts and compaction become raw `memmove`.

---

### Q: How do I remove entities?
A: `destroyEntity(id)` marks it dead; `update()` later reclaims and may defragment. For more
than a handful, `destroyEntities(ids)` takes one lock and makes one pass per array instead of
per entity — 17 ns against 58 in the thread-safe build. Ascending ids are about 4x cheaper than
shuffled ones, and ids collected from a view already are ascending.

---

### Q: How often should I call `update()`, and where?
A: Once per frame is typical, but placement no longer matters: nothing in `update()` waits.
Deferred erases destroy components in place, and compaction is attempted rather than awaited —
an array something is iterating is skipped and picked up next call. So it is safe from anywhere,
including inside a loop over a view, and calling it more than once is harmless: with nothing to
do it costs about 3.5 ns for the whole registry.

To drop the call entirely, `setAutoMaintenance(true)` once at startup makes opening a view do
the same work — for the arrays it touches, plus one more in rotation, so a component type that
is only ever looked up and never iterated is reached too.

---

### Q: Can I force defragmentation?
A: Yes—call the defrag method on a specific array (or use a helper if exposed). You can also adjust the fragmentation threshold per component/grouped set.

---

### Q: Memory overhead per entity?
A: One sector header (id + liveness mask) plus tightly packed component payloads in grouped arrays. Unused grouped components = no padding inside each sector beyond natural alignment.

---

### Q: Does ECSS support multiple worlds?
A: Yes. Each `Registry` owns its own component arrays and entity ids, and worlds never
collide, because a type id names a type rather than a slot.

The type id *space* is shared, though: ids come from one process-wide counter. The practical
effect is that a registry's per-type table is sized by the highest global id it uses rather
than by how many types it holds, so a small world in a process that defines many types indexes
a sparse table. Two bytes per unused entry. Giving each registry its own dense index was
measured and rejected: it adds about 0.3 ns to every lookup that names a component type, which
is more than the memory is worth unless you run many small worlds at once.

---

### Q: How does it compare to entt / flecs?
A: Benchmarks (see the dashboard) show faster or competitive iteration for grouped hot sets and predictable structural cost since only targeted arrays mutate. Trade‑off: fewer high‑level utilities out of the box.

---

### Q: What's the recommended component design?
A: Prefer small POD / trivially movable structs for best performance. Non‑trivial types (`std::string`, `std::vector`, custom RAII) are fully supported but slower during structural operations. Consider:

- **Hot path components**: Keep trivial (POD) for fastest iteration and relocation.
- **Rarely accessed data**: Non‑trivial types are fine; the overhead only matters during defrag/insert.
- **Large blobs**: Put behind handles or separate arrays to avoid cache pollution.

---

### Q: What if a component is non‑trivial?
A: Fully supported. The system detects non‑trivial types at compile time and uses proper move semantics:

- **Defragmentation**: Calls move constructor for each relocated element, then destructor on source.
- **Shift operations** (insert in middle): Moves elements one‑by‑one in correct order (backwards for right‑shift to prevent overwrites).
- **Copy**: Uses copy constructor for each element.
- **Erase**: Calls destructor before marking dead.

Types like `std::string`, `std::vector`, or custom RAII classes work correctly. Performance is lower than trivial types (no batch `memmove`), but correctness is guaranteed. Keep such types ungrouped unless locality gain outweighs the cost.

Because it still works, it is easy to pay this without noticing — a base class someone added for
an unrelated reason, or a mutex member, silently costs the whole array its `memmove` paths. So
registering a non‑trivially‑copyable component emits a compiler warning naming the type:

```
warning C4996: 'ecss::detail::NonTrivialComponent<MeshComponent>': ecss: this component is not
trivially copyable, so its array gives up the raw-bytes paths ...
```

It is a warning, never an error — the type is supported, you are only told what it costs.

The same thing is also reported **once at runtime**, when the array's layout is built, because the
compile‑time half does not reach everyone: a project that pulls ecss in through
`target_precompile_headers` gets a PCH that CMake opens with `#pragma system_header`, and a
warning whose location is inside a system header never reaches the build log. Route that report
into your own log (a windowed build has nowhere to show stderr) or turn it off:

```cpp
ecss::setTrivialityReporter([](std::string_view component) { myLog("ecss: %s ...", component); });
ecss::setTrivialityReporter(nullptr);   // silence the runtime half only
```

When a component owns a `std::string` or a `std::vector` and always will, say so once and both
halves go quiet for it:

```cpp
template<> struct ecss::AllowNonTrivial<MeshComponent> : std::true_type {};
```

`-DECSS_NO_TRIVIALITY_WARNINGS` removes the whole diagnostic. MSVC reports the compile‑time half
from `/W3` (what CMake and MSBuild projects use by default); GCC and clang report it at any level.

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

### Q: How well tested is it?
A: Comprehensive test suite with 550+ tests covering:

- Basic CRUD operations and iteration
- Thread safety and concurrent access patterns
- Non‑trivial types (`std::string`, move‑only, RAII) during all operations
- Edge cases: capacity boundaries, empty arrays, rapid insert/erase cycles
- Stress tests: concurrent defragmentation, copy during reads, multi‑component parallel operations

Tests run with AddressSanitizer and ThreadSanitizer in CI to catch memory and threading bugs.

---

### Q: Where can I see more real usage?
A: Browse the test suite in this repository and my pet project using ECSS: https://github.com/wagnerks/StelForge . Both contain practical integration patterns.

---

### Q: How do I contribute?
A: Open an issue / PR with focused changes (performance trace, bug reproduction, or documentation improvements). Keep additions minimal and justified by measurable wins.

---

If a question is missing, open an issue or extend this file with a concise Q/A entry.
