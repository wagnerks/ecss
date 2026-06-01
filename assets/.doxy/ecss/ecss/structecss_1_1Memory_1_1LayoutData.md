

# Struct ecss::Memory::LayoutData



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md) **>** [**LayoutData**](structecss_1_1Memory_1_1LayoutData.md)



_Metadata describing how a component type is laid out within sector data._ [More...](#detailed-description)

* `#include <SectorLayoutMeta.h>`















## Classes

| Type | Name |
| ---: | :--- |
| struct | [**FunctionTable**](structecss_1_1Memory_1_1LayoutData_1_1FunctionTable.md) <br> |






## Public Attributes

| Type | Name |
| ---: | :--- |
|  FunctionTable | [**functionTable**](#variable-functiontable)  <br> |
|  uint16\_t | [**index**](#variable-index)   = `0`<br> |
|  uint32\_t | [**isAliveMask**](#variable-isalivemask)   = `0`<br> |
|  uint32\_t | [**isNotAliveMask**](#variable-isnotalivemask)   = `0`<br> |
|  bool | [**isTrivial**](#variable-istrivial)   = `false`<br> |
|  uint16\_t | [**offset**](#variable-offset)   = `0`<br> |
|  size\_t | [**typeHash**](#variable-typehash)   = `0`<br> |












































## Detailed Description


Each component stored in a sector has a corresponding [**LayoutData**](structecss_1_1Memory_1_1LayoutData.md) that specifies its byte offset, liveness bit masks, and the functions required to move/copy/destroy the value. This enables type-erased operations while keeping the sector data trivially copyable for trivial types.


Notes:
* For trivial types, `isTrivial` should be true and the `functionTable` may hold empty/no-op functors.
* `isAliveMask` marks the bit(s) that indicate the component is present.
* `isNotAliveMask` is typically the bitwise complement used when clearing.
* Offsets now start from 0 (no embedded id/isAlive header in sector data). 




    
## Public Attributes Documentation




### variable functionTable 

```C++
FunctionTable ecss::Memory::LayoutData::functionTable;
```




<hr>



### variable index 

```C++
uint16_t ecss::Memory::LayoutData::index;
```




<hr>



### variable isAliveMask 

```C++
uint32_t ecss::Memory::LayoutData::isAliveMask;
```




<hr>



### variable isNotAliveMask 

```C++
uint32_t ecss::Memory::LayoutData::isNotAliveMask;
```




<hr>



### variable isTrivial 

```C++
bool ecss::Memory::LayoutData::isTrivial;
```




<hr>



### variable offset 

```C++
uint16_t ecss::Memory::LayoutData::offset;
```




<hr>



### variable typeHash 

```C++
size_t ecss::Memory::LayoutData::typeHash;
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/memory/SectorLayoutMeta.h`

