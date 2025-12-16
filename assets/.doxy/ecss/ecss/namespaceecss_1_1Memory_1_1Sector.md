

# Namespace ecss::Memory::Sector



[**Namespace List**](namespaces.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md) **>** [**Sector**](namespaceecss_1_1Memory_1_1Sector.md)



_Namespace containing static functions for component operations within sector data._ [More...](#detailed-description)






































## Public Functions

| Type | Name |
| ---: | :--- |
|  T \* | [**copyMember**](#function-copymember) (const T & from, std::byte \* toData, uint32\_t & toIsAlive, const [**LayoutData**](structecss_1_1Memory_1_1LayoutData.md) & layout) <br>_Copy-assign member T into destination and mark alive._  |
|  void \* | [**copyMember**](#function-copymember) (const void \* from, std::byte \* toData, uint32\_t & toIsAlive, const [**LayoutData**](structecss_1_1Memory_1_1LayoutData.md) & layout) <br>_Copy-assign an opaque member using layout function table._  |
|  void | [**copySectorData**](#function-copysectordata) (const std::byte \* fromData, uint32\_t fromIsAlive, std::byte \* toData, uint32\_t & toIsAlive, const SectorLayoutMeta \* layouts) <br>_Copy sector data from one location to another._  |
|  void | [**destroyMember**](#function-destroymember) (std::byte \* data, uint32\_t & isAliveData, const [**LayoutData**](structecss_1_1Memory_1_1LayoutData.md) & layout) <br>_Destroy a specific member if alive and clear its liveness bits._  |
|  void | [**destroySectorData**](#function-destroysectordata) (std::byte \* data, uint32\_t & isAliveData, const SectorLayoutMeta \* layouts) <br>_Destroy all alive members in sector data and clear liveness bits._  |
|  T \* | [**emplaceMember**](#function-emplacemember) (std::byte \* data, uint32\_t & isAliveData, const [**LayoutData**](structecss_1_1Memory_1_1LayoutData.md) & layout, Args &&... args) <br>_(Re)construct member T in place and mark it alive. Destroys previous value if present._  |
|  const T \* | [**getComponent**](#function-getcomponent) (const std::byte \* data, uint32\_t isAliveData, SectorLayoutMeta \* sectorLayout) <br>_Fetch component pointer of type T using layout; may be nullptr if not alive._  |
|  T \* | [**getComponent**](#function-getcomponent) (std::byte \* data, uint32\_t isAliveData, SectorLayoutMeta \* sectorLayout) <br> |
|  FORCE\_INLINE const T \* | [**getMember**](#function-getmember) (const std::byte \* data, const [**LayoutData**](structecss_1_1Memory_1_1LayoutData.md) & layout, uint32\_t isAliveData) noexcept<br> |
|  FORCE\_INLINE const T \* | [**getMember**](#function-getmember) (const std::byte \* data, uint16\_t offset, uint32\_t mask, uint32\_t isAliveData) noexcept<br> |
|  FORCE\_INLINE T \* | [**getMember**](#function-getmember) (std::byte \* data, const [**LayoutData**](structecss_1_1Memory_1_1LayoutData.md) & layout, uint32\_t isAliveData) noexcept<br>_Get a member pointer using layout metadata; returns nullptr if not alive._  |
|  FORCE\_INLINE T \* | [**getMember**](#function-getmember) (std::byte \* data, uint16\_t offset, uint32\_t mask, uint32\_t isAliveData) noexcept<br>_Get a member pointer by offset if the corresponding liveness bit is set._  |
|  FORCE\_INLINE const void \* | [**getMemberPtr**](#function-getmemberptr) (const std::byte \* data, uint16\_t offset) noexcept<br> |
|  FORCE\_INLINE void \* | [**getMemberPtr**](#function-getmemberptr) (std::byte \* data, uint16\_t offset) noexcept<br>_Raw member address by byte offset from the sector data base._  |
|  FORCE\_INLINE bool | [**isAlive**](#function-isalive) (uint32\_t isAliveData, uint32\_t mask) noexcept<br>_Check whether any masked bit is marked alive._  |
|  FORCE\_INLINE bool | [**isSectorAlive**](#function-issectoralive) (uint32\_t isAliveData) noexcept<br>_Check whether any bit is marked alive (sector has any live component)._  |
|  FORCE\_INLINE void | [**markAlive**](#function-markalive) (uint32\_t & isAliveData, uint32\_t mask) noexcept<br>_Mark bits as alive (sets them to 1)._  |
|  FORCE\_INLINE void | [**markNotAlive**](#function-marknotalive) (uint32\_t & isAliveData, uint32\_t mask) noexcept<br>_Mark bits as not alive (clears them to 0)._  |
|  T \* | [**moveMember**](#function-movemember) (T && from, std::byte \* toData, uint32\_t & toIsAlive, const [**LayoutData**](structecss_1_1Memory_1_1LayoutData.md) & layout) <br>_Move-assign member T into destination and mark alive._  |
|  void \* | [**moveMember**](#function-movemember) (void \* from, std::byte \* toData, uint32\_t & toIsAlive, const [**LayoutData**](structecss_1_1Memory_1_1LayoutData.md) & layout) <br>_Move-assign an opaque member using layout function table._  |
|  void | [**moveSectorData**](#function-movesectordata) (std::byte \* fromData, uint32\_t & fromIsAlive, std::byte \* toData, uint32\_t & toIsAlive, const SectorLayoutMeta \* layouts) <br>_Move sector data from one location to another._  |
|  FORCE\_INLINE void | [**setAlive**](#function-setalive) (uint32\_t & isAliveData, uint32\_t mask, bool value) noexcept<br>_Set or clear bits in isAliveData._  |




























## Detailed Description


[**Sector**](namespaceecss_1_1Memory_1_1Sector.md) is now a logical concept - a contiguous memory region storing multiple components. The metadata (id, isAliveData) is stored externally in [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) for better cache locality.


Memory layout per sector slot: 
```C++
[SECTOR DATA at linearIdx]
0x + 0                          { Component0 }
0x + sizeof(Component0)         { Component1 }
0x + ... + sizeof(ComponentN)   { ComponentN }
```



External arrays in [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md):
* mIds[linearIdx] -&gt; SectorId
* mIsAliveData[linearIdx] -&gt; uint32\_t bitfield of alive components 




    
## Public Functions Documentation




### function copyMember 

_Copy-assign member T into destination and mark alive._ 
```C++
template<typename T>
T * ecss::Memory::Sector::copyMember (
    const T & from,
    std::byte * toData,
    uint32_t & toIsAlive,
    const LayoutData & layout
) 
```




<hr>



### function copyMember 

_Copy-assign an opaque member using layout function table._ 
```C++
inline void * ecss::Memory::Sector::copyMember (
    const void * from,
    std::byte * toData,
    uint32_t & toIsAlive,
    const LayoutData & layout
) 
```




<hr>



### function copySectorData 

_Copy sector data from one location to another._ 
```C++
inline void ecss::Memory::Sector::copySectorData (
    const std::byte * fromData,
    uint32_t fromIsAlive,
    std::byte * toData,
    uint32_t & toIsAlive,
    const SectorLayoutMeta * layouts
) 
```





**Parameters:**


* `fromData` Source sector data 
* `fromIsAlive` Source alive state 
* `toData` Destination sector data 
* `toIsAlive` Reference to destination alive state (will be modified) 
* `layouts` [**Sector**](namespaceecss_1_1Memory_1_1Sector.md) layout metadata 




        

<hr>



### function destroyMember 

_Destroy a specific member if alive and clear its liveness bits._ 
```C++
inline void ecss::Memory::Sector::destroyMember (
    std::byte * data,
    uint32_t & isAliveData,
    const LayoutData & layout
) 
```




<hr>



### function destroySectorData 

_Destroy all alive members in sector data and clear liveness bits._ 
```C++
inline void ecss::Memory::Sector::destroySectorData (
    std::byte * data,
    uint32_t & isAliveData,
    const SectorLayoutMeta * layouts
) 
```




<hr>



### function emplaceMember 

_(Re)construct member T in place and mark it alive. Destroys previous value if present._ 
```C++
template<typename T, class ... Args>
T * ecss::Memory::Sector::emplaceMember (
    std::byte * data,
    uint32_t & isAliveData,
    const LayoutData & layout,
    Args &&... args
) 
```





**Parameters:**


* `data` Raw pointer to sector data 
* `isAliveData` Reference to alive data (will be modified) 
* `layout` Layout data for this component type 
* `args` Constructor arguments 



**Returns:**

Pointer to constructed component 





        

<hr>



### function getComponent 

_Fetch component pointer of type T using layout; may be nullptr if not alive._ 
```C++
template<typename T>
const T * ecss::Memory::Sector::getComponent (
    const std::byte * data,
    uint32_t isAliveData,
    SectorLayoutMeta * sectorLayout
) 
```




<hr>



### function getComponent 

```C++
template<typename T>
T * ecss::Memory::Sector::getComponent (
    std::byte * data,
    uint32_t isAliveData,
    SectorLayoutMeta * sectorLayout
) 
```




<hr>



### function getMember 

```C++
template<typename T>
FORCE_INLINE const T * ecss::Memory::Sector::getMember (
    const std::byte * data,
    const LayoutData & layout,
    uint32_t isAliveData
) noexcept
```




<hr>



### function getMember 

```C++
template<typename T>
FORCE_INLINE const T * ecss::Memory::Sector::getMember (
    const std::byte * data,
    uint16_t offset,
    uint32_t mask,
    uint32_t isAliveData
) noexcept
```




<hr>



### function getMember 

_Get a member pointer using layout metadata; returns nullptr if not alive._ 
```C++
template<typename T>
FORCE_INLINE T * ecss::Memory::Sector::getMember (
    std::byte * data,
    const LayoutData & layout,
    uint32_t isAliveData
) noexcept
```




<hr>



### function getMember 

_Get a member pointer by offset if the corresponding liveness bit is set._ 
```C++
template<typename T>
FORCE_INLINE T * ecss::Memory::Sector::getMember (
    std::byte * data,
    uint16_t offset,
    uint32_t mask,
    uint32_t isAliveData
) noexcept
```





**Parameters:**


* `data` Raw pointer to sector data 
* `offset` Byte offset of the component 
* `mask` Liveness bitmask for this component type 
* `isAliveData` Current alive state from external array 



**Returns:**

Pointer to T or nullptr if not alive. 





        

<hr>



### function getMemberPtr 

```C++
FORCE_INLINE const void * ecss::Memory::Sector::getMemberPtr (
    const std::byte * data,
    uint16_t offset
) noexcept
```




<hr>



### function getMemberPtr 

_Raw member address by byte offset from the sector data base._ 
```C++
FORCE_INLINE void * ecss::Memory::Sector::getMemberPtr (
    std::byte * data,
    uint16_t offset
) noexcept
```




<hr>



### function isAlive 

_Check whether any masked bit is marked alive._ 
```C++
FORCE_INLINE bool ecss::Memory::Sector::isAlive (
    uint32_t isAliveData,
    uint32_t mask
) noexcept
```




<hr>



### function isSectorAlive 

_Check whether any bit is marked alive (sector has any live component)._ 
```C++
FORCE_INLINE bool ecss::Memory::Sector::isSectorAlive (
    uint32_t isAliveData
) noexcept
```




<hr>



### function markAlive 

_Mark bits as alive (sets them to 1)._ 
```C++
FORCE_INLINE void ecss::Memory::Sector::markAlive (
    uint32_t & isAliveData,
    uint32_t mask
) noexcept
```




<hr>



### function markNotAlive 

_Mark bits as not alive (clears them to 0)._ 
```C++
FORCE_INLINE void ecss::Memory::Sector::markNotAlive (
    uint32_t & isAliveData,
    uint32_t mask
) noexcept
```




<hr>



### function moveMember 

_Move-assign member T into destination and mark alive._ 
```C++
template<typename T>
T * ecss::Memory::Sector::moveMember (
    T && from,
    std::byte * toData,
    uint32_t & toIsAlive,
    const LayoutData & layout
) 
```




<hr>



### function moveMember 

_Move-assign an opaque member using layout function table._ 
```C++
inline void * ecss::Memory::Sector::moveMember (
    void * from,
    std::byte * toData,
    uint32_t & toIsAlive,
    const LayoutData & layout
) 
```




<hr>



### function moveSectorData 

_Move sector data from one location to another._ 
```C++
inline void ecss::Memory::Sector::moveSectorData (
    std::byte * fromData,
    uint32_t & fromIsAlive,
    std::byte * toData,
    uint32_t & toIsAlive,
    const SectorLayoutMeta * layouts
) 
```





**Parameters:**


* `fromData` Source sector data 
* `fromIsAlive` Reference to source alive state (will be cleared) 
* `toData` Destination sector data 
* `toIsAlive` Reference to destination alive state (will be modified) 
* `layouts` [**Sector**](namespaceecss_1_1Memory_1_1Sector.md) layout metadata 




        

<hr>



### function setAlive 

_Set or clear bits in isAliveData._ 
```C++
FORCE_INLINE void ecss::Memory::Sector::setAlive (
    uint32_t & isAliveData,
    uint32_t mask,
    bool value
) noexcept
```





**Parameters:**


* `isAliveData` Reference to alive data to modify 
* `mask` Bitmask of bits to modify 
* `value` If true, sets the bits; if false, clears them 



**Note:**

When value == false, mask should already be ~mask 





        

<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/memory/Sector.h`

