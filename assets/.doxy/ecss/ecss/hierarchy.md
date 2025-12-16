
# Class Hierarchy

This inheritance list is sorted roughly, but not completely, alphabetically:


* **class** [**ecss::ArraysView**](classecss_1_1ArraysView.md) _Iterable view over entities with one main component and optional additional components._ 
* **class** [**ecss::ArraysView::Iterator**](classecss_1_1ArraysView_1_1Iterator.md) _Forward iterator over alive sectors of the main component type._ 
* **class** [**ecss::Memory::DenseTypeIdGenerator**](classecss_1_1Memory_1_1DenseTypeIdGenerator.md) _Dense sequential type ID generator for efficient array indexing._ 
* **class** [**ecss::Memory::SectorLayoutMeta::Iterator**](classecss_1_1Memory_1_1SectorLayoutMeta_1_1Iterator.md) _Forward iterator over the contiguous_ [_**LayoutData**_](structecss_1_1Memory_1_1LayoutData.md) _array._
* **class** [**ecss::Memory::SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) _SoA-based container managing sector data with external id/isAlive arrays._ 
* **class** [**ecss::Memory::SectorsArray::Iterator**](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) _Forward iterator over all slots (alive or dead). Optimized: uses chunk-aware pointer increment for O(1) per-element access. Uses atomic view snapshots for thread-safe iteration._ 
* **class** [**ecss::Memory::SectorsArray::IteratorAlive**](classecss_1_1Memory_1_1SectorsArray_1_1IteratorAlive.md) _Forward iterator skipping slots where component is not alive. Optimized: uses chunk-aware pointer increment for O(1) per-element access. When isPacked=true (defragmentSize==0), skipDead is bypassed for maximum speed. Uses atomic view snapshots for thread-safe iteration._ 
* **class** [**ecss::Memory::SectorsArray::RangedIterator**](classecss_1_1Memory_1_1SectorsArray_1_1RangedIterator.md) [_**Iterator**_](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) _over sectors whose IDs fall within specified SectorId ranges. Converts SectorId ranges to linear index ranges using binary search. Optimized: chunk-aware pointer access. Uses atomic view snapshots for thread-safe iteration._
* **class** [**ecss::Registry**](classecss_1_1Registry.md) _Central ECS registry that owns component sector arrays, entities and iteration utilities._ 
* **struct** [**ecss::ArraysView::EndIterator**](structecss_1_1ArraysView_1_1EndIterator.md) _Sentinel end iterator tag._ 
* **struct** [**ecss::Memory::ChunksAllocator**](structecss_1_1Memory_1_1ChunksAllocator.md) _Chunked memory allocator for sector data._ 
* **struct** [**ecss::Memory::ChunksAllocator::Cursor**](structecss_1_1Memory_1_1ChunksAllocator_1_1Cursor.md) [_**Cursor**_](structecss_1_1Memory_1_1ChunksAllocator_1_1Cursor.md) _for linear iteration over sector data._
* **struct** [**ecss::Memory::ChunksAllocator::RangesCursor**](structecss_1_1Memory_1_1ChunksAllocator_1_1RangesCursor.md) [_**Cursor**_](structecss_1_1Memory_1_1ChunksAllocator_1_1Cursor.md) _for ranged iteration over sector data._
* **struct** [**ecss::Memory::LayoutData**](structecss_1_1Memory_1_1LayoutData.md) _Metadata describing how a component type is laid out within sector data._ 
* **struct** [**ecss::Memory::LayoutData::FunctionTable**](structecss_1_1Memory_1_1LayoutData_1_1FunctionTable.md) 
* **struct** [**ecss::Memory::PinnedSector**](structecss_1_1Memory_1_1PinnedSector.md) _RAII pin for a sector to prevent relocation / destruction while in use._ 
* **struct** [**ecss::Memory::RetireAllocator**](structecss_1_1Memory_1_1RetireAllocator.md) _Allocator that defers memory reclamation to avoid use-after-free during container reallocation._ 
* **struct** [**ecss::Memory::RetireBin**](structecss_1_1Memory_1_1RetireBin.md) 
* **struct** [**ecss::Memory::SectorLayoutMeta**](structecss_1_1Memory_1_1SectorLayoutMeta.md) 
* **struct** [**ecss::Memory::SectorsArray::SlotInfo**](structecss_1_1Memory_1_1SectorsArray_1_1SlotInfo.md) _Slot info returned by iterators._ 
* **struct** [**ecss::Memory::detail::DenseArrays**](structecss_1_1Memory_1_1detail_1_1DenseArrays.md) _Atomic view for dense arrays (ids + isAlive) for thread-safe iteration._ 
* **struct** [**ecss::Memory::detail::DenseArrays&lt; false &gt;**](structecss_1_1Memory_1_1detail_1_1DenseArrays_3_01false_01_4.md) _Non-thread-safe dense arrays (simple vectors)_ 
* **struct** [**ecss::Memory::detail::DenseArrays&lt; false &gt;::View**](structecss_1_1Memory_1_1detail_1_1DenseArrays_3_01false_01_4_1_1View.md) 
* **struct** [**ecss::Memory::detail::DenseArrays&lt; true &gt;**](structecss_1_1Memory_1_1detail_1_1DenseArrays_3_01true_01_4.md) _Thread-safe dense arrays with atomic view for lock-free reads._ 
* **struct** [**ecss::Memory::detail::DenseArrays&lt; true &gt;::View**](structecss_1_1Memory_1_1detail_1_1DenseArrays_3_01true_01_4_1_1View.md) 
* **struct** [**ecss::Memory::detail::SlotInfo**](structecss_1_1Memory_1_1detail_1_1SlotInfo.md) _Slot info for fast sparse lookup - stores data pointer and linear index This enables O(1) component access with a single sparse map lookup._ 
* **struct** [**ecss::Memory::detail::SparseMap**](structecss_1_1Memory_1_1detail_1_1SparseMap.md) 
* **struct** [**ecss::Memory::detail::SparseMap&lt; false &gt;**](structecss_1_1Memory_1_1detail_1_1SparseMap_3_01false_01_4.md) _Non-thread-safe sparse map (simple vector)_ 
* **struct** [**ecss::Memory::detail::SparseMap&lt; true &gt;**](structecss_1_1Memory_1_1detail_1_1SparseMap_3_01true_01_4.md) _Thread-safe sparse map with atomic view for lock-free reads._ 
* **struct** [**ecss::PinnedComponent**](structecss_1_1PinnedComponent.md) _RAII wrapper that pins the sector holding component T and exposes a typed pointer._ 
* **struct** [**ecss::Ranges**](structecss_1_1Ranges.md) 
* **struct** [**ecss::Threads::PinCounters**](structecss_1_1Threads_1_1PinCounters.md) _Per-sector pin tracking & synchronization for safe structural mutations._ 
* **struct** [**ecss::Threads::PinnedIndexesBitMask**](structecss_1_1Threads_1_1PinnedIndexesBitMask.md) _Hierarchical bit mask indexing pinned sector ids._ 
* **struct** [**ecss::TypeAccessInfo**](structecss_1_1TypeAccessInfo.md) _Metadata for accessing a component type inside a sectors array._ 
* **struct** [**ecss::types::EmptyBase**](structecss_1_1types_1_1EmptyBase.md) _Empty base for offset calculation when sector has no header._ 
* **struct** [**ecss::types::OffsetArray**](structecss_1_1types_1_1OffsetArray.md) 
* **struct** [**ecss::Memory::ChunksAllocator::RangesCursor::SpanInfo**](structecss_1_1Memory_1_1ChunksAllocator_1_1RangesCursor_1_1SpanInfo.md) 
* **struct** [**ecss::Memory::detail::SparseMap&lt; true &gt;::SparseView**](structecss_1_1Memory_1_1detail_1_1SparseMap_3_01true_01_4_1_1SparseView.md) 

