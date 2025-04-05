# Stellar Forge Engine - ECSS Architecture Overview

The **Stellar Forge Engine** uses a custom-built Entity Component System (ECS) framework called **ECSS** (Entity Component System with Sectors). It is designed for scalability, cache locality, and high-performance multithreaded game logic. This document provides an in-depth explanation of how ECSS is structured, how it differs from other ECS designs, and the rationale behind its implementation.

---

## 🔧 Motivation

Traditional ECS systems typically rely on per-component arrays and sparse maps to associate entities with their components. While this works well for basic use cases, it can introduce performance bottlenecks when working at scale due to poor data locality and redundant iteration.

**ECSS** solves these problems by:

- Packing multiple components into a single memory block (called a *Sector*)
- Structuring memory with chunked allocation to reduce fragmentation
- Using low-level memory management with explicit component lifetimes
- Supporting true data-oriented iteration and asynchronous system access

---

## 🧠 Key Concepts

### 1. **Sectors**

A **Sector** is a tightly packed memory structure that holds multiple components belonging to the same entity. Each Sector contains:

- `SectorId` (unique identifier)
- Component data laid out linearly in memory
- A per-component "alive" flag

Each component inside a Sector is accessed by an offset, which is computed and stored in a central `membersLayout` map per Sector type.

**Diagram:**

```
+------------+--------------+------------+-----------+--------+
| Sector ID  | Alive Flags  | Transform  | Velocity  | Health |
+------------+--------------+------------+-----------+--------+
```

### 2. **SectorsArray**

`SectorsArray` is the primary data container used in ECSS:

- Stores sectors in memory **chunks** for performance and memory fragmentation control
- Uses a custom mapping from `EntityId` → sector index
- Tracks sector occupancy and alive states

Key features:

- Manual control of capacity (`reserve`, `shrinkToFit`) — useful for avoiding costly reallocations during runtime peaks
- Ability to insert/remove sectors dynamically
- Bulk operations on sectors (destroy, move, clear)
- `removeEmptySectors()` for memory compaction

### 3. **Component Reflection**

To enable dynamic component access and support a wide variety of component types, ECSS uses a `ReflectionHelper`. This approach is particularly valuable for runtime flexibility, debugging tools, and editor integration:

- Provides type IDs for each component
- Maintains a `typeFunctionsTable` with function pointers for construction, copy, move, and destruction
- Enables generic operations like `destroyMember(typeId, entity)` without knowing the type at compile-time

### 4. **Multithreaded Registry**

The `Registry` class manages all entities and components:

- Stores components in `SectorsArray` instances (one per component or group)
- Uses `std::shared_mutex` per component type for safe concurrent access
- Provides per-component read/write locking
- Tracks entities in `EntitiesRanges`, which is a compact set of ID ranges for allocation/deallocation

**Architecture Diagram:**

```
+------------+         +------------------+         +------------------+
|  Registry  | <--->   |  SectorsArray<>  | <-----> | ReflectionHelper |
+------------+         +------------------+         +------------------+
       |                     |  |  
       |                     |  └-- [Type Function Table]
       |                     └----- [Chunked Sector Storage]
       |
       └-- [EntitiesRanges] (ID management)
```

---

## 🚀 Entity Lifecycle

```cpp
EntityId entity = registry.takeEntity();
registry.addComponent<Transform>(entity, ...);
registry.addComponent<Velocity>(entity, ...);
...
registry.removeComponent<Transform>(entity);
registry.destroyEntity(entity);
```

Entities are created using `takeEntity()`, which returns a new or recycled ID. Components are added using `addComponent<T>()`, which internally reserves memory in the correct `SectorsArray`, creates the component, and sets the "alive" flag.

---

## 📦 Memory Layout & Performance

### Chunked Allocation

Sectors are allocated in memory **chunks** (e.g., 10240 Sectors per chunk). This reduces memory fragmentation and improves spatial locality.

### Component Packing

Multiple components can share the same memory sector. This:

- Improves cache locality
- Reduces pointer chasing
- Enables batch iteration over grouped data

**Memory Layout Diagram:**

```
+------------+--------------+------------+-----------+--------+
| Sector ID  | Alive Flags  | Transform  | Velocity  | Health |
+------------+--------------+------------+-----------+--------+
```

### Dirty Tracking

Only sectors that have been modified are marked as **dirty**, and only they are updated. This minimizes per-frame overhead.

---

## 🔄 Iteration Model

ECSS supports both synchronous and asynchronous iteration:

### Synchronous

```cpp
for (auto [id, transform, velocity] : registry.forEach<Transform, Velocity>()) {
    ...
}
```

- Internally uses smart iterators over the "main" component container
- Skips sectors where components are missing

### Asynchronous

```cpp
registry.forEachAsync<Transform, Velocity>(entities, [](EntityId id, Transform* t, Velocity* v) {
    ...
});
```

- Accepts a set of entity IDs (usually from frustum culling or other system)
- Parallel-safe read access

---

## 🧵 Threading Model

- **Render thread** is isolated
- **Systems run in their own threads**, reading and writing to component arrays via mutex-protected access
- Each component container has its own lock, allowing high concurrency and minimizing read/write contention
- Entity creation/destruction uses a separate mutex (`mEntitiesMutex`)

**Threading Diagram:**

```
+------------------+       +-------------------+
|   System Thread  | <---> |  Component Mutex  |
+------------------+       +-------------------+

+------------------+       +-------------------+
|   Render Thread  | <---> |    Draw Buffer    |
+------------------+       +-------------------+
```

---

## ✅ Summary

ECSS is a highly-performant and flexible ECS designed for real-time simulation of millions of entities. It combines:

- Dense memory layout with sector packing
- Strong reflection and type safety
- Efficient dynamic updates via dirty tracking
- Asynchronous safe system iteration

It’s built to scale with modern games and simulations — from deferred rendering to open-world culling. This makes ECSS ideal for complex simulations, large-scale environments, and real-time systems.

---

**Author**: [@wagnerks](https://github.com/wagnerks)  
**License**: MIT
