

# Class ecss::Memory::SectorLayoutMeta::Iterator



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md) **>** [**SectorLayoutMeta**](structecss_1_1Memory_1_1SectorLayoutMeta.md) **>** [**Iterator**](classecss_1_1Memory_1_1SectorLayoutMeta_1_1Iterator.md)



_Forward iterator over the contiguous_ [_**LayoutData**_](structecss_1_1Memory_1_1LayoutData.md) _array._[More...](#detailed-description)

* `#include <SectorLayoutMeta.h>`

















## Public Types

| Type | Name |
| ---: | :--- |
| typedef std::ptrdiff\_t | [**difference\_type**](#typedef-difference_type)  <br> |
| typedef std::forward\_iterator\_tag | [**iterator\_category**](#typedef-iterator_category)  <br> |
| typedef [**value\_type**](structecss_1_1Memory_1_1LayoutData.md) \* | [**pointer**](#typedef-pointer)  <br> |
| typedef [**value\_type**](structecss_1_1Memory_1_1LayoutData.md) & | [**reference**](#typedef-reference)  <br> |
| typedef const [**LayoutData**](structecss_1_1Memory_1_1LayoutData.md) | [**value\_type**](#typedef-value_type)  <br> |




















## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**Iterator**](#function-iterator) (const SectorLayoutMeta \* layoutMeta, uint8\_t idx) <br> |
|  bool | [**operator!=**](#function-operator) (const [**Iterator**](classecss_1_1Memory_1_1SectorLayoutMeta_1_1Iterator.md) & other) const<br> |
|  [**reference**](structecss_1_1Memory_1_1LayoutData.md) & | [**operator\***](#function-operator_1) () const<br> |
|  [**Iterator**](classecss_1_1Memory_1_1SectorLayoutMeta_1_1Iterator.md) & | [**operator++**](#function-operator_2) () <br> |
|  [**Iterator**](classecss_1_1Memory_1_1SectorLayoutMeta_1_1Iterator.md) | [**operator++**](#function-operator_3) (int) <br> |
|  [**reference**](structecss_1_1Memory_1_1LayoutData.md) & | [**operator-&gt;**](#function-operator-) () const<br> |
|  bool | [**operator==**](#function-operator_4) (const [**Iterator**](classecss_1_1Memory_1_1SectorLayoutMeta_1_1Iterator.md) & other) const<br> |




























## Detailed Description


Provides read-only iteration over all component layout records. 


    
## Public Types Documentation




### typedef difference\_type 

```C++
using ecss::Memory::SectorLayoutMeta::Iterator::difference_type =  std::ptrdiff_t;
```




<hr>



### typedef iterator\_category 

```C++
using ecss::Memory::SectorLayoutMeta::Iterator::iterator_category =  std::forward_iterator_tag;
```




<hr>



### typedef pointer 

```C++
using ecss::Memory::SectorLayoutMeta::Iterator::pointer =  value_type*;
```




<hr>



### typedef reference 

```C++
using ecss::Memory::SectorLayoutMeta::Iterator::reference =  value_type&;
```




<hr>



### typedef value\_type 

```C++
using ecss::Memory::SectorLayoutMeta::Iterator::value_type =  const LayoutData;
```




<hr>
## Public Functions Documentation




### function Iterator 

```C++
inline ecss::Memory::SectorLayoutMeta::Iterator::Iterator (
    const SectorLayoutMeta * layoutMeta,
    uint8_t idx
) 
```




<hr>



### function operator!= 

```C++
inline bool ecss::Memory::SectorLayoutMeta::Iterator::operator!= (
    const Iterator & other
) const
```




<hr>



### function operator\* 

```C++
inline reference & ecss::Memory::SectorLayoutMeta::Iterator::operator* () const
```




<hr>



### function operator++ 

```C++
inline Iterator & ecss::Memory::SectorLayoutMeta::Iterator::operator++ () 
```




<hr>



### function operator++ 

```C++
inline Iterator ecss::Memory::SectorLayoutMeta::Iterator::operator++ (
    int
) 
```




<hr>



### function operator-&gt; 

```C++
inline reference & ecss::Memory::SectorLayoutMeta::Iterator::operator-> () const
```




<hr>



### function operator== 

```C++
inline bool ecss::Memory::SectorLayoutMeta::Iterator::operator== (
    const Iterator & other
) const
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/memory/SectorLayoutMeta.h`

