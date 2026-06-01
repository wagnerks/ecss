

# Class ecss::Memory::SectorsArray::Iterator



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md) **>** [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) **>** [**Iterator**](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md)



_Forward iterator over all slots (alive or dead). Optimized: uses chunk-aware pointer increment for O(1) per-element access. Uses atomic view snapshots for thread-safe iteration._ 

* `#include <SectorsArray.h>`





































## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**Iterator**](#function-iterator) (const [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) \* array, size\_t idx) <br> |
|  FORCE\_INLINE size\_t | [**linearIndex**](#function-linearindex) () noexcept const<br> |
|  FORCE\_INLINE value\_type | [**operator\***](#function-operator) () const<br> |
|  FORCE\_INLINE [**Iterator**](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) | [**operator+**](#function-operator_1) (difference\_type n) noexcept const<br> |
|  FORCE\_INLINE [**Iterator**](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) & | [**operator++**](#function-operator_2) () noexcept<br> |
|  FORCE\_INLINE [**Iterator**](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) & | [**operator+=**](#function-operator_3) (difference\_type n) noexcept<br> |
|  FORCE\_INLINE bool | [**operator==**](#function-operator_4) (const [**Iterator**](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) & other) noexcept const<br> |
|  FORCE\_INLINE std::byte \* | [**rawPtr**](#function-rawptr) () noexcept const<br> |




























## Public Functions Documentation




### function Iterator 

```C++
inline ecss::Memory::SectorsArray::Iterator::Iterator (
    const SectorsArray * array,
    size_t idx
) 
```




<hr>



### function linearIndex 

```C++
inline FORCE_INLINE size_t ecss::Memory::SectorsArray::Iterator::linearIndex () noexcept const
```




<hr>



### function operator\* 

```C++
inline FORCE_INLINE value_type ecss::Memory::SectorsArray::Iterator::operator* () const
```




<hr>



### function operator+ 

```C++
inline FORCE_INLINE Iterator ecss::Memory::SectorsArray::Iterator::operator+ (
    difference_type n
) noexcept const
```




<hr>



### function operator++ 

```C++
inline FORCE_INLINE Iterator & ecss::Memory::SectorsArray::Iterator::operator++ () noexcept
```




<hr>



### function operator+= 

```C++
inline FORCE_INLINE Iterator & ecss::Memory::SectorsArray::Iterator::operator+= (
    difference_type n
) noexcept
```




<hr>



### function operator== 

```C++
inline FORCE_INLINE bool ecss::Memory::SectorsArray::Iterator::operator== (
    const Iterator & other
) noexcept const
```




<hr>



### function rawPtr 

```C++
inline FORCE_INLINE std::byte * ecss::Memory::SectorsArray::Iterator::rawPtr () noexcept const
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/memory/SectorsArray.h`

