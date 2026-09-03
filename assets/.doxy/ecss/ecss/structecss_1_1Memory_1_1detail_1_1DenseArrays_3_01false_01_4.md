

# Struct ecss::Memory::detail::DenseArrays&lt; false &gt;

**template &lt;&gt;**



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md) **>** [**detail**](namespaceecss_1_1Memory_1_1detail.md) **>** [**DenseArrays&lt; false &gt;**](structecss_1_1Memory_1_1detail_1_1DenseArrays_3_01false_01_4.md)



_Non-thread-safe dense arrays (simple vectors)_ 

* `#include <SectorsArray.h>`















## Classes

| Type | Name |
| ---: | :--- |
| struct | [**View**](structecss_1_1Memory_1_1detail_1_1DenseArrays_3_01false_01_4_1_1View.md) &lt;&gt;<br> |






## Public Attributes

| Type | Name |
| ---: | :--- |
|  std::vector&lt; SectorId &gt; | [**ids**](#variable-ids)  <br> |
|  std::vector&lt; uint32\_t &gt; | [**isAlive**](#variable-isalive)  <br> |
















## Public Functions

| Type | Name |
| ---: | :--- |
|  FORCE\_INLINE void | [**clear**](#function-clear) (size\_t) <br> |
|  FORCE\_INLINE void | [**drainRetired**](#function-drainretired) () <br> |
|  FORCE\_INLINE SectorId & | [**idAt**](#function-idat-12) (size\_t idx) <br> |
|  FORCE\_INLINE const SectorId & | [**idAt**](#function-idat-22) (size\_t idx) const<br> |
|  FORCE\_INLINE uint32\_t & | [**isAliveAt**](#function-isaliveat-12) (size\_t idx) <br> |
|  FORCE\_INLINE const uint32\_t & | [**isAliveAt**](#function-isaliveat-22) (size\_t idx) const<br> |
|  FORCE\_INLINE View | [**loadView**](#function-loadview) () const<br> |
|  FORCE\_INLINE void | [**pushBack**](#function-pushback) (SectorId id, uint32\_t alive) <br> |
|  FORCE\_INLINE void | [**reserve**](#function-reserve) (size\_t newCapacity) <br> |
|  FORCE\_INLINE void | [**resize**](#function-resize) (size\_t newSize, size\_t) <br> |
|  FORCE\_INLINE void | [**setAliveAt**](#function-setaliveat) (size\_t idx, uint32\_t value) <br> |
|  FORCE\_INLINE void | [**setGracePeriod**](#function-setgraceperiod) (uint32\_t) <br> |
|  FORCE\_INLINE void | [**setIdAt**](#function-setidat) (size\_t idx, SectorId value) <br>_Publish an id or a liveness word to lock-free readers.    No readers to publish to here._  |
|  FORCE\_INLINE void | [**shrinkToFit**](#function-shrinktofit) (size\_t) <br> |
|  FORCE\_INLINE void | [**storeView**](#function-storeview) (size\_t) <br> |
|  FORCE\_INLINE size\_t | [**tickRetired**](#function-tickretired) () <br> |




























## Public Attributes Documentation




### variable ids 

```C++
std::vector<SectorId> ecss::Memory::detail::DenseArrays< false >::ids;
```




<hr>



### variable isAlive 

```C++
std::vector<uint32_t> ecss::Memory::detail::DenseArrays< false >::isAlive;
```




<hr>
## Public Functions Documentation




### function clear 

```C++
inline FORCE_INLINE void ecss::Memory::detail::DenseArrays< false >::clear (
    size_t
) 
```




<hr>



### function drainRetired 

```C++
inline FORCE_INLINE void ecss::Memory::detail::DenseArrays< false >::drainRetired () 
```




<hr>



### function idAt [1/2]

```C++
inline FORCE_INLINE SectorId & ecss::Memory::detail::DenseArrays< false >::idAt (
    size_t idx
) 
```




<hr>



### function idAt [2/2]

```C++
inline FORCE_INLINE const SectorId & ecss::Memory::detail::DenseArrays< false >::idAt (
    size_t idx
) const
```




<hr>



### function isAliveAt [1/2]

```C++
inline FORCE_INLINE uint32_t & ecss::Memory::detail::DenseArrays< false >::isAliveAt (
    size_t idx
) 
```




<hr>



### function isAliveAt [2/2]

```C++
inline FORCE_INLINE const uint32_t & ecss::Memory::detail::DenseArrays< false >::isAliveAt (
    size_t idx
) const
```




<hr>



### function loadView 

```C++
inline FORCE_INLINE View ecss::Memory::detail::DenseArrays< false >::loadView () const
```




<hr>



### function pushBack 

```C++
inline FORCE_INLINE void ecss::Memory::detail::DenseArrays< false >::pushBack (
    SectorId id,
    uint32_t alive
) 
```




<hr>



### function reserve 

```C++
inline FORCE_INLINE void ecss::Memory::detail::DenseArrays< false >::reserve (
    size_t newCapacity
) 
```




<hr>



### function resize 

```C++
inline FORCE_INLINE void ecss::Memory::detail::DenseArrays< false >::resize (
    size_t newSize,
    size_t
) 
```




<hr>



### function setAliveAt 

```C++
inline FORCE_INLINE void ecss::Memory::detail::DenseArrays< false >::setAliveAt (
    size_t idx,
    uint32_t value
) 
```




<hr>



### function setGracePeriod 

```C++
inline FORCE_INLINE void ecss::Memory::detail::DenseArrays< false >::setGracePeriod (
    uint32_t
) 
```




<hr>



### function setIdAt 

_Publish an id or a liveness word to lock-free readers.    No readers to publish to here._ 
```C++
inline FORCE_INLINE void ecss::Memory::detail::DenseArrays< false >::setIdAt (
    size_t idx,
    SectorId value
) 
```



Readers walk these two arrays without any lock, so a writer that assigns to them plainly races even while holding the write lock  the lock excludes other writers, not readers. Release stores here pair with the acquire and relaxed loads on the read side. On x86-64 both are a plain MOV; the atomic\_ref only constrains the compiler, not the hardware.    No readers to publish to here. 


        

<hr>



### function shrinkToFit 

```C++
inline FORCE_INLINE void ecss::Memory::detail::DenseArrays< false >::shrinkToFit (
    size_t
) 
```




<hr>



### function storeView 

```C++
inline FORCE_INLINE void ecss::Memory::detail::DenseArrays< false >::storeView (
    size_t
) 
```




<hr>



### function tickRetired 

```C++
inline FORCE_INLINE size_t ecss::Memory::detail::DenseArrays< false >::tickRetired () 
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/memory/SectorsArray.h`

