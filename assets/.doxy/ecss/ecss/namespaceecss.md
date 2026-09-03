

# Namespace ecss



[**Namespace List**](namespaces.md) **>** [**ecss**](namespaceecss.md)


















## Namespaces

| Type | Name |
| ---: | :--- |
| namespace | [**Memory**](namespaceecss_1_1Memory.md) <br> |
| namespace | [**Threads**](namespaceecss_1_1Threads.md) <br> |
| namespace | [**detail**](namespaceecss_1_1detail.md) <br>_Iterable view over entities with one main component and optional additional components._  |
| namespace | [**types**](namespaceecss_1_1types.md) <br> |


## Classes

| Type | Name |
| ---: | :--- |
| struct | [**AllowNonTrivial**](structecss_1_1AllowNonTrivial.md) &lt;class T&gt;<br>_Declare that a component is knowingly not trivially copyable._  |
| class | [**ArraysView**](classecss_1_1ArraysView.md) &lt;ThreadSafe, typename Allocator, Ranged, typename T, CompTypes&gt;<br>_Iteration over one or more component arrays, driven by the first type named._  |
| class | [**CommandBuffer**](classecss_1_1CommandBuffer.md) &lt;ThreadSafe, typename Allocator&gt;<br>_Records structural changes and applies them together at a chosen point._  |
| struct | [**IdSet**](structecss_1_1IdSet.md) &lt;typename Type, ThreadSafe&gt;<br>_Dense set of allocated ids, one bit per id._  |
| struct | [**IdSet&lt; Type, false &gt;**](structecss_1_1IdSet_3_01Type_00_01false_01_4.md) &lt;typename Type&gt;<br> |
| struct | [**IdSet&lt; Type, true &gt;**](structecss_1_1IdSet_3_01Type_00_01true_01_4.md) &lt;typename Type&gt;<br>_Lock-free live-id set._  |
| struct | [**PinnedComponent**](structecss_1_1PinnedComponent.md) &lt;class T&gt;<br>_RAII wrapper that pins the sector holding component T and exposes a typed pointer._  |
| struct | [**Ranges**](structecss_1_1Ranges.md) &lt;typename Type&gt;<br> |
| struct | [**Read**](structecss_1_1Read.md) &lt;typename T&gt;<br>_Claim a component type for reading._  |
| class | [**Registry**](classecss_1_1Registry.md) &lt;ThreadSafe, typename Allocator&gt;<br>_Central ECS registry that owns component sector arrays, entities and iteration utilities._  |
| struct | [**TypeAccessInfo**](structecss_1_1TypeAccessInfo.md) <br>_Metadata for accessing a component type inside a sectors array._  |
| struct | [**Write**](structecss_1_1Write.md) &lt;typename T&gt;<br>_Claim a component type for writing._  |


## Public Types

| Type | Name |
| ---: | :--- |
| typedef uint16\_t | [**ECSType**](#typedef-ecstype)  <br> |
| typedef SectorId | [**EntityId**](#typedef-entityid)  <br> |
| typedef uint32\_t | [**SectorId**](#typedef-sectorid)  <br> |
| typedef void(\*)(std::string\_view typeName) | [**TrivialityReporter**](#typedef-trivialityreporter)  <br>_Receives the name of every component that costs its array the trivial fast paths._  |




## Public Attributes

| Type | Name |
| ---: | :--- |
|  SectorId | [**INVALID\_ID**](#variable-invalid_id)   = `static\_cast&lt;SectorId&gt;(~SectorId{ 0 })`<br>_Reserved id standing for "no sector"._  |
|  uint32\_t | [**INVALID\_IDX**](#variable-invalid_idx)   = `static\_cast&lt;uint32\_t&gt;(~uint32\_t{ 0 })`<br>_Reserved linear index standing for "not present"._  |
















## Public Functions

| Type | Name |
| ---: | :--- |
|  FORCE\_INLINE void | [**cpuRelax**](#function-cpurelax) () noexcept<br>_CPU pause / yield hint for short seqlock retry loops._  |
|  void | [**setTrivialityReporter**](#function-settrivialityreporter) (TrivialityReporter reporter) noexcept<br>_Route the triviality report into your own log, or pass nullptr to silence it._  |




























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



### typedef TrivialityReporter 

_Receives the name of every component that costs its array the trivial fast paths._ 
```C++
using ecss::TrivialityReporter =  void (*)(std::string_view typeName);
```




<hr>
## Public Attributes Documentation




### variable INVALID\_ID 

_Reserved id standing for "no sector"._ 
```C++
SectorId ecss::INVALID_ID;
```




<hr>



### variable INVALID\_IDX 

_Reserved linear index standing for "not present"._ 
```C++
uint32_t ecss::INVALID_IDX;
```




<hr>
## Public Functions Documentation




### function cpuRelax 

_CPU pause / yield hint for short seqlock retry loops._ 
```C++
FORCE_INLINE void ecss::cpuRelax () noexcept
```




<hr>



### function setTrivialityReporter 

_Route the triviality report into your own log, or pass nullptr to silence it._ 
```C++
inline void ecss::setTrivialityReporter (
    TrivialityReporter reporter
) noexcept
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/Access.h`

