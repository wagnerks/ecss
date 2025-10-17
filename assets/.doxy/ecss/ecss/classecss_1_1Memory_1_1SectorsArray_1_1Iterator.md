

# Class ecss::Memory::SectorsArray::Iterator



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md) **>** [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) **>** [**Iterator**](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md)



_Forward iterator over every sector (alive or dead)._ [More...](#detailed-description)

* `#include <SectorsArray.h>`





































## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**Iterator**](#function-iterator) (const [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) \* array, size\_t idx) <br>_Construct from owning container and linear index._  |
|  FORCE\_INLINE [**Iterator**](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) | [**operator+**](#function-operator) (difference\_type n) noexcept const<br> |
|  FORCE\_INLINE [**Iterator**](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) & | [**operator++**](#function-operator_1) () noexcept<br> |
|  FORCE\_INLINE [**Iterator**](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) & | [**operator+=**](#function-operator_2) (difference\_type n) noexcept<br> |
|  FORCE\_INLINE reference | [**operator[]**](#function-operator_3) (difference\_type n) noexcept const<br> |




























## Detailed Description




**Note:**

Returned Sector\* may contain dead members. Use [**IteratorAlive**](classecss_1_1Memory_1_1SectorsArray_1_1IteratorAlive.md) to skip dead ones. 





    
## Public Functions Documentation




### function Iterator 

_Construct from owning container and linear index._ 
```C++
inline ecss::Memory::SectorsArray::Iterator::Iterator (
    const SectorsArray * array,
    size_t idx
) 
```





**Parameters:**


* `array` Owning [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md). 
* `idx` Starting linear index (clamped to size). 




        

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



### function operator[] 

```C++
inline FORCE_INLINE reference ecss::Memory::SectorsArray::Iterator::operator[] (
    difference_type n
) noexcept const
```




<hr>## Friends Documentation





### friend operator+ 

```C++
inline Iterator ecss::Memory::SectorsArray::Iterator::operator+ (
    difference_type n,
    Iterator it
) noexcept
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/memory/SectorsArray.h`

