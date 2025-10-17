
# Class Hierarchy

This inheritance list is sorted roughly, but not completely, alphabetically:


* **class** [**ecss::ArraysView**](classecss_1_1ArraysView.md) _Iterable view over entities with one main component and optional additional components._ 
* **class** [**ecss::ArraysView::Iterator**](classecss_1_1ArraysView_1_1Iterator.md) _Forward iterator over alive sectors of the main component type._ 
* **class** [**ecss::Memory::ReflectionHelper**](classecss_1_1Memory_1_1ReflectionHelper.md) 
* **class** [**ecss::Memory::SectorLayoutMeta::Iterator**](classecss_1_1Memory_1_1SectorLayoutMeta_1_1Iterator.md) _Forward iterator over the contiguous_ [_**LayoutData**_](structecss_1_1Memory_1_1LayoutData.md) _array._
* **class** [**ecss::Memory::SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) _Container managing sectors of one (or grouped) component layouts with optional thread-safety._ 
* **class** [**ecss::Memory::SectorsArray::Iterator**](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) _Forward iterator over every sector (alive or dead)._ 
* **class** [**ecss::Memory::SectorsArray::IteratorAlive**](classecss_1_1Memory_1_1SectorsArray_1_1IteratorAlive.md) _Forward iterator skipping sectors where a specific component (type mask) isn't alive._ 
* **class** [**ecss::Memory::SectorsArray::RangedIterator**](classecss_1_1Memory_1_1SectorsArray_1_1RangedIterator.md) [_**Iterator**_](classecss_1_1Memory_1_1SectorsArray_1_1Iterator.md) _over specified index ranges (includes dead sectors)._
* **class** [**ecss::Registry**](classecss_1_1Registry.md) _Central ECS registry that owns component sector arrays, entities and iteration utilities._ 
* **struct** [**ecss::ArraysView::EndIterator**](structecss_1_1ArraysView_1_1EndIterator.md) _Sentinel end iterator tag._ 
* **struct** [**ecss::Memory::ChunksAllocator**](structecss_1_1Memory_1_1ChunksAllocator.md) 
* **struct** [**ecss::Memory::ChunksAllocator::Cursor**](structecss_1_1Memory_1_1ChunksAllocator_1_1Cursor.md) 
* **struct** [**ecss::Memory::ChunksAllocator::RangesCursor**](structecss_1_1Memory_1_1ChunksAllocator_1_1RangesCursor.md) 
* **struct** [**ecss::Memory::LayoutData**](structecss_1_1Memory_1_1LayoutData.md) _Metadata describing how a component type is laid out within a_ [_**Sector**_](structecss_1_1Memory_1_1Sector.md) _._
* **struct** [**ecss::Memory::LayoutData::FunctionTable**](structecss_1_1Memory_1_1LayoutData_1_1FunctionTable.md) 
* **struct** [**ecss::Memory::PinnedSector**](structecss_1_1Memory_1_1PinnedSector.md) _RAII pin for a sector to prevent relocation / destruction while in use._ 
* **struct** [**ecss::Memory::RetireAllocator**](structecss_1_1Memory_1_1RetireAllocator.md) _Allocator that defers memory reclamation to avoid use-after-free during container reallocation._ 
* **struct** [**ecss::Memory::RetireBin**](structecss_1_1Memory_1_1RetireBin.md) 
* **struct** [**ecss::Memory::Sector**](structecss_1_1Memory_1_1Sector.md) [_**Sector**_](structecss_1_1Memory_1_1Sector.md) _stores data for multiple component types; per-type offsets are described by SectorLayoutMeta._
* **struct** [**ecss::Memory::SectorLayoutMeta**](structecss_1_1Memory_1_1SectorLayoutMeta.md) 
* **struct** [**ecss::Memory::detail::SectorsMap**](structecss_1_1Memory_1_1detail_1_1SectorsMap.md) 
* **struct** [**ecss::Memory::detail::SectorsMap&lt; false &gt;**](structecss_1_1Memory_1_1detail_1_1SectorsMap_3_01false_01_4.md) 
* **struct** [**ecss::PinnedComponent**](structecss_1_1PinnedComponent.md) _RAII wrapper that pins the sector holding component T and exposes a typed pointer._ 
* **struct** [**ecss::Ranges**](structecss_1_1Ranges.md) 
* **struct** [**ecss::Threads::PinCounters**](structecss_1_1Threads_1_1PinCounters.md) _Per-sector pin tracking & synchronization for safe structural mutations._ 
* **struct** [**ecss::Threads::PinnedIndexesBitMask**](structecss_1_1Threads_1_1PinnedIndexesBitMask.md) _Hierarchical bit mask indexing pinned sector ids._ 
* **struct** [**ecss::TypeAccessInfo**](structecss_1_1TypeAccessInfo.md) _Metadata for accessing a component type inside a sectors array._ 
* **struct** [**ecss::types::OffsetArray**](structecss_1_1types_1_1OffsetArray.md) 
* **struct** [**ecss::Memory::detail::SectorsMap&lt; true &gt;::SectorsView**](structecss_1_1Memory_1_1detail_1_1SectorsMap_3_01true_01_4_1_1SectorsView.md) _Atomic snapshot view for lock-free read of sector pointers (no iteration guarantees)._ 

