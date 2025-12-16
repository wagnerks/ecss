

# Struct ecss::Memory::ChunksAllocator::Cursor



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md) **>** [**ChunksAllocator**](structecss_1_1Memory_1_1ChunksAllocator.md) **>** [**Cursor**](structecss_1_1Memory_1_1ChunksAllocator_1_1Cursor.md)



[_**Cursor**_](structecss_1_1Memory_1_1ChunksAllocator_1_1Cursor.md) _for linear iteration over sector data._[More...](#detailed-description)

* `#include <ChunksAllocator.h>`





































## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**Cursor**](#function-cursor-12) () = default<br> |
|   | [**Cursor**](#function-cursor-22) (const [**ChunksAllocator**](structecss_1_1Memory_1_1ChunksAllocator.md) \* allocator, size\_t index=0) noexcept<br> |
|  FORCE\_INLINE size\_t | [**linearIndex**](#function-linearindex) () noexcept const<br> |
|  FORCE\_INLINE | [**operator bool**](#function-operator-bool) () noexcept const<br> |
|  FORCE\_INLINE bool | [**operator!=**](#function-operator) (const [**Cursor**](structecss_1_1Memory_1_1ChunksAllocator_1_1Cursor.md) & other) noexcept const<br> |
|  FORCE\_INLINE std::byte \* | [**operator\***](#function-operator_1) () noexcept const<br> |
|  FORCE\_INLINE [**Cursor**](structecss_1_1Memory_1_1ChunksAllocator_1_1Cursor.md) & | [**operator+**](#function-operator_2) (size\_t value) noexcept<br> |
|  FORCE\_INLINE [**Cursor**](structecss_1_1Memory_1_1ChunksAllocator_1_1Cursor.md) & | [**operator++**](#function-operator_3) () noexcept<br> |
|  FORCE\_INLINE std::byte \* | [**operator-&gt;**](#function-operator-) () noexcept const<br> |
|  FORCE\_INLINE bool | [**operator==**](#function-operator_4) (const [**Cursor**](structecss_1_1Memory_1_1ChunksAllocator_1_1Cursor.md) & other) noexcept const<br> |
|  FORCE\_INLINE std::byte \* | [**rawPtr**](#function-rawptr) () noexcept const<br> |




























## Detailed Description


Provides access to raw sector data via linearIndex. Does NOT have access to sector id/isAlive - those come from external arrays. 


    
## Public Functions Documentation




### function Cursor [1/2]

```C++
ecss::Memory::ChunksAllocator::Cursor::Cursor () = default
```




<hr>



### function Cursor [2/2]

```C++
inline ecss::Memory::ChunksAllocator::Cursor::Cursor (
    const ChunksAllocator * allocator,
    size_t index=0
) noexcept
```




<hr>



### function linearIndex 

```C++
inline FORCE_INLINE size_t ecss::Memory::ChunksAllocator::Cursor::linearIndex () noexcept const
```




<hr>



### function operator bool 

```C++
inline explicit FORCE_INLINE ecss::Memory::ChunksAllocator::Cursor::operator bool () noexcept const
```




<hr>



### function operator!= 

```C++
inline FORCE_INLINE bool ecss::Memory::ChunksAllocator::Cursor::operator!= (
    const Cursor & other
) noexcept const
```




<hr>



### function operator\* 

```C++
inline FORCE_INLINE std::byte * ecss::Memory::ChunksAllocator::Cursor::operator* () noexcept const
```





**Returns:**

Raw pointer to sector data (not a Sector\*, just component data) 





        

<hr>



### function operator+ 

```C++
inline FORCE_INLINE Cursor & ecss::Memory::ChunksAllocator::Cursor::operator+ (
    size_t value
) noexcept
```




<hr>



### function operator++ 

```C++
inline FORCE_INLINE Cursor & ecss::Memory::ChunksAllocator::Cursor::operator++ () noexcept
```




<hr>



### function operator-&gt; 

```C++
inline FORCE_INLINE std::byte * ecss::Memory::ChunksAllocator::Cursor::operator-> () noexcept const
```




<hr>



### function operator== 

```C++
inline FORCE_INLINE bool ecss::Memory::ChunksAllocator::Cursor::operator== (
    const Cursor & other
) noexcept const
```




<hr>



### function rawPtr 

```C++
inline FORCE_INLINE std::byte * ecss::Memory::ChunksAllocator::Cursor::rawPtr () noexcept const
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/memory/ChunksAllocator.h`

