

# File Registry.h



[**FileList**](files.md) **>** [**ecss**](dir_194708e763cf312315c6b23555bce86f.md) **>** [**Registry.h**](Registry_8h.md)

[Go to the source code of this file](Registry_8h_source.md)

_Core ECS registry and iterator/view implementation (SoA-based storage)._ [More...](#detailed-description)

* `#include <algorithm>`
* `#include <array>`
* `#include <ranges>`
* `#include <shared_mutex>`
* `#include <tuple>`
* `#include <vector>`
* `#include <ecss/Ranges.h>`
* `#include <ecss/memory/Reflection.h>`
* `#include <ecss/memory/SectorsArray.h>`
* `#include <ecss/memory/Sector.h>`













## Namespaces

| Type | Name |
| ---: | :--- |
| namespace | [**ecss**](namespaceecss.md) <br> |


## Classes

| Type | Name |
| ---: | :--- |
| class | [**ArraysView**](classecss_1_1ArraysView.md) &lt;ThreadSafe, typename Allocator, Ranged, typename T, CompTypes&gt;<br>_Iterable view over entities with one main component and optional additional components._  |
| struct | [**EndIterator**](structecss_1_1ArraysView_1_1EndIterator.md) <br>_Sentinel end iterator tag._  |
| class | [**Iterator**](classecss_1_1ArraysView_1_1Iterator.md) <br>_Forward iterator over alive sectors of the main component type._  |
| struct | [**PinnedComponent**](structecss_1_1PinnedComponent.md) &lt;class T&gt;<br>_RAII wrapper that pins the sector holding component T and exposes a typed pointer._  |
| class | [**Registry**](classecss_1_1Registry.md) &lt;ThreadSafe, typename Allocator&gt;<br>_Central ECS registry that owns component sector arrays, entities and iteration utilities._  |
| struct | [**TypeAccessInfo**](structecss_1_1TypeAccessInfo.md) <br>_Metadata for accessing a component type inside a sectors array._  |


















































## Detailed Description


The Registry manages:
* Allocation and ownership of entity ids (dense id ranges via Ranges&lt;&gt;).
* Lazily created or pre-registered component sector arrays (SectorsArray).
* Thread-aware access and pinning (if ThreadSafe template parameter is true).
* Views (ArraysView) that iterate a "main" component and project other components.




Storage model (SoA - Structure of Arrays):
* Each component (or grouped component set) is stored in a SectorsArray.
* Sector metadata (id, isAliveData) stored in parallel arrays for cache locality.
* Component data stored contiguously in ChunksAllocator.




Thread safety:
* When ThreadSafe == true:
  * Public mutating APIs acquire internal shared / unique locks.
  * Pinning uses a pin counter to defer destruction until safe.


* When ThreadSafe == false:
  * No internal synchronization (caller is responsible).








**See also:** ArraysView for iteration semantics. 


**See also:** PinnedComponent for safe temporary access in thread-safe builds. 



    

------------------------------
The documentation for this class was generated from the following file `ecss/Registry.h`

