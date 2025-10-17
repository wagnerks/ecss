

# Class ecss::Memory::SectorsArray::RangedIterator



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md) **>** [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) **>** [**RangedIterator**](classecss_1_1Memory_1_1SectorsArray_1_1RangedIterator.md)



[_**Iterator**_](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) _over specified index ranges (includes dead sectors)._[More...](#detailed-description)

* `#include <SectorsArray.h>`





































## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**RangedIterator**](#function-rangediterator) (const [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) \* a, const Ranges&lt; SectorId &gt; & r) <br>_Construct with range definition._  |
|  FORCE\_INLINE void | [**advanceToId**](#function-advancetoid) (SectorId id) <br>_Fast-forward internal cursor near sector id (used by multi-array stitched iteration)._  |
|  FORCE\_INLINE [**RangedIterator**](classecss_1_1Memory_1_1SectorsArray_1_1RangedIterator.md) & | [**operator++**](#function-operator) () noexcept<br> |




























## Detailed Description




**See also:** RangedIteratorAlive for filtered version. 



    
## Public Functions Documentation




### function RangedIterator 

_Construct with range definition._ 
```C++
inline ecss::Memory::SectorsArray::RangedIterator::RangedIterator (
    const SectorsArray * a,
    const Ranges< SectorId > & r
) 
```





**Parameters:**


* `a` Owning container. 
* `r` Ranges object (half-open segments). 




        

<hr>



### function advanceToId 

_Fast-forward internal cursor near sector id (used by multi-array stitched iteration)._ 
```C++
inline FORCE_INLINE void ecss::Memory::SectorsArray::RangedIterator::advanceToId (
    SectorId id
) 
```




<hr>



### function operator++ 

```C++
inline FORCE_INLINE RangedIterator & ecss::Memory::SectorsArray::RangedIterator::operator++ () noexcept
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/memory/SectorsArray.h`

