

# Struct ecss::Threads::PinnedIndexesBitMask



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Threads**](namespaceecss_1_1Threads.md) **>** [**PinnedIndexesBitMask**](structecss_1_1Threads_1_1PinnedIndexesBitMask.md)



_Hierarchical bit mask indexing pinned sector ids._ [More...](#detailed-description)

* `#include <PinCounters.h>`





































## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**PinnedIndexesBitMask**](#function-pinnedindexesbitmask) () <br> |
|  PinIndex | [**highestSet**](#function-highestset) () const<br>_Get highest set sector id or -1 if none._  |
|  void | [**set**](#function-set) (SectorId index, bool state) <br>_Set or clear presence bit for sector id._  |
|  bool | [**test**](#function-test) (SectorId index) const<br>_Test if sector id bit set._  |




























## Detailed Description


Purpose:
* O(1) set / clear of a sector "present" bit.
* O(log N) query of highest pinned sector id ([**highestSet()**](structecss_1_1Threads_1_1PinnedIndexesBitMask.md#function-highestset)).




Layout:
* Level 0 stores bits for sectors (fan-out = machine word bit width).
* Higher levels store aggregated occupancy of lower level word indices.




Concurrency:
* Structural growth (vector resize) guarded by unique\_lock in ensurePath().
* Bit mutations use atomic\_ref fetch\_{or,and} with acquire-release semantics.
* Readers (test/highestSet) take shared locks plus atomic loads (acquire).




Guarantees:
* After set(i,true) completes, [**highestSet()**](structecss_1_1Threads_1_1PinnedIndexesBitMask.md#function-highestset) will eventually (after propagation) return &gt;= i unless cleared again.
* Clearing a bit propagates upward until a still-used sibling bit is found.




Used by [**PinCounters**](structecss_1_1Threads_1_1PinCounters.md) to recompute maxPinnedSector after a sector's last unpin. 


    
## Public Functions Documentation




### function PinnedIndexesBitMask 

```C++
inline ecss::Threads::PinnedIndexesBitMask::PinnedIndexesBitMask () 
```




<hr>



### function highestSet 

_Get highest set sector id or -1 if none._ 
```C++
inline PinIndex ecss::Threads::PinnedIndexesBitMask::highestSet () const
```





**Returns:**

Highest id or -1. @thread\_safety Safe concurrent with mutation; may return stale but monotonic snapshot. 





        

<hr>



### function set 

_Set or clear presence bit for sector id._ 
```C++
inline void ecss::Threads::PinnedIndexesBitMask::set (
    SectorId index,
    bool state
) 
```





**Parameters:**


* `index` Sector id. 
* `state` true to set, false to clear. 



**Note:**

Setting propagates upward until an already-marked ancestor is found. Clearing propagates until an ancestor still has other children set. @thread\_safety Concurrent calls allowed; internal locking + atomic ops. 





        

<hr>



### function test 

_Test if sector id bit set._ 
```C++
inline bool ecss::Threads::PinnedIndexesBitMask::test (
    SectorId index
) const
```





**Parameters:**


* `index` Sector id. 



**Returns:**

true if set. @thread\_safety Safe concurrent with set/clear. 





        

<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/threads/PinCounters.h`

