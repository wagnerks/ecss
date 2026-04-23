

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






















## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**SectorsArray**](#function-sectorsarray-26) (const [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) & other) <br> |
|   | [**SectorsArray**](#function-sectorsarray-36) (const [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md)&lt; T, Alloc &gt; & other) <br> |
|   | [**SectorsArray**](#function-sectorsarray-46) ([**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) && other) noexcept<br> |
|   | [**SectorsArray**](#function-sectorsarray-56) ([**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md)&lt; T, Alloc &gt; && other) noexcept<br> |
|  [**Iterator**](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) | [**begin**](#function-begin) () const<br> |
|  [**IteratorAlive**](classecss_1_1Memory_1_1SectorsArray_1_1IteratorAlive.md) | [**beginAlive**](#function-beginalive) () const<br> |
|  [**RangedIterator**](classecss_1_1Memory_1_1SectorsArray_1_1RangedIterator.md) | [**beginRanged**](#function-beginranged) (const Ranges&lt; SectorId &gt; & ranges) const<br> |
|  size\_t | [**capacity**](#function-capacity) () const<br> |
|  void | [**clear**](#function-clear) () <br> |
|  bool | [**containsSector**](#function-containssector) (SectorId id) const<br> |
|  void | [**defragment**](#function-defragment) () <br> |
|  T \* | [**emplace**](#function-emplace) (SectorId sectorId, Args &&... args) <br> |
|  bool | [**empty**](#function-empty) () const<br> |
|  [**Iterator**](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) | [**end**](#function-end) () const<br> |
|  [**IteratorAlive**](classecss_1_1Memory_1_1SectorsArray_1_1IteratorAlive.md) | [**endAlive**](#function-endalive) () const<br> |
|  [**RangedIterator**](classecss_1_1Memory_1_1SectorsArray_1_1RangedIterator.md) | [**endRanged**](#function-endranged) () const<br> |
|  [**Iterator**](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) | [**erase**](#function-erase-12) ([**Iterator**](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) it, bool defragment=false) noexcept<br> |
|  void | [**erase**](#function-erase-22) (size\_t beginIdx, size\_t count=1, bool defragment=false) <br> |
|  void | [**eraseAsync**](#function-eraseasync) (SectorId id, size\_t count=1) <br> |
|  size\_t | [**findLinearIdx**](#function-findlinearidx) (SectorId sectorId) const<br> |
|  std::byte \* | [**findSectorData**](#function-findsectordata) (SectorId id) const<br> |
|  [**detail::SlotInfo**](structecss_1_1Memory_1_1detail_1_1SlotInfo.md) | [**findSlot**](#function-findslot) (SectorId id) const<br>_Find slot info (data pointer + linearIdx) for fast sparse lookup._  |
|  auto | [**getDefragmentationRatio**](#function-getdefragmentationratio) () const<br> |
|  auto | [**getDefragmentationSize**](#function-getdefragmentationsize) () const<br> |
|  SectorId | [**getId**](#function-getid) (size\_t linearIdx) const<br> |
|  uint32\_t | [**getIsAlive**](#function-getisalive) (SectorId id) const<br> |
|  uint32\_t & | [**getIsAliveRef**](#function-getisaliveref) (size\_t linearIdx) <br> |
|  FORCE\_INLINE SectorLayoutMeta \* | [**getLayout**](#function-getlayout) () const<br> |
|  FORCE\_INLINE const [**LayoutData**](structecss_1_1Memory_1_1LayoutData.md) & | [**getLayoutData**](#function-getlayoutdata) () const<br> |
|  void | [**incDefragmentSize**](#function-incdefragmentsize) (uint32\_t count=1) <br> |
|  std::remove\_cvref\_t&lt; T &gt; \* | [**insert**](#function-insert) (SectorId sectorId, T && data) <br> |
|  bool | [**isPacked**](#function-ispacked) () const<br>_Check if array has no dead slots (defragmentSize == 0)_  |
|  FORCE\_INLINE uint32\_t | [**loadAliveWord**](#function-loadaliveword) (size\_t linearIdx) noexcept const<br>_Thread-safe alive-word read that routes through the seqlock snapshot. Unlike getIsAliveRef, this never dereferences the live std::vector, so it is safe against concurrent push\_back reallocation_  _the vector's internal \_M\_start field would otherwise race with the reader's non-atomic read of it. Old isAlive buffers remain valid because_[_**RetireAllocator**_](structecss_1_1Memory_1_1RetireAllocator.md) _defers their free. Returns 0 if linearIdx is outside the snapshot (newly allocated slot not yet published), which callers treat as "not alive"._ |
|  bool | [**needDefragment**](#function-needdefragment) () const<br> |
|  [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) & | [**operator=**](#function-operator) (const [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) & other) <br> |
|  [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) & | [**operator=**](#function-operator_1) (const [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md)&lt; T, Alloc &gt; & other) <br> |
|  [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) & | [**operator=**](#function-operator_2) ([**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) && other) noexcept<br> |
|  [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) & | [**operator=**](#function-operator_3) ([**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md)&lt; T, Alloc &gt; && other) noexcept<br> |
|  [**PinnedSector**](structecss_1_1Memory_1_1PinnedSector.md) | [**pinBackSector**](#function-pinbacksector) () const<br> |
|  [**PinnedSector**](structecss_1_1Memory_1_1PinnedSector.md) | [**pinSector**](#function-pinsector) (SectorId id) const<br> |
|  [**PinnedSector**](structecss_1_1Memory_1_1PinnedSector.md) | [**pinSectorAt**](#function-pinsectorat) (size\_t idx) const<br> |
|  void | [**processPendingErases**](#function-processpendingerases) (bool withDefragment=true) <br> |
|  T \* | [**push**](#function-push) (SectorId sectorId, Args &&... args) <br> |
|  auto | [**readLock**](#function-readlock-12) () const<br> |
|  void | [**reserve**](#function-reserve) (uint32\_t newCapacity) <br> |
|  void | [**setDefragmentThreshold**](#function-setdefragmentthreshold) (float threshold) <br> |
|  void | [**setGracePeriod**](#function-setgraceperiod) (uint32\_t ticks) <br>_Set the grace period (in ticks) before retired memory is freed._  |
|  void | [**shrinkToFit**](#function-shrinktofit) () <br> |
|  size\_t | [**size**](#function-size) () const<br> |
|  size\_t | [**sparseCapacity**](#function-sparsecapacity) () const<br> |
|  size\_t | [**tick**](#function-tick) () <br>_Process one tick of the grace period for retired memory._  |
|  void | [**tryDefragment**](#function-trydefragment) () <br> |
|  auto | [**writeLock**](#function-writelock-12) () const<br> |
|   | [**~SectorsArray**](#function-sectorsarray) () <br> |


## Public Static Functions

| Type | Name |
| ---: | :--- |
|  [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) \* | [**create**](#function-create) (Allocator && allocator={}) <br> |
|  FORCE\_INLINE uint32\_t | [**loadAliveAcquire**](#function-loadaliveacquire) (const uint32\_t \* p, size\_t i) noexcept<br>_Acquire load of an alive-bit word. Pairs with the release fetch\_or in Sector::markAlive&lt;true&gt;, so a reader observing a set bit is guaranteed to see the fully-constructed component bytes written before the bit was set. x86-64: plain MOV (all loads are acquire). ARM64: LDAR instead of LDR._  |
|  FORCE\_INLINE uint32\_t | [**loadAliveRelaxed**](#function-loadaliverelaxed) (const uint32\_t \* p, size\_t i) noexcept<br>_Relaxed atomic load of an alive-bit word. Compiles to a plain MOV on x86-64 and a plain LDR on ARM64_  _identical codegen to a raw read. Exists purely to satisfy the C++ memory model when the word may be concurrently written by Sector::markAlive&lt;true&gt;/markNotAlive&lt;true&gt;. Declared static so nested iterator classes can call it without an enclosing instance._ |


























## Detailed Description




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




<hr>



### function beginAlive 

```C++
template<class T, bool TS>
inline IteratorAlive ecss::Memory::SectorsArray::beginAlive () const
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

```C++
template<bool TS>
inline void ecss::Memory::SectorsArray::defragment () 
```




<hr>



### function emplace 

```C++
template<typename T, bool TS, class... Args>
inline T * ecss::Memory::SectorsArray::emplace (
    SectorId sectorId,
    Args &&... args
) 
```




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



### function erase [1/2]

```C++
template<bool TS>
inline Iterator ecss::Memory::SectorsArray::erase (
    Iterator it,
    bool defragment=false
) noexcept
```




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




<hr>



### function eraseAsync 

```C++
inline void ecss::Memory::SectorsArray::eraseAsync (
    SectorId id,
    size_t count=1
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




<hr>



### function findSectorData 

```C++
template<bool TS>
inline std::byte * ecss::Memory::SectorsArray::findSectorData (
    SectorId id
) const
```




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

[**SlotInfo**](structecss_1_1Memory_1_1SectorsArray_1_1SlotInfo.md) with data pointer and linear index, or INVALID\_SLOT if not found 





        

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



### function getId 

```C++
template<bool TS>
inline SectorId ecss::Memory::SectorsArray::getId (
    size_t linearIdx
) const
```




<hr>



### function getIsAlive 

```C++
template<bool TS>
inline uint32_t ecss::Memory::SectorsArray::getIsAlive (
    SectorId id
) const
```




<hr>



### function getIsAliveRef 

```C++
template<bool TS>
inline uint32_t & ecss::Memory::SectorsArray::getIsAliveRef (
    size_t linearIdx
) 
```




<hr>



### function getLayout 

```C++
inline FORCE_INLINE SectorLayoutMeta * ecss::Memory::SectorsArray::getLayout () const
```




<hr>



### function getLayoutData 

```C++
template<typename T>
inline FORCE_INLINE const LayoutData & ecss::Memory::SectorsArray::getLayoutData () const
```




<hr>



### function incDefragmentSize 

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
) 
```




<hr>



### function isPacked 

_Check if array has no dead slots (defragmentSize == 0)_ 
```C++
template<bool TS>
inline bool ecss::Memory::SectorsArray::isPacked () const
```




<hr>



### function loadAliveWord 

_Thread-safe alive-word read that routes through the seqlock snapshot. Unlike getIsAliveRef, this never dereferences the live std::vector, so it is safe against concurrent push\_back reallocation_  _the vector's internal \_M\_start field would otherwise race with the reader's non-atomic read of it. Old isAlive buffers remain valid because_[_**RetireAllocator**_](structecss_1_1Memory_1_1RetireAllocator.md) _defers their free. Returns 0 if linearIdx is outside the snapshot (newly allocated slot not yet published), which callers treat as "not alive"._
```C++
template<bool TS>
inline FORCE_INLINE uint32_t ecss::Memory::SectorsArray::loadAliveWord (
    size_t linearIdx
) noexcept const
```




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

```C++
template<bool TS>
inline PinnedSector ecss::Memory::SectorsArray::pinBackSector () const
```




<hr>



### function pinSector 

```C++
template<bool TS>
inline PinnedSector ecss::Memory::SectorsArray::pinSector (
    SectorId id
) const
```




<hr>



### function pinSectorAt 

```C++
template<bool TS>
inline PinnedSector ecss::Memory::SectorsArray::pinSectorAt (
    size_t idx
) const
```




<hr>



### function processPendingErases 

```C++
template<bool Lock>
inline void ecss::Memory::SectorsArray::processPendingErases (
    bool withDefragment=true
) 
```




<hr>



### function push 

```C++
template<typename T, bool TS, class... Args>
inline T * ecss::Memory::SectorsArray::push (
    SectorId sectorId,
    Args &&... args
) 
```




<hr>



### function readLock [1/2]

```C++
inline auto ecss::Memory::SectorsArray::readLock () const
```




<hr>



### function reserve 

```C++
template<bool TS>
inline void ecss::Memory::SectorsArray::reserve (
    uint32_t newCapacity
) 
```




<hr>



### function setDefragmentThreshold 

```C++
template<bool TS>
inline void ecss::Memory::SectorsArray::setDefragmentThreshold (
    float threshold
) 
```




<hr>



### function setGracePeriod 

_Set the grace period (in ticks) before retired memory is freed._ 
```C++
inline void ecss::Memory::SectorsArray::setGracePeriod (
    uint32_t ticks
) 
```



Higher values = safer but more memory usage. Lower values = less memory but risk of use-after-free if iterators live long.


Default is 3 ticks, which is safe for typical game loops where iterators don't survive across frames.




**Note:**

Only available in thread-safe mode. In non-thread-safe mode, memory is freed immediately (no deferred reclamation).




**Parameters:**


* `ticks` Number of [**tick()**](classecss_1_1Memory_1_1SectorsArray.md#function-tick) calls before memory is freed (0 = immediate) 




        

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



### function sparseCapacity 

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

Only available in thread-safe mode. In non-thread-safe mode, memory is freed immediately during reallocation.




**Returns:**

Number of memory blocks freed this tick 





        

<hr>



### function tryDefragment 

```C++
template<bool TS>
inline void ecss::Memory::SectorsArray::tryDefragment () 
```




<hr>



### function writeLock [1/2]

```C++
inline auto ecss::Memory::SectorsArray::writeLock () const
```




<hr>



### function ~SectorsArray 

```C++
inline ecss::Memory::SectorsArray::~SectorsArray () 
```




<hr>
## Public Static Functions Documentation




### function create 

```C++
template<typename... Types>
static inline SectorsArray * ecss::Memory::SectorsArray::create (
    Allocator && allocator={}
) 
```




<hr>



### function loadAliveAcquire 

_Acquire load of an alive-bit word. Pairs with the release fetch\_or in Sector::markAlive&lt;true&gt;, so a reader observing a set bit is guaranteed to see the fully-constructed component bytes written before the bit was set. x86-64: plain MOV (all loads are acquire). ARM64: LDAR instead of LDR._ 
```C++
static inline FORCE_INLINE uint32_t ecss::Memory::SectorsArray::loadAliveAcquire (
    const uint32_t * p,
    size_t i
) noexcept
```




<hr>



### function loadAliveRelaxed 

_Relaxed atomic load of an alive-bit word. Compiles to a plain MOV on x86-64 and a plain LDR on ARM64_  _identical codegen to a raw read. Exists purely to satisfy the C++ memory model when the word may be concurrently written by Sector::markAlive&lt;true&gt;/markNotAlive&lt;true&gt;. Declared static so nested iterator classes can call it without an enclosing instance._
```C++
static inline FORCE_INLINE uint32_t ecss::Memory::SectorsArray::loadAliveRelaxed (
    const uint32_t * p,
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

