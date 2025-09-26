

# Struct ecss::Memory::ChunksAllocator::RangesCursor



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md) **>** [**ChunksAllocator**](structecss_1_1Memory_1_1ChunksAllocator.md) **>** [**RangesCursor**](structecss_1_1Memory_1_1ChunksAllocator_1_1RangesCursor.md)










































## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**RangesCursor**](#function-rangescursor-12) () = default<br> |
|   | [**RangesCursor**](#function-rangescursor-22) (const ChunksAllocator \* alloc, const Ranges&lt; SectorId &gt; & ranges, size\_t size) <br> |
|  FORCE\_INLINE void | [**advanceToId**](#function-advancetoid) (SectorId target, size\_t linear\_threshold=4) noexcept<br> |
|  FORCE\_INLINE void | [**nextSpan**](#function-nextspan) () noexcept<br> |
|  FORCE\_INLINE | [**operator bool**](#function-operator-bool) () noexcept const<br> |
|  FORCE\_INLINE bool | [**operator!=**](#function-operator) (const RangesCursor & other) noexcept const<br> |
|  FORCE\_INLINE [**Sector**](structecss_1_1Memory_1_1Sector.md) \* | [**operator\***](#function-operator_1) () noexcept const<br> |
|  FORCE\_INLINE [**Sector**](structecss_1_1Memory_1_1Sector.md) \* | [**operator-&gt;**](#function-operator-) () noexcept const<br> |
|  FORCE\_INLINE bool | [**operator==**](#function-operator_2) (const RangesCursor & other) noexcept const<br> |
|  FORCE\_INLINE std::byte \* | [**rawPtr**](#function-rawptr) () noexcept const<br> |
|  FORCE\_INLINE void | [**step**](#function-step) () noexcept<br> |




























## Public Functions Documentation




### function RangesCursor [1/2]

```C++
ecss::Memory::ChunksAllocator::RangesCursor::RangesCursor () = default
```




<hr>



### function RangesCursor [2/2]

```C++
inline ecss::Memory::ChunksAllocator::RangesCursor::RangesCursor (
    const ChunksAllocator * alloc,
    const Ranges< SectorId > & ranges,
    size_t size
) 
```




<hr>



### function advanceToId 

```C++
inline FORCE_INLINE void ecss::Memory::ChunksAllocator::RangesCursor::advanceToId (
    SectorId target,
    size_t linear_threshold=4
) noexcept
```




<hr>



### function nextSpan 

```C++
inline FORCE_INLINE void ecss::Memory::ChunksAllocator::RangesCursor::nextSpan () noexcept
```




<hr>



### function operator bool 

```C++
inline explicit FORCE_INLINE ecss::Memory::ChunksAllocator::RangesCursor::operator bool () noexcept const
```




<hr>



### function operator!= 

```C++
inline FORCE_INLINE bool ecss::Memory::ChunksAllocator::RangesCursor::operator!= (
    const RangesCursor & other
) noexcept const
```




<hr>



### function operator\* 

```C++
inline FORCE_INLINE Sector * ecss::Memory::ChunksAllocator::RangesCursor::operator* () noexcept const
```




<hr>



### function operator-&gt; 

```C++
inline FORCE_INLINE Sector * ecss::Memory::ChunksAllocator::RangesCursor::operator-> () noexcept const
```




<hr>



### function operator== 

```C++
inline FORCE_INLINE bool ecss::Memory::ChunksAllocator::RangesCursor::operator== (
    const RangesCursor & other
) noexcept const
```




<hr>



### function rawPtr 

```C++
inline FORCE_INLINE std::byte * ecss::Memory::ChunksAllocator::RangesCursor::rawPtr () noexcept const
```




<hr>



### function step 

```C++
inline FORCE_INLINE void ecss::Memory::ChunksAllocator::RangesCursor::step () noexcept
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/memory/ChunksAllocator.h`

