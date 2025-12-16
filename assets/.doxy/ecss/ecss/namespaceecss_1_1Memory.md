

# Namespace ecss::Memory



[**Namespace List**](namespaces.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md)


















## Namespaces

| Type | Name |
| ---: | :--- |
| namespace | [**Sector**](namespaceecss_1_1Memory_1_1Sector.md) <br>_Namespace containing static functions for component operations within sector data._  |
| namespace | [**detail**](namespaceecss_1_1Memory_1_1detail.md) <br> |


## Classes

| Type | Name |
| ---: | :--- |
| struct | [**ChunksAllocator**](structecss_1_1Memory_1_1ChunksAllocator.md) &lt;ChunkCapacity&gt;<br>_Chunked memory allocator for sector data._  |
| class | [**DenseTypeIdGenerator**](classecss_1_1Memory_1_1DenseTypeIdGenerator.md) <br>_Dense sequential type ID generator for efficient array indexing._  |
| struct | [**LayoutData**](structecss_1_1Memory_1_1LayoutData.md) <br>_Metadata describing how a component type is laid out within sector data._  |
| struct | [**PinnedSector**](structecss_1_1Memory_1_1PinnedSector.md) <br>_RAII pin for a sector to prevent relocation / destruction while in use._  |
| struct | [**RetireAllocator**](structecss_1_1Memory_1_1RetireAllocator.md) &lt;class T&gt;<br>_Allocator that defers memory reclamation to avoid use-after-free during container reallocation._  |
| struct | [**RetireBin**](structecss_1_1Memory_1_1RetireBin.md) <br> |
| struct | [**SectorLayoutMeta**](structecss_1_1Memory_1_1SectorLayoutMeta.md) <br> |
| class | [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) &lt;ThreadSafe, typename Allocator&gt;<br>_SoA-based container managing sector data with external id/isAlive arrays._  |






















## Public Functions

| Type | Name |
| ---: | :--- |
|  FORCE\_INLINE size\_t | [**GlobalTypeId**](#function-globaltypeid) () noexcept<br>_Ultra-fast compile-time type ID generation._  |


## Public Static Functions

| Type | Name |
| ---: | :--- |
|  bool | [**isSameAdr**](#function-issameadr) (T1 \* ptr1, T2 \* ptr2) <br> |
|  uint32\_t | [**nextPowerOfTwo**](#function-nextpoweroftwo) (uint32\_t x) <br> |
|  void \* | [**toAdr**](#function-toadr) (T \* ptr) <br> |


























## Public Functions Documentation




### function GlobalTypeId 

_Ultra-fast compile-time type ID generation._ 
```C++
template<typename T>
FORCE_INLINE size_t ecss::Memory::GlobalTypeId () noexcept
```



Uses address of static variable as unique type identifier. This is resolved at compile/link time - zero runtime overhead. IDs are stable across the program lifetime but NOT dense (not sequential). 


        

<hr>
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

