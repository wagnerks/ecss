

# Struct ecss::Memory::detail::DenseArrays&lt; true &gt;

**template &lt;&gt;**



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md) **>** [**detail**](namespaceecss_1_1Memory_1_1detail.md) **>** [**DenseArrays&lt; true &gt;**](structecss_1_1Memory_1_1detail_1_1DenseArrays_3_01true_01_4.md)



_Thread-safe dense arrays with atomic view for lock-free reads._ 

* `#include <SectorsArray.h>`















## Classes

| Type | Name |
| ---: | :--- |
| struct | [**View**](structecss_1_1Memory_1_1detail_1_1DenseArrays_3_01true_01_4_1_1View.md) &lt;&gt;<br> |






## Public Attributes

| Type | Name |
| ---: | :--- |
|  [**Memory::RetireBin**](structecss_1_1Memory_1_1RetireBin.md) | [**bin**](#variable-bin)  <br> |
|  std::vector&lt; SectorId, [**Memory::RetireAllocator**](structecss_1_1Memory_1_1RetireAllocator.md)&lt; SectorId &gt; &gt; | [**ids**](#variable-ids)   = `{ [**Memory::RetireAllocator**](structecss_1_1Memory_1_1RetireAllocator.md)&lt;SectorId&gt;{&bin} }`<br> |
|  std::vector&lt; uint32\_t, [**Memory::RetireAllocator**](structecss_1_1Memory_1_1RetireAllocator.md)&lt; uint32\_t &gt; &gt; | [**isAlive**](#variable-isalive)   = `{ [**Memory::RetireAllocator**](structecss_1_1Memory_1_1RetireAllocator.md)&lt;uint32\_t&gt;{&bin} }`<br> |
















## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**DenseArrays**](#function-densearrays-13) () = default<br> |
|   | [**DenseArrays**](#function-densearrays-23) (const [**DenseArrays**](structecss_1_1Memory_1_1detail_1_1DenseArrays.md) & other) <br> |
|   | [**DenseArrays**](#function-densearrays-33) ([**DenseArrays**](structecss_1_1Memory_1_1detail_1_1DenseArrays.md) && other) noexcept<br> |
|  FORCE\_INLINE void | [**clear**](#function-clear) (size\_t actualSize) <br> |
|  FORCE\_INLINE void | [**drainRetired**](#function-drainretired) () <br> |
|  FORCE\_INLINE SectorId & | [**idAt**](#function-idat-12) (size\_t idx) <br> |
|  FORCE\_INLINE const SectorId & | [**idAt**](#function-idat-22) (size\_t idx) const<br> |
|  FORCE\_INLINE uint32\_t & | [**isAliveAt**](#function-isaliveat-12) (size\_t idx) <br> |
|  FORCE\_INLINE const uint32\_t & | [**isAliveAt**](#function-isaliveat-22) (size\_t idx) const<br> |
|  FORCE\_INLINE View | [**loadView**](#function-loadview) () noexcept const<br>_Seqlock snapshot of {ids, isAlive, size}. Hot path (no concurrent writer): 2 x 8B atomic loads (seq) + 3 relaxed loads + 1 compare. On x86-64 all loads compile to plain MOVs and the acquire fence is a no-op. Replaces the previous std::atomic&lt;View&gt; which was mutex-backed (View is 24 bytes, exceeds the 16-byte lock-free boundary on every mainstream platform). Data fields are std::atomic&lt;&gt; with relaxed ordering_  _the seq counter provides all real synchronization; atomics just tell the compiler/TSan these concurrent accesses are intentional (torn reads are detected and retried via the seq comparison)._ |
|  [**DenseArrays**](structecss_1_1Memory_1_1detail_1_1DenseArrays.md) & | [**operator=**](#function-operator) (const [**DenseArrays**](structecss_1_1Memory_1_1detail_1_1DenseArrays.md) & other) <br> |
|  [**DenseArrays**](structecss_1_1Memory_1_1detail_1_1DenseArrays.md) & | [**operator=**](#function-operator_1) ([**DenseArrays**](structecss_1_1Memory_1_1detail_1_1DenseArrays.md) && other) noexcept<br> |
|  FORCE\_INLINE void | [**pushBack**](#function-pushback) (SectorId id, uint32\_t alive) <br> |
|  FORCE\_INLINE void | [**reserve**](#function-reserve) (size\_t newCapacity) <br> |
|  FORCE\_INLINE void | [**resize**](#function-resize) (size\_t newSize, size\_t actualSize) <br> |
|  FORCE\_INLINE void | [**setGracePeriod**](#function-setgraceperiod) (uint32\_t ticks) <br> |
|  FORCE\_INLINE void | [**shrinkToFit**](#function-shrinktofit) () <br> |
|  FORCE\_INLINE void | [**storeView**](#function-storeview) (size\_t size) <br>_Publish a new view. Caller must hold the_ [_**SectorsArray**_](classecss_1_1Memory_1_1SectorsArray.md) _write lock so_[_**storeView()**_](structecss_1_1Memory_1_1detail_1_1DenseArrays_3_01true_01_4.md#function-storeview) _is never called concurrently with itself._[_**loadView()**_](structecss_1_1Memory_1_1detail_1_1DenseArrays_3_01true_01_4.md#function-loadview) _is lock-free and may run concurrently with_[_**storeView()**_](structecss_1_1Memory_1_1detail_1_1DenseArrays_3_01true_01_4.md#function-storeview) _._ |
|  FORCE\_INLINE size\_t | [**tickRetired**](#function-tickretired) () <br> |




























## Public Attributes Documentation




### variable bin 

```C++
Memory::RetireBin ecss::Memory::detail::DenseArrays< true >::bin;
```




<hr>



### variable ids 

```C++
std::vector<SectorId, Memory::RetireAllocator<SectorId> > ecss::Memory::detail::DenseArrays< true >::ids;
```




<hr>



### variable isAlive 

```C++
std::vector<uint32_t, Memory::RetireAllocator<uint32_t> > ecss::Memory::detail::DenseArrays< true >::isAlive;
```




<hr>
## Public Functions Documentation




### function DenseArrays [1/3]

```C++
ecss::Memory::detail::DenseArrays< true >::DenseArrays () = default
```




<hr>



### function DenseArrays [2/3]

```C++
inline ecss::Memory::detail::DenseArrays< true >::DenseArrays (
    const DenseArrays & other
) 
```




<hr>



### function DenseArrays [3/3]

```C++
inline ecss::Memory::detail::DenseArrays< true >::DenseArrays (
    DenseArrays && other
) noexcept
```




<hr>



### function clear 

```C++
inline FORCE_INLINE void ecss::Memory::detail::DenseArrays< true >::clear (
    size_t actualSize
) 
```




<hr>



### function drainRetired 

```C++
inline FORCE_INLINE void ecss::Memory::detail::DenseArrays< true >::drainRetired () 
```




<hr>



### function idAt [1/2]

```C++
inline FORCE_INLINE SectorId & ecss::Memory::detail::DenseArrays< true >::idAt (
    size_t idx
) 
```




<hr>



### function idAt [2/2]

```C++
inline FORCE_INLINE const SectorId & ecss::Memory::detail::DenseArrays< true >::idAt (
    size_t idx
) const
```




<hr>



### function isAliveAt [1/2]

```C++
inline FORCE_INLINE uint32_t & ecss::Memory::detail::DenseArrays< true >::isAliveAt (
    size_t idx
) 
```




<hr>



### function isAliveAt [2/2]

```C++
inline FORCE_INLINE const uint32_t & ecss::Memory::detail::DenseArrays< true >::isAliveAt (
    size_t idx
) const
```




<hr>



### function loadView 

_Seqlock snapshot of {ids, isAlive, size}. Hot path (no concurrent writer): 2 x 8B atomic loads (seq) + 3 relaxed loads + 1 compare. On x86-64 all loads compile to plain MOVs and the acquire fence is a no-op. Replaces the previous std::atomic&lt;View&gt; which was mutex-backed (View is 24 bytes, exceeds the 16-byte lock-free boundary on every mainstream platform). Data fields are std::atomic&lt;&gt; with relaxed ordering_  _the seq counter provides all real synchronization; atomics just tell the compiler/TSan these concurrent accesses are intentional (torn reads are detected and retried via the seq comparison)._
```C++
inline FORCE_INLINE View ecss::Memory::detail::DenseArrays< true >::loadView () noexcept const
```




<hr>



### function operator= 

```C++
inline DenseArrays & ecss::Memory::detail::DenseArrays< true >::operator= (
    const DenseArrays & other
) 
```




<hr>



### function operator= 

```C++
inline DenseArrays & ecss::Memory::detail::DenseArrays< true >::operator= (
    DenseArrays && other
) noexcept
```




<hr>



### function pushBack 

```C++
inline FORCE_INLINE void ecss::Memory::detail::DenseArrays< true >::pushBack (
    SectorId id,
    uint32_t alive
) 
```




<hr>



### function reserve 

```C++
inline FORCE_INLINE void ecss::Memory::detail::DenseArrays< true >::reserve (
    size_t newCapacity
) 
```




<hr>



### function resize 

```C++
inline FORCE_INLINE void ecss::Memory::detail::DenseArrays< true >::resize (
    size_t newSize,
    size_t actualSize
) 
```




<hr>



### function setGracePeriod 

```C++
inline FORCE_INLINE void ecss::Memory::detail::DenseArrays< true >::setGracePeriod (
    uint32_t ticks
) 
```




<hr>



### function shrinkToFit 

```C++
inline FORCE_INLINE void ecss::Memory::detail::DenseArrays< true >::shrinkToFit () 
```




<hr>



### function storeView 

_Publish a new view. Caller must hold the_ [_**SectorsArray**_](classecss_1_1Memory_1_1SectorsArray.md) _write lock so_[_**storeView()**_](structecss_1_1Memory_1_1detail_1_1DenseArrays_3_01true_01_4.md#function-storeview) _is never called concurrently with itself._[_**loadView()**_](structecss_1_1Memory_1_1detail_1_1DenseArrays_3_01true_01_4.md#function-loadview) _is lock-free and may run concurrently with_[_**storeView()**_](structecss_1_1Memory_1_1detail_1_1DenseArrays_3_01true_01_4.md#function-storeview) _._
```C++
inline FORCE_INLINE void ecss::Memory::detail::DenseArrays< true >::storeView (
    size_t size
) 
```




<hr>



### function tickRetired 

```C++
inline FORCE_INLINE size_t ecss::Memory::detail::DenseArrays< true >::tickRetired () 
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/memory/SectorsArray.h`

