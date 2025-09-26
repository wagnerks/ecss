

# Class ecss::Memory::SectorsArray

**template &lt;bool ThreadSafe, typename Allocator&gt;**



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md) **>** [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md)



_Container managing sectors of one (or grouped) component layouts with optional thread-safety._ [More...](#detailed-description)

* `#include <SectorsArray.h>`















## Classes

| Type | Name |
| ---: | :--- |
| class | [**Iterator**](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) <br>_Forward iterator over every sector (alive or dead)._  |
| class | [**IteratorAlive**](classecss_1_1Memory_1_1SectorsArray_1_1IteratorAlive.md) <br>_Forward iterator skipping sectors where a specific component (type mask) isn't alive._  |
| class | [**RangedIterator**](classecss_1_1Memory_1_1SectorsArray_1_1RangedIterator.md) <br>[_**Iterator**_](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) _over specified index ranges (includes dead sectors)._ |






















## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**SectorsArray**](#function-sectorsarray-26) (const [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) & other) <br>_Same-type copy constructor._  |
|   | [**SectorsArray**](#function-sectorsarray-36) (const [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md)&lt; T, Alloc &gt; & other) <br>_Cross-template copy constructor (allows copying between differently configured_ [_**SectorsArray**_](classecss_1_1Memory_1_1SectorsArray.md) _types if layout compatible)._ |
|   | [**SectorsArray**](#function-sectorsarray-46) ([**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) && other) noexcept<br>_Same-type move constructor._  |
|   | [**SectorsArray**](#function-sectorsarray-56) ([**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md)&lt; T, Alloc &gt; && other) noexcept<br>_Cross-template move constructor._  |
|  [**Sector**](structecss_1_1Memory_1_1Sector.md) \* | [**at**](#function-at) (size\_t sectorIndex) const<br> |
|  [**Iterator**](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) | [**begin**](#function-begin) () const<br> |
|  [**IteratorAlive**](classecss_1_1Memory_1_1SectorsArray_1_1IteratorAlive.md) | [**beginAlive**](#function-beginalive-12) () const<br> |
|  [**IteratorAlive**](classecss_1_1Memory_1_1SectorsArray_1_1IteratorAlive.md) | [**beginAlive**](#function-beginalive-22) (const Ranges&lt; SectorId &gt; & ranges) const<br> |
|  [**RangedIterator**](classecss_1_1Memory_1_1SectorsArray_1_1RangedIterator.md) | [**beginRanged**](#function-beginranged) (const Ranges&lt; SectorId &gt; & ranges) const<br> |
|  size\_t | [**capacity**](#function-capacity) () const<br> |
|  void | [**clear**](#function-clear) () <br> |
|  bool | [**containsSector**](#function-containssector) (SectorId id) const<br> |
|  void | [**defragment**](#function-defragment) () <br>_Defragment by collapsing dead (erased) sectors; preserves relative order of alive ones._  |
|  T \* | [**emplace**](#function-emplace) (SectorId sectorId, Args &&... args) <br>_Construct in-place a member of type T._  |
|  bool | [**empty**](#function-empty) () const<br> |
|  [**Iterator**](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) | [**end**](#function-end) () const<br> |
|  [**IteratorAlive**](classecss_1_1Memory_1_1SectorsArray_1_1IteratorAlive.md) | [**endAlive**](#function-endalive) () const<br> |
|  [**RangedIterator**](classecss_1_1Memory_1_1SectorsArray_1_1RangedIterator.md) | [**endRanged**](#function-endranged) () const<br> |
|  [**Iterator**](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) | [**erase**](#function-erase-13) ([**Iterator**](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) first, [**Iterator**](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) last, bool defragment=false) noexcept<br>_Erase range [first, last)._  |
|  [**Iterator**](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) | [**erase**](#function-erase-23) ([**Iterator**](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) it, bool defragment=false) noexcept<br>_Erase a single sector at iterator position._  |
|  void | [**erase**](#function-erase-33) (size\_t beginIdx, size\_t count=1, bool defragment=false) <br>_Erase sectors by linear index range (optionally defragment immediately)._  |
|  void | [**eraseAsync**](#function-eraseasync) (SectorId id, size\_t count=1) <br>_Queue asynchronous erase by id (actual removal deferred)._  |
|  void | [**erase\_if**](#function-erase_if) ([**Iterator**](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) first, [**Iterator**](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) last, Func && func, bool defragment=false) noexcept<br>_Conditionally erase sectors in [first,last), invoking predicate on each Sector\*._  |
|  size\_t | [**findRightNearestSectorIndex**](#function-findrightnearestsectorindex) (SectorId sectorId) const<br>_Find the first linear index whose sector id &gt;= sectorId._  |
|  [**Sector**](structecss_1_1Memory_1_1Sector.md) \* | [**findSector**](#function-findsector) (SectorId id) const<br> |
|  auto | [**getDefragmentationRatio**](#function-getdefragmentationratio) () const<br> |
|  auto | [**getDefragmentationSize**](#function-getdefragmentationsize) () const<br> |
|  FORCE\_INLINE SectorLayoutMeta \* | [**getLayout**](#function-getlayout) () const<br> |
|  FORCE\_INLINE const [**LayoutData**](structecss_1_1Memory_1_1LayoutData.md) & | [**getLayoutData**](#function-getlayoutdata) () const<br>_Access per-type layout metadata (offset, alive mask, etc.)._  |
|  [**Sector**](structecss_1_1Memory_1_1Sector.md) \* | [**getSector**](#function-getsector) (SectorId id) const<br> |
|  size\_t | [**getSectorIndex**](#function-getsectorindex-12) (SectorId id) const<br> |
|  size\_t | [**getSectorIndex**](#function-getsectorindex-22) ([**Sector**](structecss_1_1Memory_1_1Sector.md) \* sector) const<br> |
|  void | [**incDefragmentSize**](#function-incdefragmentsize) (uint32\_t count=1) <br>_Increment deferred defragment bytes counter._  |
|  std::remove\_cvref\_t&lt; T &gt; \* | [**insert**](#function-insert) (SectorId sectorId, T && data) <br>_Insert / overwrite a member (or whole_ [_**Sector**_](structecss_1_1Memory_1_1Sector.md) _) at sectorId._ |
|  bool | [**needDefragment**](#function-needdefragment) () const<br> |
|  [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) & | [**operator=**](#function-operator) (const [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) & other) <br> |
|  [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) & | [**operator=**](#function-operator_1) (const [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md)&lt; T, Alloc &gt; & other) <br> |
|  [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) & | [**operator=**](#function-operator_2) ([**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) && other) noexcept<br> |
|  [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) & | [**operator=**](#function-operator_3) ([**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md)&lt; T, Alloc &gt; && other) noexcept<br> |
|  [**PinnedSector**](structecss_1_1Memory_1_1PinnedSector.md) | [**pinBackSector**](#function-pinbacksector) () const<br>_Pin last sector (by linear order)._  |
|  [**PinnedSector**](structecss_1_1Memory_1_1PinnedSector.md) | [**pinSector**](#function-pinsector-12) ([**Sector**](structecss_1_1Memory_1_1Sector.md) \* sector) const<br>_Pin a specific sector pointer (if not null)._  |
|  [**PinnedSector**](structecss_1_1Memory_1_1PinnedSector.md) | [**pinSector**](#function-pinsector-22) (SectorId id) const<br>_Pin sector by id if found._  |
|  [**PinnedSector**](structecss_1_1Memory_1_1PinnedSector.md) | [**pinSectorAt**](#function-pinsectorat) (size\_t idx) const<br>_Pin sector at a given linear index._  |
|  void | [**processPendingErases**](#function-processpendingerases) (bool withDefragment=true) <br>_Drain deferred erase queue and optionally defragment._  |
|  T \* | [**push**](#function-push) (SectorId sectorId, Args &&... args) <br>_Convenience insertion: if single T argument =&gt;_ [_**insert()**_](classecss_1_1Memory_1_1SectorsArray.md#function-insert) _; else_[_**emplace()**_](classecss_1_1Memory_1_1SectorsArray.md#function-emplace) _._ |
|  void | [**reserve**](#function-reserve) (uint32\_t newCapacity) <br> |
|  size\_t | [**sectorsMapCapacity**](#function-sectorsmapcapacity) () const<br> |
|  void | [**setDefragmentThreshold**](#function-setdefragmentthreshold) (float threshold) <br>_Set ratio threshold (0..1) above which needDefragment()==true._  |
|  void | [**shrinkToFit**](#function-shrinktofit) () <br> |
|  size\_t | [**size**](#function-size) () const<br> |
|  void | [**tryDefragment**](#function-trydefragment) () <br>_Attempt a defragment only if array not logically pinned (non-blocking attempt)._  |
|   | [**~SectorsArray**](#function-sectorsarray) () <br>_Destructor clears all sectors and releases underlying memory._  |


## Public Static Functions

| Type | Name |
| ---: | :--- |
|  [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) \* | [**create**](#function-create) (Allocator && allocator={}) <br>_Factory to allocate a_ [_**SectorsArray**_](classecss_1_1Memory_1_1SectorsArray.md) _for the unique component type set_`Types` _._ |


























## Detailed Description




**Template parameters:**


* `ThreadSafe` If true, public calls are internally synchronized & relocation waits on pins. 
* `Allocator` Allocation policy (e.g. ChunksAllocator).

Main features:
* [**Sector**](structecss_1_1Memory_1_1Sector.md) acquisition by id (insertion keeps sectors ordered by id).
* Per-member (component) construction, move, copy, overwrite.
* Iterators for full scan, alive-only, ranged, and ranged-alive traversal.
* Deferred erasure via [**eraseAsync()**](classecss_1_1Memory_1_1SectorsArray.md#function-eraseasync) drained by [**processPendingErases()**](classecss_1_1Memory_1_1SectorsArray.md#function-processpendingerases).
* Defragmentation collapses dead (erased) holes to the left while preserving order stability.




Thread-safe mode guarantees:
* No sector memory relocation while pinned (pin counters + waiting writers).
* Safe concurrent readers & single writer semantics on container-level operations.






**Note:**

Most public template methods accept an optional TS template parameter (defaulting to the class ThreadSafe flag) so internal code can bypass extra locking when it already holds locks. 





    
## Public Functions Documentation




### function SectorsArray [2/6]

_Same-type copy constructor._ 
```C++
inline ecss::Memory::SectorsArray::SectorsArray (
    const SectorsArray & other
) 
```




<hr>



### function SectorsArray [3/6]

_Cross-template copy constructor (allows copying between differently configured_ [_**SectorsArray**_](classecss_1_1Memory_1_1SectorsArray.md) _types if layout compatible)._
```C++
template<bool T, typename Alloc>
inline ecss::Memory::SectorsArray::SectorsArray (
    const SectorsArray < T, Alloc > & other
) 
```





**Template parameters:**


* `T` Other ThreadSafe flag. 
* `Alloc` Other allocator type. 




        

<hr>



### function SectorsArray [4/6]

_Same-type move constructor._ 
```C++
inline ecss::Memory::SectorsArray::SectorsArray (
    SectorsArray && other
) noexcept
```




<hr>



### function SectorsArray [5/6]

_Cross-template move constructor._ 
```C++
template<bool T, typename Alloc>
inline ecss::Memory::SectorsArray::SectorsArray (
    SectorsArray < T, Alloc > && other
) noexcept
```




<hr>



### function at 

```C++
template<bool TS>
inline Sector * ecss::Memory::SectorsArray::at (
    size_t sectorIndex
) const
```




<hr>



### function begin 

```C++
template<bool TS>
inline Iterator ecss::Memory::SectorsArray::begin () const
```




<hr>



### function beginAlive [1/2]

```C++
template<class T, bool TS>
inline IteratorAlive ecss::Memory::SectorsArray::beginAlive () const
```




<hr>



### function beginAlive [2/2]

```C++
template<class T, bool TS>
inline IteratorAlive ecss::Memory::SectorsArray::beginAlive (
    const Ranges< SectorId > & ranges
) const
```




<hr>



### function beginRanged 

```C++
template<bool TS>
inline RangedIterator ecss::Memory::SectorsArray::beginRanged (
    const Ranges< SectorId > & ranges
) const
```




<hr>



### function capacity 

```C++
template<bool TS>
inline size_t ecss::Memory::SectorsArray::capacity () const
```




<hr>



### function clear 

```C++
template<bool TS>
inline void ecss::Memory::SectorsArray::clear () 
```




<hr>



### function containsSector 

```C++
template<bool TS>
inline bool ecss::Memory::SectorsArray::containsSector (
    SectorId id
) const
```




<hr>



### function defragment 

_Defragment by collapsing dead (erased) sectors; preserves relative order of alive ones._ 
```C++
template<bool TS>
inline void ecss::Memory::SectorsArray::defragment () 
```





**Note:**

Blocks until all pins are released (ThreadSafe build). 





        

<hr>



### function emplace 

_Construct in-place a member of type T._ 
```C++
template<typename T, bool TS, class... Args>
inline T * ecss::Memory::SectorsArray::emplace (
    SectorId sectorId,
    Args &&... args
) 
```





**Template parameters:**


* `T` Component/member type. 



**Parameters:**


* `sectorId` [**Sector**](structecss_1_1Memory_1_1Sector.md) id. 
* `args` Constructor arguments. 



**Returns:**

Pointer to newly constructed member. 





        

<hr>



### function empty 

```C++
template<bool TS>
inline bool ecss::Memory::SectorsArray::empty () const
```




<hr>



### function end 

```C++
template<bool TS>
inline Iterator ecss::Memory::SectorsArray::end () const
```




<hr>



### function endAlive 

```C++
template<bool TS>
inline IteratorAlive ecss::Memory::SectorsArray::endAlive () const
```




<hr>



### function endRanged 

```C++
template<bool TS>
inline RangedIterator ecss::Memory::SectorsArray::endRanged () const
```




<hr>



### function erase [1/3]

_Erase range [first, last)._ 
```C++
template<bool TS>
inline Iterator ecss::Memory::SectorsArray::erase (
    Iterator first,
    Iterator last,
    bool defragment=false
) noexcept
```





**Parameters:**


* `first` Begin iterator. 
* `last` End iterator. 
* `defragment` If true, compact storage after removal. 



**Returns:**

[**Iterator**](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) at the starting position. 





        

<hr>



### function erase [2/3]

_Erase a single sector at iterator position._ 
```C++
template<bool TS>
inline Iterator ecss::Memory::SectorsArray::erase (
    Iterator it,
    bool defragment=false
) noexcept
```





**Parameters:**


* `it` [**Iterator**](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) referencing the sector. 
* `defragment` If true, compact storage afterwards. 



**Returns:**

[**Iterator**](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) positioned at erased slot (or unchanged if invalid). 





        

<hr>



### function erase [3/3]

_Erase sectors by linear index range (optionally defragment immediately)._ 
```C++
template<bool TS>
inline void ecss::Memory::SectorsArray::erase (
    size_t beginIdx,
    size_t count=1,
    bool defragment=false
) 
```





**Parameters:**


* `beginIdx` First index. 
* `count` Number of sectors to erase (&gt;=1). 
* `defragment` If true, compacts immediately (costly). 



**Template parameters:**


* `TS` Allows skipping internal locking when false. 




        

<hr>



### function eraseAsync 

_Queue asynchronous erase by id (actual removal deferred)._ 
```C++
inline void ecss::Memory::SectorsArray::eraseAsync (
    SectorId id,
    size_t count=1
) 
```





**Parameters:**


* `id` First sector id. 
* `count` Number of consecutive ids to queue. 



**Note:**

Removal occurs in [**processPendingErases()**](classecss_1_1Memory_1_1SectorsArray.md#function-processpendingerases); pinned sectors remain until unpinned. 





        

<hr>



### function erase\_if 

_Conditionally erase sectors in [first,last), invoking predicate on each Sector\*._ 
```C++
template<typename Func, bool TS>
inline void ecss::Memory::SectorsArray::erase_if (
    Iterator first,
    Iterator last,
    Func && func,
    bool defragment=false
) noexcept
```





**Template parameters:**


* `Func` Predicate (Sector\*) returning bool; if true sector is destroyed. 



**Parameters:**


* `first` Begin iterator. 
* `last` End iterator (half-open). 
* `func` Predicate functor. 
* `defragment` If true, triggers defragment at end (if something erased). 




        

<hr>



### function findRightNearestSectorIndex 

_Find the first linear index whose sector id &gt;= sectorId._ 
```C++
template<bool TS>
inline size_t ecss::Memory::SectorsArray::findRightNearestSectorIndex (
    SectorId sectorId
) const
```





**Returns:**

Linear index or size() if none. 





        

<hr>



### function findSector 

```C++
template<bool TS>
inline Sector * ecss::Memory::SectorsArray::findSector (
    SectorId id
) const
```




<hr>



### function getDefragmentationRatio 

```C++
template<bool TS>
inline auto ecss::Memory::SectorsArray::getDefragmentationRatio () const
```




<hr>



### function getDefragmentationSize 

```C++
template<bool TS>
inline auto ecss::Memory::SectorsArray::getDefragmentationSize () const
```




<hr>



### function getLayout 

```C++
inline FORCE_INLINE SectorLayoutMeta * ecss::Memory::SectorsArray::getLayout () const
```





**Returns:**

The sector layout metadata used by this array. 





        

<hr>



### function getLayoutData 

_Access per-type layout metadata (offset, alive mask, etc.)._ 
```C++
template<typename T>
inline FORCE_INLINE const LayoutData & ecss::Memory::SectorsArray::getLayoutData () const
```




<hr>



### function getSector 

```C++
template<bool TS>
inline Sector * ecss::Memory::SectorsArray::getSector (
    SectorId id
) const
```




<hr>



### function getSectorIndex [1/2]

```C++
template<bool TS>
inline size_t ecss::Memory::SectorsArray::getSectorIndex (
    SectorId id
) const
```




<hr>



### function getSectorIndex [2/2]

```C++
template<bool TS>
inline size_t ecss::Memory::SectorsArray::getSectorIndex (
    Sector * sector
) const
```




<hr>



### function incDefragmentSize 

_Increment deferred defragment bytes counter._ 
```C++
inline void ecss::Memory::SectorsArray::incDefragmentSize (
    uint32_t count=1
) 
```




<hr>



### function insert 

_Insert / overwrite a member (or whole_ [_**Sector**_](structecss_1_1Memory_1_1Sector.md) _) at sectorId._
```C++
template<typename T, bool TS>
inline std::remove_cvref_t< T > * ecss::Memory::SectorsArray::insert (
    SectorId sectorId,
    T && data
) 
```





**Template parameters:**


* `T` Member type or [**Sector**](structecss_1_1Memory_1_1Sector.md) (for whole copy/move). 



**Parameters:**


* `sectorId` [**Sector**](structecss_1_1Memory_1_1Sector.md) id (allocated if missing). 
* `data` Source object (moved if rvalue). 



**Returns:**

Pointer to inserted member (or sector pointer if T == [**Sector**](structecss_1_1Memory_1_1Sector.md)). 





        

<hr>



### function needDefragment 

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

_Pin last sector (by linear order)._ 
```C++
template<bool TS>
inline PinnedSector ecss::Memory::SectorsArray::pinBackSector () const
```





**Template parameters:**


* `TS` If false, skip locking. 



**Returns:**

Last sector pinned or empty if array empty. 





        

<hr>



### function pinSector [1/2]

_Pin a specific sector pointer (if not null)._ 
```C++
template<bool TS>
inline PinnedSector ecss::Memory::SectorsArray::pinSector (
    Sector * sector
) const
```





**Template parameters:**


* `TS` If false, skip acquiring a shared lock (caller must ensure safety). 



**Parameters:**


* `sector` [**Sector**](structecss_1_1Memory_1_1Sector.md) pointer (maybe null). 



**Returns:**

[**PinnedSector**](structecss_1_1Memory_1_1PinnedSector.md) (empty if sector==nullptr). 





        

<hr>



### function pinSector [2/2]

_Pin sector by id if found._ 
```C++
template<bool TS>
inline PinnedSector ecss::Memory::SectorsArray::pinSector (
    SectorId id
) const
```





**Template parameters:**


* `TS` If false, expect external synchronization. 



**Parameters:**


* `id` [**Sector**](structecss_1_1Memory_1_1Sector.md) id. 



**Returns:**

[**PinnedSector**](structecss_1_1Memory_1_1PinnedSector.md) or empty if not present. 





        

<hr>



### function pinSectorAt 

_Pin sector at a given linear index._ 
```C++
template<bool TS>
inline PinnedSector ecss::Memory::SectorsArray::pinSectorAt (
    size_t idx
) const
```





**Template parameters:**


* `TS` If false, skip locking. 



**Parameters:**


* `idx` Linear index (0..size()). 



**Returns:**

[**PinnedSector**](structecss_1_1Memory_1_1PinnedSector.md) (empty if out-of-range). 





        

<hr>



### function processPendingErases 

_Drain deferred erase queue and optionally defragment._ 
```C++
template<bool Lock>
inline void ecss::Memory::SectorsArray::processPendingErases (
    bool withDefragment=true
) 
```





**Parameters:**


* `withDefragment` If true and ratio above threshold, compacts after processing. 



**Template parameters:**


* `Lock` If true acquire internal lock (set false if caller already holds lock). 



**Note:**

Old retired buffers freed at end (ThreadSafe only). 





        

<hr>



### function push 

_Convenience insertion: if single T argument =&gt;_ [_**insert()**_](classecss_1_1Memory_1_1SectorsArray.md#function-insert) _; else_[_**emplace()**_](classecss_1_1Memory_1_1SectorsArray.md#function-emplace) _._
```C++
template<typename T, bool TS, class... Args>
inline T * ecss::Memory::SectorsArray::push (
    SectorId sectorId,
    Args &&... args
) 
```





**Template parameters:**


* `T` Component/member type. 



**Parameters:**


* `sectorId` [**Sector**](structecss_1_1Memory_1_1Sector.md) id. 
* `args` Forwarded creation args. 




        

<hr>



### function reserve 

```C++
template<bool TS>
inline void ecss::Memory::SectorsArray::reserve (
    uint32_t newCapacity
) 
```




<hr>



### function sectorsMapCapacity 

```C++
template<bool TS>
inline size_t ecss::Memory::SectorsArray::sectorsMapCapacity () const
```




<hr>



### function setDefragmentThreshold 

_Set ratio threshold (0..1) above which needDefragment()==true._ 
```C++
template<bool TS>
inline void ecss::Memory::SectorsArray::setDefragmentThreshold (
    float threshold
) 
```





**Parameters:**


* `threshold` Clamped into [0..1]. 




        

<hr>



### function shrinkToFit 

```C++
template<bool TS>
inline void ecss::Memory::SectorsArray::shrinkToFit () 
```




<hr>



### function size 

```C++
template<bool TS>
inline size_t ecss::Memory::SectorsArray::size () const
```




<hr>



### function tryDefragment 

_Attempt a defragment only if array not logically pinned (non-blocking attempt)._ 
```C++
template<bool TS>
inline void ecss::Memory::SectorsArray::tryDefragment () 
```




<hr>



### function ~SectorsArray 

_Destructor clears all sectors and releases underlying memory._ 
```C++
inline ecss::Memory::SectorsArray::~SectorsArray () 
```




<hr>
## Public Static Functions Documentation




### function create 

_Factory to allocate a_ [_**SectorsArray**_](classecss_1_1Memory_1_1SectorsArray.md) _for the unique component type set_`Types` _._
```C++
template<typename... Types>
static inline SectorsArray * ecss::Memory::SectorsArray::create (
    Allocator && allocator={}
) 
```





**Template parameters:**


* `Types` Component types grouped in each sector (must be unique). 



**Parameters:**


* `allocator` Allocator instance to move in. 



**Returns:**

Newly allocated heap object (caller owns via delete). 





        

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

