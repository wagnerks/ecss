

# File SectorsArray.h



[**FileList**](files.md) **>** [**ecss**](dir_194708e763cf312315c6b23555bce86f.md) **>** [**memory**](dir_3333283e221f8a8f53c5923bc4c386e0.md) **>** [**SectorsArray.h**](SectorsArray_8h.md)

[Go to the source code of this file](SectorsArray_8h_source.md)

_SoA-based sparse storage for ECS components with optional thread-safety._ [More...](#detailed-description)

* `#include <algorithm>`
* `#include <cassert>`
* `#include <shared_mutex>`
* `#include <utility>`
* `#include <vector>`
* `#include <ecss/Ranges.h>`
* `#include <ecss/threads/PinCounters.h>`
* `#include <ecss/memory/ChunksAllocator.h>`
* `#include <ecss/memory/Sector.h>`













## Namespaces

| Type | Name |
| ---: | :--- |
| namespace | [**ecss**](namespaceecss.md) <br> |
| namespace | [**Memory**](namespaceecss_1_1Memory.md) <br> |
| namespace | [**detail**](namespaceecss_1_1Memory_1_1detail.md) <br> |


## Classes

| Type | Name |
| ---: | :--- |
| struct | [**PinnedSector**](structecss_1_1Memory_1_1PinnedSector.md) <br>_RAII pin for a sector to prevent relocation / destruction while in use._  |
| class | [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) &lt;ThreadSafe, typename Allocator&gt;<br>_SoA-based container managing sector data with external id/isAlive arrays._  |
| class | [**Iterator**](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) <br>_Forward iterator over all slots (alive or dead). Optimized: uses chunk-aware pointer increment for O(1) per-element access. Uses atomic view snapshots for thread-safe iteration._  |
| class | [**IteratorAlive**](classecss_1_1Memory_1_1SectorsArray_1_1IteratorAlive.md) <br>_Forward iterator skipping slots where component is not alive. Optimized: uses chunk-aware pointer increment for O(1) per-element access. When isPacked=true (defragmentSize==0), skipDead is bypassed for maximum speed. Uses atomic view snapshots for thread-safe iteration._  |
| class | [**RangedIterator**](classecss_1_1Memory_1_1SectorsArray_1_1RangedIterator.md) <br>[_**Iterator**_](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) _over sectors whose IDs fall within specified SectorId ranges. Converts SectorId ranges to linear index ranges using binary search. Optimized: chunk-aware pointer access. Uses atomic view snapshots for thread-safe iteration._ |
| struct | [**SlotInfo**](structecss_1_1Memory_1_1SectorsArray_1_1SlotInfo.md) <br>_Slot info returned by iterators._  |
| struct | [**DenseArrays&lt; false &gt;**](structecss_1_1Memory_1_1detail_1_1DenseArrays_3_01false_01_4.md) &lt;&gt;<br>_Non-thread-safe dense arrays (simple vectors)_  |
| struct | [**View**](structecss_1_1Memory_1_1detail_1_1DenseArrays_3_01false_01_4_1_1View.md) &lt;&gt;<br> |
| struct | [**DenseArrays&lt; true &gt;**](structecss_1_1Memory_1_1detail_1_1DenseArrays_3_01true_01_4.md) &lt;&gt;<br>_Thread-safe dense arrays with atomic view for lock-free reads._  |
| struct | [**View**](structecss_1_1Memory_1_1detail_1_1DenseArrays_3_01true_01_4_1_1View.md) &lt;&gt;<br> |
| struct | [**SlotInfo**](structecss_1_1Memory_1_1detail_1_1SlotInfo.md) <br>_Slot info for fast sparse lookup - stores data pointer and linear index This enables O(1) component access with a single sparse map lookup._  |
| struct | [**SparseMap&lt; false &gt;**](structecss_1_1Memory_1_1detail_1_1SparseMap_3_01false_01_4.md) &lt;&gt;<br>_Non-thread-safe sparse map (simple vector)_  |
| struct | [**SparseMap&lt; true &gt;**](structecss_1_1Memory_1_1detail_1_1SparseMap_3_01true_01_4.md) &lt;&gt;<br>_Thread-safe sparse map with atomic view for lock-free reads._  |

















































## Macros

| Type | Name |
| ---: | :--- |
| define  | [**ITERATOR\_COMMON\_USING**](SectorsArray_8h.md#define-iterator_common_using) (IteratorName) `/* multi line expression */`<br> |
| define  | [**SHARED\_LOCK**](SectorsArray_8h.md#define-shared_lock) () `auto lock = readLock()`<br> |
| define  | [**TS\_GUARD**](SectorsArray_8h.md#define-ts_guard) (TS\_FLAG, LOCK\_MACRO, EXPR) `	do {enforceTSMode&lt;TS&gt;(); if constexpr (TS\_FLAG) { LOCK\_MACRO##\_LOCK(); EXPR; } else { EXPR; }} while(0)`<br> |
| define  | [**TS\_GUARD\_S**](SectorsArray_8h.md#define-ts_guard_s) (TS\_FLAG, LOCK\_MACRO, ADDITIONAL\_SINK, EXPR) `	do {enforceTSMode&lt;TS&gt;(); if constexpr (TS\_FLAG) { LOCK\_MACRO##\_LOCK(); ADDITIONAL\_SINK; EXPR; } else { EXPR; }} while(0)`<br> |
| define  | [**UNIQUE\_LOCK**](SectorsArray_8h.md#define-unique_lock) () `auto lock = writeLock()`<br> |

## Detailed Description


Architecture (Sorted Dense + Sparse Index):
* mIds[linearIdx] -&gt; SectorId (sorted, dense)
* mIsAliveData[linearIdx] -&gt; uint32\_t bitfield of alive components
* mSparse[sectorId] -&gt; linearIdx (sparse map for O(1) lookup)
* ChunksAllocator -&gt; raw component data at linearIdx




Core responsibilities:
* O(1) random access by SectorId through sparse map
* O(1) iteration with excellent cache locality (SoA layout)
* Supports insertion / emplacement / overwrite of component members
* Supports conditional & ranged erasure, deferred erase, and defragmentation
* Exposes multiple iterator flavours: linear, alive-only, ranged




Thread safety model (when ThreadSafe=true):
* Read-only APIs acquire a shared (reader) lock
* Mutating APIs acquire a unique (writer) lock
* Structural operations wait on PinCounters before relocating memory






**See also:** [**ecss::Registry**](classecss_1_1Registry.md) (higher-level orchestration) 



    
## Macro Definition Documentation





### define ITERATOR\_COMMON\_USING 

```C++
#define ITERATOR_COMMON_USING (
    IteratorName
) `/* multi line expression */`
```




<hr>



### define SHARED\_LOCK 

```C++
#define SHARED_LOCK (
    
) `auto lock = readLock()`
```




<hr>



### define TS\_GUARD 

```C++
#define TS_GUARD (
    TS_FLAG,
    LOCK_MACRO,
    EXPR
) `do {enforceTSMode<TS>(); if constexpr (TS_FLAG) { LOCK_MACRO##_LOCK(); EXPR; } else { EXPR; }} while(0)`
```




<hr>



### define TS\_GUARD\_S 

```C++
#define TS_GUARD_S (
    TS_FLAG,
    LOCK_MACRO,
    ADDITIONAL_SINK,
    EXPR
) `do {enforceTSMode<TS>(); if constexpr (TS_FLAG) { LOCK_MACRO##_LOCK(); ADDITIONAL_SINK; EXPR; } else { EXPR; }} while(0)`
```




<hr>



### define UNIQUE\_LOCK 

```C++
#define UNIQUE_LOCK (
    
) `auto lock = writeLock()`
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/memory/SectorsArray.h`

