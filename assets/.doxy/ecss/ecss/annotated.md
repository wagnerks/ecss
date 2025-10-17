
# Class List


Here are the classes, structs, unions and interfaces with brief descriptions:

* **namespace** [**ecss**](namespaceecss.md)     
    * **class** [**ArraysView**](classecss_1_1ArraysView.md) _Iterable view over entities with one main component and optional additional components._     
        * **struct** [**EndIterator**](structecss_1_1ArraysView_1_1EndIterator.md) _Sentinel end iterator tag._ 
        * **class** [**Iterator**](classecss_1_1ArraysView_1_1Iterator.md) _Forward iterator over alive sectors of the main component type._     
    * **namespace** [**Memory**](namespaceecss_1_1Memory.md)     
        * **struct** [**ChunksAllocator**](structecss_1_1Memory_1_1ChunksAllocator.md)     
            * **struct** [**Cursor**](structecss_1_1Memory_1_1ChunksAllocator_1_1Cursor.md)     
            * **struct** [**RangesCursor**](structecss_1_1Memory_1_1ChunksAllocator_1_1RangesCursor.md)     
        * **struct** [**LayoutData**](structecss_1_1Memory_1_1LayoutData.md) _Metadata describing how a component type is laid out within a_ [_**Sector**_](structecss_1_1Memory_1_1Sector.md) _._    
            * **struct** [**FunctionTable**](structecss_1_1Memory_1_1LayoutData_1_1FunctionTable.md)     
        * **struct** [**PinnedSector**](structecss_1_1Memory_1_1PinnedSector.md) _RAII pin for a sector to prevent relocation / destruction while in use._     
        * **class** [**ReflectionHelper**](classecss_1_1Memory_1_1ReflectionHelper.md)     
        * **struct** [**RetireAllocator**](structecss_1_1Memory_1_1RetireAllocator.md) _Allocator that defers memory reclamation to avoid use-after-free during container reallocation._     
        * **struct** [**RetireBin**](structecss_1_1Memory_1_1RetireBin.md)     
        * **struct** [**Sector**](structecss_1_1Memory_1_1Sector.md) [_**Sector**_](structecss_1_1Memory_1_1Sector.md) _stores data for multiple component types; per-type offsets are described by SectorLayoutMeta._    
        * **struct** [**SectorLayoutMeta**](structecss_1_1Memory_1_1SectorLayoutMeta.md)     
            * **class** [**Iterator**](classecss_1_1Memory_1_1SectorLayoutMeta_1_1Iterator.md) _Forward iterator over the contiguous_ [_**LayoutData**_](structecss_1_1Memory_1_1LayoutData.md) _array._    
        * **class** [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) _Container managing sectors of one (or grouped) component layouts with optional thread-safety._     
            * **class** [**Iterator**](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) _Forward iterator over every sector (alive or dead)._     
            * **class** [**IteratorAlive**](classecss_1_1Memory_1_1SectorsArray_1_1IteratorAlive.md) _Forward iterator skipping sectors where a specific component (type mask) isn't alive._     
            * **class** [**RangedIterator**](classecss_1_1Memory_1_1SectorsArray_1_1RangedIterator.md) [_**Iterator**_](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) _over specified index ranges (includes dead sectors)._    
        * **namespace** [**detail**](namespaceecss_1_1Memory_1_1detail.md)     
            * **struct** [**SectorsMap**](structecss_1_1Memory_1_1detail_1_1SectorsMap.md) 
            * **struct** [**SectorsMap&lt; false &gt;**](structecss_1_1Memory_1_1detail_1_1SectorsMap_3_01false_01_4.md)     
    * **struct** [**PinnedComponent**](structecss_1_1PinnedComponent.md) _RAII wrapper that pins the sector holding component T and exposes a typed pointer._     
    * **struct** [**Ranges**](structecss_1_1Ranges.md)     
    * **class** [**Registry**](classecss_1_1Registry.md) _Central ECS registry that owns component sector arrays, entities and iteration utilities._     
    * **namespace** [**Threads**](namespaceecss_1_1Threads.md)     
        * **struct** [**PinCounters**](structecss_1_1Threads_1_1PinCounters.md) _Per-sector pin tracking & synchronization for safe structural mutations._     
        * **struct** [**PinnedIndexesBitMask**](structecss_1_1Threads_1_1PinnedIndexesBitMask.md) _Hierarchical bit mask indexing pinned sector ids._     
    * **struct** [**TypeAccessInfo**](structecss_1_1TypeAccessInfo.md) _Metadata for accessing a component type inside a sectors array._     
    * **namespace** [**types**](namespaceecss_1_1types.md)     
        * **struct** [**OffsetArray**](structecss_1_1types_1_1OffsetArray.md)     
* **struct** [**SectorsView**](structecss_1_1Memory_1_1detail_1_1SectorsMap_3_01true_01_4_1_1SectorsView.md) _Atomic snapshot view for lock-free read of sector pointers (no iteration guarantees)._     

