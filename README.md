# ecss – Lightweight high‑speed C++ Entity Component System (sector / chunk based)

[![CI](https://github.com/wagnerks/ecss/actions/workflows/ci.yml/badge.svg)](https://github.com/wagnerks/ecss/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)

`ecss` is a minimal, performance‑oriented ECS that groups one or more component types for an entity into a single fixed layout memory block called a *sector*.  Storage is chunked, trivially indexable, and optionally thread‑safe.  All core logic lives in a handful of header files – no code generation, no heavy RTTI, no macro DSL.

## Goals
- Lightweight: single header modules, no external dependencies beyond the STL / `<atomic>` / `<thread>`.
- High speed: branch‑lean inner loops, cache friendly tightly packed sectors, O(1) id → sector* lookup.
- Deterministic layouts: compile‑time offset computation (`Types.h` + `SectorLayoutMeta.h`).
- Optional concurrency: per‑container shared/unique locking + fine grained pin counters (no global world lock).
- Low fragmentation: chunk allocator + opportunistic / explicit defragmentation.
- Safe structural mutation under readers: relocation waits on per‑sector pins (`PinCounters.h`).

## Core building blocks
| File | Responsibility (summary) |
|------|--------------------------|
| `Types.h` | Fundamental typedefs (`SectorId`, `ECSType`), compile‑time uniqueness checks & offset helper. |
| `SectorLayoutMeta.h` | Per grouped component layout metadata (offsets, masks, function tables). |
| `Sector.h` | Trivial POD header (id + liveness bitfield) + type‑erased member construct / move / destroy helpers. |
| `ChunksAllocator.h` | Chunked, power‑of‑two capacity growth, stable intra‑chunk addresses, cursor & ranged cursors. |
| `RetireAllocator.h` | Deferred buffer reclamation (ABA avoidance for lock‑free read snapshots of pointer maps). |
| `SectorsArray.h` | Sparse (id→ptr) + dense (linear) sector container: insert/emplace, erase, async erase, defrag, iterators. |
| `PinCounters.h` | Hierarchical bit mask + per‑sector ref counts; waits & opportunistic movement checks. |
| `Ranges.h` | Compact half‑open range set (merge, search, take, insert, erase) for entity id management & ranged views. |
| `Reflection.h` | Per‑registry light type id assignment (dense `ECSType` values). |
| `Registry.h` | High‑level orchestration: entities, component arrays, views, bulk ops, maintenance. |

## Memory model
```
[Chunk]
  ├─ sector 0: [Sector header | CompA | CompB | ...]
  ├─ sector 1: [Sector header | CompA | CompB | ...]
  └─ ... up to chunk capacity (power‑of‑two)
```
- Sector header (id + 32‑bit liveness mask) precedes packed component payloads.
- Offsets resolved at compile time (no per‑entity indirection table).
- Grouping multiple components into the same sector eliminates pointer chasing for hot sets.

## Threading model (when `ThreadSafe == true`)
- Read access: shared lock on a `SectorsArray` (cheap, short duration).
- Structural write (insert / erase / defrag): unique lock + waits on `PinCounters` for impacted ids.
- Lock‑free lookup path: atomic snapshot (`SectorsMap` view) for id→sector* reads (no iteration guarantees).
- Memory reclamation: retired pointer maps freed only when no readers remain (`RetireBin`).

## Pin counters & safety
- `pinSector()` increments per‑sector ref count; destruction / relocation waits for counters to reach zero.
- Opportunistic defragmentation early‑outs if any pin is active (constant time aggregate check).
- Precise waiting (`waitUntilChangeable(id)`) for targeted sector range or full array.

## Iteration forms
- Linear (all sectors): `for (auto s : *array)`.
- Alive filtered: `beginAlive<T>()` – skips sectors where `T` dead.
- Ranged (sparse index windows): `beginRanged(ranges)`.
- Views (`Registry::view<Main, Others...>()`): alive sectors of `Main` + fast projection of grouped or foreign components.

## Defragmentation
- Deferred erase marks holes and increments a fragmentation counter.
- Ratio heuristic (`defragmentationSize / size > threshold`) triggers compaction.
- Compaction moves only alive runs left; updates id→ptr map in O(moved) time.
- Opportunistic variant aborts if any alive pinned sector detected.

## Performance characteristics (representative intent)
| Operation | Cost (amortized / typical) |
|-----------|----------------------------|
| Id→sector* lookup | O(1) (direct index) |
| Insert (append id) | O(1) |
| Insert (middle id) | O(N) worst (shift) but rare if ids monotonic |
| Pin / Unpin | O(1) (atomic + optional hierarchy update) |
| Defragment scan | O(S) where S = sectors (skips trivial sectors fast) |
| Ranged iteration | O(runs + visited) |

## Real-world numbers
- **100M iterations in 156 ms** on an Intel i9-14900HX (Release, C++20, clang-cl).  
- Scales linearly with entity count and remains cache-friendly due to tight sector packing.

## Installation

- **Requirements**: C++20 compiler (tested with MSVC 19.44 / clang 21 / GCC 14).  
- **Dependencies**: none (only standard C++ library).  

### Using CMake (FetchContent)
```cmake
include(FetchContent)
FetchContent_Declare(
  ecss
  GIT_REPOSITORY https://github.com/wagnerks/ecss.git
  GIT_TAG v0.1.0 # use the latest release
)
FetchContent_MakeAvailable(ecss)

target_link_libraries(my_app PRIVATE ecss)
```
### As a Git submodule
```bash
git submodule add https://github.com/wagnerks/ecss.git external/ecss
```
Then add to your CMakeLists.txt:

```cmake
add_subdirectory(external/ecss)
target_link_libraries(my_app PRIVATE ecss)
```

### Manual include
Just copy the ecss/ headers into your project and add the folder to your include path:

```cmake
target_include_directories(my_app PRIVATE path/to/ecss)
```

## Quick start

```cpp
#include <ecss/Registry.h>

struct Transform { float x,y,z; };
struct Velocity  { float vx,vy,vz; };

using Reg = ecss::Registry<true>; // thread-safe

int main(){
    Reg reg;
    auto e = reg.takeEntity();
    reg.addComponent<Transform>(e, {0,0,0});
    reg.addComponent<Velocity >(e, {1,0,0});

    for (auto [id, t, v] : reg.view<Transform, Velocity>()) {
        if (t && v) { t->x += v->vx; }
    }

    reg.update(); // process deferred erases / optional defrag
}
```

## Grouped sectors
```cpp
reg.registerArray<Transform, Velocity>(); // same sector, single liveness mask update
```
Grouping hot components improves spatial locality – both pointers computed from one base address.

## Deferred erase & maintenance
```cpp
reg.destroyEntity(e);      // marks sector members dead
reg.update();              // later: processes queued erasures + compaction heuristic
```

## Ranged view
```cpp
ecss::Ranges<ecss::EntityId> subset({ {100, 200}, {400, 450} });
for (auto [id, t] : reg.view<Transform>(subset)) { /* ... */ }
```

## Why not archetypes?
Sectors act like a fixed micro‑archetype of explicitly grouped types – no dynamic hash / relocation when an entity gains a new unrelated component; simply place that component in its own (possibly separate) `SectorsArray`.  You opt into grouping only where it wins.

## Reflection & type ids
`ReflectionHelper` assigns a dense `ECSType` per component type in a registry instance. Used for array mapping without RTTI or string hashing.

## Lightweight design choices
- No virtual dispatch in hot loops.
- Trivial `Sector` enables raw `memmove` for defrag when all members trivial.
- Per file responsibilities are sharply delimited (see table above).
- All algorithms favor straight‑line predictable code paths.

## When to defragment
Call `update()` each frame (TS build) – internal heuristic decides; or force manually:
```cpp
for (auto* arr : /* your stored arrays */) arr->defragment();
```
Tune per type:
```cpp
reg.setDefragmentThreshold<Transform>(0.15f);
```

## Error handling philosophy
Assertions catch misuse in debug (invalid ids, duplicate grouping, layout mismatches). Release builds assume validated usage for speed.

## Integration
Add as a submodule or vendor the headers; include path root at `ecss/`. Only C++20 standard library required.

## License
MIT – see `LICENSE`.

## Status
Actively evolving; API is intentionally small & stable.  Feedback and performance traces are welcome.
