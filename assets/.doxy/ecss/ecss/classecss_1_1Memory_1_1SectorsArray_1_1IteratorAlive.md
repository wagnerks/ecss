

# Class ecss::Memory::SectorsArray::IteratorAlive



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md) **>** [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) **>** [**IteratorAlive**](classecss_1_1Memory_1_1SectorsArray_1_1IteratorAlive.md)



_Forward iterator skipping sectors where a specific component (type mask) isn't alive._ [More...](#detailed-description)

* `#include <SectorsArray.h>`





































## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**IteratorAlive**](#function-iteratoralive-12) (const [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) \* array, const Ranges&lt; SectorId &gt; & range, uint32\_t aliveMask) <br>_Ranged constructor (uses pre-built ranges object)._  |
|   | [**IteratorAlive**](#function-iteratoralive-22) (const [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) \* array, size\_t idx, size\_t sz, uint32\_t aliveMask) <br>_Constructor over a linear range [idx, sz)._  |
|  FORCE\_INLINE [**IteratorAlive**](classecss_1_1Memory_1_1SectorsArray_1_1IteratorAlive.md) & | [**operator++**](#function-operator) () <br>_Advance to next alive sector matching mask._  |




























## Detailed Description


Filtering uses a bitmask (aliveMask) and Sector::isAliveData. 


    
## Public Functions Documentation




### function IteratorAlive [1/2]

_Ranged constructor (uses pre-built ranges object)._ 
```C++
inline ecss::Memory::SectorsArray::IteratorAlive::IteratorAlive (
    const SectorsArray * array,
    const Ranges< SectorId > & range,
    uint32_t aliveMask
) 
```





**Parameters:**


* `array` Owning container. 
* `range` Ranges describing sector indices. 
* `aliveMask` Liveness bitmask. 




        

<hr>



### function IteratorAlive [2/2]

_Constructor over a linear range [idx, sz)._ 
```C++
inline ecss::Memory::SectorsArray::IteratorAlive::IteratorAlive (
    const SectorsArray * array,
    size_t idx,
    size_t sz,
    uint32_t aliveMask
) 
```





**Parameters:**


* `array` Owning container. 
* `idx` Start linear index. 
* `sz` End linear size. 
* `aliveMask` Bitmask to test sector liveness for the requested type. 




        

<hr>



### function operator++ 

_Advance to next alive sector matching mask._ 
```C++
inline FORCE_INLINE IteratorAlive & ecss::Memory::SectorsArray::IteratorAlive::operator++ () 
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/memory/SectorsArray.h`

