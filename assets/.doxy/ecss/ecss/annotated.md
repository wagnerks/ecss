
# Class List


Here are the classes, structs, unions and interfaces with brief descriptions:

* **namespace** [**ecss**](namespaceecss.md)     
    * **struct** [**AllowNonTrivial**](structecss_1_1AllowNonTrivial.md) _Declare that a component is knowingly not trivially copyable._ 
    * **class** [**ArraysView**](classecss_1_1ArraysView.md) _Iteration over one or more component arrays, driven by the first type named._     
        * **struct** [**EndIterator**](structecss_1_1ArraysView_1_1EndIterator.md) _Sentinel end iterator tag._ 
        * **class** [**Iterator**](classecss_1_1ArraysView_1_1Iterator.md) _Forward iterator over alive sectors of the main component type._     
    * **class** [**CommandBuffer**](classecss_1_1CommandBuffer.md) _Records structural changes and applies them together at a chosen point._     
    * **struct** [**IdSet**](structecss_1_1IdSet.md) _Dense set of allocated ids, one bit per id._ 
    * **struct** [**IdSet&lt; Type, false &gt;**](structecss_1_1IdSet_3_01Type_00_01false_01_4.md)     
    * **struct** [**IdSet&lt; Type, true &gt;**](structecss_1_1IdSet_3_01Type_00_01true_01_4.md) _Lock-free live-id set._     
    * **namespace** [**Memory**](namespaceecss_1_1Memory.md)     
        * **struct** [**ChunksAllocator**](structecss_1_1Memory_1_1ChunksAllocator.md) _Chunked memory allocator for sector data._     
            * **struct** [**ChunksView**](structecss_1_1Memory_1_1ChunksAllocator_1_1ChunksView.md) _Consistent {chunk table, count} pair for lock-free readers._     
            * **struct** [**Cursor**](structecss_1_1Memory_1_1ChunksAllocator_1_1Cursor.md) [_**Cursor**_](structecss_1_1Memory_1_1ChunksAllocator_1_1Cursor.md) _for linear iteration over sector data._    
            * **struct** [**RangesCursor**](structecss_1_1Memory_1_1ChunksAllocator_1_1RangesCursor.md) [_**Cursor**_](structecss_1_1Memory_1_1ChunksAllocator_1_1Cursor.md) _for ranged iteration over sector data._    
        * **class** [**DenseTypeIdGenerator**](classecss_1_1Memory_1_1DenseTypeIdGenerator.md) _Dense sequential type ID generator for efficient array indexing._     
        * **struct** [**LayoutData**](structecss_1_1Memory_1_1LayoutData.md) _Metadata describing how a component type is laid out within sector data._     
            * **struct** [**FunctionTable**](structecss_1_1Memory_1_1LayoutData_1_1FunctionTable.md)     
        * **struct** [**PinnedSector**](structecss_1_1Memory_1_1PinnedSector.md) _RAII pin for a sector to prevent relocation / destruction while in use._     
        * **struct** [**RetireAllocator**](structecss_1_1Memory_1_1RetireAllocator.md) _Allocator that defers memory reclamation to avoid use-after-free during container reallocation._     
        * **struct** [**RetireBin**](structecss_1_1Memory_1_1RetireBin.md) _Deferred memory reclamation bin with grace period support._     
        * **namespace** [**Sector**](namespaceecss_1_1Memory_1_1Sector.md) _Namespace containing static functions for component operations within sector data._     
        * **struct** [**SectorLayoutMeta**](structecss_1_1Memory_1_1SectorLayoutMeta.md) _Immutable description of one sector's component layout._     
            * **class** [**Iterator**](classecss_1_1Memory_1_1SectorLayoutMeta_1_1Iterator.md) _Forward iterator over the contiguous_ [_**LayoutData**_](structecss_1_1Memory_1_1LayoutData.md) _array._    
        * **class** [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) _SoA-based container managing sector data with external id/isAlive arrays._     
            * **class** [**Iterator**](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) _Forward iterator over all slots (alive or dead). Optimized: uses chunk-aware pointer increment for O(1) per-element access. Uses atomic view snapshots for thread-safe iteration._     
            * **class** [**IteratorAlive**](classecss_1_1Memory_1_1SectorsArray_1_1IteratorAlive.md) _Forward iterator skipping slots where component is not alive. Optimized: uses chunk-aware pointer increment for O(1) per-element access. When isPacked=true (defragmentSize==0), skipDead is bypassed for maximum speed. Uses atomic view snapshots for thread-safe iteration._     
            * **class** [**RangedIterator**](classecss_1_1Memory_1_1SectorsArray_1_1RangedIterator.md) [_**Iterator**_](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) _over sectors whose IDs fall within specified SectorId ranges. Converts SectorId ranges to linear index ranges using binary search. Optimized: chunk-aware pointer access. Uses atomic view snapshots for thread-safe iteration._    
            * **struct** [**SlotInfo**](structecss_1_1Memory_1_1SectorsArray_1_1SlotInfo.md) _Slot info returned by iterators._     
            * **struct** [**StructuralEdit**](structecss_1_1Memory_1_1SectorsArray_1_1StructuralEdit.md) _RAII publication of "sector storage is changing" around a writer body._     
        * **struct** [**StructuralHold**](structecss_1_1Memory_1_1StructuralHold.md) _RAII structural hold: while one is alive, no sector in the array may be relocated._     
        * **namespace** [**detail**](namespaceecss_1_1Memory_1_1detail.md)     
            * **struct** [**DenseArrays**](structecss_1_1Memory_1_1detail_1_1DenseArrays.md) _Atomic view for dense arrays (ids + isAlive) for thread-safe iteration._ 
            * **struct** [**DenseArrays&lt; false &gt;**](structecss_1_1Memory_1_1detail_1_1DenseArrays_3_01false_01_4.md) _Non-thread-safe dense arrays (simple vectors)_     
                * **struct** [**View**](structecss_1_1Memory_1_1detail_1_1DenseArrays_3_01false_01_4_1_1View.md)     
            * **struct** [**DenseArrays&lt; true &gt;**](structecss_1_1Memory_1_1detail_1_1DenseArrays_3_01true_01_4.md) _Thread-safe dense arrays with atomic view for lock-free reads._     
                * **struct** [**View**](structecss_1_1Memory_1_1detail_1_1DenseArrays_3_01true_01_4_1_1View.md)     
            * **struct** [**SlotInfo**](structecss_1_1Memory_1_1detail_1_1SlotInfo.md) _Result of a sparse lookup: the sector data address plus its linear index. This is composed on demand from the stored index and the chunk snapshot_  _it is not what the sparse table holds (that is a bare uint32\_t; see_[_**SparseMap&lt;true&gt;**_](structecss_1_1Memory_1_1detail_1_1SparseMap_3_01true_01_4.md) _)._    
            * **struct** [**SparseMap**](structecss_1_1Memory_1_1detail_1_1SparseMap.md) 
            * **struct** [**SparseMap&lt; false &gt;**](structecss_1_1Memory_1_1detail_1_1SparseMap_3_01false_01_4.md) _Non-thread-safe sparse map: sector id -&gt; linear index._     
            * **struct** [**SparseMap&lt; true &gt;**](structecss_1_1Memory_1_1detail_1_1SparseMap_3_01true_01_4.md) _Thread-safe sparse map with atomic view for lock-free reads Writer: store data (release) then store linearIdx (release)_  _single consistent update. Reader: load linearIdx (acquire), load data (acquire), re-load linearIdx (acquire). If linearIdx unchanged, the pair is consistent. Otherwise retry (seqlock pattern). On the hot path (no concurrent write) this is one load + one branch, never retries._    
                * **struct** [**SparseView**](structecss_1_1Memory_1_1detail_1_1SparseMap_3_01true_01_4_1_1SparseView.md) _Consistent {table, size} pair handed to readers by_ [_**loadView()**_](structecss_1_1Memory_1_1detail_1_1SparseMap_3_01true_01_4.md#function-loadview) _._    
    * **struct** [**PinnedComponent**](structecss_1_1PinnedComponent.md) _RAII wrapper that pins the sector holding component T and exposes a typed pointer._     
    * **struct** [**Ranges**](structecss_1_1Ranges.md)     
    * **struct** [**Read**](structecss_1_1Read.md) _Claim a component type for reading._     
    * **class** [**Registry**](classecss_1_1Registry.md) _Central ECS registry that owns component sector arrays, entities and iteration utilities._     
        * **struct** [**ComponentAccess**](structecss_1_1Registry_1_1ComponentAccess.md) _The array holding T together with T's layout record, from one snapshot load._     
    * **namespace** [**Threads**](namespaceecss_1_1Threads.md)     
        * **struct** [**PinCounters**](structecss_1_1Threads_1_1PinCounters.md) _Per-sector pin tracking & synchronization for safe structural mutations._     
            * **struct** [**WriterIntent**](structecss_1_1Threads_1_1PinCounters_1_1WriterIntent.md) _RAII announcement that a writer wants the array to go quiet._     
        * **struct** [**SelfWaitDebug**](structecss_1_1Threads_1_1SelfWaitDebug.md) _Debug-only record of what the calling thread is holding, per array._     
    * **struct** [**TypeAccessInfo**](structecss_1_1TypeAccessInfo.md) _Metadata for accessing a component type inside a sectors array._     
    * **struct** [**Write**](structecss_1_1Write.md) _Claim a component type for writing._     
    * **namespace** [**detail**](namespaceecss_1_1detail.md) _Iterable view over entities with one main component and optional additional components._     
        * **class** [**AccessGuard**](classecss_1_1detail_1_1AccessGuard.md) _Reader-writer lock per component type, held for the length of a system._     
            * **struct** [**Entry**](structecss_1_1detail_1_1AccessGuard_1_1Entry.md) _One component type's claim: which lock, and whether for writing._     
        * **class** [**AccessScope**](classecss_1_1detail_1_1AccessScope.md) _RAII claim on one component type, for the tracker to see._     
        * **class** [**AccessTracker**](classecss_1_1detail_1_1AccessTracker.md) _Debug-only detector for two threads touching one component type at once._     
        * **struct** [**IdSetLayout**](structecss_1_1detail_1_1IdSetLayout.md) _Bit arithmetic shared by both_ [_**IdSet**_](structecss_1_1IdSet.md) _flavours._    
        * **struct** [**NonTrivialComponent**](structecss_1_1detail_1_1NonTrivialComponent.md) _Instantiated for nothing but its own deprecation warning._ 
    * **namespace** [**types**](namespaceecss_1_1types.md)     
        * **struct** [**EmptyBase**](structecss_1_1types_1_1EmptyBase.md) _Empty base for offset calculation when sector has no header._ 
        * **struct** [**OffsetArray**](structecss_1_1types_1_1OffsetArray.md)     
* **struct** [**IStore**](structecss_1_1CommandBuffer_1_1IStore.md) _Type-erased handle so the buffer can hold one bucket per component type._     
* **struct** [**Store**](structecss_1_1CommandBuffer_1_1Store.md)     
* **struct** [**Latest**](structecss_1_1CommandBuffer_1_1Store_1_1Latest.md)     
* **struct** [**Table**](structecss_1_1IdSet_3_01Type_00_01true_01_4_1_1Table.md) _Immutable published index over blocks. Blocks themselves never move._     
* **struct** [**SpanInfo**](structecss_1_1Memory_1_1ChunksAllocator_1_1RangesCursor_1_1SpanInfo.md)     
* **struct** [**RetiredBlock**](structecss_1_1Memory_1_1RetireBin_1_1RetiredBlock.md)     
* **struct** [**Registered**](structecss_1_1Registry_1_1Registered.md) _Immutable snapshot of the registered arrays, published on registration._ `map` _is indexed by componentTypeId;_`list` _is the de-duplicated array list._    
* **struct** [**HoldShard**](structecss_1_1Threads_1_1PinCounters_1_1HoldShard.md)     
* **struct** [**PinShard**](structecss_1_1Threads_1_1PinCounters_1_1PinShard.md)     
* **struct** [**Table**](structecss_1_1Threads_1_1PinCounters_1_1Table.md) _Immutable published snapshot of the block pointer array._     
* **struct** [**WaiterScope**](structecss_1_1Threads_1_1PinCounters_1_1WaiterScope.md) _RAII announce/retract of a blocked waiter; gates the notify syscalls._     
* **struct** [**Depth**](structecss_1_1detail_1_1AccessGuard_1_1Depth.md)     
* **struct** [**State**](structecss_1_1detail_1_1AccessTracker_1_1State.md)     
* **namespace** [**std**](namespacestd.md) 

