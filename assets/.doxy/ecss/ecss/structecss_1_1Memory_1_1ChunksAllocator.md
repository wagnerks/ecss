

# Struct ecss::Memory::ChunksAllocator

**template &lt;uint32\_t ChunkCapacity&gt;**



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md) **>** [**ChunksAllocator**](structecss_1_1Memory_1_1ChunksAllocator.md)



_Chunked memory allocator for sector data._ [More...](#detailed-description)

* `#include <ChunksAllocator.h>`















## Classes

| Type | Name |
| ---: | :--- |
| struct | [**Cursor**](structecss_1_1Memory_1_1ChunksAllocator_1_1Cursor.md) <br>[_**Cursor**_](structecss_1_1Memory_1_1ChunksAllocator_1_1Cursor.md) _for linear iteration over sector data._ |
| struct | [**RangesCursor**](structecss_1_1Memory_1_1ChunksAllocator_1_1RangesCursor.md) <br>[_**Cursor**_](structecss_1_1Memory_1_1ChunksAllocator_1_1Cursor.md) _for ranged iteration over sector data._ |






## Public Attributes

| Type | Name |
| ---: | :--- |
|  Memory::RetireBin | [**mBin**](#variable-mbin)  <br> |
|  std::vector&lt; void \*, [**Memory::RetireAllocator**](structecss_1_1Memory_1_1RetireAllocator.md)&lt; void \* &gt; &gt; | [**mChunks**](#variable-mchunks)   = `{ [**Memory::RetireAllocator**](structecss_1_1Memory_1_1RetireAllocator.md)&lt;void\*&gt;{ &mBin } }`<br> |
|  bool | [**mIsSectorTrivial**](#variable-missectortrivial)   = `true`<br> |
|  SectorLayoutMeta \* | [**mSectorLayout**](#variable-msectorlayout)   = `nullptr`<br> |
|  uint16\_t | [**mSectorSize**](#variable-msectorsize)   = `0`<br> |


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
|  void | [**allocate**](#function-allocate) (size\_t newCapacity) <br> |
|  FORCE\_INLINE std::byte \* | [**at**](#function-at) (size\_t index) const<br> |
|  FORCE\_INLINE size\_t | [**capacity**](#function-capacity) () const<br> |
|  void | [**copyCommonData**](#function-copycommondata) (const [**ChunksAllocator**](structecss_1_1Memory_1_1ChunksAllocator.md)&lt; OC &gt; & other) <br> |
|  void | [**deallocate**](#function-deallocate) (size\_t from, size\_t to) <br> |
|  size\_t | [**find**](#function-find) (const std::byte \* dataPtr) const<br> |
|  FORCE\_INLINE [**Cursor**](structecss_1_1Memory_1_1ChunksAllocator_1_1Cursor.md) | [**getCursor**](#function-getcursor) (size\_t index=0) const<br> |
|  FORCE\_INLINE [**RangesCursor**](structecss_1_1Memory_1_1ChunksAllocator_1_1RangesCursor.md) | [**getRangesCursor**](#function-getrangescursor) (const Ranges&lt; SectorId &gt; & ranges, size\_t size) const<br> |
|  FORCE\_INLINE SectorLayoutMeta \* | [**getSectorLayout**](#function-getsectorlayout) () const<br> |
|  FORCE\_INLINE void | [**init**](#function-init) (SectorLayoutMeta \* layoutMeta) <br> |
|  void | [**moveSectorsDataTrivial**](#function-movesectorsdatatrivial) (size\_t dst, size\_t src, size\_t n) const<br>_Simple memmove for trivial sector data (no alive array needed)_  |
|  [**ChunksAllocator**](structecss_1_1Memory_1_1ChunksAllocator.md) & | [**operator=**](#function-operator) ([**ChunksAllocator**](structecss_1_1Memory_1_1ChunksAllocator.md) && other) noexcept<br> |
|  [**ChunksAllocator**](structecss_1_1Memory_1_1ChunksAllocator.md) & | [**operator=**](#function-operator_1) ([**ChunksAllocator**](structecss_1_1Memory_1_1ChunksAllocator.md)&lt; OC &gt; && other) noexcept<br> |
|  [**ChunksAllocator**](structecss_1_1Memory_1_1ChunksAllocator.md) & | [**operator=**](#function-operator_2) (const [**ChunksAllocator**](structecss_1_1Memory_1_1ChunksAllocator.md) & other) <br> |
|  [**ChunksAllocator**](structecss_1_1Memory_1_1ChunksAllocator.md) & | [**operator=**](#function-operator_3) (const [**ChunksAllocator**](structecss_1_1Memory_1_1ChunksAllocator.md)&lt; OC &gt; & other) <br> |
|  FORCE\_INLINE std::byte \* | [**operator[]**](#function-operator_4) (size\_t index) const<br> |
|   | [**~ChunksAllocator**](#function-chunksallocator) () <br> |




























## Detailed Description


Stores raw sector data (component payloads only) in fixed-size chunks. [**Sector**](namespaceecss_1_1Memory_1_1Sector.md) metadata (id, isAliveData) is stored externally in [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md).




**Template parameters:**


* `ChunkCapacity` Number of sectors per chunk (rounded up to power of 2). 




    
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



### variable mIsSectorTrivial 

```C++
bool ecss::Memory::ChunksAllocator< ChunkCapacity >::mIsSectorTrivial;
```




<hr>



### variable mSectorLayout 

```C++
SectorLayoutMeta* ecss::Memory::ChunksAllocator< ChunkCapacity >::mSectorLayout;
```




<hr>



### variable mSectorSize 

```C++
uint16_t ecss::Memory::ChunksAllocator< ChunkCapacity >::mSectorSize;
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




<hr>



### function ChunksAllocator [3/6]

```C++
inline ecss::Memory::ChunksAllocator::ChunksAllocator (
    ChunksAllocator && other
) noexcept
```




<hr>



### function ChunksAllocator [4/6]

```C++
template<uint32_t OC>
inline ecss::Memory::ChunksAllocator::ChunksAllocator (
    ChunksAllocator < OC > && other
) noexcept
```




<hr>



### function ChunksAllocator [5/6]

```C++
inline ecss::Memory::ChunksAllocator::ChunksAllocator (
    const ChunksAllocator & other
) 
```




<hr>



### function ChunksAllocator [6/6]

```C++
template<uint32_t OC>
inline ecss::Memory::ChunksAllocator::ChunksAllocator (
    const ChunksAllocator < OC > & other
) 
```




<hr>



### function allocate 

```C++
inline void ecss::Memory::ChunksAllocator::allocate (
    size_t newCapacity
) 
```




<hr>



### function at 

```C++
inline FORCE_INLINE std::byte * ecss::Memory::ChunksAllocator::at (
    size_t index
) const
```




<hr>



### function capacity 

```C++
inline FORCE_INLINE size_t ecss::Memory::ChunksAllocator::capacity () const
```




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




<hr>



### function find 

```C++
inline size_t ecss::Memory::ChunksAllocator::find (
    const std::byte * dataPtr
) const
```




<hr>



### function getCursor 

```C++
inline FORCE_INLINE Cursor ecss::Memory::ChunksAllocator::getCursor (
    size_t index=0
) const
```




<hr>



### function getRangesCursor 

```C++
inline FORCE_INLINE RangesCursor ecss::Memory::ChunksAllocator::getRangesCursor (
    const Ranges< SectorId > & ranges,
    size_t size
) const
```




<hr>



### function getSectorLayout 

```C++
inline FORCE_INLINE SectorLayoutMeta * ecss::Memory::ChunksAllocator::getSectorLayout () const
```




<hr>



### function init 

```C++
inline FORCE_INLINE void ecss::Memory::ChunksAllocator::init (
    SectorLayoutMeta * layoutMeta
) 
```




<hr>



### function moveSectorsDataTrivial 

_Simple memmove for trivial sector data (no alive array needed)_ 
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



### function ~ChunksAllocator 

```C++
inline ecss::Memory::ChunksAllocator::~ChunksAllocator () 
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

