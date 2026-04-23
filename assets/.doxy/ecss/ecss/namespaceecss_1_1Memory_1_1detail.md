

# Namespace ecss::Memory::detail



[**Namespace List**](namespaces.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md) **>** [**detail**](namespaceecss_1_1Memory_1_1detail.md)




















## Classes

| Type | Name |
| ---: | :--- |
| struct | [**DenseArrays**](structecss_1_1Memory_1_1detail_1_1DenseArrays.md) &lt;TS&gt;<br>_Atomic view for dense arrays (ids + isAlive) for thread-safe iteration._  |
| struct | [**DenseArrays&lt; false &gt;**](structecss_1_1Memory_1_1detail_1_1DenseArrays_3_01false_01_4.md) &lt;&gt;<br>_Non-thread-safe dense arrays (simple vectors)_  |
| struct | [**DenseArrays&lt; true &gt;**](structecss_1_1Memory_1_1detail_1_1DenseArrays_3_01true_01_4.md) &lt;&gt;<br>_Thread-safe dense arrays with atomic view for lock-free reads._  |
| struct | [**SlotInfo**](structecss_1_1Memory_1_1detail_1_1SlotInfo.md) <br>_Slot info for fast sparse lookup - stores data pointer and linear index data pointer (8 bytes) is read/written atomically for thread-safety linearIdx is written BEFORE data on insert, so it's valid when data != nullptr Memory ordering: release on write, acquire on read - ensures linearIdx visible when data is._  |
| struct | [**SparseMap**](structecss_1_1Memory_1_1detail_1_1SparseMap.md) &lt;TS&gt;<br> |
| struct | [**SparseMap&lt; false &gt;**](structecss_1_1Memory_1_1detail_1_1SparseMap_3_01false_01_4.md) &lt;&gt;<br>_Non-thread-safe sparse map (simple vector)_  |
| struct | [**SparseMap&lt; true &gt;**](structecss_1_1Memory_1_1detail_1_1SparseMap_3_01true_01_4.md) &lt;&gt;<br>_Thread-safe sparse map with atomic view for lock-free reads Writer: store data (release) then store linearIdx (release)_  _single consistent update. Reader: load linearIdx (acquire), load data (acquire), re-load linearIdx (acquire). If linearIdx unchanged, the pair is consistent. Otherwise retry (seqlock pattern). On the hot path (no concurrent write) this is one load + one branch, never retries._ |






## Public Attributes

| Type | Name |
| ---: | :--- |
|  [**SlotInfo**](structecss_1_1Memory_1_1detail_1_1SlotInfo.md) | [**INVALID\_SLOT**](#variable-invalid_slot)   = `{ nullptr, INVALID\_IDX }`<br>_Invalid slot info constant._  |












































## Public Attributes Documentation




### variable INVALID\_SLOT 

_Invalid slot info constant._ 
```C++
SlotInfo ecss::Memory::detail::INVALID_SLOT;
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/memory/SectorsArray.h`

