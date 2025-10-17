

# Struct ecss::Memory::detail::SectorsMap&lt; false &gt;

**template &lt;&gt;**



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md) **>** [**detail**](namespaceecss_1_1Memory_1_1detail.md) **>** [**SectorsMap&lt; false &gt;**](structecss_1_1Memory_1_1detail_1_1SectorsMap_3_01false_01_4.md)


























## Public Attributes

| Type | Name |
| ---: | :--- |
|  std::vector&lt; [**Sector**](structecss_1_1Memory_1_1Sector.md) \* &gt; | [**sectorsMap**](#variable-sectorsmap)  <br> |
















## Public Functions

| Type | Name |
| ---: | :--- |
|  FORCE\_INLINE size\_t | [**capacity**](#function-capacity) () const<br> |
|  FORCE\_INLINE [**Sector**](structecss_1_1Memory_1_1Sector.md) \* | [**findSector**](#function-findsector) (SectorId id) const<br> |
|  FORCE\_INLINE [**Sector**](structecss_1_1Memory_1_1Sector.md) \* | [**getSector**](#function-getsector) (SectorId id) const<br> |
|  FORCE\_INLINE void | [**storeVector**](#function-storevector) () <br> |




























## Public Attributes Documentation




### variable sectorsMap 

```C++
std::vector<Sector*> ecss::Memory::detail::SectorsMap< false >::sectorsMap;
```




<hr>
## Public Functions Documentation




### function capacity 

```C++
inline FORCE_INLINE size_t ecss::Memory::detail::SectorsMap< false >::capacity () const
```




<hr>



### function findSector 

```C++
inline FORCE_INLINE Sector * ecss::Memory::detail::SectorsMap< false >::findSector (
    SectorId id
) const
```




<hr>



### function getSector 

```C++
inline FORCE_INLINE Sector * ecss::Memory::detail::SectorsMap< false >::getSector (
    SectorId id
) const
```




<hr>



### function storeVector 

```C++
inline FORCE_INLINE void ecss::Memory::detail::SectorsMap< false >::storeVector () 
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/memory/SectorsArray.h`

