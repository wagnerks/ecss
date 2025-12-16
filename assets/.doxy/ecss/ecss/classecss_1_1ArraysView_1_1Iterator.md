

# Class ecss::ArraysView::Iterator



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**ArraysView**](classecss_1_1ArraysView.md) **>** [**Iterator**](classecss_1_1ArraysView_1_1Iterator.md)



_Forward iterator over alive sectors of the main component type._ [More...](#detailed-description)

* `#include <Registry.h>`

















## Public Types

| Type | Name |
| ---: | :--- |
| typedef std::array&lt; [**Sectors**](classecss_1_1Memory_1_1SectorsArray.md) \*, TypesCount &gt; | [**SectorArrays**](#typedef-sectorarrays)  <br> |
| typedef std::tuple&lt; [**TypeInfo**](structecss_1_1TypeAccessInfo.md), decltype((void) sizeof(CompTypes), [**TypeInfo**](structecss_1_1TypeAccessInfo.md){})... &gt; | [**TypeAccessTuple**](#typedef-typeaccesstuple)  <br> |
| typedef std::ptrdiff\_t | [**difference\_type**](#typedef-difference_type)  <br> |
| typedef std::forward\_iterator\_tag | [**iterator\_category**](#typedef-iterator_category)  <br> |
| typedef value\_type \* | [**pointer**](#typedef-pointer)  <br> |
| typedef value\_type & | [**reference**](#typedef-reference)  <br> |
| typedef std::tuple&lt; EntityId, T \*, CompTypes \*... &gt; | [**value\_type**](#typedef-value_type)  <br> |




















## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**Iterator**](#function-iterator-12) () noexcept<br> |
|   | [**Iterator**](#function-iterator-22) (const SectorArrays & arrays, SectorsIt iterator, const std::vector&lt; std::pair&lt; [**Sectors**](classecss_1_1Memory_1_1SectorsArray.md) \*, SectorsRangeIt &gt; &gt; & secondary) <br>_Construct iterator with main iterator + secondary arrays._  |
|  FORCE\_INLINE bool | [**operator!=**](#function-operator) (const [**EndIterator**](structecss_1_1ArraysView_1_1EndIterator.md) &) noexcept const<br> |
|  FORCE\_INLINE bool | [**operator!=**](#function-operator_1) (const [**Iterator**](classecss_1_1ArraysView_1_1Iterator.md) & other) noexcept const<br> |
|  FORCE\_INLINE value\_type | [**operator\***](#function-operator_2) () noexcept const<br> |
|  FORCE\_INLINE [**Iterator**](classecss_1_1ArraysView_1_1Iterator.md) & | [**operator++**](#function-operator_3) () noexcept<br> |
|  FORCE\_INLINE bool | [**operator==**](#function-operator_4) (const [**EndIterator**](structecss_1_1ArraysView_1_1EndIterator.md) &) noexcept const<br> |
|  FORCE\_INLINE bool | [**operator==**](#function-operator_5) (const [**Iterator**](classecss_1_1ArraysView_1_1Iterator.md) & other) noexcept const<br> |
|  FORCE\_INLINE bool | [**tryInvoke**](#function-tryinvoke) (Func && func) noexcept const<br>_Invoke func directly without tuple creation. Returns true if all components found._  |




























## Detailed Description


Dereferencing produces a tuple (EntityId, T\*, CompTypes\*...). Non-main pointers may be nullptr if component not present for that entity.




**Note:**

[**Iterator**](classecss_1_1ArraysView_1_1Iterator.md) validity is bounded by the pinned back-sector (thread-safe mode). 





    
## Public Types Documentation




### typedef SectorArrays 

```C++
using ecss::ArraysView< ThreadSafe, Allocator, Ranged, T, CompTypes >::Iterator::SectorArrays =  std::array<Sectors*, TypesCount>;
```




<hr>



### typedef TypeAccessTuple 

```C++
using ecss::ArraysView< ThreadSafe, Allocator, Ranged, T, CompTypes >::Iterator::TypeAccessTuple =  std::tuple<TypeInfo, decltype((void)sizeof(CompTypes), TypeInfo{})...>;
```




<hr>



### typedef difference\_type 

```C++
using ecss::ArraysView< ThreadSafe, Allocator, Ranged, T, CompTypes >::Iterator::difference_type =  std::ptrdiff_t;
```




<hr>



### typedef iterator\_category 

```C++
using ecss::ArraysView< ThreadSafe, Allocator, Ranged, T, CompTypes >::Iterator::iterator_category =  std::forward_iterator_tag;
```




<hr>



### typedef pointer 

```C++
using ecss::ArraysView< ThreadSafe, Allocator, Ranged, T, CompTypes >::Iterator::pointer =  value_type*;
```




<hr>



### typedef reference 

```C++
using ecss::ArraysView< ThreadSafe, Allocator, Ranged, T, CompTypes >::Iterator::reference =  value_type&;
```




<hr>



### typedef value\_type 

```C++
using ecss::ArraysView< ThreadSafe, Allocator, Ranged, T, CompTypes >::Iterator::value_type =  std::tuple<EntityId, T*, CompTypes*...>;
```




<hr>
## Public Functions Documentation




### function Iterator [1/2]

```C++
ecss::ArraysView::Iterator::Iterator () noexcept
```




<hr>



### function Iterator [2/2]

_Construct iterator with main iterator + secondary arrays._ 
```C++
inline ecss::ArraysView::Iterator::Iterator (
    const SectorArrays & arrays,
    SectorsIt iterator,
    const std::vector< std::pair< Sectors *, SectorsRangeIt > > & secondary
) 
```





**Parameters:**


* `arrays` Array of sector arrays for all involved component types. 
* `iterator` Alive iterator for main component. 
* `secondary` Arrays (+ iterators for ThreadSafe) for component lookup. 




        

<hr>



### function operator!= 

```C++
inline FORCE_INLINE bool ecss::ArraysView::Iterator::operator!= (
    const EndIterator &
) noexcept const
```




<hr>



### function operator!= 

```C++
inline FORCE_INLINE bool ecss::ArraysView::Iterator::operator!= (
    const Iterator & other
) noexcept const
```




<hr>



### function operator\* 

```C++
inline FORCE_INLINE value_type ecss::ArraysView::Iterator::operator* () noexcept const
```




<hr>



### function operator++ 

```C++
inline FORCE_INLINE Iterator & ecss::ArraysView::Iterator::operator++ () noexcept
```




<hr>



### function operator== 

```C++
inline FORCE_INLINE bool ecss::ArraysView::Iterator::operator== (
    const EndIterator &
) noexcept const
```




<hr>



### function operator== 

```C++
inline FORCE_INLINE bool ecss::ArraysView::Iterator::operator== (
    const Iterator & other
) noexcept const
```




<hr>



### function tryInvoke 

_Invoke func directly without tuple creation. Returns true if all components found._ 
```C++
template<typename Func>
inline FORCE_INLINE bool ecss::ArraysView::Iterator::tryInvoke (
    Func && func
) noexcept const
```




<hr>## Friends Documentation





### friend operator!= 

```C++
inline FORCE_INLINE friend bool ecss::ArraysView::Iterator::operator!= (
    const EndIterator endIt,
    const Iterator & it
) noexcept
```




<hr>



### friend operator== 

```C++
inline FORCE_INLINE friend bool ecss::ArraysView::Iterator::operator== (
    const EndIterator endIt,
    const Iterator & it
) noexcept
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/Registry.h`

