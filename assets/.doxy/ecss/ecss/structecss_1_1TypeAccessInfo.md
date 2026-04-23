

# Struct ecss::TypeAccessInfo



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**TypeAccessInfo**](structecss_1_1TypeAccessInfo.md)



_Metadata for accessing a component type inside a sectors array._ [More...](#detailed-description)

* `#include <Registry.h>`





















## Public Attributes

| Type | Name |
| ---: | :--- |
|  uint8\_t | [**iteratorIdx**](#variable-iteratoridx)   = `[**kMainIteratorIdx**](structecss_1_1TypeAccessInfo.md#variable-kmainiteratoridx)`<br>_Which secondary iterator provides this type (or 255 if main)._  |
|  uint32\_t | [**typeAliveMask**](#variable-typealivemask)   = `0`<br>_Bit mask for liveness._  |
|  uint16\_t | [**typeOffsetInSector**](#variable-typeoffsetinsector)   = `0`<br>_Byte offset within sector memory._  |


## Public Static Attributes

| Type | Name |
| ---: | :--- |
|  uint8\_t | [**kMainIteratorIdx**](#variable-kmainiteratoridx)   = `255`<br>_Sentinel for "main" component (no secondary iterator)._  |










































## Detailed Description


Used internally by [**ArraysView**](classecss_1_1ArraysView.md) iterator to map component offsets/masks. 


    
## Public Attributes Documentation




### variable iteratorIdx 

_Which secondary iterator provides this type (or 255 if main)._ 
```C++
uint8_t ecss::TypeAccessInfo::iteratorIdx;
```




<hr>



### variable typeAliveMask 

_Bit mask for liveness._ 
```C++
uint32_t ecss::TypeAccessInfo::typeAliveMask;
```




<hr>



### variable typeOffsetInSector 

_Byte offset within sector memory._ 
```C++
uint16_t ecss::TypeAccessInfo::typeOffsetInSector;
```




<hr>
## Public Static Attributes Documentation




### variable kMainIteratorIdx 

_Sentinel for "main" component (no secondary iterator)._ 
```C++
uint8_t ecss::TypeAccessInfo::kMainIteratorIdx;
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/Registry.h`

