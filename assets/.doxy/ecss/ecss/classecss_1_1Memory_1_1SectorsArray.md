

# Class ecss::Memory::SectorsArray

**template &lt;bool ThreadSafe, typename Allocator&gt;**



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md) **>** [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md)



_SoA-based container managing sector data with external id/isAlive arrays._ [More...](#detailed-description)

* `#include <SectorsArray.h>`















## Classes

| Type | Name |
| ---: | :--- |
| class | [**Iterator**](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) <br>_Forward iterator over all slots (alive or dead). Optimized: uses chunk-aware pointer increment for O(1) per-element access. Uses atomic view snapshots for thread-safe iteration._  |
| class | [**IteratorAlive**](classecss_1_1Memory_1_1SectorsArray_1_1IteratorAlive.md) <br>_Forward iterator skipping slots where component is not alive. Optimized: uses chunk-aware pointer increment for O(1) per-element access. When isPacked=true (defragmentSize==0), skipDead is bypassed for maximum speed. Uses atomic view snapshots for thread-safe iteration._  |
| class | [**RangedIterator**](classecss_1_1Memory_1_1SectorsArray_1_1RangedIterator.md) <br>[_**Iterator**_](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) _over sectors whose IDs fall within specified SectorId ranges. Converts SectorId ranges to linear index ranges using binary search. Optimized: chunk-aware pointer access. Uses atomic view snapshots for thread-safe iteration._ |
| struct | [**SlotInfo**](structecss_1_1Memory_1_1SectorsArray_1_1SlotInfo.md) <br>_Slot info returned by iterators._  |
| struct | [**StructuralEdit**](structecss_1_1Memory_1_1SectorsArray_1_1StructuralEdit.md) <br>_RAII publication of "sector storage is changing" around a writer body._  |






















## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**SectorsArray**](#function-sectorsarray-26) (const [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) & other) <br> |
|   | [**SectorsArray**](#function-sectorsarray-36) (const [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md)&lt; T, Alloc &gt; & other) <br> |
|   | [**SectorsArray**](#function-sectorsarray-46) ([**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) && other) noexcept<br> |
|   | [**SectorsArray**](#function-sectorsarray-56) ([**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md)&lt; T, Alloc &gt; && other) noexcept<br> |
|  [**Iterator**](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) | [**begin**](#function-begin) () const<br> |
|  [**IteratorAlive**](classecss_1_1Memory_1_1SectorsArray_1_1IteratorAlive.md) | [**beginAlive**](#function-beginalive) () const<br> |
|  [**RangedIterator**](classecss_1_1Memory_1_1SectorsArray_1_1RangedIterator.md) | [**beginRanged**](#function-beginranged) (const [**Ranges**](structecss_1_1Ranges.md)&lt; SectorId &gt; & ranges) const<br> |
|  size\_t | [**capacity**](#function-capacity) () const<br> |
|  void | [**clear**](#function-clear) () <br> |
|  void | [**clearAsync**](#function-clearasync) () <br>_Ask for the array to be cleared at the next safe point, instead of now._  |
|  bool | [**containsSector**](#function-containssector) (SectorId id) const<br> |
|  FORCE\_INLINE std::byte \* | [**dataAt**](#function-dataat-12) (const typename Allocator::ChunksView & chunks, uint32\_t linearIdx) const<br> |
|  FORCE\_INLINE std::byte \* | [**dataAt**](#function-dataat-22) (uint32\_t linearIdx) const<br>[_**Sector**_](namespaceecss_1_1Memory_1_1Sector.md) _data address for a linear index, read through the chunk snapshot. Loops should hoist loadChunks() and use the two-argument form instead._ |
|  void | [**defragment**](#function-defragment) () <br> |
|  T \* | [**emplace**](#function-emplace) (SectorId sectorId, Args &&... args) noexcept<br> |
|  bool | [**empty**](#function-empty) () const<br>_@thread\_safety Internally synchronized. One atomic load._  |
|  [**Iterator**](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) | [**end**](#function-end) () const<br> |
|  [**IteratorAlive**](classecss_1_1Memory_1_1SectorsArray_1_1IteratorAlive.md) | [**endAlive**](#function-endalive) () const<br> |
|  [**RangedIterator**](classecss_1_1Memory_1_1SectorsArray_1_1RangedIterator.md) | [**endRanged**](#function-endranged) () const<br> |
|  [**Iterator**](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) | [**erase**](#function-erase-12) ([**Iterator**](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) it, bool defragment=false) noexcept<br> |
|  void | [**erase**](#function-erase-22) (size\_t beginIdx, size\_t count=1, bool defragment=false) <br> |
|  void | [**eraseAsync**](#function-eraseasync) (SectorId id, size\_t count=1) <br> |
|  auto | [**exclusiveForInsert**](#function-exclusiveforinsert) (SectorId sectorId, Fn && fn) <br>_Acquire (or reuse) the slot for_ `sectorId` _and run_`fn(linearIdx)` _on it._ |
|  auto | [**exclusiveWhenQuiescent**](#function-exclusivewhenquiescent) (Fn && fn) <br>_Run_ `fn` _under the write lock once no sector at all is pinned. Required by anything that relocates sectors (shift, defragment, clear, copy). @thread\_safety Internally synchronized; blocks. Waits for the whole array: fn runs under the write lock once nothing is pinned and nothing is held. This is the gate for everything that relocates sectors, and the one to use when fn moves anything. It re-checks after taking the lock and waits again if a reader slipped in, so a thread that keeps opening views on this array can hold it off; from the thread that holds the view itself, it never completes._ |
|  auto | [**exclusiveWhenUnpinned**](#function-exclusivewhenunpinned-12) (const EntityId \* begin, const EntityId \* end, Fn && fn) <br>_Same as exclusiveWhenUnpinned(id), for a batch of in-place destroys._  |
|  auto | [**exclusiveWhenUnpinned**](#function-exclusivewhenunpinned-22) (SectorId sectorId, Fn && fn) <br>_Run_ `fn` _under the write lock once sector_`sectorId` _carries no pins. For operations confined to that one sector (in-place destroy / overwrite). @thread\_safety Internally synchronized; blocks. Waits for one sector only. Runs fn under the write lock once that sector carries no pins; pins on other sectors and holds over the array do not delay it, because fn is expected to change that sector in place and move nothing. Deadlocks if this thread pins that sector._ |
|  size\_t | [**findLinearIdx**](#function-findlinearidx) (SectorId sectorId) const<br> |
|  std::byte \* | [**findSectorData**](#function-findsectordata) (SectorId id) const<br> |
|  [**detail::SlotInfo**](structecss_1_1Memory_1_1detail_1_1SlotInfo.md) | [**findSlot**](#function-findslot) (SectorId id) const<br>_Find slot info (data pointer + linearIdx) for fast sparse lookup._  |
|  auto | [**getDefragmentationRatio**](#function-getdefragmentationratio) () const<br>_@thread\_safety Internally synchronized. Two relaxed loads; the ratio may be a hair stale._  |
|  auto | [**getDefragmentationSize**](#function-getdefragmentationsize) () const<br>_@thread\_safety Internally synchronized. One relaxed load._  |
|  SectorId | [**getId**](#function-getid) (size\_t linearIdx) const<br> |
|  uint32\_t | [**getIsAlive**](#function-getisalive) (SectorId id) const<br> |
|  uint32\_t & | [**getIsAliveRef**](#function-getisaliveref) (size\_t linearIdx) <br> |
|  FORCE\_INLINE const [**SectorLayoutMeta**](structecss_1_1Memory_1_1SectorLayoutMeta.md) \* | [**getLayout**](#function-getlayout) () const<br>_@thread\_safety Internally synchronized. The layout is fixed at creation and never changes._  |
|  FORCE\_INLINE const [**LayoutData**](structecss_1_1Memory_1_1LayoutData.md) & | [**getLayoutData**](#function-getlayoutdata) () const<br> |
|  bool | [**hasPendingClear**](#function-haspendingclear) () noexcept const<br>_Whether a_ [_**clearAsync()**_](classecss_1_1Memory_1_1SectorsArray.md#function-clearasync) _is still waiting for a frame it can run in. @thread\_safety Internally synchronized. One atomic load._ |
|  [**StructuralHold**](structecss_1_1Memory_1_1StructuralHold.md) | [**holdStructure**](#function-holdstructure) () const<br>_Block compaction of this array for as long as the returned object lives. Use this, not a pin, when the point is "do not move things", not "leave this sector". @thread\_safety Internally synchronized. Cheaper than a pin and per thread rather than per sector. While one lives no sector in this array may be relocated_  _so every whole-array writer waits for it. This is what a view holds for its lifetime. Hold it for as short a time as the reading takes._ |
|  void | [**incDefragmentSize**](#function-incdefragmentsize) (uint32\_t count=1) <br>_@thread\_safety Internally synchronized. One relaxed increment of the dead-slot counter._  |
|  std::remove\_cvref\_t&lt; T &gt; \* | [**insert**](#function-insert) (SectorId sectorId, T && data) noexcept<br> |
|  void | [**insertBulk**](#function-insertbulk) (It first, It last) noexcept<br>_Bulk insert. Each \*it yields a pair-like {SectorId, C}._  |
|  bool | [**isPacked**](#function-ispacked) () const<br>_Check if array has no dead slots (defragmentSize == 0) @thread\_safety Internally synchronized. One relaxed load. True when no dead slot is waiting to be compacted away._  |
|  FORCE\_INLINE uint32\_t | [**loadAliveWord**](#function-loadaliveword) (size\_t linearIdx) noexcept const<br>_Thread-safe alive-word read that routes through the seqlock snapshot. Unlike getIsAliveRef, this never dereferences the live std::vector, so it is safe against concurrent push\_back reallocation_  _the vector's internal \_M\_start field would otherwise race with the reader's non-atomic read of it. Old isAlive buffers remain valid because_[_**RetireAllocator**_](structecss_1_1Memory_1_1RetireAllocator.md) _defers their free. Returns 0 if linearIdx is outside the snapshot (newly allocated slot not yet published), which callers treat as "not alive". @thread\_safety Internally synchronized. Lock-free: reads a published snapshot, takes no lock and waits for nothing. Bounds-checked: an index past the published size reads as not alive rather than out of bounds._ |
|  FORCE\_INLINE auto | [**loadChunks**](#function-loadchunks) () const<br> |
|  bool | [**needDefragment**](#function-needdefragment) () const<br>_@thread\_safety Internally synchronized. Relaxed loads against the threshold._  |
|  [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) & | [**operator=**](#function-operator) (const [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) & other) <br> |
|  [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) & | [**operator=**](#function-operator_1) (const [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md)&lt; T, Alloc &gt; & other) <br> |
|  [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) & | [**operator=**](#function-operator_2) ([**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) && other) noexcept<br> |
|  [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) & | [**operator=**](#function-operator_3) ([**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md)&lt; T, Alloc &gt; && other) noexcept<br> |
|  [**PinnedSector**](structecss_1_1Memory_1_1PinnedSector.md) | [**pinBackSector**](#function-pinbacksector) () const<br> |
|  [**PinnedSector**](structecss_1_1Memory_1_1PinnedSector.md) | [**pinSector**](#function-pinsector) (SectorId id) const<br> |
|  [**PinnedSector**](structecss_1_1Memory_1_1PinnedSector.md) | [**pinSectorAt**](#function-pinsectorat) (size\_t idx) const<br> |
|  void | [**processPendingErases**](#function-processpendingerases) (bool withDefragment=true) <br> |
|  T \* | [**push**](#function-push) (SectorId sectorId, Args &&... args) noexcept<br> |
|  auto | [**readLock**](#function-readlock-12) () const<br> |
|  void | [**reserve**](#function-reserve) (uint32\_t newCapacity) <br> |
|  void | [**setDefragmentThreshold**](#function-setdefragmentthreshold) (float threshold) <br> |
|  void | [**shrinkToFit**](#function-shrinktofit) () <br> |
|  size\_t | [**size**](#function-size) () const<br> |
|  size\_t | [**sparseCapacity**](#function-sparsecapacity) () const<br>_@thread\_safety Internally synchronized. One atomic load._  |
|  size\_t | [**tick**](#function-tick) () <br>_Process one tick of the grace period for retired memory._  |
|  bool | [**tryClearImpl**](#function-tryclearimpl) () <br>_Clear if the array is free right now; leave it for the next call if not._  |
|  void | [**tryDefragment**](#function-trydefragment) () <br> |
|  bool | [**tryDefragmentImpl**](#function-trydefragmentimpl) () <br>_Compact if the array is free right now; leave it for the next call if not._  |
|  auto | [**writeLock**](#function-writelock-12) () const<br> |
|   | [**~SectorsArray**](#function-sectorsarray) () <br> |


## Public Static Functions

| Type | Name |
| ---: | :--- |
|  [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) \* | [**create**](#function-create) (Allocator && allocator={}) <br> |
|  FORCE\_INLINE uint32\_t | [**loadAliveAcquire**](#function-loadaliveacquire) (const uint32\_t \* p, size\_t i) noexcept<br> |
|  FORCE\_INLINE uint32\_t | [**loadAliveRelaxed**](#function-loadaliverelaxed) (const uint32\_t \* p, size\_t i) noexcept<br> |
|  FORCE\_INLINE SectorId | [**loadId**](#function-loadid) (const SectorId \* p, size\_t i) noexcept<br>_Relaxed load of a sector id. The id array is written by a writer holding only the array's write lock, which excludes other writers but not the lock-free readers, so both sides go through atomic\_ref. Declared static so nested iterator classes can call it without an enclosing instance. @thread\_safety Internally synchronized. A single load with the ordering the thread-safe build needs and none in the plain build. Given a pointer, not an index into anything it validates_  _the caller supplies both, and both come from a snapshot it already holds._ |


























## Detailed Description




**Warning:**

Structural changes to an array are not allowed while the calling thread holds a view or a pin on that same array. Relocating sectors would invalidate the iterator that is reading them, so writers wait for every pin and hold to drain  correct against other threads, and unsatisfiable against yourself: the thread blocks on a condition only it could clear. In a release build that is a hang with no diagnosis; debug builds assert instead.


Illegal while a view or pin on the same array is alive  anything that relocates sectors:
* insert / emplace / push of an id that lands anywhere but past the end
* insertBulk, and [**Registry::addComponents**](classecss_1_1Registry.md#function-addcomponents)
* defragment, clear, shrinkToFit, copy and move assignment
* erase with defragmentation [**Registry::update()**](classecss_1_1Registry.md#function-update-12) is deliberately not in that list: it attempts compaction rather than waiting for it, and leaves a busy array for the next call. Also illegal: destroying or overwriting in place the one sector you are holding a pin to.




Legal, because nothing moves:
* appending an id above every id already stored
* destroying or overwriting a sector in place, other than one you pin yourself
* anything at all on a _different_ array




A different thread doing any of this is fine and is what the waiting is for; it blocks until your view ends.


Every public member carries an @thread\_safety line. It answers whether two threads may call it on the same object at once:
* "Internally synchronized." yes.
* "Thread-confined." no, and do not try: the object belongs to one thread and holds no lock, as a std::vector does. Give each thread its own rather than a mutex.
* "Caller must ensure exclusive access." no; this one is shared, and you must supply the ordering yourself.
* "Not applicable (single-threaded build)."




and adds "; blocks" when the call waits for a pin or a hold to be released  for one sector's pins, or for the whole array to carry neither; the method says which. Only that counts as blocking here: it is state a reader controls, so a caller can be the reader it waits for. Ordinary contention for the array's mutex is not marked, because nearly every synchronized call has some. The two axes are independent: a destructor waits for in-flight readers and still must not race with new ones.


Many members take `template<bool TS = ThreadSafe>`. Leaving it defaulted is the safe choice and gives the behaviour documented on the method. Passing TS=false on a ThreadSafe array deliberately drops the lock, and then the guarantee is yours: it is only correct where exclusivity is already established  inside a body handed to exclusiveWhenUnpinned/exclusiveWhenQuiescent, or under a lock you hold yourself. Passing TS=true on a non-thread-safe array is a compile error.




**Template parameters:**


* `ThreadSafe` If true, operations are synchronized & relocation waits on pins. 
* `Allocator` Allocation policy (e.g. [**ChunksAllocator**](structecss_1_1Memory_1_1ChunksAllocator.md)). 




    
## Public Functions Documentation




### function SectorsArray [2/6]

```C++
inline ecss::Memory::SectorsArray::SectorsArray (
    const SectorsArray & other
) 
```




<hr>



### function SectorsArray [3/6]

```C++
template<bool T, typename Alloc>
inline ecss::Memory::SectorsArray::SectorsArray (
    const SectorsArray < T, Alloc > & other
) 
```




<hr>



### function SectorsArray [4/6]

```C++
inline ecss::Memory::SectorsArray::SectorsArray (
    SectorsArray && other
) noexcept
```




<hr>



### function SectorsArray [5/6]

```C++
template<bool T, typename Alloc>
inline ecss::Memory::SectorsArray::SectorsArray (
    SectorsArray < T, Alloc > && other
) noexcept
```




<hr>



### function begin 

```C++
template<bool TS>
inline Iterator ecss::Memory::SectorsArray::begin () const
```



@thread\_safety Internally synchronized. Building one takes no lock: an iterator is made entirely from lock-free snapshots. The iterator itself belongs to one thread, and it stays valid only while nothing compacts the array  which is what the view's structural hold is for. Do not carry one across a defragment. 


        

<hr>



### function beginAlive 

```C++
template<class T, bool TS>
inline IteratorAlive ecss::Memory::SectorsArray::beginAlive () const
```



@thread\_safety Internally synchronized. Building one takes no lock: an iterator is made entirely from lock-free snapshots. The iterator itself belongs to one thread, and it stays valid only while nothing compacts the array  which is what the view's structural hold is for. Do not carry one across a defragment. 


        

<hr>



### function beginRanged 

```C++
template<bool TS>
inline RangedIterator ecss::Memory::SectorsArray::beginRanged (
    const Ranges < SectorId > & ranges
) const
```



@thread\_safety Internally synchronized. Building one takes no lock: an iterator is made entirely from lock-free snapshots. The iterator itself belongs to one thread, and it stays valid only while nothing compacts the array  which is what the view's structural hold is for. Do not carry one across a defragment. 


        

<hr>



### function capacity 

```C++
template<bool TS>
inline size_t ecss::Memory::SectorsArray::capacity () const
```



@thread\_safety Internally synchronized. Takes the shared lock, briefly: it reads the chunk vector, which a concurrent growth may be reallocating, and that vector is not published through a seqlock the way the dense arrays are. 


        

<hr>



### function clear 

```C++
template<bool TS>
inline void ecss::Memory::SectorsArray::clear () 
```



@thread\_safety Internally synchronized; blocks on the whole array. Destroys every sector, no pins and no holds. A view open on another thread holds it up; a view open on this thread deadlocks.


The deferred way to ask for this from anywhere, including from inside a loop over a view, is [**clearAsync()**](classecss_1_1Memory_1_1SectorsArray.md#function-clearasync). 


        

<hr>



### function clearAsync 

_Ask for the array to be cleared at the next safe point, instead of now._ 
```C++
inline void ecss::Memory::SectorsArray::clearAsync () 
```



The deferred counterpart to [**clear()**](classecss_1_1Memory_1_1SectorsArray.md#function-clear). Clearing destroys every sector and drops the published size to zero, so a reader mid-iteration would be walking sectors that are gone  which is why [**clear()**](classecss_1_1Memory_1_1SectorsArray.md#function-clear) waits for the array to carry no pins and no holds, and why calling it from a thread that is itself iterating deadlocks.


This only records the wish and returns. [**processPendingErases()**](classecss_1_1Memory_1_1SectorsArray.md#function-processpendingerases)  which [**Registry::update()**](classecss_1_1Registry.md#function-update-12) calls for every array  performs it the first time it finds the array free. That makes it safe to ask for from anywhere, including from inside a loop over a view.




**Note:**

Asked for, not promised. An array that something is always iterating stays busy and the clear keeps waiting, quietly, for a frame where it can run. If you need it to have happened, call [**clear()**](classecss_1_1Memory_1_1SectorsArray.md#function-clear) at a point where nothing is reading. @thread\_safety Internally synchronized. One relaxed store; nothing is waited for. 




**See also:** [**clear()**](classecss_1_1Memory_1_1SectorsArray.md#function-clear), [**eraseAsync()**](classecss_1_1Memory_1_1SectorsArray.md#function-eraseasync), [**Registry::update()**](classecss_1_1Registry.md#function-update-12) 



        

<hr>



### function containsSector 

```C++
template<bool TS>
inline bool ecss::Memory::SectorsArray::containsSector (
    SectorId id
) const
```



@thread\_safety Internally synchronized. Lock-free: reads a published snapshot, takes no lock and waits for nothing. True when the call was made; another thread may destroy the entity immediately after. 


        

<hr>



### function dataAt [1/2]

```C++
inline FORCE_INLINE std::byte * ecss::Memory::SectorsArray::dataAt (
    const typename Allocator::ChunksView & chunks,
    uint32_t linearIdx
) const
```




<hr>



### function dataAt [2/2]

[_**Sector**_](namespaceecss_1_1Memory_1_1Sector.md) _data address for a linear index, read through the chunk snapshot. Loops should hoist loadChunks() and use the two-argument form instead._
```C++
inline FORCE_INLINE std::byte * ecss::Memory::SectorsArray::dataAt (
    uint32_t linearIdx
) const
```




<hr>



### function defragment 

```C++
template<bool TS>
inline void ecss::Memory::SectorsArray::defragment () 
```



@thread\_safety Internally synchronized; blocks on the whole array. Compaction moves sectors, so it waits until the array carries no pins and no holds. For the non-waiting version, which gives up if the array is busy, see [**tryDefragment()**](classecss_1_1Memory_1_1SectorsArray.md#function-trydefragment)  or [**Registry::update()**](classecss_1_1Registry.md#function-update-12), which calls it for every array. 


        

<hr>



### function emplace 

```C++
template<typename T, bool TS, class... Args>
inline T * ecss::Memory::SectorsArray::emplace (
    SectorId sectorId,
    Args &&... args
) noexcept
```



@thread\_safety Internally synchronized; blocks. An id above every id already stored is appended, and that waits for nothing. An id landing anywhere else has to shift the sectors after it, so it waits until the array carries no pins and no holds  from a thread holding a view on this array, a deadlock. Overwriting an id that is already stored waits only on that one sector.


To add from inside a loop over a view, record into an [**ecss::CommandBuffer**](classecss_1_1CommandBuffer.md) and apply it once the loop is done. 


        

<hr>



### function empty 

_@thread\_safety Internally synchronized. One atomic load._ 
```C++
template<bool TS>
inline bool ecss::Memory::SectorsArray::empty () const
```





**See also:** [**size()**](classecss_1_1Memory_1_1SectorsArray.md#function-size) 



        

<hr>



### function end 

```C++
template<bool TS>
inline Iterator ecss::Memory::SectorsArray::end () const
```



@thread\_safety Internally synchronized. Building one takes no lock: an iterator is made entirely from lock-free snapshots. The iterator itself belongs to one thread, and it stays valid only while nothing compacts the array  which is what the view's structural hold is for. Do not carry one across a defragment. 


        

<hr>



### function endAlive 

```C++
template<bool TS>
inline IteratorAlive ecss::Memory::SectorsArray::endAlive () const
```



@thread\_safety Internally synchronized. Building one takes no lock: an iterator is made entirely from lock-free snapshots. The iterator itself belongs to one thread, and it stays valid only while nothing compacts the array  which is what the view's structural hold is for. Do not carry one across a defragment. 


        

<hr>



### function endRanged 

```C++
template<bool TS>
inline RangedIterator ecss::Memory::SectorsArray::endRanged () const
```



@thread\_safety Internally synchronized. Building one takes no lock: an iterator is made entirely from lock-free snapshots. The iterator itself belongs to one thread, and it stays valid only while nothing compacts the array  which is what the view's structural hold is for. Do not carry one across a defragment. 


        

<hr>



### function erase [1/2]

```C++
template<bool TS>
inline Iterator ecss::Memory::SectorsArray::erase (
    Iterator it,
    bool defragment=false
) noexcept
```



@thread\_safety Internally synchronized; blocks on the whole array. Same as erase(idx): it moves sectors, so it waits for every pin and hold. Passing an iterator obtained from a view that is still open is exactly the deadlock case.


The deferred counterpart, safe from anywhere, is [**eraseAsync()**](classecss_1_1Memory_1_1SectorsArray.md#function-eraseasync). 


        

<hr>



### function erase [2/2]

```C++
template<bool TS>
inline void ecss::Memory::SectorsArray::erase (
    size_t beginIdx,
    size_t count=1,
    bool defragment=false
) 
```



@thread\_safety Internally synchronized; blocks on the whole array. Erasing by position closes the gap, which moves sectors, so it waits until the array carries no pins and no holds. Illegal from a thread holding a view on this array.


The deferred counterpart, safe from anywhere, is [**eraseAsync()**](classecss_1_1Memory_1_1SectorsArray.md#function-eraseasync). 


        

<hr>



### function eraseAsync 

```C++
inline void ecss::Memory::SectorsArray::eraseAsync (
    SectorId id,
    size_t count=1
) 
```



@thread\_safety Internally synchronized. Queues the ids and returns; nothing is moved and nothing is waited for. The work happens in [**processPendingErases()**](classecss_1_1Memory_1_1SectorsArray.md#function-processpendingerases), which [**Registry::update()**](classecss_1_1Registry.md#function-update-12) calls. This is the erase to use while others iterate. 


        

<hr>



### function exclusiveForInsert 

_Acquire (or reuse) the slot for_ `sectorId` _and run_`fn(linearIdx)` _on it._
```C++
template<typename Fn>
inline auto ecss::Memory::SectorsArray::exclusiveForInsert (
    SectorId sectorId,
    Fn && fn
) 
```



Appends and overwrites only need the target sector unpinned. A middle insert also shifts every following sector, so it additionally needs quiescence  but that is only discoverable under the lock, hence the two-tier retry: tryAcquireSlotImpl declines with INVALID\_IDX and the wait happens after the lock is released. @thread\_safety Internally synchronized; blocks. Two tiers, because how much it must wait for is only knowable under the lock. An append or an overwrite of an existing id proceeds immediately; an id landing in the middle needs the array quiescent, and that wait happens after the lock is dropped. 


        

<hr>



### function exclusiveWhenQuiescent 

_Run_ `fn` _under the write lock once no sector at all is pinned. Required by anything that relocates sectors (shift, defragment, clear, copy). @thread\_safety Internally synchronized; blocks. Waits for the whole array: fn runs under the write lock once nothing is pinned and nothing is held. This is the gate for everything that relocates sectors, and the one to use when fn moves anything. It re-checks after taking the lock and waits again if a reader slipped in, so a thread that keeps opening views on this array can hold it off; from the thread that holds the view itself, it never completes._
```C++
template<typename Fn>
inline auto ecss::Memory::SectorsArray::exclusiveWhenQuiescent (
    Fn && fn
) 
```




<hr>



### function exclusiveWhenUnpinned [1/2]

_Same as exclusiveWhenUnpinned(id), for a batch of in-place destroys._ 
```C++
template<typename Fn>
inline auto ecss::Memory::SectorsArray::exclusiveWhenUnpinned (
    const EntityId * begin,
    const EntityId * end,
    Fn && fn
) 
```



Does not wait for pins or holds on sectors outside ``[begin, end). A camera pin on the same array must not stall destroying unrelated entities — that was exclusiveWhenQuiescent, which is for relocation only. @thread\_safety Internally synchronized; blocks. Waits for the named sectors only, then runs fn under the write lock. As the single-id form: fn must change those sectors in place and move nothing. 


        

<hr>



### function exclusiveWhenUnpinned [2/2]

_Run_ `fn` _under the write lock once sector_`sectorId` _carries no pins. For operations confined to that one sector (in-place destroy / overwrite). @thread\_safety Internally synchronized; blocks. Waits for one sector only. Runs fn under the write lock once that sector carries no pins; pins on other sectors and holds over the array do not delay it, because fn is expected to change that sector in place and move nothing. Deadlocks if this thread pins that sector._
```C++
template<typename Fn>
inline auto ecss::Memory::SectorsArray::exclusiveWhenUnpinned (
    SectorId sectorId,
    Fn && fn
) 
```




<hr>



### function findLinearIdx 

```C++
template<bool TS>
inline size_t ecss::Memory::SectorsArray::findLinearIdx (
    SectorId sectorId
) const
```



@thread\_safety Internally synchronized. Lock-free: reads a published snapshot, takes no lock and waits for nothing. The index is a position, and a position is only stable while nothing compacts the array  hold a view or a pin if you mean to use it afterwards. 


        

<hr>



### function findSectorData 

```C++
template<bool TS>
inline std::byte * ecss::Memory::SectorsArray::findSectorData (
    SectorId id
) const
```



@thread\_safety Internally synchronized. Lock-free: reads a published snapshot, takes no lock and waits for nothing. The pointer is only good while the sector cannot move: pin it, or hold the array, if the pointer outlives the call. 


        

<hr>



### function findSlot 

_Find slot info (data pointer + linearIdx) for fast sparse lookup._ 
```C++
template<bool TS>
inline detail::SlotInfo ecss::Memory::SectorsArray::findSlot (
    SectorId id
) const
```





**Returns:**

[**SlotInfo**](structecss_1_1Memory_1_1SectorsArray_1_1SlotInfo.md) with data pointer and linear index, or INVALID\_SLOT if not found @thread\_safety Internally synchronized. Lock-free: reads a published snapshot, takes no lock and waits for nothing. Same caveat as [**findSectorData()**](classecss_1_1Memory_1_1SectorsArray.md#function-findsectordata): the slot is a position, valid only while nothing compacts. 





        

<hr>



### function getDefragmentationRatio 

_@thread\_safety Internally synchronized. Two relaxed loads; the ratio may be a hair stale._ 
```C++
template<bool TS>
inline auto ecss::Memory::SectorsArray::getDefragmentationRatio () const
```




<hr>



### function getDefragmentationSize 

_@thread\_safety Internally synchronized. One relaxed load._ 
```C++
template<bool TS>
inline auto ecss::Memory::SectorsArray::getDefragmentationSize () const
```




<hr>



### function getId 

```C++
template<bool TS>
inline SectorId ecss::Memory::SectorsArray::getId (
    size_t linearIdx
) const
```



@thread\_safety Internally synchronized. Lock-free: reads a published snapshot, takes no lock and waits for nothing. 


        

<hr>



### function getIsAlive 

```C++
template<bool TS>
inline uint32_t ecss::Memory::SectorsArray::getIsAlive (
    SectorId id
) const
```



@thread\_safety Internally synchronized. Lock-free: reads a published snapshot, takes no lock and waits for nothing. 


        

<hr>



### function getIsAliveRef 

```C++
template<bool TS>
inline uint32_t & ecss::Memory::SectorsArray::getIsAliveRef (
    size_t linearIdx
) 
```



@thread\_safety Caller must ensure exclusive access. Hands out a reference into the live liveness array, so it is only sound where the array cannot be compacted and nobody else writes that word  inside a body given to exclusiveWhenUnpinned, or under a lock you hold. Prefer [**loadAliveWord()**](classecss_1_1Memory_1_1SectorsArray.md#function-loadaliveword) to read one. 


        

<hr>



### function getLayout 

_@thread\_safety Internally synchronized. The layout is fixed at creation and never changes._ 
```C++
inline FORCE_INLINE const SectorLayoutMeta * ecss::Memory::SectorsArray::getLayout () const
```




<hr>



### function getLayoutData 

```C++
template<typename T>
inline FORCE_INLINE const LayoutData & ecss::Memory::SectorsArray::getLayoutData () const
```



@thread\_safety Internally synchronized. The layout is fixed when the array is created and never changes, so this needs no lock and cannot go stale. 


        

<hr>



### function hasPendingClear 

_Whether a_ [_**clearAsync()**_](classecss_1_1Memory_1_1SectorsArray.md#function-clearasync) _is still waiting for a frame it can run in. @thread\_safety Internally synchronized. One atomic load._
```C++
inline bool ecss::Memory::SectorsArray::hasPendingClear () noexcept const
```




<hr>



### function holdStructure 

_Block compaction of this array for as long as the returned object lives. Use this, not a pin, when the point is "do not move things", not "leave this sector". @thread\_safety Internally synchronized. Cheaper than a pin and per thread rather than per sector. While one lives no sector in this array may be relocated_  _so every whole-array writer waits for it. This is what a view holds for its lifetime. Hold it for as short a time as the reading takes._
```C++
inline StructuralHold ecss::Memory::SectorsArray::holdStructure () const
```




<hr>



### function incDefragmentSize 

_@thread\_safety Internally synchronized. One relaxed increment of the dead-slot counter._ 
```C++
inline void ecss::Memory::SectorsArray::incDefragmentSize (
    uint32_t count=1
) 
```




<hr>



### function insert 

```C++
template<typename T, bool TS>
inline std::remove_cvref_t< T > * ecss::Memory::SectorsArray::insert (
    SectorId sectorId,
    T && data
) noexcept
```



@thread\_safety Internally synchronized; blocks. An id above every id already stored is appended, and that waits for nothing. An id landing anywhere else has to shift the sectors after it, so it waits until the array carries no pins and no holds  from a thread holding a view on this array, a deadlock. Overwriting an id that is already stored waits only on that one sector.


To add from inside a loop over a view, record into an [**ecss::CommandBuffer**](classecss_1_1CommandBuffer.md) and apply it once the loop is done. 


        

<hr>



### function insertBulk 

_Bulk insert. Each \*it yields a pair-like {SectorId, C}._ 
```C++
template<typename C, typename It, bool TS>
inline void ecss::Memory::SectorsArray::insertBulk (
    It first,
    It last
) noexcept
```



Ids may arrive in any order and may fall anywhere in the range already stored; an id that is already present is overwritten. Reserves once and publishes the dense view once, skipping the per-element existence check / insert-position search / view publish that addComponent() pays. In the TS build it also batches the write lock, the pin wait and the dense-view publish across the whole range.


Prefer this over a loop of addComponent() whenever the ids are not ascending. Adding M components one at a time costs O(M\*N): each middle insert shifts the tail and rewrites the sparse entry of every sector it shifted past. This sorts the batch once and merges it in a single pass, so each sector moves at most once. @thread\_safety Internally synchronized; blocks. A batch entirely above what is stored is appended in one pass and waits for nothing. Otherwise the batch is merged into place, which moves existing sectors, and that waits until the array carries no pins and no holds. Merging is linear in the batch, so this is the cheap way to add many ids at once  one wait instead of one per id. 


        

<hr>



### function isPacked 

_Check if array has no dead slots (defragmentSize == 0) @thread\_safety Internally synchronized. One relaxed load. True when no dead slot is waiting to be compacted away._ 
```C++
template<bool TS>
inline bool ecss::Memory::SectorsArray::isPacked () const
```




<hr>



### function loadAliveWord 

_Thread-safe alive-word read that routes through the seqlock snapshot. Unlike getIsAliveRef, this never dereferences the live std::vector, so it is safe against concurrent push\_back reallocation_  _the vector's internal \_M\_start field would otherwise race with the reader's non-atomic read of it. Old isAlive buffers remain valid because_[_**RetireAllocator**_](structecss_1_1Memory_1_1RetireAllocator.md) _defers their free. Returns 0 if linearIdx is outside the snapshot (newly allocated slot not yet published), which callers treat as "not alive". @thread\_safety Internally synchronized. Lock-free: reads a published snapshot, takes no lock and waits for nothing. Bounds-checked: an index past the published size reads as not alive rather than out of bounds._
```C++
template<bool TS>
inline FORCE_INLINE uint32_t ecss::Memory::SectorsArray::loadAliveWord (
    size_t linearIdx
) noexcept const
```




<hr>



### function loadChunks 

```C++
inline FORCE_INLINE auto ecss::Memory::SectorsArray::loadChunks () const
```




<hr>



### function needDefragment 

_@thread\_safety Internally synchronized. Relaxed loads against the threshold._ 
```C++
template<bool TS>
inline bool ecss::Memory::SectorsArray::needDefragment () const
```




<hr>



### function operator= 

```C++
inline SectorsArray & ecss::Memory::SectorsArray::operator= (
    const SectorsArray & other
) 
```




<hr>



### function operator= 

```C++
template<bool T, typename Alloc>
inline SectorsArray & ecss::Memory::SectorsArray::operator= (
    const SectorsArray < T, Alloc > & other
) 
```




<hr>



### function operator= 

```C++
inline SectorsArray & ecss::Memory::SectorsArray::operator= (
    SectorsArray && other
) noexcept
```




<hr>



### function operator= 

```C++
template<bool T, typename Alloc>
inline SectorsArray & ecss::Memory::SectorsArray::operator= (
    SectorsArray < T, Alloc > && other
) noexcept
```




<hr>



### function pinBackSector 

```C++
template<bool TS>
inline PinnedSector ecss::Memory::SectorsArray::pinBackSector () const
```



@thread\_safety Internally synchronized. As [**pinSector()**](classecss_1_1Memory_1_1SectorsArray.md#function-pinsector), for whichever sector is last at the moment of the call. 


        

<hr>



### function pinSector 

```C++
template<bool TS>
inline PinnedSector ecss::Memory::SectorsArray::pinSector (
    SectorId id
) const
```



@thread\_safety Internally synchronized. Takes no lock. While the pin lives, that sector will not be moved, destroyed or reused  which is also what makes another thread destroying this entity wait. Do not pin a sector and then destroy or overwrite it from the same thread. 


        

<hr>



### function pinSectorAt 

```C++
template<bool TS>
inline PinnedSector ecss::Memory::SectorsArray::pinSectorAt (
    size_t idx
) const
```



@thread\_safety Internally synchronized. As [**pinSector()**](classecss_1_1Memory_1_1SectorsArray.md#function-pinsector), addressed by dense index. The index is only meaningful while the array is not being compacted, so this is for callers already iterating. 


        

<hr>



### function processPendingErases 

```C++
template<bool Lock>
inline void ecss::Memory::SectorsArray::processPendingErases (
    bool withDefragment=true
) 
```



@thread\_safety Internally synchronized. Nothing here waits. The queued erases destroy sectors in place, and compaction is attempted rather than awaited: an array something is iterating right now is left for the next call. This is what makes [**Registry::update()**](classecss_1_1Registry.md#function-update-12) safe to call from inside a loop over a view. 


        

<hr>



### function push 

```C++
template<typename T, bool TS, class... Args>
inline T * ecss::Memory::SectorsArray::push (
    SectorId sectorId,
    Args &&... args
) noexcept
```



@thread\_safety Internally synchronized; blocks. An id above every id already stored is appended, and that waits for nothing. An id landing anywhere else has to shift the sectors after it, so it waits until the array carries no pins and no holds  from a thread holding a view on this array, a deadlock. Overwriting an id that is already stored waits only on that one sector.


To add from inside a loop over a view, record into an [**ecss::CommandBuffer**](classecss_1_1CommandBuffer.md) and apply it once the loop is done. 


        

<hr>



### function readLock [1/2]

```C++
inline auto ecss::Memory::SectorsArray::readLock () const
```



@thread\_safety Caller must ensure exclusive access  to the decision, not the lock. Hands out the raw shared lock, so it waits for the mutex like any lock does; that is not the "blocks" above, which means waiting on a pin or a hold. What you do under it is yours to get right. Note it guards the chunk table and the sparse map, not the sectors: holding it does not stop compaction, only a structural hold or a pin does. 


        

<hr>



### function reserve 

```C++
template<bool TS>
inline void ecss::Memory::SectorsArray::reserve (
    uint32_t newCapacity
) 
```



@thread\_safety Internally synchronized. Takes the write lock and adds chunks. Nothing is moved, so it does not wait for pins or holds  growing is always legal, even while others iterate. Doing it up front keeps it off the frame. 


        

<hr>



### function setDefragmentThreshold 

```C++
template<bool TS>
inline void ecss::Memory::SectorsArray::setDefragmentThreshold (
    float threshold
) 
```



@thread\_safety Internally synchronized. One relaxed store. Changes when compaction is asked for, never compaction itself. 


        

<hr>



### function shrinkToFit 

```C++
template<bool TS>
inline void ecss::Memory::SectorsArray::shrinkToFit () 
```



@thread\_safety Internally synchronized. Takes the write lock and returns the chunks past the end. It moves no sector, so it does not wait for pins or holds. The chunks are retired rather than freed, so a reader still holding a pointer into one is safe until the grace period expires. 


        

<hr>



### function size 

```C++
template<bool TS>
inline size_t ecss::Memory::SectorsArray::size () const
```



@thread\_safety Internally synchronized. One atomic load. A count, not a promise: another thread may add or destroy before you act on it. 


        

<hr>



### function sparseCapacity 

_@thread\_safety Internally synchronized. One atomic load._ 
```C++
template<bool TS>
inline size_t ecss::Memory::SectorsArray::sparseCapacity () const
```




<hr>



### function tick 

_Process one tick of the grace period for retired memory._ 
```C++
inline size_t ecss::Memory::SectorsArray::tick () 
```



Call this once per frame/update cycle. Memory blocks that have waited the full grace period (default 3 ticks) will be freed.


This is safe to call while iterators may be active - only sufficiently old memory (older than grace period) will be freed.




**Note:**

In non-thread-safe mode there is no grace period, so memory is already freed as it is released and this is a no-op returning zero. It stays callable so that a frame loop does not have to branch on the mode.




**Returns:**

Number of memory blocks freed this tick @thread\_safety Internally synchronized. Advances the grace period and frees what has expired. Takes a bin's mutex only when that bin holds something. 





        

<hr>



### function tryClearImpl 

_Clear if the array is free right now; leave it for the next call if not._ 
```C++
inline bool ecss::Memory::SectorsArray::tryClearImpl () 
```





**Returns:**

true if the clear ran. @thread\_safety Internally synchronized. Gives up rather than waiting, so it is safe to call from a thread holding a view  it simply does nothing that time. 





        

<hr>



### function tryDefragment 

```C++
template<bool TS>
inline void ecss::Memory::SectorsArray::tryDefragment () 
```



@thread\_safety Internally synchronized. The non-blocking counterpart to [**defragment()**](classecss_1_1Memory_1_1SectorsArray.md#function-defragment): if anything is pinned or held it returns without compacting, and the work is left for a later call. Safe to call on a schedule from a busy frame. 


        

<hr>



### function tryDefragmentImpl 

_Compact if the array is free right now; leave it for the next call if not._ 
```C++
inline bool ecss::Memory::SectorsArray::tryDefragmentImpl () 
```





**Returns:**

true if compaction ran. @thread\_safety Internally synchronized. Gives up instead of waiting: if anything is pinned or held it returns false and compacts nothing. 




**Returns:**

true if compaction ran. 





        

<hr>



### function writeLock [1/2]

```C++
inline auto ecss::Memory::SectorsArray::writeLock () const
```



@thread\_safety Caller must ensure exclusive access  to the decision, not the lock. Hands out the raw unique lock, so it waits for the mutex and for readers holding it shared; that is not the "blocks" above, which means waiting on a pin or a hold. Taking it is not enough to relocate sectors: readers pin and hold without any lock at all, so anything that moves a sector must establish quiescence first. Use [**exclusiveWhenQuiescent()**](classecss_1_1Memory_1_1SectorsArray.md#function-exclusivewhenquiescent) rather than this. 


        

<hr>



### function ~SectorsArray 

```C++
inline ecss::Memory::SectorsArray::~SectorsArray () 
```



@thread\_safety Internally synchronized; blocks. The destructor clears, and clearing waits for every pin and hold to drain, so a view open on another thread stalls it for as long as that view lives  in-flight readers are waited for.


That is a synchronization guarantee, not a lifetime one: nothing may start reading the array once destruction has begun. 


        

<hr>
## Public Static Functions Documentation




### function create 

```C++
template<typename... Types>
static inline SectorsArray * ecss::Memory::SectorsArray::create (
    Allocator && allocator={}
) 
```



@thread\_safety Internally synchronized, vacuously: nothing can name the array until this returns, so there is nothing to race with. 


        

<hr>



### function loadAliveAcquire 

```C++
static inline FORCE_INLINE uint32_t ecss::Memory::SectorsArray::loadAliveAcquire (
    const uint32_t * p,
    size_t i
) noexcept
```



@thread\_safety Internally synchronized. A single load with the ordering the thread-safe build needs and none in the plain build. Given a pointer, not an index into anything it validates  the caller supplies both, and both come from a snapshot it already holds. 


        

<hr>



### function loadAliveRelaxed 

```C++
static inline FORCE_INLINE uint32_t ecss::Memory::SectorsArray::loadAliveRelaxed (
    const uint32_t * p,
    size_t i
) noexcept
```



@thread\_safety Internally synchronized. A single load with the ordering the thread-safe build needs and none in the plain build. Given a pointer, not an index into anything it validates  the caller supplies both, and both come from a snapshot it already holds. 


        

<hr>



### function loadId 

_Relaxed load of a sector id. The id array is written by a writer holding only the array's write lock, which excludes other writers but not the lock-free readers, so both sides go through atomic\_ref. Declared static so nested iterator classes can call it without an enclosing instance. @thread\_safety Internally synchronized. A single load with the ordering the thread-safe build needs and none in the plain build. Given a pointer, not an index into anything it validates_  _the caller supplies both, and both come from a snapshot it already holds._
```C++
static inline FORCE_INLINE SectorId ecss::Memory::SectorsArray::loadId (
    const SectorId * p,
    size_t i
) noexcept
```




<hr>## Friends Documentation





### friend ArraysView 

```C++
template<bool TS, typename Alloc, bool Ranged, typename T, typename ... ComponentTypes>
class ecss::Memory::SectorsArray::ArraysView (
    ecss::ArraysView
) 
```




<hr>



### friend Registry 

```C++
template<bool, typename>
class ecss::Memory::SectorsArray::Registry (
    ecss::Registry
) 
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/memory/SectorsArray.h`

