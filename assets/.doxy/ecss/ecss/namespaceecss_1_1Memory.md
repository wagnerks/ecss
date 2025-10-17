

# Namespace ecss::Memory



[**Namespace List**](namespaces.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md)


















## Namespaces

| Type | Name |
| ---: | :--- |
| namespace | [**detail**](namespaceecss_1_1Memory_1_1detail.md) <br> |


## Classes

| Type | Name |
| ---: | :--- |
| struct | [**ChunksAllocator**](structecss_1_1Memory_1_1ChunksAllocator.md) &lt;ChunkCapacity&gt;<br> |
| struct | [**LayoutData**](structecss_1_1Memory_1_1LayoutData.md) <br>_Metadata describing how a component type is laid out within a_ [_**Sector**_](structecss_1_1Memory_1_1Sector.md) _._ |
| struct | [**PinnedSector**](structecss_1_1Memory_1_1PinnedSector.md) <br>_RAII pin for a sector to prevent relocation / destruction while in use._  |
| class | [**ReflectionHelper**](classecss_1_1Memory_1_1ReflectionHelper.md) <br> |
| struct | [**RetireAllocator**](structecss_1_1Memory_1_1RetireAllocator.md) &lt;class T&gt;<br>_Allocator that defers memory reclamation to avoid use-after-free during container reallocation._  |
| struct | [**RetireBin**](structecss_1_1Memory_1_1RetireBin.md) <br> |
| struct | [**Sector**](structecss_1_1Memory_1_1Sector.md) <br>[_**Sector**_](structecss_1_1Memory_1_1Sector.md) _stores data for multiple component types; per-type offsets are described by SectorLayoutMeta._ |
| struct | [**SectorLayoutMeta**](structecss_1_1Memory_1_1SectorLayoutMeta.md) <br> |
| class | [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) &lt;ThreadSafe, typename Allocator&gt;<br>_Container managing sectors of one (or grouped) component layouts with optional thread-safety._  |
























## Public Static Functions

| Type | Name |
| ---: | :--- |
|  bool | [**isSameAdr**](#function-issameadr) (T1 \* ptr1, T2 \* ptr2) <br> |
|  uint32\_t | [**nextPowerOfTwo**](#function-nextpoweroftwo) (uint32\_t x) <br> |
|  void \* | [**toAdr**](#function-toadr) (T \* ptr) <br> |


























## Public Static Functions Documentation




### function isSameAdr 

```C++
template<typename T1, typename T2>
static bool ecss::Memory::isSameAdr (
    T1 * ptr1,
    T2 * ptr2
) 
```




<hr>



### function nextPowerOfTwo 

```C++
static uint32_t ecss::Memory::nextPowerOfTwo (
    uint32_t x
) 
```




<hr>



### function toAdr 

```C++
template<typename T>
static void * ecss::Memory::toAdr (
    T * ptr
) 
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/memory/ChunksAllocator.h`

