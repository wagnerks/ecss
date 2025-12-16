

# Struct ecss::Threads::PinCounters



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Threads**](namespaceecss_1_1Threads.md) **>** [**PinCounters**](structecss_1_1Threads_1_1PinCounters.md)



_Per-sector pin tracking & synchronization for safe structural mutations._ [More...](#detailed-description)

* `#include <PinCounters.h>`





































## Public Functions

| Type | Name |
| ---: | :--- |
|  bool | [**canMoveSector**](#function-canmovesector) (SectorId sectorId) const<br>_Fast test whether a sector can be moved (id greater than highest pinned and not itself pinned)._  |
|  FORCE\_INLINE bool | [**hasAnyPins**](#function-hasanypins) () noexcept const<br>_Distinct pinned sector presence check._  |
|  FORCE\_INLINE bool | [**isArrayLocked**](#function-isarraylocked) () const<br>_True if any sector is currently pinned._  |
|  FORCE\_INLINE bool | [**isPinned**](#function-ispinned) (SectorId id) const<br>_Test if a sector presently has a non-zero pin counter._  |
|  void | [**pin**](#function-pin) (SectorId id) <br>_Increment pin counter for sector id (first pin sets bit & updates aggregates)._  |
|  void | [**unpin**](#function-unpin) (SectorId id) <br>_Decrement pin counter; if last pin clears bit, updates aggregates._  |
|  void | [**waitUntilChangeable**](#function-waituntilchangeable) (SectorId sid=0) const<br>_Block until sector id (and any lower pinned sector) is safe to mutate._  |




























## Detailed Description


Features:
* Per-sector ref-counted pins (uint16\_t counters, on-demand block allocation).
* Global aggregated distinct pinned sector count: totalPinnedSectors.
* Highest pinned sector id (maxPinnedSector) for fast range checks (e.g. canMoveSector).
* Hierarchical bit mask to recompute highest pinned id after unpin.
* Wait primitive (waitUntilChangeable) that blocks until target sector id and its counter are movable (no active pins &lt;= id).




Invariants:
* Sector considered "pinned" while its counter &gt; 0.
* totalPinnedSectors equals number of sectors whose counter &gt; 0 (distinct, not sum of counts).
* When totalPinnedSectors == 0: all per-sector counters are 0 and structural compaction is safe.
* maxPinnedSector == -1 implies (eventually) totalPinnedSectors == 0, but writer logic now prefers totalPinnedSectors for reliability.




Memory order:
* Pin/unpin use release on increments/decrements; readers use acquire loads to observe a consistent state.
* CAS on maxPinnedSector uses release when publishing a higher id, acquire when reading for canMoveSector.




Typical usage pattern:
* Reader pins sector (pin), accesses data, then unpins (unpin).
* Writer wanting to destroy/compact sectors calls waitUntilChangeable(id) or (for full defrag) waitUntilChangeable(0) after verifying [**hasAnyPins()**](structecss_1_1Threads_1_1PinCounters.md#function-hasanypins)==false for opportunistic defrag.
* Destruction/move executes only when relevant sectors are unpinned. 




    
## Public Functions Documentation




### function canMoveSector 

_Fast test whether a sector can be moved (id greater than highest pinned and not itself pinned)._ 
```C++
inline bool ecss::Threads::PinCounters::canMoveSector (
    SectorId sectorId
) const
```





**Parameters:**


* `sectorId` Sector id. 



**Returns:**

true if movable now. 





        

<hr>



### function hasAnyPins 

_Distinct pinned sector presence check._ 
```C++
inline FORCE_INLINE bool ecss::Threads::PinCounters::hasAnyPins () noexcept const
```





**Returns:**

true if one or more sectors pinned. 





        

<hr>



### function isArrayLocked 

_True if any sector is currently pinned._ 
```C++
inline FORCE_INLINE bool ecss::Threads::PinCounters::isArrayLocked () const
```





**Note:**

Preferred over checking maxPinnedSector &gt;= 0 (more precise & immediate). 





        

<hr>



### function isPinned 

_Test if a sector presently has a non-zero pin counter._ 
```C++
inline FORCE_INLINE bool ecss::Threads::PinCounters::isPinned (
    SectorId id
) const
```





**Parameters:**


* `id` Sector id. 




        

<hr>



### function pin 

_Increment pin counter for sector id (first pin sets bit & updates aggregates)._ 
```C++
inline void ecss::Threads::PinCounters::pin (
    SectorId id
) 
```





**Parameters:**


* `id` Sector id (!= INVALID\_ID). 



**Note:**

May raise maxPinnedSector via CAS if id is the largest active pin. 





        

<hr>



### function unpin 

_Decrement pin counter; if last pin clears bit, updates aggregates._ 
```C++
inline void ecss::Threads::PinCounters::unpin (
    SectorId id
) 
```





**Parameters:**


* `id` Sector id. 



**Note:**

On last unpin triggers updateMaxPinned() and notifies waiters. 





        

<hr>



### function waitUntilChangeable 

_Block until sector id (and any lower pinned sector) is safe to mutate._ 
```C++
inline void ecss::Threads::PinCounters::waitUntilChangeable (
    SectorId sid=0
) const
```





**Parameters:**


* `sid` Sector id (default 0 for full barrier).

Two-phase wait:
* Wait while id &lt;= maxPinnedSector (meaning some pin &lt;= id active).
* Wait while per-sector counter for sid &gt; 0. 

**Warning:**

Use with id=0 for full-array structural changes (defragment). 







        

<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/threads/PinCounters.h`

