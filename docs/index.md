# ECSS — Lightweight high‑speed C++ Entity Component System

<p align="center">
  <img src="logo.svg" alt="ECSS logo" width="150" style="vertical-align: middle;"/>
</p>

`ECSS` is a minimal, performance‑oriented ECS for modern C++ that groups one or more component types for an entity into a single fixed layout memory block called a *sector*. Layouts are deterministic, iteration is cache‑friendly, structural mutation can be made thread‑safe, and the core stays small (a handful of headers, no codegen, no heavy RTTI).

---

## Core Characteristics
- Sector (chunk) based storage: tightly packed, predictable offsets, low fragmentation
- Optional grouping of hot components into one sector (no pointer chasing between them)
- O(1) entity id ➜ sector* lookup (sparse+direct mapping)
- Optional thread safety (`Registry<true>`) with shared / unique locks + pin counters
- Deferred erase + opportunistic / explicit defragmentation
- Reflection helper assigns dense type ids (no strings / no RTTI in hot paths)
- Header‑only style integration, no external dependencies (C++20/23 standard library only)

---

## Memory Model (Conceptual)
```
[Chunk]
  ├─ sector N: [SectorHeader | CompA | CompB | ...]
  ├─ sector N+1: [SectorHeader | CompA | CompB | ...]
  └─ ... (power‑of‑two capacity growth)
```
- `SectorHeader` holds id + liveness bit mask.
- Component payloads packed immediately after header (compile‑time offsets).
- Group only where locality wins; unrelated components can live in separate arrays.

---

## Threading & Safety (when enabled)
- Readers take cheap shared locks.
- Writers (insert / erase / defrag) take unique lock + wait on pin counters of impacted sectors.
- Pin counters prevent relocation while a sector is observed.
- Retired pointer maps reclaimed only after last reader (deferred reclamation pattern).

---

## Defragmentation
- Erase marks holes; fragmentation ratio tracked.
- Heuristic or manual trigger compacts alive sectors left.
- Opportunistic mode aborts instantly if any pinned (alive) sector blocks movement.

---

## Iteration Modes
- Linear over all sectors
- Alive‑filtered (skip dead in mask)
- Ranged (subset windows of entity ids)
- Views combining main + foreign components (`reg.view<Main, Others...>()`)

---

## Design Principles
- Straight‑line branch‑lean inner loops
- No virtual dispatch in hot iteration
- Deterministic layouts for reproducible performance tuning
- Minimal API surface; explicit operations (no hidden archetype shuffles)

---

## Quick Start
See: [Getting Started](getting_started.md)

---

## When To Use It
Choose `ECSS` if you want:
- Manual control over which components are co‑located
- High iteration speed on hot component sets
- Predictable memory & defrag you can reason about
- An ECS without code generation or large framework weight

---

## Links & Further Docs
- Architecture: [architecture.md](architecture.md)
- Examples: [examples.md](examples.md)
- Benchmarks: https://wagnerks.github.io/ecss_benchmarks
- FAQ: [faq.md](faq.md)
- Repository: https://github.com/wagnerks/ecss

---

MIT Licensed. Active development; API intentionally small & stable.
