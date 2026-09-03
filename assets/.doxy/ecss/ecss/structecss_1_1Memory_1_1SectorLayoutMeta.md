

# Struct ecss::Memory::SectorLayoutMeta



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md) **>** [**SectorLayoutMeta**](structecss_1_1Memory_1_1SectorLayoutMeta.md)



_Immutable description of one sector's component layout._ [More...](#detailed-description)

* `#include <SectorLayoutMeta.h>`















## Classes

| Type | Name |
| ---: | :--- |
| class | [**Iterator**](classecss_1_1Memory_1_1SectorLayoutMeta_1_1Iterator.md) <br>_Forward iterator over the contiguous_ [_**LayoutData**_](structecss_1_1Memory_1_1LayoutData.md) _array._ |






















## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**SectorLayoutMeta**](#function-sectorlayoutmeta-13) (const [**SectorLayoutMeta**](structecss_1_1Memory_1_1SectorLayoutMeta.md) & other) = delete<br> |
|   | [**SectorLayoutMeta**](#function-sectorlayoutmeta-23) ([**SectorLayoutMeta**](structecss_1_1Memory_1_1SectorLayoutMeta.md) && other) noexcept<br> |
|  [**Iterator**](classecss_1_1Memory_1_1SectorLayoutMeta_1_1Iterator.md) | [**begin**](#function-begin) () const<br>_Begin/end iterators over layout records. @thread\_safety Internally synchronized. Walks layout records that are immutable once built._  |
|  [**Iterator**](classecss_1_1Memory_1_1SectorLayoutMeta_1_1Iterator.md) | [**end**](#function-end) () const<br>_@thread\_safety Internally synchronized. Walks layout records that are immutable once built._  |
|  const [**LayoutData**](structecss_1_1Memory_1_1LayoutData.md) & | [**getLayoutData**](#function-getlayoutdata-12) () const<br>_Access_ [_**LayoutData**_](structecss_1_1Memory_1_1LayoutData.md) _for a given component type T (throws in debug if not present)._ |
|  const [**LayoutData**](structecss_1_1Memory_1_1LayoutData.md) & | [**getLayoutData**](#function-getlayoutdata-22) (uint8\_t idx) const<br>_Access_ [_**LayoutData**_](structecss_1_1Memory_1_1LayoutData.md) _by index (0..count-1)._ |
|  uint16\_t | [**getTotalSize**](#function-gettotalsize) () const<br> |
|  uint8\_t | [**getTypesCount**](#function-gettypescount) () const<br> |
|  bool | [**isCompatibleWith**](#function-iscompatiblewith) (const [**SectorLayoutMeta**](structecss_1_1Memory_1_1SectorLayoutMeta.md) & other) noexcept const<br>_Do these two describe the same sector shape?_  |
|  bool | [**isTrivial**](#function-istrivial) () const<br> |
|  [**SectorLayoutMeta**](structecss_1_1Memory_1_1SectorLayoutMeta.md) & | [**operator=**](#function-operator) (const [**SectorLayoutMeta**](structecss_1_1Memory_1_1SectorLayoutMeta.md) & other) = delete<br> |
|  [**SectorLayoutMeta**](structecss_1_1Memory_1_1SectorLayoutMeta.md) & | [**operator=**](#function-operator_1) ([**SectorLayoutMeta**](structecss_1_1Memory_1_1SectorLayoutMeta.md) && other) noexcept<br> |
|   | [**~SectorLayoutMeta**](#function-sectorlayoutmeta) () = default<br> |


## Public Static Functions

| Type | Name |
| ---: | :--- |
|  size\_t | [**TypeId**](#function-typeid) () <br>_Get a process-stable (but not ABI/serialization-stable) type token for T._  |
|  [**SectorLayoutMeta**](structecss_1_1Memory_1_1SectorLayoutMeta.md) \* | [**create**](#function-create) () <br>_Factory: allocate and initialize metadata for a set of component types._  |


























## Detailed Description


Built once by [**create()**](structecss_1_1Memory_1_1SectorLayoutMeta.md#function-create) and never changed again: [**SectorsArray::create()**](classecss_1_1Memory_1_1SectorsArray.md#function-create) keeps one instance per distinct type pack in a function-local static, so every array sharing a pack shares this object for the lifetime of the process, and the [**LayoutData**](structecss_1_1Memory_1_1LayoutData.md) records live at fixed addresses. Everything that reads a layout therefore holds it by const pointer  the only mutation in the type is [**create()**](structecss_1_1Memory_1_1SectorLayoutMeta.md#function-create) initialising what it just allocated, which is why initData/initLayoutData are private. @thread\_safety Internally synchronized, because it never changes  and that applies to every member below. The layout is built once, by [**create()**](structecss_1_1Memory_1_1SectorLayoutMeta.md#function-create), and is const from then on, so any number of threads may read it without a lock and none of it can go stale. Only [**create()**](structecss_1_1Memory_1_1SectorLayoutMeta.md#function-create) and the init\* helpers it calls are exclusive, and they run before anything can name the object. 


    
## Public Functions Documentation




### function SectorLayoutMeta [1/3]

```C++
ecss::Memory::SectorLayoutMeta::SectorLayoutMeta (
    const SectorLayoutMeta & other
) = delete
```




<hr>



### function SectorLayoutMeta [2/3]

```C++
ecss::Memory::SectorLayoutMeta::SectorLayoutMeta (
    SectorLayoutMeta && other
) noexcept
```




<hr>



### function begin 

_Begin/end iterators over layout records. @thread\_safety Internally synchronized. Walks layout records that are immutable once built._ 
```C++
inline Iterator ecss::Memory::SectorLayoutMeta::begin () const
```




<hr>



### function end 

_@thread\_safety Internally synchronized. Walks layout records that are immutable once built._ 
```C++
inline Iterator ecss::Memory::SectorLayoutMeta::end () const
```




<hr>



### function getLayoutData [1/2]

_Access_ [_**LayoutData**_](structecss_1_1Memory_1_1LayoutData.md) _for a given component type T (throws in debug if not present)._
```C++
template<typename T>
inline const LayoutData & ecss::Memory::SectorLayoutMeta::getLayoutData () const
```




<hr>



### function getLayoutData [2/2]

_Access_ [_**LayoutData**_](structecss_1_1Memory_1_1LayoutData.md) _by index (0..count-1)._
```C++
inline const LayoutData & ecss::Memory::SectorLayoutMeta::getLayoutData (
    uint8_t idx
) const
```




<hr>



### function getTotalSize 

```C++
inline uint16_t ecss::Memory::SectorLayoutMeta::getTotalSize () const
```





**Returns:**

Total bytes consumed by sector data (component payloads only, no header). 





        

<hr>



### function getTypesCount 

```C++
inline uint8_t ecss::Memory::SectorLayoutMeta::getTypesCount () const
```




<hr>



### function isCompatibleWith 

_Do these two describe the same sector shape?_ 
```C++
inline bool ecss::Memory::SectorLayoutMeta::isCompatibleWith (
    const SectorLayoutMeta & other
) noexcept const
```



Instances are per type pack _per template instantiation_, so the same components laid out for [**SectorsArray&lt;true&gt;**](classecss_1_1Memory_1_1SectorsArray.md) and for [**SectorsArray&lt;false&gt;**](classecss_1_1Memory_1_1SectorsArray.md) are two distinct objects with identical contents. Pointer equality would reject copying between them, which is a supported operation, so the contents are compared.


Order is part of the shape: [A, B] and [B, A] give each component a different offset and a different liveness bit, so they are not compatible. 


        

<hr>



### function isTrivial 

```C++
inline bool ecss::Memory::SectorLayoutMeta::isTrivial () const
```





**Returns:**

True if all component types are trivial (copy/move/destroy are trivial). 





        

<hr>



### function operator= 

```C++
SectorLayoutMeta & ecss::Memory::SectorLayoutMeta::operator= (
    const SectorLayoutMeta & other
) = delete
```




<hr>



### function operator= 

```C++
SectorLayoutMeta & ecss::Memory::SectorLayoutMeta::operator= (
    SectorLayoutMeta && other
) noexcept
```




<hr>



### function ~SectorLayoutMeta 

```C++
ecss::Memory::SectorLayoutMeta::~SectorLayoutMeta () = default
```




<hr>
## Public Static Functions Documentation




### function TypeId 

_Get a process-stable (but not ABI/serialization-stable) type token for T._ 
```C++
template<typename T>
static inline size_t ecss::Memory::SectorLayoutMeta::TypeId () 
```



Implementation uses the address of an internal static tag, which is:
* Unique per (type, process)
* NOT stable across processes/builds/DSOs






**Template parameters:**


* `T` Component type. 



**Returns:**

Opaque size\_t token; suitable for in-process lookup only. 




**Warning:**

Do not persist/serialize this value; it is not stable across runs. 





        

<hr>



### function create 

_Factory: allocate and initialize metadata for a set of component types._ 
```C++
template<typename... Types>
static inline SectorLayoutMeta * ecss::Memory::SectorLayoutMeta::create () 
```





**Template parameters:**


* `Types` ... Component types stored in a sector. 



**Returns:**

Newly allocated SectorLayoutMeta\*; caller owns and must delete. @thread\_safety Caller must ensure exclusive access. Builds the layout. Nothing may read it until this returns; afterwards it never changes again. 





        

<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/memory/SectorLayoutMeta.h`

