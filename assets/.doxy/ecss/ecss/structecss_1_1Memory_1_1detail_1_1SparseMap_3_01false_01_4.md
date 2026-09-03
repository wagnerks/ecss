

# Struct ecss::Memory::detail::SparseMap&lt; false &gt;

**template &lt;&gt;**



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md) **>** [**detail**](namespaceecss_1_1Memory_1_1detail.md) **>** [**SparseMap&lt; false &gt;**](structecss_1_1Memory_1_1detail_1_1SparseMap_3_01false_01_4.md)



_Non-thread-safe sparse map: sector id -&gt; linear index._ 

* `#include <SectorsArray.h>`





















## Public Attributes

| Type | Name |
| ---: | :--- |
|  std::vector&lt; uint32\_t &gt; | [**sparse**](#variable-sparse)  <br> |
















## Public Functions

| Type | Name |
| ---: | :--- |
|  FORCE\_INLINE size\_t | [**capacity**](#function-capacity) () const<br> |
|  FORCE\_INLINE void | [**drainRetired**](#function-drainretired) () <br> |
|  FORCE\_INLINE uint32\_t | [**findIdx**](#function-findidx) (SectorId id) const<br> |
|  FORCE\_INLINE void | [**resize**](#function-resize) (size\_t newSize) <br> |
|  FORCE\_INLINE void | [**set**](#function-set) (SectorId id, uint32\_t idx) <br> |
|  FORCE\_INLINE void | [**setGracePeriod**](#function-setgraceperiod) (uint32\_t) <br> |
|  FORCE\_INLINE void | [**storeView**](#function-storeview) () <br> |
|  FORCE\_INLINE size\_t | [**tickRetired**](#function-tickretired) () <br> |




























## Public Attributes Documentation




### variable sparse 

```C++
std::vector<uint32_t> ecss::Memory::detail::SparseMap< false >::sparse;
```




<hr>
## Public Functions Documentation




### function capacity 

```C++
inline FORCE_INLINE size_t ecss::Memory::detail::SparseMap< false >::capacity () const
```




<hr>



### function drainRetired 

```C++
inline FORCE_INLINE void ecss::Memory::detail::SparseMap< false >::drainRetired () 
```




<hr>



### function findIdx 

```C++
inline FORCE_INLINE uint32_t ecss::Memory::detail::SparseMap< false >::findIdx (
    SectorId id
) const
```




<hr>



### function resize 

```C++
inline FORCE_INLINE void ecss::Memory::detail::SparseMap< false >::resize (
    size_t newSize
) 
```




<hr>



### function set 

```C++
inline FORCE_INLINE void ecss::Memory::detail::SparseMap< false >::set (
    SectorId id,
    uint32_t idx
) 
```




<hr>



### function setGracePeriod 

```C++
inline FORCE_INLINE void ecss::Memory::detail::SparseMap< false >::setGracePeriod (
    uint32_t
) 
```




<hr>



### function storeView 

```C++
inline FORCE_INLINE void ecss::Memory::detail::SparseMap< false >::storeView () 
```




<hr>



### function tickRetired 

```C++
inline FORCE_INLINE size_t ecss::Memory::detail::SparseMap< false >::tickRetired () 
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/memory/SectorsArray.h`

