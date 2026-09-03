
# Class Hierarchy

This inheritance list is sorted roughly, but not completely, alphabetically:


* **class** [**ecss::ArraysView**](classecss_1_1ArraysView.md) _Iteration over one or more component arrays, driven by the first type named._ 
* **class** [**ecss::ArraysView::Iterator**](classecss_1_1ArraysView_1_1Iterator.md) _Forward iterator over alive sectors of the main component type._ 
* **class** [**ecss::CommandBuffer**](classecss_1_1CommandBuffer.md) _Records structural changes and applies them together at a chosen point._ 
* **class** [**ecss::Memory::DenseTypeIdGenerator**](classecss_1_1Memory_1_1DenseTypeIdGenerator.md) _Dense sequential type ID generator for efficient array indexing._ 
* **class** [**ecss::Memory::SectorLayoutMeta::Iterator**](classecss_1_1Memory_1_1SectorLayoutMeta_1_1Iterator.md) _Forward iterator over the contiguous_ [_**LayoutData**_](structecss_1_1Memory_1_1LayoutData.md) _array._
* **class** [**ecss::Memory::SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) _SoA-based container managing sector data with external id/isAlive arrays._ 
* **class** [**ecss::Memory::SectorsArray::Iterator**](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) _Forward iterator over all slots (alive or dead). Optimized: uses chunk-aware pointer increment for O(1) per-element access. Uses atomic view snapshots for thread-safe iteration._ 
* **class** [**ecss::Memory::SectorsArray::IteratorAlive**](classecss_1_1Memory_1_1SectorsArray_1_1IteratorAlive.md) _Forward iterator skipping slots where component is not alive. Optimized: uses chunk-aware pointer increment for O(1) per-element access. When isPacked=true (defragmentSize==0), skipDead is bypassed for maximum speed. Uses atomic view snapshots for thread-safe iteration._ 
* **class** [**ecss::Memory::SectorsArray::RangedIterator**](classecss_1_1Memory_1_1SectorsArray_1_1RangedIterator.md) [_**Iterator**_](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) _over sectors whose IDs fall within specified SectorId ranges. Converts SectorId ranges to linear index ranges using binary search. Optimized: chunk-aware pointer access. Uses atomic view snapshots for thread-safe iteration._
* **class** [**ecss::Registry**](classecss_1_1Registry.md) _Central ECS registry that owns component sector arrays, entities and iteration utilities._ 
* **class** [**ecss::detail::AccessGuard**](classecss_1_1detail_1_1AccessGuard.md) _Reader-writer lock per component type, held for the length of a system._ 
* **class** [**ecss::detail::AccessScope**](classecss_1_1detail_1_1AccessScope.md) _RAII claim on one component type, for the tracker to see._ 
* **class** [**ecss::detail::AccessTracker**](classecss_1_1detail_1_1AccessTracker.md) _Debug-only detector for two threads touching one component type at once._ 
* **struct** [**ecss::ArraysView::EndIterator**](structecss_1_1ArraysView_1_1EndIterator.md) _Sentinel end iterator tag._ 
* **struct** [**ecss::IdSet**](structecss_1_1IdSet.md) _Dense set of allocated ids, one bit per id._ 
* **struct** [**ecss::detail::IdSetLayout**](structecss_1_1detail_1_1IdSetLayout.md) _Bit arithmetic shared by both_ [_**IdSet**_](structecss_1_1IdSet.md) _flavours._    
    * **struct** [**ecss::IdSet&lt; Type, false &gt;**](structecss_1_1IdSet_3_01Type_00_01false_01_4.md) 
    * **struct** [**ecss::IdSet&lt; Type, true &gt;**](structecss_1_1IdSet_3_01Type_00_01true_01_4.md) _Lock-free live-id set._ 
* **struct** [**ecss::Memory::ChunksAllocator**](structecss_1_1Memory_1_1ChunksAllocator.md) _Chunked memory allocator for sector data._ 
* **struct** [**ecss::Memory::ChunksAllocator::ChunksView**](structecss_1_1Memory_1_1ChunksAllocator_1_1ChunksView.md) _Consistent {chunk table, count} pair for lock-free readers._ 
* **struct** [**ecss::Memory::ChunksAllocator::Cursor**](structecss_1_1Memory_1_1ChunksAllocator_1_1Cursor.md) [_**Cursor**_](structecss_1_1Memory_1_1ChunksAllocator_1_1Cursor.md) _for linear iteration over sector data._
* **struct** [**ecss::Memory::ChunksAllocator::RangesCursor**](structecss_1_1Memory_1_1ChunksAllocator_1_1RangesCursor.md) [_**Cursor**_](structecss_1_1Memory_1_1ChunksAllocator_1_1Cursor.md) _for ranged iteration over sector data._
* **struct** [**ecss::Memory::LayoutData**](structecss_1_1Memory_1_1LayoutData.md) _Metadata describing how a component type is laid out within sector data._ 
* **struct** [**ecss::Memory::LayoutData::FunctionTable**](structecss_1_1Memory_1_1LayoutData_1_1FunctionTable.md) 
* **struct** [**ecss::Memory::PinnedSector**](structecss_1_1Memory_1_1PinnedSector.md) _RAII pin for a sector to prevent relocation / destruction while in use._ 
* **struct** [**ecss::Memory::RetireAllocator**](structecss_1_1Memory_1_1RetireAllocator.md) _Allocator that defers memory reclamation to avoid use-after-free during container reallocation._ 
* **struct** [**ecss::Memory::RetireBin**](structecss_1_1Memory_1_1RetireBin.md) _Deferred memory reclamation bin with grace period support._ 
* **struct** [**ecss::Memory::SectorLayoutMeta**](structecss_1_1Memory_1_1SectorLayoutMeta.md) _Immutable description of one sector's component layout._ 
* **struct** [**ecss::Memory::SectorsArray::SlotInfo**](structecss_1_1Memory_1_1SectorsArray_1_1SlotInfo.md) _Slot info returned by iterators._ 
* **struct** [**ecss::Memory::SectorsArray::StructuralEdit**](structecss_1_1Memory_1_1SectorsArray_1_1StructuralEdit.md) _RAII publication of "sector storage is changing" around a writer body._ 
* **struct** [**ecss::Memory::StructuralHold**](structecss_1_1Memory_1_1StructuralHold.md) _RAII structural hold: while one is alive, no sector in the array may be relocated._ 
* **struct** [**ecss::Memory::detail::DenseArrays**](structecss_1_1Memory_1_1detail_1_1DenseArrays.md) _Atomic view for dense arrays (ids + isAlive) for thread-safe iteration._ 
* **struct** [**ecss::Memory::detail::DenseArrays&lt; false &gt;**](structecss_1_1Memory_1_1detail_1_1DenseArrays_3_01false_01_4.md) _Non-thread-safe dense arrays (simple vectors)_ 
* **struct** [**ecss::Memory::detail::DenseArrays&lt; false &gt;::View**](structecss_1_1Memory_1_1detail_1_1DenseArrays_3_01false_01_4_1_1View.md) 
* **struct** [**ecss::Memory::detail::DenseArrays&lt; true &gt;**](structecss_1_1Memory_1_1detail_1_1DenseArrays_3_01true_01_4.md) _Thread-safe dense arrays with atomic view for lock-free reads._ 
* **struct** [**ecss::Memory::detail::DenseArrays&lt; true &gt;::View**](structecss_1_1Memory_1_1detail_1_1DenseArrays_3_01true_01_4_1_1View.md) 
* **struct** [**ecss::Memory::detail::SlotInfo**](structecss_1_1Memory_1_1detail_1_1SlotInfo.md) _Result of a sparse lookup: the sector data address plus its linear index. This is composed on demand from the stored index and the chunk snapshot_  _it is not what the sparse table holds (that is a bare uint32\_t; see_[_**SparseMap&lt;true&gt;**_](structecss_1_1Memory_1_1detail_1_1SparseMap_3_01true_01_4.md) _)._
* **struct** [**ecss::Memory::detail::SparseMap**](structecss_1_1Memory_1_1detail_1_1SparseMap.md) 
* **struct** [**ecss::Memory::detail::SparseMap&lt; false &gt;**](structecss_1_1Memory_1_1detail_1_1SparseMap_3_01false_01_4.md) _Non-thread-safe sparse map: sector id -&gt; linear index._ 
* **struct** [**ecss::Memory::detail::SparseMap&lt; true &gt;**](structecss_1_1Memory_1_1detail_1_1SparseMap_3_01true_01_4.md) _Thread-safe sparse map with atomic view for lock-free reads Writer: store data (release) then store linearIdx (release)_  _single consistent update. Reader: load linearIdx (acquire), load data (acquire), re-load linearIdx (acquire). If linearIdx unchanged, the pair is consistent. Otherwise retry (seqlock pattern). On the hot path (no concurrent write) this is one load + one branch, never retries._
* **struct** [**ecss::Memory::detail::SparseMap&lt; true &gt;::SparseView**](structecss_1_1Memory_1_1detail_1_1SparseMap_3_01true_01_4_1_1SparseView.md) _Consistent {table, size} pair handed to readers by_ [_**loadView()**_](structecss_1_1Memory_1_1detail_1_1SparseMap_3_01true_01_4.md#function-loadview) _._
* **struct** [**ecss::PinnedComponent**](structecss_1_1PinnedComponent.md) _RAII wrapper that pins the sector holding component T and exposes a typed pointer._ 
* **struct** [**ecss::Ranges**](structecss_1_1Ranges.md) 
* **struct** [**ecss::Read**](structecss_1_1Read.md) _Claim a component type for reading._ 
* **struct** [**ecss::Registry::ComponentAccess**](structecss_1_1Registry_1_1ComponentAccess.md) _The array holding T together with T's layout record, from one snapshot load._ 
* **struct** [**ecss::Threads::PinCounters**](structecss_1_1Threads_1_1PinCounters.md) _Per-sector pin tracking & synchronization for safe structural mutations._ 
* **struct** [**ecss::Threads::PinCounters::WriterIntent**](structecss_1_1Threads_1_1PinCounters_1_1WriterIntent.md) _RAII announcement that a writer wants the array to go quiet._ 
* **struct** [**ecss::Threads::SelfWaitDebug**](structecss_1_1Threads_1_1SelfWaitDebug.md) _Debug-only record of what the calling thread is holding, per array._ 
* **struct** [**ecss::TypeAccessInfo**](structecss_1_1TypeAccessInfo.md) _Metadata for accessing a component type inside a sectors array._ 
* **struct** [**ecss::Write**](structecss_1_1Write.md) _Claim a component type for writing._ 
* **struct** [**ecss::detail::AccessGuard::Entry**](structecss_1_1detail_1_1AccessGuard_1_1Entry.md) _One component type's claim: which lock, and whether for writing._ 
* **struct** [**ecss::detail::NonTrivialComponent**](structecss_1_1detail_1_1NonTrivialComponent.md) _Instantiated for nothing but its own deprecation warning._ 
* **struct** [**ecss::types::EmptyBase**](structecss_1_1types_1_1EmptyBase.md) _Empty base for offset calculation when sector has no header._ 
* **struct** [**ecss::types::OffsetArray**](structecss_1_1types_1_1OffsetArray.md) 
* **struct** [**ecss::CommandBuffer::IStore**](structecss_1_1CommandBuffer_1_1IStore.md) _Type-erased handle so the buffer can hold one bucket per component type._ 
* **struct** [**ecss::CommandBuffer::Store::Latest**](structecss_1_1CommandBuffer_1_1Store_1_1Latest.md) 
* **struct** [**ecss::IdSet&lt; Type, true &gt;::Table**](structecss_1_1IdSet_3_01Type_00_01true_01_4_1_1Table.md) _Immutable published index over blocks. Blocks themselves never move._ 
* **struct** [**ecss::Memory::ChunksAllocator::RangesCursor::SpanInfo**](structecss_1_1Memory_1_1ChunksAllocator_1_1RangesCursor_1_1SpanInfo.md) 
* **struct** [**ecss::Memory::RetireBin::RetiredBlock**](structecss_1_1Memory_1_1RetireBin_1_1RetiredBlock.md) 
* **struct** [**ecss::Registry::Registered**](structecss_1_1Registry_1_1Registered.md) _Immutable snapshot of the registered arrays, published on registration._ `map` _is indexed by componentTypeId;_`list` _is the de-duplicated array list._
* **struct** [**ecss::Threads::PinCounters::HoldShard**](structecss_1_1Threads_1_1PinCounters_1_1HoldShard.md) 
* **struct** [**ecss::Threads::PinCounters::PinShard**](structecss_1_1Threads_1_1PinCounters_1_1PinShard.md) 
* **struct** [**ecss::Threads::PinCounters::Table**](structecss_1_1Threads_1_1PinCounters_1_1Table.md) _Immutable published snapshot of the block pointer array._ 
* **struct** [**ecss::Threads::PinCounters::WaiterScope**](structecss_1_1Threads_1_1PinCounters_1_1WaiterScope.md) _RAII announce/retract of a blocked waiter; gates the notify syscalls._ 
* **struct** [**ecss::detail::AccessGuard::Depth**](structecss_1_1detail_1_1AccessGuard_1_1Depth.md) 
* **struct** [**ecss::detail::AccessTracker::State**](structecss_1_1detail_1_1AccessTracker_1_1State.md) 
* **class** **std::false_type**    
    * **struct** [**ecss::AllowNonTrivial**](structecss_1_1AllowNonTrivial.md) _Declare that a component is knowingly not trivially copyable._ 
* **class** **ecss::CommandBuffer< ThreadSafe, Allocator >::IStore**    
    * **struct** [**ecss::CommandBuffer::Store**](structecss_1_1CommandBuffer_1_1Store.md) 

