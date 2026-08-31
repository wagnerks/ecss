---
hide:
  - navigation
---

# Structural Changes, Batching & Deferral

Two kinds of write happen in an ECS, and they cost very different things.

**Writing a component's value** — `pos.x += vel.dx` inside a view — goes straight through the
iterator to memory. It is free in the thread-safe build:

| | `Registry<false>` | `Registry<true>` |
|---|---|---|
| integrate through a view | 1.06 ns | 0.87 ns |
| scattered write by entity id | 5.58 ns | 5.89 ns |

**Changing an array's shape** — adding or removing a component, destroying an entity — is what
costs. It takes a lock, publishes to lock-free readers, and may move sectors. Those same
operations are also the ones you must not do while iterating the array they touch.

This page is about keeping that second kind cheap.

---

## The one rule

!!! warning "No structural change to an array you are iterating"
    While a view or a pin on an array is alive **on this thread**, that array must not be
    inserted into anywhere but the end, defragmented, cleared, copied or assigned. Relocating a
    sector would invalidate the iterator reading it, so the writer waits for the iteration to
    finish — and it is waiting for you.

    Debug builds assert and name the rule. Release builds block.

Legal while iterating, because nothing moves:

- appending an id above every id already stored;
- destroying or overwriting a component in place, other than one you hold a pin to;
- anything at all on a **different** array;
- the same operations from a **different** thread — that is what the waiting is for, and it
  resolves as soon as your view ends.

The cleanest way never to think about this is to record changes and apply them later —
see [Command buffer](#command-buffer) below.

---

## Add many components at once

Adding components one at a time costs `O(M·N)` when the ids are not ascending: every insert
that lands in the middle shifts the tail and rewrites the sparse entry of every sector it moves
past. `insertBulk` sorts the batch once and merges it in a single pass.

```cpp
std::vector<std::pair<ecss::EntityId, Velocity>> batch;
batch.reserve(targets.size());
for (auto e : targets) { batch.emplace_back(e, Velocity{1.f, 0.f}); }

reg.insertBulk<Velocity>(batch.begin(), batch.end());
```

Ids may arrive in any order and may fall anywhere in the range already stored; an id already
present is overwritten. Adding a component to a quarter of the entities, non-thread-safe build:

| entities | loop of `addComponent` | `insertBulk` |
|---|---|---|
| 50 000 | 55.6 ms | 0.6 ms |
| 100 000 | 237.1 ms | 1.3 ms |
| 200 000 | 891.8 ms | 2.6 ms |
| 400 000 | 5808.1 ms | 7.5 ms |

The loop quadruples as N doubles; the merge scales linearly. Ascending batches keep the plain
append path and are unaffected.

`addComponents<T>(generator)` does the same for a callable that yields
`{EntityId, T}` until it returns `INVALID_ID`.

---

## Allocate many ids at once

`takeEntities` walks the id bitmap once and claims a wholly free word — 64 ids — in a single
step, instead of rescanning from the allocation hint per id.

```cpp
std::vector<ecss::EntityId> ids;
reg.takeEntities(200000, ids);   // appends to ids
```

| | loop of `takeEntity` | `takeEntities` |
|---|---|---|
| `Registry<false>` | 3.07 ns | 1.12 ns |
| `Registry<true>` | 7.83 ns | 1.29 ns |

Nothing about the allocation policy changes: ids still come from the lowest free word first,
so the high watermark stays tied to the *peak* number of live entities rather than to how many
have ever been handed out. That is what keeps memory flat under constant create/destroy churn.

---

## Destroy many entities at once

```cpp
std::vector<ecss::EntityId> doomed = collectOutOfRange();
reg.destroyEntities(doomed);     // one lock and one pass per array
```

| | one at a time | `destroyEntities` |
|---|---|---|
| `Registry<false>` | 14.4 ns | 14.3 ns |
| `Registry<true>` | 57.8 ns | 17.1 ns |

!!! tip "Ascending input is ~4x cheaper"
    `destroyEntities` binary-searches the list to trim ids past each array, so it needs the ids
    ordered. It checks first and only sorts when it has to — 0.18 ns per entity to check against
    3.4 to sort. Ids gathered by walking a view, or from `getAllEntities()`, are already
    ascending; a shuffled list costs 54 ns per entity instead of 14.

---

## Command buffer

`CommandBuffer` records structural changes and applies them where you choose. It is the general
answer to both problems on this page: the work is batched, and nothing is applied while a view
is open.

```cpp
#include <ecss/CommandBuffer.h>

ecss::CommandBuffer<true> cmd;

for (auto [e, hp, dmg] : reg.view<Health, Damage>()) {
    if (hp && dmg && hp->value <= dmg->amount) {
        cmd.addComponent<Dying>(e, Dying{});   // recorded, not applied
        cmd.destroyComponent<Damage>(e);
    }
}                                              // view ends

cmd.apply(reg);                                // one pass per component type
```

Deferral is explicit — a separate object with its own verbs — so a recorded change never
surprises you by not being visible yet. `Registry::addComponent` still applies immediately.

Adding a component to 200 000 entities, ns per add:

| | immediate | recorded + applied |
|---|---|---|
| ascending ids, `Registry<false>` | 7.3 | 18.8 |
| ascending ids, `Registry<true>` | 37.1 | 21.2 |
| shuffled ids, `Registry<false>` | 62563.7 | 73.9 |
| shuffled ids, `Registry<true>` | 101611.1 | 89.7 |

It loses against a plain append in the non-thread-safe build, where the immediate path is
already just a push. It wins wherever the immediate call would pay for per-call synchronisation
or a middle insert — every other row.

**Ordering.** For one entity and component type the last thing recorded wins, so removing a
component and adding it back in the same frame leaves it present. Entity destruction is applied
last and is terminal. Recording anything against an entity already destroyed in the same buffer
is a caller error: its id may already belong to something else.

**Threads.** One buffer belongs to one thread. Give each recording thread its own and apply
them one after another; the buffer takes no locks.

---

## Maintenance

`update()` frees memory whose grace period has run out, applies deferred erases, and compacts
arrays that asked for it.

Nothing in it waits. Deferred erases destroy components in place, and compaction is *attempted*
— an array something is iterating is skipped and left for the next call. So it can go anywhere
in the loop, including inside iteration, and more than once: with nothing to do it costs about
3.5 ns for the whole registry.

```cpp
reg.update();   // wherever it suits the frame
```

To stop calling it entirely, let views do it:

```cpp
reg.setAutoMaintenance(true);   // once, at startup
```

Opening a view then gives its arrays the pass `update()` would have, right before the iteration
that benefits from the compaction, plus one more array in rotation so that a type which is only
ever looked up by id — never iterated — still gets its turn. A busy array is skipped exactly as
in `update()`. Costs about 23 ns per view creation.

Off by default: opening a view is a read, and structural work inside one should be asked for.

!!! note
    In the non-thread-safe build there are no holds to tell that another view is open, so leave
    the switch off if you nest views there. `SectorsArray::defragment()` still blocks and waits
    when you want compaction to happen for certain.

---

## Putting it together

Streaming a region of 200 000 entities with three components each, `Registry<true>`:

| | one at a time | batched |
|---|---|---|
| take ids | 1.80 ms | 0.29 ms |
| add components | 22.61 ms | 6.31 ms |
| unload | 15.58 ms | 3.99 ms |
| **total** | **40 ms** | **10.6 ms** |

---

## What thread safety costs

With the batch paths, `Registry<true>` costs very little over `Registry<false>` on a single
thread — and buys read scaling the plain build cannot have at all.

| operation | `Registry<false>` | `Registry<true>` | cost |
|---|---|---|---|
| `hasComponent` | 3.92 | 4.77 | 1.2x |
| `each<Pos>` | 0.45 | 0.56 | 1.3x |
| `view<Pos,Vel>` range-for | 3.10 | 3.47 | 1.1x |
| `insertBulk` | 7.78 | 10.68 | 1.4x |
| `destroyEntities` | 58.41 | 59.96 | 1.0x |
| `takeEntities` | 1.12 | 1.29 | 1.1x |
| `addComponent`, one at a time | 7.68 | 36.64 | 4.8x |
| `destroyEntity`, one at a time | 14.38 | 57.78 | 4.0x |

Read throughput by thread count, `Registry<true>`:

| threads | vs 1 thread |
|---|---|
| 2 | 2.31x |
| 4 | 4.38x |
| 8 | 6.83x |

The only rows that cost real money are structural changes made one call at a time — which is
what everything above is for.
