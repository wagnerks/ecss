# ecss — High-performance C++ ECS with sector-based memory layout

> A data-oriented Entity-Component System focused on cache locality, predictable memory, and safe multithreading. MIT licensed.

## Why ECSS?

ECSS (Entity Component System with **Sectors**) groups multiple components of an entity into a single tightly-packed memory block (a *sector*). This improves cache behavior, reduces pointer chasing, and enables fast iteration (sync or async) over large worlds. The architecture centers on:

- **Sector-based layout** (components are packed together per entity)
- **Chunked storage** for low fragmentation and stable addresses
- **Reflection helpers** for generic operations without RTTI bloat
- **Thread-safe access** with per-type shared/unique locking
- **Iterators** tailored for dense traversal and filtered access

---

## Features

- 🧱 **Sectors**: multiple components in one cache-friendly block  
- 🧭 **Chunked allocation**: capacity planning, fewer reallocations  
- 🔍 **Reflection table**: construct/copy/move/destroy by type id  
- 🔒 **Concurrency**: shared/unique locks per component container  
- 🔁 **Iterators**: dense `forEach<...>()` + async traversal by ID sets  
- 🧹 **Compaction utilities**: remove empty sectors, recycle IDs  
- 🧰 **Modern C++**: header/impl split with zero-overhead paths where possible

---

## Getting started

### 1) Add to your project

**As a submodule**
```bash
git submodule add https://github.com/wagnerks/ecss external/ecss
```

**CMake**
```cmake
# In your CMakeLists.txt
add_subdirectory(external/ecss)
target_link_libraries(your_app PRIVATE ecss)
```

### 2) Define components

```cpp
struct Transform { float x{}, y{}, z{}; };
struct Velocity  { float vx{}, vy{}, vz{}; };
struct Health    { int hp{100}; };
```

### 3) Create a registry and spawn entities

```cpp
#include <ecss/Registry.h>

using Registry = ecss::Registry</*ThreadSafe=*/true, ecss::Memory::ChunksAllocator>;

int main() {
    Registry reg;

    auto e = reg.takeEntity();
    reg.addComponent<Transform>(e, {0,0,0});
    reg.addComponent<Velocity >(e, {1,0,0});
    reg.addComponent<Health   >(e, {100});

    reg.forEach<Transform, Velocity>([&](auto id, Transform* t, Velocity* v) {
        t->x += v->vx; t->y += v->vy; t->z += v->vz;
    });

    return 0;
}
```

### 4) Async / filtered traversal

```cpp
std::vector<ecss::EntityId> visible;

reg.forEachAsync<Transform, Velocity>(visible, [](auto id, Transform* t, Velocity* v){
    // safe traversal with locks
});
```

### 5) Reflection-driven ops

```cpp
ecss::TypeId tid = resolveTypeId("Health");
reg.destroyMember(tid, e);
```

---

## Concepts in a nutshell

### Sectors
A *Sector* is a tightly packed block containing all/selected components for a single entity, plus per-component flags and offsets.

### SectorsArray
Stores sectors in **chunks** to control fragmentation and keep addresses stable.

### Reflection helpers
Generates per-type function tables (construct, move, destroy, etc.) without RTTI.

### Threading model
Per-component containers use `shared_mutex` to allow multiple readers with exclusive writers when needed.

---

## Build

**Requirements**
- C++20 (or newer)
- CMake

**Standard build**
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

**Tests**
```bash
cmake -S . -B build -DECSS_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build
```

---

## Usage tips

- Call `reserve` to avoid reallocations during spikes  
- Group hot components together in the same sector  
- Use `forEach<Ts...>` for fastest traversal  
- Use async traversal for read-heavy systems  
- Compact periodically if entities churn heavily

---

## Roadmap

- Serialization hooks  
- Stable archetype hashes  
- Lock-free read paths  
- Debug UI with live sector maps  
- More benchmarks

---

## FAQ

**Is ECSS an archetype ECS?**  
It’s closer to a **sector-packed** design.

**Does it require RTTI?**  
No.

**How do I mix TS and non-TS modes?**  
`Registry` is templated: enable or disable thread-safety at compile time.

---

## Contributing

Contributions welcome. Include reasoning or benchmarks for perf changes.

---

## License

MIT. See [`LICENSE`](./LICENSE).

---

## Acknowledgements

Built as part of the [**Stellar Forge Engine**](https://github.com/wagnerks/StelForge)  initiative and refined through real-world use.


**Author**: [@wagnerks](https://github.com/wagnerks)  
**License**: MIT
