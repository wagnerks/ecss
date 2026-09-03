

# Struct ecss::Memory::ChunksAllocator

**template &lt;uint32\_t ChunkCapacity&gt;**



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md) **>** [**ChunksAllocator**](structecss_1_1Memory_1_1ChunksAllocator.md)



_Chunked memory allocator for sector data._ [More...](#detailed-description)

* `#include <ChunksAllocator.h>`















## Classes

| Type | Name |
| ---: | :--- |
| struct | [**ChunksView**](structecss_1_1Memory_1_1ChunksAllocator_1_1ChunksView.md) <br>_Consistent {chunk table, count} pair for lock-free readers._  |
| struct | [**Cursor**](structecss_1_1Memory_1_1ChunksAllocator_1_1Cursor.md) <br>[_**Cursor**_](structecss_1_1Memory_1_1ChunksAllocator_1_1Cursor.md) _for linear iteration over sector data._ |
| struct | [**RangesCursor**](structecss_1_1Memory_1_1ChunksAllocator_1_1RangesCursor.md) <br>[_**Cursor**_](structecss_1_1Memory_1_1ChunksAllocator_1_1Cursor.md) _for ranged iteration over sector data._ |






## Public Attributes

| Type | Name |
| ---: | :--- |
|  [**Memory::RetireBin**](structecss_1_1Memory_1_1RetireBin.md) | [**mBin**](#variable-mbin)  <br> |
|  std::vector&lt; void \*, [**Memory::RetireAllocator**](structecss_1_1Memory_1_1RetireAllocator.md)&lt; void \* &gt; &gt; | [**mChunks**](#variable-mchunks)   = `{ [**Memory::RetireAllocator**](structecss_1_1Memory_1_1RetireAllocator.md)&lt;void\*&gt;{ &mBin } }`<br> |
|  std::atomic&lt; size\_t &gt; | [**mChunksCount**](#variable-mchunkscount)   = `{ 0 }`<br> |
|  std::atomic&lt; void \*const  \* &gt; | [**mChunksPtr**](#variable-mchunksptr)   = `{ nullptr }`<br> |
|  bool | [**mIsSectorTrivial**](#variable-missectortrivial)   = `true`<br> |
|  const [**SectorLayoutMeta**](structecss_1_1Memory_1_1SectorLayoutMeta.md) \* | [**mSectorLayout**](#variable-msectorlayout)   = `nullptr`<br> |
|  uint16\_t | [**mSectorSize**](#variable-msectorsize)   = `0`<br> |
|  std::atomic&lt; uint64\_t &gt; | [**mSeq**](#variable-mseq)   = `{ 0 }`<br> |


## Public Static Attributes

| Type | Name |
| ---: | :--- |
|  uint32\_t | [**mChunkCapacity**](#variable-mchunkcapacity)   = `nextPowerOfTwo(ChunkCapacity)`<br> |
|  uint32\_t | [**mChunkShift**](#variable-mchunkshift)   = `std::countr\_zero(mChunkCapacity)`<br> |














## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**ChunksAllocator**](#function-chunksallocator-26) () = default<br> |
|   | [**ChunksAllocator**](#function-chunksallocator-36) ([**ChunksAllocator**](structecss_1_1Memory_1_1ChunksAllocator.md) && other) noexcept<br> |
|   | [**ChunksAllocator**](#function-chunksallocator-46) ([**ChunksAllocator**](structecss_1_1Memory_1_1ChunksAllocator.md)&lt; OC &gt; && other) noexcept<br> |
|   | [**ChunksAllocator**](#function-chunksallocator-56) (const [**ChunksAllocator**](structecss_1_1Memory_1_1ChunksAllocator.md) & other) <br> |
|   | [**ChunksAllocator**](#function-chunksallocator-66) (const [**ChunksAllocator**](structecss_1_1Memory_1_1ChunksAllocator.md)&lt; OC &gt; & other) <br> |
|  bool | [**adoptOrMatchLayout**](#function-adoptormatchlayout) (const [**ChunksAllocator**](structecss_1_1Memory_1_1ChunksAllocator.md)&lt; OC &gt; & other) <br>_Take on_ `other's` _layout, or confirm it is the one already held._ |
|  void | [**allocate**](#function-allocate) (size\_t newCapacity) <br> |
|  FORCE\_INLINE std::byte \* | [**at**](#function-at) (size\_t index) const<br> |
|  FORCE\_INLINE size\_t | [**capacity**](#function-capacity) () const<br> |
|  void | [**copyCommonData**](#function-copycommondata) (const [**ChunksAllocator**](structecss_1_1Memory_1_1ChunksAllocator.md)&lt; OC &gt; & other) <br> |
|  void | [**deallocate**](#function-deallocate) (size\_t from, size\_t to) <br> |
|  size\_t | [**find**](#function-find) (const std::byte \* dataPtr) const<br> |
|  FORCE\_INLINE [**Cursor**](structecss_1_1Memory_1_1ChunksAllocator_1_1Cursor.md) | [**getCursor**](#function-getcursor) (size\_t index=0) const<br> |
|  FORCE\_INLINE [**RangesCursor**](structecss_1_1Memory_1_1ChunksAllocator_1_1RangesCursor.md) | [**getRangesCursor**](#function-getrangescursor) (const [**Ranges**](structecss_1_1Ranges.md)&lt; SectorId &gt; & ranges, size\_t size) const<br> |
|  FORCE\_INLINE const [**SectorLayoutMeta**](structecss_1_1Memory_1_1SectorLayoutMeta.md) \* | [**getSectorLayout**](#function-getsectorlayout) () const<br> |
|  FORCE\_INLINE void | [**init**](#function-init) (const [**SectorLayoutMeta**](structecss_1_1Memory_1_1SectorLayoutMeta.md) \* layoutMeta) <br> |
|  FORCE\_INLINE [**ChunksView**](structecss_1_1Memory_1_1ChunksAllocator_1_1ChunksView.md) | [**loadChunks**](#function-loadchunks) () noexcept const<br>_Seqlock snapshot of the chunk table._  |
|  void | [**moveSectorsDataTrivial**](#function-movesectorsdatatrivial) (size\_t dst, size\_t src, size\_t n) const<br>_Simple memmove for trivial sector data (no alive array needed) @thread\_safety Caller must ensure exclusive access. Moves sector bytes, so on top of the write lock the array must be quiescent: no pin and no hold. Any reader would be looking at the sectors being moved._  |
|  [**ChunksAllocator**](structecss_1_1Memory_1_1ChunksAllocator.md) & | [**operator=**](#function-operator) ([**ChunksAllocator**](structecss_1_1Memory_1_1ChunksAllocator.md) && other) noexcept<br> |
|  [**ChunksAllocator**](structecss_1_1Memory_1_1ChunksAllocator.md) & | [**operator=**](#function-operator_1) ([**ChunksAllocator**](structecss_1_1Memory_1_1ChunksAllocator.md)&lt; OC &gt; && other) noexcept<br> |
|  [**ChunksAllocator**](structecss_1_1Memory_1_1ChunksAllocator.md) & | [**operator=**](#function-operator_2) (const [**ChunksAllocator**](structecss_1_1Memory_1_1ChunksAllocator.md) & other) <br> |
|  [**ChunksAllocator**](structecss_1_1Memory_1_1ChunksAllocator.md) & | [**operator=**](#function-operator_3) (const [**ChunksAllocator**](structecss_1_1Memory_1_1ChunksAllocator.md)&lt; OC &gt; & other) <br> |
|  FORCE\_INLINE std::byte \* | [**operator[]**](#function-operator_4) (size\_t index) const<br> |
|  FORCE\_INLINE void | [**storeChunks**](#function-storechunks) () <br>_Publish the current table. Called after every mutation of mChunks. @thread\_safety Caller must ensure exclusive access. Publishes a new chunk table. Every caller is already under the owning array's write lock; the seqlock is what makes the_ _readers_ _safe, not the writers._ |
|   | [**~ChunksAllocator**](#function-chunksallocator) () <br> |


## Public Static Functions

| Type | Name |
| ---: | :--- |
|  FORCE\_INLINE std::byte \* | [**atView**](#function-atview) (const [**ChunksView**](structecss_1_1Memory_1_1ChunksAllocator_1_1ChunksView.md) & view, size\_t index) <br>[_**Sector**_](namespaceecss_1_1Memory_1_1Sector.md) _data address inside a snapshot; nullptr when out of range. @thread\_safety Internally synchronized. Addresses into a snapshot the caller already holds, so nothing it reads can be replaced underneath it._ |


























## Detailed Description


Stores raw sector data (component payloads only) in fixed-size chunks. [**Sector**](namespaceecss_1_1Memory_1_1Sector.md) metadata (id, isAliveData) is stored externally in [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md).




**Template parameters:**


* `ChunkCapacity` Number of sectors per chunk (rounded up to power of 2). @thread\_safety Caller must ensure exclusive access. This is the owning [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md)'s private storage and carries no lock of its own: every mutating call is made under that array's write lock.

Readers are the exception, and are why the chunk table is published rather than simply written: [**loadChunks()**](structecss_1_1Memory_1_1ChunksAllocator.md#function-loadchunks) hands out a snapshot a lock-free reader can walk while chunks are being added or returned. Chunks that go away are retired, not freed, so a reader holding a pointer into one stays safe until the grace period expires. 


    
## Public Attributes Documentation




### variable mBin 

```C++
Memory::RetireBin ecss::Memory::ChunksAllocator< ChunkCapacity >::mBin;
```




<hr>



### variable mChunks 

```C++
std::vector<void*, Memory::RetireAllocator<void*> > ecss::Memory::ChunksAllocator< ChunkCapacity >::mChunks;
```




<hr>



### variable mChunksCount 

```C++
std::atomic<size_t> ecss::Memory::ChunksAllocator< ChunkCapacity >::mChunksCount;
```




<hr>



### variable mChunksPtr 

```C++
std::atomic<void* const*> ecss::Memory::ChunksAllocator< ChunkCapacity >::mChunksPtr;
```




<hr>



### variable mIsSectorTrivial 

```C++
bool ecss::Memory::ChunksAllocator< ChunkCapacity >::mIsSectorTrivial;
```




<hr>



### variable mSectorLayout 

```C++
const SectorLayoutMeta* ecss::Memory::ChunksAllocator< ChunkCapacity >::mSectorLayout;
```




<hr>



### variable mSectorSize 

```C++
uint16_t ecss::Memory::ChunksAllocator< ChunkCapacity >::mSectorSize;
```




<hr>



### variable mSeq 

```C++
std::atomic<uint64_t> ecss::Memory::ChunksAllocator< ChunkCapacity >::mSeq;
```




<hr>
## Public Static Attributes Documentation




### variable mChunkCapacity 

```C++
uint32_t ecss::Memory::ChunksAllocator< ChunkCapacity >::mChunkCapacity;
```




<hr>



### variable mChunkShift 

```C++
uint32_t ecss::Memory::ChunksAllocator< ChunkCapacity >::mChunkShift;
```




<hr>
## Public Functions Documentation




### function ChunksAllocator [2/6]

```C++
ecss::Memory::ChunksAllocator::ChunksAllocator () = default
```



@thread\_safety Caller must ensure exclusive access. Construction, assignment and destruction of the allocator are not concurrent with anything reading it. 


        

<hr>



### function ChunksAllocator [3/6]

```C++
inline ecss::Memory::ChunksAllocator::ChunksAllocator (
    ChunksAllocator && other
) noexcept
```



@thread\_safety Caller must ensure exclusive access. Construction, assignment and destruction of the allocator are not concurrent with anything reading it. 


        

<hr>



### function ChunksAllocator [4/6]

```C++
template<uint32_t OC>
inline ecss::Memory::ChunksAllocator::ChunksAllocator (
    ChunksAllocator < OC > && other
) noexcept
```



@thread\_safety Caller must ensure exclusive access. Construction, assignment and destruction of the allocator are not concurrent with anything reading it. 


        

<hr>



### function ChunksAllocator [5/6]

```C++
inline ecss::Memory::ChunksAllocator::ChunksAllocator (
    const ChunksAllocator & other
) 
```



@thread\_safety Caller must ensure exclusive access. Construction, assignment and destruction of the allocator are not concurrent with anything reading it. 


        

<hr>



### function ChunksAllocator [6/6]

```C++
template<uint32_t OC>
inline ecss::Memory::ChunksAllocator::ChunksAllocator (
    const ChunksAllocator < OC > & other
) 
```



@thread\_safety Caller must ensure exclusive access. Construction, assignment and destruction of the allocator are not concurrent with anything reading it. 


        

<hr>



### function adoptOrMatchLayout 

_Take on_ `other's` _layout, or confirm it is the one already held._
```C++
template<uint32_t OC>
inline bool ecss::Memory::ChunksAllocator::adoptOrMatchLayout (
    const ChunksAllocator < OC > & other
) 
```



An allocator adopts a layout once  from [**init()**](structecss_1_1Memory_1_1ChunksAllocator.md#function-init), or here when it was built by copying another rather than from a type pack  and keeps it for good. Repointing a live allocator at a different layout would change the sector size and the liveness bits under everything already stored in it.




**Returns:**

false if the layouts differ, in which case nothing is changed and the caller must abandon the operation rather than proceed with a description that does not match the bytes. @thread\_safety Caller must ensure exclusive access. May call [**init()**](structecss_1_1Memory_1_1ChunksAllocator.md#function-init). Runs during copy and move assignment, which are already exclusive. 





        

<hr>



### function allocate 

```C++
inline void ecss::Memory::ChunksAllocator::allocate (
    size_t newCapacity
) 
```



@thread\_safety Caller must ensure exclusive access. Mutates the chunk vector, so it needs the owning array's write lock. Growth only adds chunks and moves no sector, so it does not need the array to be quiescent. 


        

<hr>



### function at 

```C++
inline FORCE_INLINE std::byte * ecss::Memory::ChunksAllocator::at (
    size_t index
) const
```



@thread\_safety Caller must ensure exclusive access. Reads the live chunk vector, so it is only correct under the owning array's lock. A concurrent [**allocate()**](structecss_1_1Memory_1_1ChunksAllocator.md#function-allocate) or [**deallocate()**](structecss_1_1Memory_1_1ChunksAllocator.md#function-deallocate) may be reallocating that vector. Readers outside the lock must go through [**loadChunks()**](structecss_1_1Memory_1_1ChunksAllocator.md#function-loadchunks) and [**atView()**](structecss_1_1Memory_1_1ChunksAllocator.md#function-atview) instead. 


        

<hr>



### function capacity 

```C++
inline FORCE_INLINE size_t ecss::Memory::ChunksAllocator::capacity () const
```



@thread\_safety Caller must ensure exclusive access. Reads the live chunk vector, so it is only correct under the owning array's lock  which is exactly why [**SectorsArray::capacity()**](classecss_1_1Memory_1_1SectorsArray.md#function-capacity) takes the shared lock rather than reading an atomic. 


        

<hr>



### function copyCommonData 

```C++
template<uint32_t OC>
inline void ecss::Memory::ChunksAllocator::copyCommonData (
    const ChunksAllocator < OC > & other
) 
```




<hr>



### function deallocate 

```C++
inline void ecss::Memory::ChunksAllocator::deallocate (
    size_t from,
    size_t to
) 
```



@thread\_safety Caller must ensure exclusive access. Mutates the chunk vector, so it needs the owning array's write lock. Chunks are retired rather than freed, which is what keeps a lock-free reader holding a pointer into one safe. 


        

<hr>



### function find 

```C++
inline size_t ecss::Memory::ChunksAllocator::find (
    const std::byte * dataPtr
) const
```



@thread\_safety Caller must ensure exclusive access. Reads the live chunk vector, so it is only correct under the owning array's lock. 


        

<hr>



### function getCursor 

```C++
inline FORCE_INLINE Cursor ecss::Memory::ChunksAllocator::getCursor (
    size_t index=0
) const
```



@thread\_safety Internally synchronized. Reads the published chunk snapshot to build the cursor. What the cursor then requires is on the cursor. 


        

<hr>



### function getRangesCursor 

```C++
inline FORCE_INLINE RangesCursor ecss::Memory::ChunksAllocator::getRangesCursor (
    const Ranges < SectorId > & ranges,
    size_t size
) const
```



@thread\_safety Internally synchronized. Reads the published chunk snapshot to build the cursor. The [**Ranges**](structecss_1_1Ranges.md) argument must not be mutated while the cursor walks it. 


        

<hr>



### function getSectorLayout 

```C++
inline FORCE_INLINE const SectorLayoutMeta * ecss::Memory::ChunksAllocator::getSectorLayout () const
```



@thread\_safety Internally synchronized. The layout pointer is set once and never changes afterwards, so this is a plain read that cannot go stale. 


        

<hr>



### function init 

```C++
inline FORCE_INLINE void ecss::Memory::ChunksAllocator::init (
    const SectorLayoutMeta * layoutMeta
) 
```



@thread\_safety Caller must ensure exclusive access. Sets the layout the whole allocator is shaped by. Call it before anything else touches the allocator. 


        

<hr>



### function loadChunks 

_Seqlock snapshot of the chunk table._ 
```C++
inline FORCE_INLINE ChunksView ecss::Memory::ChunksAllocator::loadChunks () noexcept const
```



The table is a std::vector, so its buffer pointer and size cannot be read together without tearing while [**allocate()**](structecss_1_1Memory_1_1ChunksAllocator.md#function-allocate) is pushing to it. Readers used to be protected by the array shared lock instead, which is precisely the lock that made view() construction and every sparse lookup serialise. Old buffers stay readable because mChunks is retire-allocated. @thread\_safety Internally synchronized. Lock-free, and the reason readers can walk chunks at all: a seqlock read that retries until it sees a table nobody was mid-way through replacing. This, not [**at()**](structecss_1_1Memory_1_1ChunksAllocator.md#function-at), is what a lock-free reader must use. 


        

<hr>



### function moveSectorsDataTrivial 

_Simple memmove for trivial sector data (no alive array needed) @thread\_safety Caller must ensure exclusive access. Moves sector bytes, so on top of the write lock the array must be quiescent: no pin and no hold. Any reader would be looking at the sectors being moved._ 
```C++
inline void ecss::Memory::ChunksAllocator::moveSectorsDataTrivial (
    size_t dst,
    size_t src,
    size_t n
) const
```




<hr>



### function operator= 

```C++
inline ChunksAllocator & ecss::Memory::ChunksAllocator::operator= (
    ChunksAllocator && other
) noexcept
```




<hr>



### function operator= 

```C++
template<uint32_t OC>
inline ChunksAllocator & ecss::Memory::ChunksAllocator::operator= (
    ChunksAllocator < OC > && other
) noexcept
```




<hr>



### function operator= 

```C++
inline ChunksAllocator & ecss::Memory::ChunksAllocator::operator= (
    const ChunksAllocator & other
) 
```




<hr>



### function operator= 

```C++
template<uint32_t OC>
inline ChunksAllocator & ecss::Memory::ChunksAllocator::operator= (
    const ChunksAllocator < OC > & other
) 
```




<hr>



### function operator[] 

```C++
inline FORCE_INLINE std::byte * ecss::Memory::ChunksAllocator::operator[] (
    size_t index
) const
```





**Returns:**

Raw pointer to sector data at given linear index 





        

<hr>



### function storeChunks 

_Publish the current table. Called after every mutation of mChunks. @thread\_safety Caller must ensure exclusive access. Publishes a new chunk table. Every caller is already under the owning array's write lock; the seqlock is what makes the_ _readers_ _safe, not the writers._
```C++
inline FORCE_INLINE void ecss::Memory::ChunksAllocator::storeChunks () 
```




<hr>



### function ~ChunksAllocator 

```C++
inline ecss::Memory::ChunksAllocator::~ChunksAllocator () 
```



@thread\_safety Caller must ensure exclusive access. Construction, assignment and destruction of the allocator are not concurrent with anything reading it. 


        

<hr>
## Public Static Functions Documentation




### function atView 

[_**Sector**_](namespaceecss_1_1Memory_1_1Sector.md) _data address inside a snapshot; nullptr when out of range. @thread\_safety Internally synchronized. Addresses into a snapshot the caller already holds, so nothing it reads can be replaced underneath it._
```C++
static inline FORCE_INLINE std::byte * ecss::Memory::ChunksAllocator::atView (
    const ChunksView & view,
    size_t index
) 
```




<hr>## Friends Documentation





### friend ChunksAllocator [1/6]

```C++
template<uint32_t>
struct ecss::Memory::ChunksAllocator::ChunksAllocator (
    ChunksAllocator
) 
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/memory/ChunksAllocator.h`

