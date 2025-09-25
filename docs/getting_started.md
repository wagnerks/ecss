---
hide:
  - navigation
---

# Getting Started with ECSS

Welcome to **ECSS** — a high-performance Entity Component System (ECS) for C++20.  
This guide will help you install, set up, and run your first example in minutes.

---

## 📦 Installation

### Option 1 — CMake FetchContent (recommended)
```cmake
include(FetchContent)

FetchContent_Declare(
  ecss
  GIT_REPOSITORY https://github.com/wagnerks/ecss.git
  GIT_TAG        main
)

FetchContent_MakeAvailable(ecss)

target_link_libraries(my_project PRIVATE ecss)
```

### Option 2 — As a submodule
```bash
git submodule add https://github.com/wagnerks/ecss external/ecss
```
And in `CMakeLists.txt`:
```cmake
add_subdirectory(external/ecss)
target_link_libraries(my_project PRIVATE ecss)
```

---

## 🛠 Basic Example

```cpp
#include <ecss/Registry.h>
#include <iostream>

struct Position { float x, y; };
struct Velocity { float dx, dy; };

int main() {
    ecss::Registry<false> reg; // not thread-safe for simplicity

    // Create entity
    auto e = reg.takeEntity();

    // Add components
    reg.addComponent<Position>(e, {0.0f, 0.0f});
    reg.addComponent<Velocity>(e, {1.0f, 1.5f});

    // Iterate over entities with Position + Velocity
    for (auto [entityId, pos, vel] : reg.view<Position, Velocity>()){
        pos->x += vel->dx;
        pos->y += vel->dy;
        std::cout << "Entity " << entityId << " → (" << pos->x << ", " << pos->y << ")\n";
    }

    return 0;
}
```

---

## ⚡ Next Steps
- [Architecture Overview](architecture.md) — learn how the sector-based layout works  
- [Examples](examples.md) — practical use cases and patterns  
