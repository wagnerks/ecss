

# File SectorsArray.h



[**FileList**](files.md) **>** [**ecss**](dir_194708e763cf312315c6b23555bce86f.md) **>** [**memory**](dir_3333283e221f8a8f53c5923bc4c386e0.md) **>** [**SectorsArray.h**](SectorsArray_8h.md)

[Go to the source code of this file](SectorsArray_8h_source.md)

_Sector-based sparse storage for one (or a grouped set of) ECS component type(s) with optional thread-safety._ [More...](#detailed-description)

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
| class | [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) &lt;ThreadSafe, typename Allocator&gt;<br>_Container managing sectors of one (or grouped) component layouts with optional thread-safety._  |
| class | [**Iterator**](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) <br>_Forward iterator over every sector (alive or dead)._  |
| class | [**IteratorAlive**](classecss_1_1Memory_1_1SectorsArray_1_1IteratorAlive.md) <br>_Forward iterator skipping sectors where a specific component (type mask) isn't alive._  |
| class | [**RangedIterator**](classecss_1_1Memory_1_1SectorsArray_1_1RangedIterator.md) <br>[_**Iterator**_](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) _over specified index ranges (includes dead sectors)._ |
| struct | [**SectorsMap&lt; false &gt;**](structecss_1_1Memory_1_1detail_1_1SectorsMap_3_01false_01_4.md) &lt;&gt;<br> |

















































## Macros

| Type | Name |
| ---: | :--- |
| define  | [**ITERATOR\_COMMON\_USING**](SectorsArray_8h.md#define-iterator_common_using) (IteratorName) `/* multi line expression */`<br> |
| define  | [**SHARED\_LOCK**](SectorsArray_8h.md#define-shared_lock) () `auto lock = readLock()`<br> |
| define  | [**TS\_GUARD**](SectorsArray_8h.md#define-ts_guard) (TS\_FLAG, LOCK\_MACRO, EXPR) `	do {enforceTSMode&lt;TS&gt;(); if constexpr (TS\_FLAG) { LOCK\_MACRO##\_LOCK(); EXPR; } else { EXPR; }} while(0)`<br>_Execute an expression with (optional) locking depending on template TS\_FLAG._  |
| define  | [**TS\_GUARD\_S**](SectorsArray_8h.md#define-ts_guard_s) (TS\_FLAG, LOCK\_MACRO, ADDITIONAL\_SINK, EXPR) `	do {enforceTSMode&lt;TS&gt;(); if constexpr (TS\_FLAG) { LOCK\_MACRO##\_LOCK(); ADDITIONAL\_SINK; EXPR; } else { EXPR; }} while(0)`<br>_Same as TS\_GUARD but allows an additional pre-expression executed under the lock._  |
| define  | [**UNIQUE\_LOCK**](SectorsArray_8h.md#define-unique_lock) () `auto lock = writeLock()`<br> |

## Detailed Description


Core responsibilities:
* Owns a set of variable-sized "sectors" (fixed layout defined by SectorLayoutMeta).
* Provides O(1) random access by SectorId through a direct pointer map.
* Supports insertion / emplacement / overwrite of component members inside sectors.
* Supports conditional & ranged erasure, deferred (asynchronous) erase, and defragmentation.
* Exposes multiple iterator flavours: linear, alive-only, ranged, ranged+alive.
* Coordinates with PinCounters so that erasure / relocation waits for readers (ThreadSafe build).




Thread safety model (when ThreadSafe=true):
* Read-only APIs acquire a shared (reader) lock.
* Mutating APIs acquire a unique (writer) lock.
* Structural operations (erase / move / defragment) wait on PinCounters before relocating memory.
* Vector memory reclamation is deferred via RetireBin until no reader references remain.




Performance notes:
* Lookup of sector pointer by id: O(1) (array indexing).
* Insert of a new sector id: amortized O(N) worst-case (due to ordered insertion shift); O(1) if appended.
* Defragmentation: compacts only alive runs; complexity proportional to total sectors.






**See also:** [**ecss::Registry**](classecss_1_1Registry.md) (higher-level orchestration) 


**See also:** Sector / SectorLayoutMeta (layout & storage details) 



    
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

_Execute an expression with (optional) locking depending on template TS\_FLAG._ 
```C++
#define TS_GUARD (
    TS_FLAG,
    LOCK_MACRO,
    EXPR
) `do {enforceTSMode<TS>(); if constexpr (TS_FLAG) { LOCK_MACRO##_LOCK(); EXPR; } else { EXPR; }} while(0)`
```





**Parameters:**


* `TS_FLAG` Compile-time boolean (usually template ThreadSafe). 
* `LOCK_MACRO` Either SHARED or UNIQUE (without trailing \_LOCK). 
* `EXPR` Expression executed under the lock (or directly if TS\_FLAG==false). 




        

<hr>



### define TS\_GUARD\_S 

_Same as TS\_GUARD but allows an additional pre-expression executed under the lock._ 
```C++
#define TS_GUARD_S (
    TS_FLAG,
    LOCK_MACRO,
    ADDITIONAL_SINK,
    EXPR
) `do {enforceTSMode<TS>(); if constexpr (TS_FLAG) { LOCK_MACRO##_LOCK(); ADDITIONAL_SINK; EXPR; } else { EXPR; }} while(0)`
```





**Parameters:**


* `TS_FLAG` Compile-time boolean. 
* `LOCK_MACRO` SHARED or UNIQUE. 
* `ADDITIONAL_SINK` Additional code executed (under lock) before EXPR. 
* `EXPR` Main expression executed under the same lock. 




        

<hr>



### define UNIQUE\_LOCK 

```C++
#define UNIQUE_LOCK (
    
) `auto lock = writeLock()`
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/memory/SectorsArray.h`

