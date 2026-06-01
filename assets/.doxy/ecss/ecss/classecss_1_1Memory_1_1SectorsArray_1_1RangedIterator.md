

# Class ecss::Memory::SectorsArray::RangedIterator



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md) **>** [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) **>** [**RangedIterator**](classecss_1_1Memory_1_1SectorsArray_1_1RangedIterator.md)



[_**Iterator**_](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) _over sectors whose IDs fall within specified SectorId ranges. Converts SectorId ranges to linear index ranges using binary search. Optimized: chunk-aware pointer access. Uses atomic view snapshots for thread-safe iteration._

* `#include <SectorsArray.h>`





































## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**RangedIterator**](#function-rangediterator) (const [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) \* array, const Ranges&lt; SectorId &gt; & ranges) <br> |
|  FORCE\_INLINE void | [**advanceToLinearIdx**](#function-advancetolinearidx) (size\_t targetIdx) <br> |
|  FORCE\_INLINE size\_t | [**linearIndex**](#function-linearindex) () noexcept const<br> |
|  FORCE\_INLINE | [**operator bool**](#function-operator-bool) () noexcept const<br> |
|  FORCE\_INLINE value\_type | [**operator\***](#function-operator) () const<br> |
|  FORCE\_INLINE [**RangedIterator**](classecss_1_1Memory_1_1SectorsArray_1_1RangedIterator.md) & | [**operator++**](#function-operator_1) () noexcept<br> |
|  FORCE\_INLINE bool | [**operator==**](#function-operator_2) (const [**RangedIterator**](classecss_1_1Memory_1_1SectorsArray_1_1RangedIterator.md) & other) noexcept const<br> |
|  FORCE\_INLINE std::byte \* | [**rawPtr**](#function-rawptr) () noexcept const<br> |




























## Public Functions Documentation




### function RangedIterator 

```C++
inline ecss::Memory::SectorsArray::RangedIterator::RangedIterator (
    const SectorsArray * array,
    const Ranges< SectorId > & ranges
) 
```




<hr>



### function advanceToLinearIdx 

```C++
inline FORCE_INLINE void ecss::Memory::SectorsArray::RangedIterator::advanceToLinearIdx (
    size_t targetIdx
) 
```




<hr>



### function linearIndex 

```C++
inline FORCE_INLINE size_t ecss::Memory::SectorsArray::RangedIterator::linearIndex () noexcept const
```




<hr>



### function operator bool 

```C++
inline explicit FORCE_INLINE ecss::Memory::SectorsArray::RangedIterator::operator bool () noexcept const
```




<hr>



### function operator\* 

```C++
inline FORCE_INLINE value_type ecss::Memory::SectorsArray::RangedIterator::operator* () const
```




<hr>



### function operator++ 

```C++
inline FORCE_INLINE RangedIterator & ecss::Memory::SectorsArray::RangedIterator::operator++ () noexcept
```




<hr>



### function operator== 

```C++
inline FORCE_INLINE bool ecss::Memory::SectorsArray::RangedIterator::operator== (
    const RangedIterator & other
) noexcept const
```




<hr>



### function rawPtr 

```C++
inline FORCE_INLINE std::byte * ecss::Memory::SectorsArray::RangedIterator::rawPtr () noexcept const
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/memory/SectorsArray.h`

