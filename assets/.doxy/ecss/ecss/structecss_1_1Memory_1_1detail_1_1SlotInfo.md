

# Struct ecss::Memory::detail::SlotInfo



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md) **>** [**detail**](namespaceecss_1_1Memory_1_1detail.md) **>** [**SlotInfo**](structecss_1_1Memory_1_1detail_1_1SlotInfo.md)



_Slot info for fast sparse lookup - stores data pointer and linear index This enables O(1) component access with a single sparse map lookup._ 

* `#include <SectorsArray.h>`





















## Public Attributes

| Type | Name |
| ---: | :--- |
|  std::byte \* | [**data**](#variable-data)   = `nullptr`<br>_Direct pointer to sector data (nullptr = not present)_  |
|  uint32\_t | [**linearIdx**](#variable-linearidx)   = `INVALID\_IDX`<br>_Linear index for isAlive access via dense arrays._  |
















## Public Functions

| Type | Name |
| ---: | :--- |
|  FORCE\_INLINE bool | [**isValid**](#function-isvalid) () const<br> |
|  FORCE\_INLINE | [**operator bool**](#function-operator-bool) () const<br> |




























## Public Attributes Documentation




### variable data 

_Direct pointer to sector data (nullptr = not present)_ 
```C++
std::byte* ecss::Memory::detail::SlotInfo::data;
```




<hr>



### variable linearIdx 

_Linear index for isAlive access via dense arrays._ 
```C++
uint32_t ecss::Memory::detail::SlotInfo::linearIdx;
```




<hr>
## Public Functions Documentation




### function isValid 

```C++
inline FORCE_INLINE bool ecss::Memory::detail::SlotInfo::isValid () const
```




<hr>



### function operator bool 

```C++
inline explicit FORCE_INLINE ecss::Memory::detail::SlotInfo::operator bool () const
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/memory/SectorsArray.h`

