

# Namespace ecss::Memory::detail



[**Namespace List**](namespaces.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md) **>** [**detail**](namespaceecss_1_1Memory_1_1detail.md)




















## Classes

| Type | Name |
| ---: | :--- |
| struct | [**DenseArrays**](structecss_1_1Memory_1_1detail_1_1DenseArrays.md) &lt;TS&gt;<br>_Atomic view for dense arrays (ids + isAlive) for thread-safe iteration._  |
| struct | [**DenseArrays&lt; false &gt;**](structecss_1_1Memory_1_1detail_1_1DenseArrays_3_01false_01_4.md) &lt;&gt;<br>_Non-thread-safe dense arrays (simple vectors)_  |
| struct | [**DenseArrays&lt; true &gt;**](structecss_1_1Memory_1_1detail_1_1DenseArrays_3_01true_01_4.md) &lt;&gt;<br>_Thread-safe dense arrays with atomic view for lock-free reads._  |
| struct | [**SlotInfo**](structecss_1_1Memory_1_1detail_1_1SlotInfo.md) <br>_Result of a sparse lookup: the sector data address plus its linear index. This is composed on demand from the stored index and the chunk snapshot_  _it is not what the sparse table holds (that is a bare uint32\_t; see_[_**SparseMap&lt;true&gt;**_](structecss_1_1Memory_1_1detail_1_1SparseMap_3_01true_01_4.md) _)._ |
| struct | [**SparseMap**](structecss_1_1Memory_1_1detail_1_1SparseMap.md) &lt;TS&gt;<br> |
| struct | [**SparseMap&lt; false &gt;**](structecss_1_1Memory_1_1detail_1_1SparseMap_3_01false_01_4.md) &lt;&gt;<br>_Non-thread-safe sparse map: sector id -&gt; linear index._  |
| struct | [**SparseMap&lt; true &gt;**](structecss_1_1Memory_1_1detail_1_1SparseMap_3_01true_01_4.md) &lt;&gt;<br>_Thread-safe sparse map with atomic view for lock-free reads Writer: store data (release) then store linearIdx (release)_  _single consistent update. Reader: load linearIdx (acquire), load data (acquire), re-load linearIdx (acquire). If linearIdx unchanged, the pair is consistent. Otherwise retry (seqlock pattern). On the hot path (no concurrent write) this is one load + one branch, never retries._ |






## Public Attributes

| Type | Name |
| ---: | :--- |
|  [**SlotInfo**](structecss_1_1Memory_1_1detail_1_1SlotInfo.md) | [**INVALID\_SLOT**](#variable-invalid_slot)   = `{ nullptr, INVALID\_IDX }`<br>_Invalid slot info constant._  |
















## Public Functions

| Type | Name |
| ---: | :--- |
|  FORCE\_INLINE void | [**cpuRelax**](#function-cpurelax) () noexcept<br>_CPU pause / yield hint for short seqlock retry loops._  |
|  FORCE\_INLINE uint32\_t | [**loadAliveAcquire**](#function-loadaliveacquire) (const uint32\_t \* p, size\_t i) noexcept<br>_Acquire load of an alive-bit word, pairing with the release fetch\_or in_ [_**Sector::markAlive&lt;true&gt;**_](namespaceecss_1_1Memory_1_1Sector.md#function-markalive) _: a reader seeing a set bit is guaranteed the component bytes written before it. x86-64: plain MOV. ARM64: LDAR instead of LDR._ |
|  FORCE\_INLINE T | [**loadRelaxed**](#function-loadrelaxed) (const T \* p, size\_t i) noexcept<br>_Relaxed load of a word a lock-free reader may catch mid-write._  |




























## Public Attributes Documentation




### variable INVALID\_SLOT 

_Invalid slot info constant._ 
```C++
SlotInfo ecss::Memory::detail::INVALID_SLOT;
```




<hr>
## Public Functions Documentation




### function cpuRelax 

_CPU pause / yield hint for short seqlock retry loops._ 
```C++
FORCE_INLINE void ecss::Memory::detail::cpuRelax () noexcept
```




<hr>



### function loadAliveAcquire 

_Acquire load of an alive-bit word, pairing with the release fetch\_or in_ [_**Sector::markAlive&lt;true&gt;**_](namespaceecss_1_1Memory_1_1Sector.md#function-markalive) _: a reader seeing a set bit is guaranteed the component bytes written before it. x86-64: plain MOV. ARM64: LDAR instead of LDR._
```C++
template<bool ThreadSafe>
FORCE_INLINE uint32_t ecss::Memory::detail::loadAliveAcquire (
    const uint32_t * p,
    size_t i
) noexcept
```




<hr>



### function loadRelaxed 

_Relaxed load of a word a lock-free reader may catch mid-write._ 
```C++
template<bool ThreadSafe, class T>
FORCE_INLINE T ecss::Memory::detail::loadRelaxed (
    const T * p,
    size_t i
) noexcept
```



Compiles to a plain MOV on x86-64 and a plain LDR on ARM64  identical codegen to a raw read. It exists to satisfy the memory model where the word is concurrently written by [**Sector::markAlive&lt;true&gt;**](namespaceecss_1_1Memory_1_1Sector.md#function-markalive)/markNotAlive&lt;true&gt;, and, in the single-threaded build, to stop the vectoriser from reordering the surrounding scan.


Parameterised on ThreadSafe alone rather than living in [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md), so that code which needs the load without naming an allocator can share the one definition. 


        

<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/memory/SectorsArray.h`

