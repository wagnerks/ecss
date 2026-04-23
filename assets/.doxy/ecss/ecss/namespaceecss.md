

# Namespace ecss



[**Namespace List**](namespaces.md) **>** [**ecss**](namespaceecss.md)


















## Namespaces

| Type | Name |
| ---: | :--- |
| namespace | [**Memory**](namespaceecss_1_1Memory.md) <br> |
| namespace | [**Threads**](namespaceecss_1_1Threads.md) <br> |
| namespace | [**types**](namespaceecss_1_1types.md) <br> |


## Classes

| Type | Name |
| ---: | :--- |
| class | [**ArraysView**](classecss_1_1ArraysView.md) &lt;ThreadSafe, typename Allocator, Ranged, typename T, CompTypes&gt;<br>_Iterable view over entities with one main component and optional additional components._  |
| struct | [**PinnedComponent**](structecss_1_1PinnedComponent.md) &lt;class T&gt;<br>_RAII wrapper that pins the sector holding component T and exposes a typed pointer._  |
| struct | [**Ranges**](structecss_1_1Ranges.md) &lt;typename Type&gt;<br> |
| class | [**Registry**](classecss_1_1Registry.md) &lt;ThreadSafe, typename Allocator&gt;<br>_Central ECS registry that owns component sector arrays, entities and iteration utilities._  |
| struct | [**TypeAccessInfo**](structecss_1_1TypeAccessInfo.md) <br>_Metadata for accessing a component type inside a sectors array._  |


## Public Types

| Type | Name |
| ---: | :--- |
| typedef uint16\_t | [**ECSType**](#typedef-ecstype)  <br> |
| typedef SectorId | [**EntityId**](#typedef-entityid)  <br> |
| typedef uint32\_t | [**SectorId**](#typedef-sectorid)  <br> |




## Public Attributes

| Type | Name |
| ---: | :--- |
|  SectorId | [**INVALID\_ID**](#variable-invalid_id)   = `std::numeric\_limits&lt;SectorId&gt;::max()`<br> |
|  uint32\_t | [**INVALID\_IDX**](#variable-invalid_idx)   = `std::numeric\_limits&lt;uint32\_t&gt;::max()`<br> |












































## Public Types Documentation




### typedef ECSType 

```C++
using ecss::ECSType =  uint16_t;
```




<hr>



### typedef EntityId 

```C++
using ecss::EntityId =  SectorId;
```




<hr>



### typedef SectorId 

```C++
using ecss::SectorId =  uint32_t;
```




<hr>
## Public Attributes Documentation




### variable INVALID\_ID 

```C++
SectorId ecss::INVALID_ID;
```




<hr>



### variable INVALID\_IDX 

```C++
uint32_t ecss::INVALID_IDX;
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/memory/ChunksAllocator.h`

