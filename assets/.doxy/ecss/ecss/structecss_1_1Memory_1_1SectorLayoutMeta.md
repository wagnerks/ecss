

# Struct ecss::Memory::SectorLayoutMeta



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md) **>** [**SectorLayoutMeta**](structecss_1_1Memory_1_1SectorLayoutMeta.md)




















## Classes

| Type | Name |
| ---: | :--- |
| class | [**Iterator**](classecss_1_1Memory_1_1SectorLayoutMeta_1_1Iterator.md) <br>_Forward iterator over the contiguous_ [_**LayoutData**_](structecss_1_1Memory_1_1LayoutData.md) _array._ |






















## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**SectorLayoutMeta**](#function-sectorlayoutmeta-13) (const SectorLayoutMeta & other) = delete<br> |
|   | [**SectorLayoutMeta**](#function-sectorlayoutmeta-23) (SectorLayoutMeta && other) noexcept<br> |
|  [**Iterator**](classecss_1_1Memory_1_1SectorLayoutMeta_1_1Iterator.md) | [**begin**](#function-begin) () const<br>_Begin/end iterators over layout records._  |
|  [**Iterator**](classecss_1_1Memory_1_1SectorLayoutMeta_1_1Iterator.md) | [**end**](#function-end) () const<br> |
|  const [**LayoutData**](structecss_1_1Memory_1_1LayoutData.md) & | [**getLayoutData**](#function-getlayoutdata-12) () const<br>_Access_ [_**LayoutData**_](structecss_1_1Memory_1_1LayoutData.md) _for a given component type T (throws in debug if not present)._ |
|  const [**LayoutData**](structecss_1_1Memory_1_1LayoutData.md) & | [**getLayoutData**](#function-getlayoutdata-22) (uint8\_t idx) const<br>_Access_ [_**LayoutData**_](structecss_1_1Memory_1_1LayoutData.md) _by index (0..count-1)._ |
|  uint16\_t | [**getTotalSize**](#function-gettotalsize) () const<br> |
|  void | [**initData**](#function-initdata) () <br>_Compute counts, total size, allocate storage, and populate per-type metadata._  |
|  void | [**initLayoutData**](#function-initlayoutdata-12) () <br>_Initialize an array of_ [_**LayoutData**_](structecss_1_1Memory_1_1LayoutData.md) _for a parameter pack of types._ |
|  void | [**initLayoutData**](#function-initlayoutdata-22) ([**LayoutData**](structecss_1_1Memory_1_1LayoutData.md) & data, uint8\_t & index, uint16\_t offset) noexcept const<br>_Initialize_ [_**LayoutData**_](structecss_1_1Memory_1_1LayoutData.md) _for a single component type U._ |
|  bool | [**isTrivial**](#function-istrivial) () const<br> |
|  SectorLayoutMeta & | [**operator=**](#function-operator) (const SectorLayoutMeta & other) = delete<br> |
|  SectorLayoutMeta & | [**operator=**](#function-operator_1) (SectorLayoutMeta && other) noexcept<br> |
|   | [**~SectorLayoutMeta**](#function-sectorlayoutmeta) () = default<br> |


## Public Static Functions

| Type | Name |
| ---: | :--- |
|  size\_t | [**TypeId**](#function-typeid) () <br>_Get a process-stable (but not ABI/serialization-stable) type token for T._  |
|  SectorLayoutMeta \* | [**create**](#function-create) () <br>_Factory: allocate and initialize metadata for a set of component types._  |


























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

_Begin/end iterators over layout records._ 
```C++
inline Iterator ecss::Memory::SectorLayoutMeta::begin () const
```




<hr>



### function end 

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



### function initData 

_Compute counts, total size, allocate storage, and populate per-type metadata._ 
```C++
template<typename... Types>
inline void ecss::Memory::SectorLayoutMeta::initData () 
```



Initializes:
* `\p count` (number of component types)
* `\p totalSize` (bytes for component payloads, starting from offset 0)
* `\p typeIds` (stable per-process type tokens)
* `\p layout` (array of [**LayoutData**](structecss_1_1Memory_1_1LayoutData.md) entries) Also computes `mIsTrivial` (true if all components are trivial). 




        

<hr>



### function initLayoutData [1/2]

_Initialize an array of_ [_**LayoutData**_](structecss_1_1Memory_1_1LayoutData.md) _for a parameter pack of types._
```C++
template<typename... U>
inline void ecss::Memory::SectorLayoutMeta::initLayoutData () 
```



Uses the compile-time OffsetArray to compute per-type offsets and calls the single-type initializer for each entry. Offsets now start from 0.




**Template parameters:**


* `U` ... Component types to lay out in the sector. 




        

<hr>



### function initLayoutData [2/2]

_Initialize_ [_**LayoutData**_](structecss_1_1Memory_1_1LayoutData.md) _for a single component type U._
```C++
template<typename U>
inline void ecss::Memory::SectorLayoutMeta::initLayoutData (
    LayoutData & data,
    uint8_t & index,
    uint16_t offset
) noexcept const
```



Populates per-type metadata: byte offset in the sector data, index, liveness bit masks, triviality flag, and the type-erased function table (move/copy/destroy).




**Template parameters:**


* `U` Component type. 



**Parameters:**


* `data` [**LayoutData**](structecss_1_1Memory_1_1LayoutData.md) record to fill. 
* `index` Running index (incremented after use). The index determines which liveness bit is used; the mask is (1 &lt;&lt; index). 
* `offset` Byte offset from the beginning of sector data at which U is stored.



**Note:**

U must be move-constructible. 




**Warning:**

The function table stores operations using type-erased lambdas; these must match the object lifetime semantics you expect. 





        

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

Newly allocated SectorLayoutMeta\*; caller owns and must delete. 





        

<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/memory/SectorLayoutMeta.h`

