

# Struct ecss::Memory::Sector



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md) **>** [**Sector**](structecss_1_1Memory_1_1Sector.md)



[_**Sector**_](structecss_1_1Memory_1_1Sector.md) _stores data for multiple component types; per-type offsets are described by SectorLayoutMeta._[More...](#detailed-description)

* `#include <Sector.h>`





















## Public Attributes

| Type | Name |
| ---: | :--- |
|  SectorId | [**id**](#variable-id)  <br> |
|  uint32\_t | [**isAliveData**](#variable-isalivedata)  <br> |
















## Public Functions

| Type | Name |
| ---: | :--- |
|  void | [**destroyMember**](#function-destroymember) (const [**LayoutData**](structecss_1_1Memory_1_1LayoutData.md) & layout) <br>_Destroy a specific member if alive and clear its liveness bits._  |
|  T \* | [**emplaceMember**](#function-emplacemember-12) (const [**LayoutData**](structecss_1_1Memory_1_1LayoutData.md) & layout, Args &&... args) <br>_(Re)construct member T in place and mark it alive. Destroys previous value if present._  |
|  FORCE\_INLINE constexpr T \* | [**getMember**](#function-getmember-14) (const [**LayoutData**](structecss_1_1Memory_1_1LayoutData.md) & layout) <br> |
|  FORCE\_INLINE constexpr T \* | [**getMember**](#function-getmember-24) (const [**LayoutData**](structecss_1_1Memory_1_1LayoutData.md) & layout) const<br>_Get a member pointer using layout metadata; returns nullptr if not alive._  |
|  FORCE\_INLINE constexpr T \* | [**getMember**](#function-getmember-34) (uint16\_t offset, uint32\_t mask) <br> |
|  FORCE\_INLINE constexpr T \* | [**getMember**](#function-getmember-44) (uint16\_t offset, uint32\_t mask) const<br>_Get a member pointer by offset if the corresponding liveness bit is set._  |
|  FORCE\_INLINE constexpr bool | [**isAlive**](#function-isalive) (size\_t mask) const<br>_Check whether any masked bit is marked alive._  |
|  FORCE\_INLINE constexpr bool | [**isSectorAlive**](#function-issectoralive) () const<br>_Check whether any bit is marked alive._  |
|  FORCE\_INLINE constexpr void | [**markAlive**](#function-markalive) (size\_t mask) <br>_Branch-free convenience for marking bits as alive (sets them to 1)._  |
|  FORCE\_INLINE constexpr void | [**markNotAlive**](#function-marknotalive) (size\_t mask) <br>_Branch-free convenience for marking bits as not alive (clears them to 0)._  |
|  FORCE\_INLINE constexpr void | [**setAlive**](#function-setalive) (size\_t mask, bool value) <br>_Set or clear bits in_ `isAliveData` _._ |


## Public Static Functions

| Type | Name |
| ---: | :--- |
|  T \* | [**copyMember**](#function-copymember-12) (const T & from, [**Sector**](structecss_1_1Memory_1_1Sector.md) \* to, const [**LayoutData**](structecss_1_1Memory_1_1LayoutData.md) & layout) <br>_Copy-assign member T into_ `to` _(replaces existing value) and mark alive._ |
|  void \* | [**copyMember**](#function-copymember-22) (const void \* from, [**Sector**](structecss_1_1Memory_1_1Sector.md) \* to, const [**LayoutData**](structecss_1_1Memory_1_1LayoutData.md) & layout) <br>_Copy-assign an opaque member using layout function table; marks alive if_ `from` _is non-null._ |
|  [**Sector**](structecss_1_1Memory_1_1Sector.md) \* | [**copySector**](#function-copysector) ([**Sector**](structecss_1_1Memory_1_1Sector.md) \* from, [**Sector**](structecss_1_1Memory_1_1Sector.md) \* to, const SectorLayoutMeta \* layouts) <br>_Copy-assign a sector using layout function table._  |
|  void | [**destroySector**](#function-destroysector) ([**Sector**](structecss_1_1Memory_1_1Sector.md) \* sector, const SectorLayoutMeta \* layouts) <br>_Destroy all alive members in the sector using layout metadata and clear liveness bits._  |
|  T \* | [**emplaceMember**](#function-emplacemember-22) ([**Sector**](structecss_1_1Memory_1_1Sector.md) \* sector, const [**LayoutData**](structecss_1_1Memory_1_1LayoutData.md) & layout, Args &&... args) <br>_Static helper: emplace member into a given sector._  |
|  const T \* | [**getComponentFromSector**](#function-getcomponentfromsector-12) (const [**Sector**](structecss_1_1Memory_1_1Sector.md) \* sector, SectorLayoutMeta \* sectorLayout) <br>_Fetch component pointer of type T from a sector using its layout; may be nullptr if not alive._  |
|  T \* | [**getComponentFromSector**](#function-getcomponentfromsector-22) ([**Sector**](structecss_1_1Memory_1_1Sector.md) \* sector, SectorLayoutMeta \* sectorLayout) <br> |
|  FORCE\_INLINE void \* | [**getMemberPtr**](#function-getmemberptr-13) (const [**Sector**](structecss_1_1Memory_1_1Sector.md) \* sectorAdr, uint16\_t offset) <br>_Raw member address by byte offset from the sector base._  |
|  FORCE\_INLINE void \* | [**getMemberPtr**](#function-getmemberptr-23) ([**Sector**](structecss_1_1Memory_1_1Sector.md) \* sectorAdr, uint16\_t offset) <br> |
|  FORCE\_INLINE void \* | [**getMemberPtr**](#function-getmemberptr-33) (std::byte \* sectorAdr, uint16\_t offset) <br> |
|  T \* | [**moveMember**](#function-movemember-12) (T && from, [**Sector**](structecss_1_1Memory_1_1Sector.md) \* to, const [**LayoutData**](structecss_1_1Memory_1_1LayoutData.md) & layout) <br>_Move-assign member T into_ `to` _(replaces existing value) and mark alive._ |
|  void \* | [**moveMember**](#function-movemember-22) (void \* from, [**Sector**](structecss_1_1Memory_1_1Sector.md) \* to, const [**LayoutData**](structecss_1_1Memory_1_1LayoutData.md) & layout) <br>_Move-assign an opaque member using layout function table; marks alive if_ `from` _is non-null._ |
|  [**Sector**](structecss_1_1Memory_1_1Sector.md) \* | [**moveSector**](#function-movesector) ([**Sector**](structecss_1_1Memory_1_1Sector.md) \* from, [**Sector**](structecss_1_1Memory_1_1Sector.md) \* to, const SectorLayoutMeta \* layouts) <br>_Move-assign a sector using layout function table._  |


























## Detailed Description



```C++
[SECTOR]
0x + 0                                       { id }
0x + sizeof(SectorId)                        { isAliveData }
0x + sizeof(Sector)                          { SomeMember }
0x + sizeof(Sector) + sizeof(SomeMember)     { SomeMember1 }
0x + sizeof(Sector) + ... + sizeof(MemberN)  { SomeMemberN }
```
 

**Parameters:**


* `id` [**Sector**](structecss_1_1Memory_1_1Sector.md) identifier. 
* `isAliveData` Bitfield of component liveness; 32 bits =&gt; up to 32 components per sector. 




    
## Public Attributes Documentation




### variable id 

```C++
SectorId ecss::Memory::Sector::id;
```




<hr>



### variable isAliveData 

```C++
uint32_t ecss::Memory::Sector::isAliveData;
```




<hr>
## Public Functions Documentation




### function destroyMember 

_Destroy a specific member if alive and clear its liveness bits._ 
```C++
inline void ecss::Memory::Sector::destroyMember (
    const LayoutData & layout
) 
```




<hr>



### function emplaceMember [1/2]

_(Re)construct member T in place and mark it alive. Destroys previous value if present._ 
```C++
template<typename T, class ... Args>
inline T * ecss::Memory::Sector::emplaceMember (
    const LayoutData & layout,
    Args &&... args
) 
```




<hr>



### function getMember [1/4]

```C++
template<typename T>
inline FORCE_INLINE constexpr T * ecss::Memory::Sector::getMember (
    const LayoutData & layout
) 
```



This is an overloaded member function, provided for convenience. It differs from the above function only in what argument(s) it accepts. 


        

<hr>



### function getMember [2/4]

_Get a member pointer using layout metadata; returns nullptr if not alive._ 
```C++
template<typename T>
inline FORCE_INLINE constexpr T * ecss::Memory::Sector::getMember (
    const LayoutData & layout
) const
```




<hr>



### function getMember [3/4]

```C++
template<typename T>
inline FORCE_INLINE constexpr T * ecss::Memory::Sector::getMember (
    uint16_t offset,
    uint32_t mask
) 
```



This is an overloaded member function, provided for convenience. It differs from the above function only in what argument(s) it accepts. 


        

<hr>



### function getMember [4/4]

_Get a member pointer by offset if the corresponding liveness bit is set._ 
```C++
template<typename T>
inline FORCE_INLINE constexpr T * ecss::Memory::Sector::getMember (
    uint16_t offset,
    uint32_t mask
) const
```





**Returns:**

Pointer to T or nullptr if not alive. 





        

<hr>



### function isAlive 

_Check whether any masked bit is marked alive._ 
```C++
inline FORCE_INLINE constexpr bool ecss::Memory::Sector::isAlive (
    size_t mask
) const
```





**Parameters:**


* `mask` Bitmask to test against `isAliveData`. 



**Returns:**

true if any of the masked bits are set; otherwise, false. 





        

<hr>



### function isSectorAlive 

_Check whether any bit is marked alive._ 
```C++
inline FORCE_INLINE constexpr bool ecss::Memory::Sector::isSectorAlive () const
```





**Returns:**

true if any of the bits are set; otherwise, false. 





        

<hr>



### function markAlive 

_Branch-free convenience for marking bits as alive (sets them to 1)._ 
```C++
inline FORCE_INLINE constexpr void ecss::Memory::Sector::markAlive (
    size_t mask
) 
```





**Parameters:**


* `mask` Bitmask of bits to set in `isAliveData`. 




        

<hr>



### function markNotAlive 

_Branch-free convenience for marking bits as not alive (clears them to 0)._ 
```C++
inline FORCE_INLINE constexpr void ecss::Memory::Sector::markNotAlive (
    size_t mask
) 
```





**Parameters:**


* `mask` Bitmask of bits to clear in `isAliveData`. 




        

<hr>



### function setAlive 

_Set or clear bits in_ `isAliveData` _._
```C++
inline FORCE_INLINE constexpr void ecss::Memory::Sector::setAlive (
    size_t mask,
    bool value
) 
```





**Parameters:**


* `mask` Bitmask of bits to modify (1s mark the affected bits). 
* `value` If true, sets the bits in `mask`; if false, clears them. 



**Note:**

expected that if value == false - mask is already ~mask 





        

<hr>
## Public Static Functions Documentation




### function copyMember [1/2]

_Copy-assign member T into_ `to` _(replaces existing value) and mark alive._
```C++
template<typename T>
static inline T * ecss::Memory::Sector::copyMember (
    const T & from,
    Sector * to,
    const LayoutData & layout
) 
```




<hr>



### function copyMember [2/2]

_Copy-assign an opaque member using layout function table; marks alive if_ `from` _is non-null._
```C++
static inline void * ecss::Memory::Sector::copyMember (
    const void * from,
    Sector * to,
    const LayoutData & layout
) 
```




<hr>



### function copySector 

_Copy-assign a sector using layout function table._ 
```C++
static inline Sector * ecss::Memory::Sector::copySector (
    Sector * from,
    Sector * to,
    const SectorLayoutMeta * layouts
) 
```




<hr>



### function destroySector 

_Destroy all alive members in the sector using layout metadata and clear liveness bits._ 
```C++
static inline void ecss::Memory::Sector::destroySector (
    Sector * sector,
    const SectorLayoutMeta * layouts
) 
```




<hr>



### function emplaceMember [2/2]

_Static helper: emplace member into a given sector._ 
```C++
template<typename T, class ... Args>
static inline T * ecss::Memory::Sector::emplaceMember (
    Sector * sector,
    const LayoutData & layout,
    Args &&... args
) 
```




<hr>



### function getComponentFromSector [1/2]

_Fetch component pointer of type T from a sector using its layout; may be nullptr if not alive._ 
```C++
template<typename T>
static inline const T * ecss::Memory::Sector::getComponentFromSector (
    const Sector * sector,
    SectorLayoutMeta * sectorLayout
) 
```




<hr>



### function getComponentFromSector [2/2]

```C++
template<typename T>
static inline T * ecss::Memory::Sector::getComponentFromSector (
    Sector * sector,
    SectorLayoutMeta * sectorLayout
) 
```



This is an overloaded member function, provided for convenience. It differs from the above function only in what argument(s) it accepts. 


        

<hr>



### function getMemberPtr [1/3]

_Raw member address by byte offset from the sector base._ 
```C++
static inline FORCE_INLINE void * ecss::Memory::Sector::getMemberPtr (
    const Sector * sectorAdr,
    uint16_t offset
) 
```




<hr>



### function getMemberPtr [2/3]

```C++
static inline FORCE_INLINE void * ecss::Memory::Sector::getMemberPtr (
    Sector * sectorAdr,
    uint16_t offset
) 
```



This is an overloaded member function, provided for convenience. It differs from the above function only in what argument(s) it accepts. 


        

<hr>



### function getMemberPtr [3/3]

```C++
static inline FORCE_INLINE void * ecss::Memory::Sector::getMemberPtr (
    std::byte * sectorAdr,
    uint16_t offset
) 
```



This is an overloaded member function, provided for convenience. It differs from the above function only in what argument(s) it accepts. 


        

<hr>



### function moveMember [1/2]

_Move-assign member T into_ `to` _(replaces existing value) and mark alive._
```C++
template<typename T>
static inline T * ecss::Memory::Sector::moveMember (
    T && from,
    Sector * to,
    const LayoutData & layout
) 
```




<hr>



### function moveMember [2/2]

_Move-assign an opaque member using layout function table; marks alive if_ `from` _is non-null._
```C++
static inline void * ecss::Memory::Sector::moveMember (
    void * from,
    Sector * to,
    const LayoutData & layout
) 
```




<hr>



### function moveSector 

_Move-assign a sector using layout function table._ 
```C++
static inline Sector * ecss::Memory::Sector::moveSector (
    Sector * from,
    Sector * to,
    const SectorLayoutMeta * layouts
) 
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/memory/Sector.h`

