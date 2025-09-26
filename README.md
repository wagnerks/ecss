# ecss – Lightweight high‑speed C++ Entity Component System (sector / chunk based)

[![CI (build & tests)](https://github.com/wagnerks/ecss/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/wagnerks/ecss/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![docs](https://img.shields.io/badge/docs-mkDocs-blue.svg)](https://wagnerks.github.io/ecss/) 

`ecss` is a minimal, performance‑oriented ECS. One (or several explicitly grouped) component types for an entity are stored contiguously inside a fixed layout memory block called a **sector**:
```
[SectorHeader(id|mask) | CompA | CompB | ...]
```
Sectors live inside chunked storage that grows by powers of two. Offsets are computed at compile time (no RTTI / string hashing in hot loops) and the core stays small (a few headers, no code generation, no macro DSL).
  
[What is ECS?](https://github.com/SanderMertens/ecs-faq?tab=readme-ov-file#what-is-ecs)  

## Projects using ECSS

[![Stellar Forge Engine](https://img.shields.io/badge/Stellar%20Forge%20Engine-Repository-blue?logo=github)](https://github.com/wagnerks/StelForge)  
  ECSS was originally developed inside the Stellar Forge Engine as its core ECS solution.  
  After proving its performance and flexibility in a real engine, ECSS was extracted into a standalone library.

## 🔑 Core Characteristics
- Sector (chunk) storage: tightly packed, deterministic offsets, low fragmentation
- Explicit grouping: opt‑in only (avoid archetype explosion); unrelated types live in their own arrays
- O(1) id → sector* lookup (direct pointer map, lock‑free snapshot view in TS build)
- Optional thread safety: `Registry<true>` adds shared/unique locks + per‑sector pin counters
- Deferred erase + opportunistic / explicit defragmentation (alive runs compacted left)
- Dense reflection: lightweight type id per registry (no global state)
- Header‑only style integration, no external deps (C++20/23 stdlib only)

## 🧠 Design Principles
- Branch‑lean inner loops (mask test + pointer arithmetic only)
- No virtual dispatch in hot iteration
- Deterministic layout => stable perf tuning / profiling
- Costs are per `SectorsArray`, never across whole registry
- Favor trivial POD components (enables raw `memmove` for insertion / defrag)

## 🧱 Memory Model (Conceptual)
```
[Chunk]
  ├─ sector N   : [Hdr | CompA | CompB]
  ├─ sector N+1 : [Hdr | CompA | CompB]
  └─ ... (power‑of‑two growth)
```
- Header: id + 32‑bit liveness bit mask (≤ 32 grouped types)
- Group only hot, co‑accessed sets (e.g. Position + Velocity)
- Random middle insert shifts are local to one array; trivial components => fast `memmove`

## 🧵 Threading (when `Registry<true>`)
| Operation | Mechanism |
|-----------|-----------|
| Read / iterate | Shared lock (short) + optional pin
| Insert / erase / defrag | Unique lock + wait on pins for affected ids
| Id → sector lookup | Atomic snapshot pointer map (lock‑free read)
| Reclamation | Retire old maps after last reader (deferred)

Pins block relocation only for sectors in use; opportunistic defrag aborts instantly if any active pin conflicts.

## 🧹 Defragmentation
1. Erase marks liveness bits dead and increments per‑array fragmentation counter
2. Heuristic: `(dead / size) > threshold` → compaction candidate
3. Compaction moves alive runs left; updates id→ptr map only for moved sectors (O(moved))
4. Trivial sectors copy via raw bytes; non‑trivial invoke move/dtor from layout function table
5. Optional manual trigger per array or global `registry.defragment()`

## 🔍 Iteration Forms
- Linear: `for (auto s : *array)` (may include dead bits)
- Alive filtered: `beginAlive<T>()`
- Ranged: `beginRanged(ranges)` on sparse id windows
- Views: `reg.view<Main, Others...>()` (alive Main + projected grouped / foreign components)
- Ranged View: `reg.view<Main, ...>(Ranges)`

## ✅ Why Not Full Archetypes?
Traditional archetypes relocate entities whenever composition changes & explode with many combos. `ecss` lets you group *only* where locality wins; adding an unrelated component touches only its dedicated array (no reshuffle of existing grouped data).

## ⚡ Representative Costs (intent)
| Operation | Cost |
|-----------|------|
| Id → sector* | O(1)
| Append insert | Amortized O(1)
| Random insert (trivial types) | O(shift) raw `memmove` (local array only)
| Defrag scan | O(sectors) early‑out per dead run
| View iteration | O(alive visited) (mask + projections)

## 🚀 Quick Start
```cpp
#include <ecss/Registry.h>
struct Position { float x,y; }; struct Velocity { float dx,dy; };
using Reg = ecss::Registry<true>; // thread-safe
int main(){
    Reg reg; auto e = reg.takeEntity();
    reg.addComponent<Position>(e, {0,0});
    reg.addComponent<Velocity>(e, {1,0});
    reg.registerArray<Position, Velocity>(); // optional grouping (do before mass add ideally)
    for (auto [id, p, v] : reg.view<Position, Velocity>()) {
        if (p && v) p->x += v->dx;
    }
    reg.update(); // process deferred erases & maybe defrag
}
```

### Deferred Erase & Maintenance
```cpp
reg.destroyEntity(e);  // marks dead; memory reclaimed later
reg.update();          // drains + optional compaction
```

### Ranged View
```cpp
ecss::Ranges<ecss::EntityId> slice({ {100,200}, {400,450} });
for (auto [id, p] : reg.view<Position>(slice)) { /* ... */ }
```

### Per-Type Defrag Threshold
```cpp
reg.setDefragmentThreshold<Position>(0.15f); // 15% dead triggers
```

## 🛠 Installation
CMake (FetchContent):
```cmake
include(FetchContent)
FetchContent_Declare(
  ecss
  GIT_REPOSITORY https://github.com/wagnerks/ecss.git
  GIT_TAG v0.1.0 # or latest release
)
FetchContent_MakeAvailable(ecss)

target_link_libraries(my_app PRIVATE ecss)
```
Submodule:
```bash
git submodule add https://github.com/wagnerks/ecss external/ecss
```
```cmake
add_subdirectory(external/ecss)
target_link_libraries(my_app PRIVATE ecss)
```
Manual: copy headers & `target_include_directories()`.

## 🧪 Benchmarks
Live dashboard: https://wagnerks.github.io/ecss_benchmarks  (compares iteration & structural ops against other ECS libraries under large entity counts).

## 🔧 Tips
- Keep grouped sets small & hot (avoid cold heavy data in same sector)
- Make components trivially movable when possible
- Call `update()` once per frame (TS build)
- Use ranged views for sparse workloads to reduce cache waste
- Tune per‑array defrag thresholds rather than forcing global compaction

## 🆚 Compared To entt / flecs (conceptual)
| Aspect | ecss |
|--------|------|
| Grouping | Explicit, opt‑in micro groups (sectors) |
| Moves on add/remove | Only arrays of affected types |
| Hot iteration | Straight line, no variant dispatch |
| Random insert | Local to one array; trivial => memmove |
| Thread safety | Shared/unique + per‑sector pins |
| Reflection | Dense ids per registry (no RTTI) |

## 📁 Key Files
| File | Purpose |
|------|---------|
| `Sector.h` | Trivial sector header + type‑erased member ops |
| `SectorLayoutMeta.h` | Compile‑time layout (offsets, masks, function table) |
| `SectorsArray.h` | Sparse+linear container (insert/erase/defrag/iterators) |
| `PinCounters.h` | Per‑sector pin tracking & wait primitives |
| `Registry.h` | Entities, views, maintenance, grouping API |
| `Ranges.h` | Half‑open range set for id windows |

## 📚 Documentation
- Overview / Architecture: `docs/architecture.md`
- Examples: `docs/examples.md` (more patterns; also see tests + https://github.com/wagnerks/StelForge )
- FAQ: `docs/faq.md`
- API Reference: `docs/api_reference.md`

## ⚖️ License
MIT – see `LICENSE`.

## 📌 Status
Active. Core layout & iteration model stable; expect additive utilities (instrumentation, bulk helpers) without heavy churn. Feedback & performance traces welcome.
