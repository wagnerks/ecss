---
hide:
  - navigation
---

# ECSS — Lightweight high‑speed C++ Entity Component System

<p align="center">
  <img src="img/logo.svg" alt="ECSS logo" width="150" style="vertical-align: middle;"/>
</p>

`ECSS` is a minimal, performance‑oriented [ECS](https://github.com/SanderMertens/ecs-faq?tab=readme-ov-file#what-is-ecs) for modern C++ that groups one or more component types for an entity into a single fixed layout memory block called a *sector*. Layouts are deterministic, iteration is cache‑friendly, structural mutation can be made thread‑safe, and the core stays small (a handful of headers, no codegen, no heavy RTTI).

---

## Core Characteristics
- Sector (chunk) based storage: tightly packed, predictable offsets, low fragmentation
- Optional grouping of hot components into one sector (no pointer chasing between them)
- O(1) entity id ➜ sector* lookup (sparse+direct mapping)
- Optional thread safety (`Registry<true>`): lock-free reads, locks only for structural writes
- Batch and deferred structural changes (`insertBulk`, `takeEntities`, `CommandBuffer`)
- Deferred erase + opportunistic / explicit defragmentation, from anywhere in the frame
- Full support for non‑trivial types (`std::string`, `std::vector`, RAII) with proper move semantics
- Reflection helper assigns dense type ids (no strings / no RTTI in hot paths)
- Header‑only style integration, no external dependencies (C++20/23 standard library only)
- Comprehensive test suite (550+ tests covering threading, non‑trivial types, edge cases)

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
- **Trivial types**: relocated via fast `memmove` during defrag/shifts.
- **Non‑trivial types**: proper move constructors / destructors invoked automatically.

---

## Threading & Safety (when enabled)
- Reads take no lock at all: lookups and iteration go through seqlock-published snapshots.
  Reading costs the same as in the non-thread-safe build, and scales — 6.8x on eight threads.
- Writers (insert / erase / defrag) take a unique lock; only relocation waits on pin counters,
  and appends do not wait at all.
- Pin counters prevent relocation while a sector is observed; a view holds the array's shape
  rather than any one sector.
- Retired buffers are reclaimed only after the last reader could have left them (deferred
  reclamation), so a snapshot stays readable across a resize.
- Structural changes made one call at a time are the only ones that cost noticeably more than
  the plain build — see [Batching & Deferral](batching.md).
- The shape of an array is guaranteed; a component's *value* is not, since holding it would
  need a lock or a pin per element. `access<Read<T>, Write<U>>()` claims types a system at a
  time, and `setAccessTracking(true)` finds the overlaps you missed, free in release.

---

## Defragmentation
- Erase marks holes; fragmentation ratio tracked.
- Heuristic or manual trigger compacts alive sectors left.
- In `Registry<true>`, `update()` attempts compaction rather than waiting for it, so it is safe
  to call from anywhere in the frame — including from inside iteration — and costs ~3.5 ns when
  idle. `Registry<false>` has no holds to notice an open view and compacts outright, so call it
  between passes.
- `setAutoMaintenance(true)` hands the erase and compaction pass to view creation — including a
  rotation slot so arrays that are never iterated are still reached. Freeing retired memory
  stays in `update()`, so the call does not go away.

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
- [Architecture](architecture.md)
- [Examples](examples.md)
- [Batching & Deferral](batching.md)
- [Benchmarks](https://wagnerks.github.io/ecss_benchmarks)
- [FAQ](faq.md)
- [API Reference](ecss/annotated.md)
- [Repository](https://github.com/wagnerks/ecss)

---

MIT Licensed. Active development; API intentionally small & stable.
