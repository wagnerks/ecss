

# Struct ecss::Memory::detail::SparseMap&lt; true &gt;

**template &lt;&gt;**



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md) **>** [**detail**](namespaceecss_1_1Memory_1_1detail.md) **>** [**SparseMap&lt; true &gt;**](structecss_1_1Memory_1_1detail_1_1SparseMap_3_01true_01_4.md)



_Thread-safe sparse map with atomic view for lock-free reads Writer: store data (release) then store linearIdx (release)_  _single consistent update. Reader: load linearIdx (acquire), load data (acquire), re-load linearIdx (acquire). If linearIdx unchanged, the pair is consistent. Otherwise retry (seqlock pattern). On the hot path (no concurrent write) this is one load + one branch, never retries._[More...](#detailed-description)

* `#include <SectorsArray.h>`















## Classes

| Type | Name |
| ---: | :--- |
| struct | [**SparseView**](structecss_1_1Memory_1_1detail_1_1SparseMap_3_01true_01_4_1_1SparseView.md) &lt;&gt;<br>_Consistent {table, size} pair handed to readers by_ [_**loadView()**_](structecss_1_1Memory_1_1detail_1_1SparseMap_3_01true_01_4.md#function-loadview) _._ |






## Public Attributes

| Type | Name |
| ---: | :--- |
|  std::vector&lt; uint32\_t, [**Memory::RetireAllocator**](structecss_1_1Memory_1_1RetireAllocator.md)&lt; uint32\_t &gt; &gt; | [**sparse**](#variable-sparse)   = `{ [**Memory::RetireAllocator**](structecss_1_1Memory_1_1RetireAllocator.md)&lt;uint32\_t&gt;{&bin} }`<br> |
















## Public Functions

| Type | Name |
| ---: | :--- |
|  FORCE\_INLINE size\_t | [**capacity**](#function-capacity) () const<br> |
|  FORCE\_INLINE void | [**drainRetired**](#function-drainretired) () <br> |
|  FORCE\_INLINE uint32\_t | [**findIdx**](#function-findidx) (SectorId id) const<br> |
|  FORCE\_INLINE [**SparseView**](structecss_1_1Memory_1_1detail_1_1SparseMap_3_01true_01_4_1_1SparseView.md) | [**loadView**](#function-loadview) () noexcept const<br>_Seqlock snapshot of {table, size}; the table itself is retire-allocated, so a snapshot stays readable after a resize._  |
|  FORCE\_INLINE void | [**resize**](#function-resize) (size\_t newSize) <br>_Resize and publish (caller holds the write lock)._  |
|  FORCE\_INLINE void | [**set**](#function-set) (SectorId id, uint32\_t idx) <br>_Point_ `id` _at linear index_`idx` _, or INVALID\_IDX to clear it. A single release store: the entry is one word, so there is nothing to tear._ |
|  FORCE\_INLINE void | [**setGracePeriod**](#function-setgraceperiod) (uint32\_t ticks) <br> |
|  FORCE\_INLINE void | [**storeView**](#function-storeview) () <br> |
|  FORCE\_INLINE size\_t | [**tickRetired**](#function-tickretired) () <br> |




























## Detailed Description


Thread-safe sparse map: sector id -&gt; linear index.


The entry used to be {data pointer, linearIdx}  16 bytes, which cannot be read atomically, so every lookup ran a per-slot seqlock (load idx, load data, re-load idx, retry) and every update wrote three times. Storing the index alone makes an entry a single 4-byte atomic: one load to read, one store to write, no retry loop. It also quarters the table, which is what actually decides the cost of a random lookup once it stops fitting in cache (measured -40% at 200k ids, -17% at 1M).


The data pointer is recovered from the chunk snapshot instead, which is one extra dependent load from a table that is a few hundred bytes and always hot. 


    
## Public Attributes Documentation




### variable sparse 

```C++
std::vector<uint32_t, Memory::RetireAllocator<uint32_t> > ecss::Memory::detail::SparseMap< true >::sparse;
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



### function findIdx 

```C++
inline FORCE_INLINE uint32_t ecss::Memory::detail::SparseMap< true >::findIdx (
    SectorId id
) const
```




<hr>



### function loadView 

_Seqlock snapshot of {table, size}; the table itself is retire-allocated, so a snapshot stays readable after a resize._ 
```C++
inline FORCE_INLINE SparseView ecss::Memory::detail::SparseMap< true >::loadView () noexcept const
```




<hr>



### function resize 

_Resize and publish (caller holds the write lock)._ 
```C++
inline FORCE_INLINE void ecss::Memory::detail::SparseMap< true >::resize (
    size_t newSize
) 
```




<hr>



### function set 

_Point_ `id` _at linear index_`idx` _, or INVALID\_IDX to clear it. A single release store: the entry is one word, so there is nothing to tear._
```C++
inline FORCE_INLINE void ecss::Memory::detail::SparseMap< true >::set (
    SectorId id,
    uint32_t idx
) 
```




<hr>



### function setGracePeriod 

```C++
inline FORCE_INLINE void ecss::Memory::detail::SparseMap< true >::setGracePeriod (
    uint32_t ticks
) 
```




<hr>



### function storeView 

```C++
inline FORCE_INLINE void ecss::Memory::detail::SparseMap< true >::storeView () 
```




<hr>



### function tickRetired 

```C++
inline FORCE_INLINE size_t ecss::Memory::detail::SparseMap< true >::tickRetired () 
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/memory/SectorsArray.h`

