

# Class ecss::Memory::SectorsArray::IteratorAlive



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md) **>** [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) **>** [**IteratorAlive**](classecss_1_1Memory_1_1SectorsArray_1_1IteratorAlive.md)



_Forward iterator skipping slots where component is not alive. Optimized: uses chunk-aware pointer increment for O(1) per-element access. When isPacked=true (defragmentSize==0), skipDead is bypassed for maximum speed. Uses atomic view snapshots for thread-safe iteration._ 

* `#include <SectorsArray.h>`





































## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**IteratorAlive**](#function-iteratoralive) (const [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) \* array, size\_t idx, size\_t sz, uint32\_t aliveMask, bool isPacked=false) <br> |
|  FORCE\_INLINE size\_t | [**linearIndex**](#function-linearindex) () noexcept const<br> |
|  FORCE\_INLINE | [**operator bool**](#function-operator-bool) () noexcept const<br> |
|  FORCE\_INLINE value\_type | [**operator\***](#function-operator) () const<br> |
|  FORCE\_INLINE [**IteratorAlive**](classecss_1_1Memory_1_1SectorsArray_1_1IteratorAlive.md) & | [**operator++**](#function-operator_1) () noexcept<br> |
|  FORCE\_INLINE bool | [**operator==**](#function-operator_2) (const [**IteratorAlive**](classecss_1_1Memory_1_1SectorsArray_1_1IteratorAlive.md) & other) noexcept const<br> |
|  FORCE\_INLINE std::byte \* | [**rawPtr**](#function-rawptr) () noexcept const<br> |




























## Public Functions Documentation




### function IteratorAlive 

```C++
inline ecss::Memory::SectorsArray::IteratorAlive::IteratorAlive (
    const SectorsArray * array,
    size_t idx,
    size_t sz,
    uint32_t aliveMask,
    bool isPacked=false
) 
```




<hr>



### function linearIndex 

```C++
inline FORCE_INLINE size_t ecss::Memory::SectorsArray::IteratorAlive::linearIndex () noexcept const
```




<hr>



### function operator bool 

```C++
inline explicit FORCE_INLINE ecss::Memory::SectorsArray::IteratorAlive::operator bool () noexcept const
```




<hr>



### function operator\* 

```C++
inline FORCE_INLINE value_type ecss::Memory::SectorsArray::IteratorAlive::operator* () const
```




<hr>



### function operator++ 

```C++
inline FORCE_INLINE IteratorAlive & ecss::Memory::SectorsArray::IteratorAlive::operator++ () noexcept
```




<hr>



### function operator== 

```C++
inline FORCE_INLINE bool ecss::Memory::SectorsArray::IteratorAlive::operator== (
    const IteratorAlive & other
) noexcept const
```




<hr>



### function rawPtr 

```C++
inline FORCE_INLINE std::byte * ecss::Memory::SectorsArray::IteratorAlive::rawPtr () noexcept const
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/memory/SectorsArray.h`

