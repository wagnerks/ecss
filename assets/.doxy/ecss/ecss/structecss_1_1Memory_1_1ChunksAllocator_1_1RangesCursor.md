

# Struct ecss::Memory::ChunksAllocator::RangesCursor



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md) **>** [**ChunksAllocator**](structecss_1_1Memory_1_1ChunksAllocator.md) **>** [**RangesCursor**](structecss_1_1Memory_1_1ChunksAllocator_1_1RangesCursor.md)



[_**Cursor**_](structecss_1_1Memory_1_1ChunksAllocator_1_1Cursor.md) _for ranged iteration over sector data._[More...](#detailed-description)

* `#include <ChunksAllocator.h>`





































## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**RangesCursor**](#function-rangescursor-12) () = default<br> |
|   | [**RangesCursor**](#function-rangescursor-22) (const [**ChunksAllocator**](structecss_1_1Memory_1_1ChunksAllocator.md) \* alloc, const Ranges&lt; SectorId &gt; & ranges, size\_t size) <br> |
|  FORCE\_INLINE void | [**advanceToLinearIdx**](#function-advancetolinearidx) (size\_t targetIdx) noexcept<br>_Advance to a specific linear index (binary search if needed)._  |
|  FORCE\_INLINE size\_t | [**linearIndex**](#function-linearindex) () noexcept const<br> |
|  FORCE\_INLINE void | [**nextSpan**](#function-nextspan) () noexcept<br> |
|  FORCE\_INLINE | [**operator bool**](#function-operator-bool) () noexcept const<br> |
|  FORCE\_INLINE bool | [**operator!=**](#function-operator) (const [**RangesCursor**](structecss_1_1Memory_1_1ChunksAllocator_1_1RangesCursor.md) & other) noexcept const<br> |
|  FORCE\_INLINE std::byte \* | [**operator\***](#function-operator_1) () noexcept const<br> |
|  FORCE\_INLINE std::byte \* | [**operator-&gt;**](#function-operator-) () noexcept const<br> |
|  FORCE\_INLINE bool | [**operator==**](#function-operator_2) (const [**RangesCursor**](structecss_1_1Memory_1_1ChunksAllocator_1_1RangesCursor.md) & other) noexcept const<br> |
|  FORCE\_INLINE std::byte \* | [**rawPtr**](#function-rawptr) () noexcept const<br> |
|  FORCE\_INLINE void | [**step**](#function-step) () noexcept<br> |




























## Detailed Description


Iterates over pre-computed spans of sector data. Does NOT have access to sector id/isAlive - those come from external arrays. 


    
## Public Functions Documentation




### function RangesCursor [1/2]

```C++
ecss::Memory::ChunksAllocator::RangesCursor::RangesCursor () = default
```




<hr>



### function RangesCursor [2/2]

```C++
inline ecss::Memory::ChunksAllocator::RangesCursor::RangesCursor (
    const ChunksAllocator * alloc,
    const Ranges< SectorId > & ranges,
    size_t size
) 
```




<hr>



### function advanceToLinearIdx 

_Advance to a specific linear index (binary search if needed)._ 
```C++
inline FORCE_INLINE void ecss::Memory::ChunksAllocator::RangesCursor::advanceToLinearIdx (
    size_t targetIdx
) noexcept
```




<hr>



### function linearIndex 

```C++
inline FORCE_INLINE size_t ecss::Memory::ChunksAllocator::RangesCursor::linearIndex () noexcept const
```




<hr>



### function nextSpan 

```C++
inline FORCE_INLINE void ecss::Memory::ChunksAllocator::RangesCursor::nextSpan () noexcept
```




<hr>



### function operator bool 

```C++
inline explicit FORCE_INLINE ecss::Memory::ChunksAllocator::RangesCursor::operator bool () noexcept const
```




<hr>



### function operator!= 

```C++
inline FORCE_INLINE bool ecss::Memory::ChunksAllocator::RangesCursor::operator!= (
    const RangesCursor & other
) noexcept const
```




<hr>



### function operator\* 

```C++
inline FORCE_INLINE std::byte * ecss::Memory::ChunksAllocator::RangesCursor::operator* () noexcept const
```





**Returns:**

Raw pointer to sector data 





        

<hr>



### function operator-&gt; 

```C++
inline FORCE_INLINE std::byte * ecss::Memory::ChunksAllocator::RangesCursor::operator-> () noexcept const
```




<hr>



### function operator== 

```C++
inline FORCE_INLINE bool ecss::Memory::ChunksAllocator::RangesCursor::operator== (
    const RangesCursor & other
) noexcept const
```




<hr>



### function rawPtr 

```C++
inline FORCE_INLINE std::byte * ecss::Memory::ChunksAllocator::RangesCursor::rawPtr () noexcept const
```




<hr>



### function step 

```C++
inline FORCE_INLINE void ecss::Memory::ChunksAllocator::RangesCursor::step () noexcept
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/memory/ChunksAllocator.h`

