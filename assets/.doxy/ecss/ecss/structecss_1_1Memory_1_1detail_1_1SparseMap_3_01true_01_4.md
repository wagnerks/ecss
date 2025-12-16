

# Struct ecss::Memory::detail::SparseMap&lt; true &gt;

**template &lt;&gt;**



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md) **>** [**detail**](namespaceecss_1_1Memory_1_1detail.md) **>** [**SparseMap&lt; true &gt;**](structecss_1_1Memory_1_1detail_1_1SparseMap_3_01true_01_4.md)



_Thread-safe sparse map with atomic view for lock-free reads._ 

* `#include <SectorsArray.h>`





















## Public Attributes

| Type | Name |
| ---: | :--- |
|  std::vector&lt; [**SlotInfo**](structecss_1_1Memory_1_1detail_1_1SlotInfo.md), [**Memory::RetireAllocator**](structecss_1_1Memory_1_1RetireAllocator.md)&lt; [**SlotInfo**](structecss_1_1Memory_1_1detail_1_1SlotInfo.md) &gt; &gt; | [**sparse**](#variable-sparse)   = `{ [**Memory::RetireAllocator**](structecss_1_1Memory_1_1RetireAllocator.md)&lt;[**SlotInfo**](structecss_1_1Memory_1_1detail_1_1SlotInfo.md)&gt;{&bin} }`<br> |
















## Public Functions

| Type | Name |
| ---: | :--- |
|  FORCE\_INLINE size\_t | [**capacity**](#function-capacity) () const<br> |
|  FORCE\_INLINE void | [**drainRetired**](#function-drainretired) () <br> |
|  FORCE\_INLINE [**SlotInfo**](structecss_1_1Memory_1_1detail_1_1SlotInfo.md) | [**find**](#function-find) (SectorId id) const<br> |
|  FORCE\_INLINE uint32\_t | [**findLinearIdx**](#function-findlinearidx) (SectorId id) const<br> |
|  FORCE\_INLINE [**SlotInfo**](structecss_1_1Memory_1_1detail_1_1SlotInfo.md) | [**get**](#function-get) (SectorId id) const<br> |
|  FORCE\_INLINE [**SlotInfo**](structecss_1_1Memory_1_1detail_1_1SlotInfo.md) & | [**operator[]**](#function-operator) (SectorId id) <br> |
|  FORCE\_INLINE const [**SlotInfo**](structecss_1_1Memory_1_1detail_1_1SlotInfo.md) & | [**operator[]**](#function-operator_1) (SectorId id) const<br> |
|  FORCE\_INLINE void | [**resize**](#function-resize) (size\_t newSize) <br> |
|  FORCE\_INLINE void | [**storeView**](#function-storeview) () <br> |




























## Public Attributes Documentation




### variable sparse 

```C++
std::vector<SlotInfo, Memory::RetireAllocator<SlotInfo> > ecss::Memory::detail::SparseMap< true >::sparse;
```




<hr>
## Public Functions Documentation




### function capacity 

```C++
inline FORCE_INLINE size_t ecss::Memory::detail::SparseMap< true >::capacity () const
```




<hr>



### function drainRetired 

```C++
inline FORCE_INLINE void ecss::Memory::detail::SparseMap< true >::drainRetired () 
```




<hr>



### function find 

```C++
inline FORCE_INLINE SlotInfo ecss::Memory::detail::SparseMap< true >::find (
    SectorId id
) const
```




<hr>



### function findLinearIdx 

```C++
inline FORCE_INLINE uint32_t ecss::Memory::detail::SparseMap< true >::findLinearIdx (
    SectorId id
) const
```




<hr>



### function get 

```C++
inline FORCE_INLINE SlotInfo ecss::Memory::detail::SparseMap< true >::get (
    SectorId id
) const
```




<hr>



### function operator[] 

```C++
inline FORCE_INLINE SlotInfo & ecss::Memory::detail::SparseMap< true >::operator[] (
    SectorId id
) 
```




<hr>



### function operator[] 

```C++
inline FORCE_INLINE const SlotInfo & ecss::Memory::detail::SparseMap< true >::operator[] (
    SectorId id
) const
```




<hr>



### function resize 

```C++
inline FORCE_INLINE void ecss::Memory::detail::SparseMap< true >::resize (
    size_t newSize
) 
```




<hr>



### function storeView 

```C++
inline FORCE_INLINE void ecss::Memory::detail::SparseMap< true >::storeView () 
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/memory/SectorsArray.h`

