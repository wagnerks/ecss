

# Struct ecss::Memory::ChunksAllocator::ChunksView



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md) **>** [**ChunksAllocator**](structecss_1_1Memory_1_1ChunksAllocator.md) **>** [**ChunksView**](structecss_1_1Memory_1_1ChunksAllocator_1_1ChunksView.md)



_Consistent {chunk table, count} pair for lock-free readers._ 

* `#include <ChunksAllocator.h>`





















## Public Attributes

| Type | Name |
| ---: | :--- |
|  void \*const  \* | [**chunks**](#variable-chunks)   = `nullptr`<br> |
|  size\_t | [**count**](#variable-count)   = `0`<br> |
|  uint16\_t | [**sectorSize**](#variable-sectorsize)   = `0`<br> |












































## Public Attributes Documentation




### variable chunks 

```C++
void* const* ecss::Memory::ChunksAllocator< ChunkCapacity >::ChunksView::chunks;
```




<hr>



### variable count 

```C++
size_t ecss::Memory::ChunksAllocator< ChunkCapacity >::ChunksView::count;
```




<hr>



### variable sectorSize 

```C++
uint16_t ecss::Memory::ChunksAllocator< ChunkCapacity >::ChunksView::sectorSize;
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/memory/ChunksAllocator.h`

