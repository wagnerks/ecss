

# Struct ecss::Memory::detail::SparseMap&lt; true &gt;

**template &lt;&gt;**



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md) **>** [**detail**](namespaceecss_1_1Memory_1_1detail.md) **>** [**SparseMap&lt; true &gt;**](structecss_1_1Memory_1_1detail_1_1SparseMap_3_01true_01_4.md)



_Thread-safe sparse map with atomic view for lock-free reads Writer: store data (release) then store linearIdx (release)_  _single consistent update. Reader: load linearIdx (acquire), load data (acquire), re-load linearIdx (acquire). If linearIdx unchanged, the pair is consistent. Otherwise retry (seqlock pattern). On the hot path (no concurrent write) this is one load + one branch, never retries._

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
|  FORCE\_INLINE void | [**resize**](#function-resize) (size\_t newSize) <br>_Resize sparse map and publish view (caller must hold write lock)_  |
|  FORCE\_INLINE void | [**set**](#function-set) (SectorId id, [**SlotInfo**](structecss_1_1Memory_1_1detail_1_1SlotInfo.md) info) <br>_Set slot value atomically. Three-phase write so lock-free readers never observe a torn {data, linearIdx} pair during a valid-&gt;valid update: (1) flash data to nullptr_  _slot appears "not present" to readers mid-update (2) publish new linearIdx_ _reader still sees INVALID\_SLOT (data still null) (3) publish new data_ _commit; slot is now consistently visible Combined with the reader's nullptr short-circuit, every observable state is internally consistent: pre-update: {old\_data, old\_idx} mid-update: INVALID\_SLOT (brief "not present" window) post-update: {new\_data, new\_idx} Cost: one extra atomic store on the write path. Reader hot path unchanged. Note: only one writer runs at a time (caller holds the array's unique write lock), so consecutive_[_**set()**_](structecss_1_1Memory_1_1detail_1_1SparseMap_3_01true_01_4.md#function-set) _calls cannot interleave._ |
|  FORCE\_INLINE void | [**setGracePeriod**](#function-setgraceperiod) (uint32\_t ticks) <br> |
|  FORCE\_INLINE void | [**storeView**](#function-storeview) () <br> |
|  FORCE\_INLINE size\_t | [**tickRetired**](#function-tickretired) () <br> |




























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



### function resize 

_Resize sparse map and publish view (caller must hold write lock)_ 
```C++
inline FORCE_INLINE void ecss::Memory::detail::SparseMap< true >::resize (
    size_t newSize
) 
```




<hr>



### function set 

_Set slot value atomically. Three-phase write so lock-free readers never observe a torn {data, linearIdx} pair during a valid-&gt;valid update: (1) flash data to nullptr_  _slot appears "not present" to readers mid-update (2) publish new linearIdx_ _reader still sees INVALID\_SLOT (data still null) (3) publish new data_ _commit; slot is now consistently visible Combined with the reader's nullptr short-circuit, every observable state is internally consistent: pre-update: {old\_data, old\_idx} mid-update: INVALID\_SLOT (brief "not present" window) post-update: {new\_data, new\_idx} Cost: one extra atomic store on the write path. Reader hot path unchanged. Note: only one writer runs at a time (caller holds the array's unique write lock), so consecutive_[_**set()**_](structecss_1_1Memory_1_1detail_1_1SparseMap_3_01true_01_4.md#function-set) _calls cannot interleave._
```C++
inline FORCE_INLINE void ecss::Memory::detail::SparseMap< true >::set (
    SectorId id,
    SlotInfo info
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

