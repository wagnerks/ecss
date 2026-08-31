---
hide:
  - navigation
---

# Architecture Overview

`ECSS` centers on a minimal set of cooperating header‑only primitives that implement a sector (chunk) based ECS with deterministic layout, optional component grouping, and explicit maintenance (update / defrag). This document drills into the main subsystems and their interaction.

---

## 1. Core Concepts
- **EntityId**: Dense integral id (recycled). Maps in O(1) to a sector pointer through a sparse+direct table.
- **Component**: Plain struct. No inheritance or virtual cost expected. Users may group selected types.
- **Sector**: Fixed layout memory block: `[SectorHeader | CompA | CompB | ...]` for one entity across a specific grouped set.
- **SectorsArray<Ts...>**: Container that owns sectors of the grouped component set `Ts...` (possibly a single `T`). Handles allocation, iteration, erase, defrag, pin checks.
- **Registry<ThreadSafe>**: Orchestrates multiple `SectorsArray` instances, reflection (dense type ids), entity lifecycle, cross‑array views.
- **PinCounters**: Lightweight safety gate for relocation / destruction while readers observe sectors.
- **RetireAllocator / RetireBin**: Deferred reclamation of replaced pointer maps to avoid ABA / use‑after‑free in lock‑free read paths.

> Note: Costs (insert / erase / defrag) are always scoped per `SectorsArray`, never across the entire registry. A random insertion only shifts within the affected array.  

---

## 2. Memory Layout
```
[Chunk]
  ├─ sector 0: [Hdr | CompA | CompB]
  ├─ sector 1: [Hdr | CompA | CompB]
  ├─ ... up to chunk capacity (power‑of‑two growth)
```
- **Chunk Growth**: Capacity doubles (or power‑of‑two progression) to keep reallocation count low while preserving intra‑chunk address stability.
- **Header**: Stores entity id + 32‑bit (or similar) liveness mask (one bit per grouped type) enabling selective dead marking without tearing apart grouped data.
- **Offsets**: Computed at compile time in `SectorLayoutMeta.h`; inner loops avoid dynamic lookups or string hashing.
- **Grouping**: Only opt in where locality matters. Non‑grouped components get their own array (acts like a micro‑archetype only for that type).

---

## 3. Entity Lifecycle Flow
```
 takeEntity() -> (id pool / recycled) -> id reserved
 addComponent<T>(id):
   if array for T not present -> create/register
   place sector (or member) & mark alive bit
 destroyEntity(id):
   mark bits dead (deferred)
 update():
   free retired buffers past their grace period
   process deferred erasures (reclaim holes)
   attempt defragmentation (heuristic; skips arrays being iterated)
```
Dead members remain until maintenance to keep fast inner loops (simple mask test) and amortize compaction.

Batch forms exist for every step and are markedly cheaper than the per-entity ones:
`takeEntities`, `insertBulk` / `addComponents`, `destroyEntities`, and `CommandBuffer` for
recording changes during iteration and applying them after. See
[Batching & Deferral](batching.md).

---

## 4. Iteration Modes
- **Full linear**: range‑for over `SectorsArray` (all sectors, including dead bits masked by component presence checks).
- **Alive component iteration**: Skip sectors where target component liveness bit is 0.
- **Ranged**: Use `Ranges` to iterate a subset of entity id intervals (reduces cache pollution with sparse workloads).
- **View<Main, Others...>**: Iterate alive sectors of the main component array; project foreign components via direct id→sector lookup (O(1) each). Grouped members of the main array require only offset arithmetic.

Cost characteristics aim for branch‑lean loops: liveness mask test + (optional) pointer projection.

---

## 5. Threading & Concurrency (when `Registry<true>`) 
| Aspect | Mechanism |
|--------|-----------|
| Reads (iteration / lookup) | No lock. Seqlock-published snapshots of the sparse map, the dense arrays and the chunk table |
| Iteration | A *structural hold* on the array — "do not compact this", counted per thread rather than per sector |
| Pointer retention | A *pin* on one sector — "do not move this one" — validated against a structural epoch |
| Append | Unique lock only. Relocates nothing and names a sector that cannot be pinned yet, so no epoch and no wait |
| Relocating change (middle insert / defrag / clear / copy) | Unique lock + wait for pins and holds to drain, always outside the lock |
| In-place change (destroy / overwrite a member) | Unique lock + that one sector unpinned |
| Reclamation | Retire old buffers, freed only after a grace period no reader can still be inside |

Pins and holds provide precise blocking: only the arrays actually being restructured wait, and
only for readers of that array.

!!! warning "The waiting is per array, and it includes you"
    A thread that holds a view or a pin on an array and then makes a relocating change to *that
    array* waits for something only it could release. Debug builds assert and name the rule;
    release builds block. See [Batching & Deferral](batching.md).

---

## 6. Pin Counters
- Each sector has a small ref count + aggregate mask for quick non‑zero checks.
- Iteration optionally pins sectors (or groups) if relocation risk exists.
- Defragmentation first performs an opportunistic check; if any pin active, it may abort early to keep frame budget stable.

---

## 7. Defragmentation Strategy
1. **Deferred Erase**: Mark dead bits; fragmentation metric increments.
2. **Heuristic**: If `(dead / total) > threshold` schedule compaction.
3. **Compaction**: Pack alive runs left; update id→ptr map only for moved sectors (O(moved)).
4. **Abort Condition**: Active pins -> opportunistic attempt aborts.

Manual overrides allow explicit `defragment()` per array or threshold adjustment per component set.

---

## 8. Reflection & Type Ids
`ReflectionHelper` assigns dense incremental `ECSType` values at registration time. Usage:
- Map component types to their owning `SectorsArray`.
- Avoid string hashing / `type_index` in hot paths.
Ids come from one process-wide counter, so a given component type has the same id in every
`Registry`. That is deliberate: a lookup has to identify the type somehow, and a dense global
id reads as a constant where hashing a type token would cost. It is also what lets an array
built by one registry be handed to another.

Multiple worlds work and never collide, since an id names a type rather than a slot. What they
share is the id *space*: a registry's per-type table is sized by the highest global id it uses,
not by how many types it holds, so a world using ten types in a process that defines four
hundred still indexes a four-hundred-entry table. Two bytes per unused entry, and only in the
tables the registry actually keeps.

Making the index per registry was measured and rejected: it costs an extra dependent load on
every lookup that names a component type, about 0.3 ns, which is roughly 6% of `hasComponent`
and lands on `pinComponent`, `addComponent`, `destroyComponent`, `insertBulk` and every view
construction (once per component type in the pack). The memory it saves is real only when a
registry uses a small fraction of the process's types.

---

## 9. Error Handling Philosophy
- Debug builds: assertions (invalid id, duplicate grouping, out‑of‑bounds, conflicting layouts).
- Release builds: assume validated usage to minimize checks (fewer branches in inner loops).

---

## 10. Differences vs Traditional Archetype ECS
| Traditional Archetypes | `ECSS` Approach |
|------------------------|-----------------|
| Entity moves between full archetype tables when composition changes | Only grouped sets share storage; adding unrelated component just touches its own array |
| Potential explosion of archetype combinations | Explicit opt‑in grouping keeps combination count controlled |
| Central structural churn on component add/remove | Localized mutation (only affected arrays) |
| Complex query planner | Straightforward view: main + projected foreign arrays |

Result: predictable performance and simpler mental model for targeted locality.

---

## 11. Hot Path Anatomy (View Iteration)
Pseudocode (conceptual):
```cpp
for (Sector* s : mainArray) {
    if (!s->aliveMaskBit(MainIdx)) continue;
    auto* mainComp = s->componentPtr<Main>();
    auto* other    = lookupForeign<Other>(s->id); // O(1) direct map
    // ... user function ...
}
```
No variant visitation, no dynamic dispatch; only mask test + pointer arithmetic + optional projection.

---

## 12. Memory Safety & Relocation

### Trivial vs Non‑Trivial Type Handling
The system automatically detects component triviality at compile time via `SectorLayoutMeta::isTrivial()`:

| Operation | Trivial Types | Non‑Trivial Types |
|-----------|--------------|-------------------|
| Defragmentation | `memmove` (batch) | Per‑element move ctor + dtor |
| Shift (insert middle) | `memmove` (batch) | Per‑element move (reverse order for right‑shift) |
| Copy array | `memcpy` (batch) | Per‑element copy ctor |
| Move array | Pointer swap | Pointer swap (same) |
| Erase | Mark dead bit | Mark dead + dtor call |

### Implementation Details
- `SectorLayoutMeta` stores a `trivial` flag computed from `std::is_trivially_copyable` for all grouped types.
- When `isTrivial() == true`: `ChunksAllocator::moveSectorsDataTrivial()` uses raw `memmove`.
- When `isTrivial() == false`: `Sector::moveSectorData()` invokes move constructors, properly destructs source, and placement‑news into destination.
- Shift operations iterate in correct order (backwards for right‑shift) to avoid overwriting source before move.

### Pin Safety
- Pins ensure no reader references stale addresses during relocation.
- Defragmentation aborts opportunistically if any pinned sector blocks movement.

> **Recommendation**: Keep components trivially movable/destructible whenever possible. If **all** grouped members in a `SectorsArray` are trivial, random (non‑tail) insertions and defragment moves degrade to `memmove` of contiguous bytes, greatly improving worst‑case cost. This advantage is per `SectorsArray` — having trivial components in one array does not affect others.

> **Non‑trivial types are fully supported**: `std::string`, `std::vector`, custom RAII types all work correctly. The system properly invokes constructors/destructors during all structural operations (copy, move, defragment, shift). Performance is lower than trivial types but correctness is guaranteed.

---

## 13. Configuration Points
- `ThreadSafe` template parameter on `Registry`.
- Per grouped set defrag threshold setter.
- Optional explicit grouping via `registerArray<A,B,...>()`.
- `update()` cadence (e.g. once per frame) to amortize maintenance. Placement is free: nothing
  in it waits, so it may be called from anywhere and more than once.
- `setAutoMaintenance(bool)` to let view creation do that work instead, removing the call.
- Retire grace period per array (`setGracePeriod`), fixed at zero in the non-thread-safe build
  where there are no lock-free readers to outlive.

---

## 14. Performance Intent (Qualitative)
- O(1) id→sector lookup.
- Append / tail insert amortized O(1).
- Random middle insert cost limited to shifting within the single affected `SectorsArray` (faster when trivial: raw `memmove`).
- Defrag proportional to moved sectors, early abort to keep worst‑case jitter low.

---

## 15. Extensibility Notes
- Additional component metadata (e.g., custom allocators, debug instrumentation) can wrap arrays without altering iteration semantics.
- Future systems (e.g., scheduling) can pin sector ranges for deterministic SOA transformations.

---

## 16. Summary
`ECSS` trades universal automatic archetype re‑composition for explicit, controllable grouping and deterministic low‑overhead memory management. The architecture emphasizes:
- Small surface area
- Predictable cache behavior
- Optional concurrency
- Cheap structural mutation isolated to the minimum necessary storage
- Improved random insertion & relocation speed when arrays use only trivial components

Refer back to the index or examples for practical usage patterns.
