

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
|   | [**Iterator**](#function-iterator-12) () noexcept<br>_Construct an end-like iterator that refers to no element._  |
|   | [**Iterator**](#function-iterator-22) (const SectorArrays & arrays, SectorsIt iterator, const std::vector&lt; std::pair&lt; [**Sectors**](classecss_1_1Memory_1_1SectorsArray.md) \*, SectorsRangeIt &gt; &gt; & secondary, const [**Ranges**](structecss_1_1Ranges.md)&lt; EntityId &gt; \* rangeFilter=nullptr) <br>_Construct iterator with main iterator + secondary arrays._  |
|  FORCE\_INLINE bool | [**operator!=**](#function-operator) (const [**EndIterator**](structecss_1_1ArraysView_1_1EndIterator.md) &) noexcept const<br> |
|  FORCE\_INLINE bool | [**operator!=**](#function-operator_1) (const [**Iterator**](classecss_1_1ArraysView_1_1Iterator.md) & other) noexcept const<br> |
|  FORCE\_INLINE value\_type | [**operator\***](#function-operator_2) () noexcept const<br> |
|  FORCE\_INLINE [**Iterator**](classecss_1_1ArraysView_1_1Iterator.md) & | [**operator++**](#function-operator_3) () noexcept<br>_Advance to the next alive main component accepted by the optional range filter._  |
|  FORCE\_INLINE bool | [**operator==**](#function-operator_4) (const [**EndIterator**](structecss_1_1ArraysView_1_1EndIterator.md) &) noexcept const<br> |
|  FORCE\_INLINE bool | [**operator==**](#function-operator_5) (const [**Iterator**](classecss_1_1ArraysView_1_1Iterator.md) & other) noexcept const<br> |
|  FORCE\_INLINE bool | [**tryInvoke**](#function-tryinvoke) (Func && func) noexcept const<br>_Invoke func directly without tuple creation. Returns true if all components found._  |




























## Detailed Description


Dereferencing produces a tuple (EntityId, T\*, CompTypes\*...). Non-main pointers may be nullptr if component not present for that entity.




**Note:**

In the thread-safe build the bounds stay valid because the view holds one structural hold per array for its whole lifetime, not because anything is pinned. Views did pin the back sector once, and every view on every thread then contended on that one counter; 




**See also:** [**Threads::PinCounters**](structecss_1_1Threads_1_1PinCounters.md). 



    
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

_Construct an end-like iterator that refers to no element._ 
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
    const std::vector< std::pair< Sectors *, SectorsRangeIt > > & secondary,
    const Ranges < EntityId > * rangeFilter=nullptr
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





**Returns:**

True when this iterator still refers to an element. 





        

<hr>



### function operator!= 

```C++
inline FORCE_INLINE bool ecss::ArraysView::Iterator::operator!= (
    const Iterator & other
) noexcept const
```





**Returns:**

Whether two iterators from the same view refer to different positions. 





        

<hr>



### function operator\* 

```C++
inline FORCE_INLINE value_type ecss::ArraysView::Iterator::operator* () noexcept const
```





**Returns:**

`(EntityId, T*, CompTypes*...)` for the current alive main component. 




**Precondition:**

The iterator is not at end. The main pointer is non-null; secondary pointers may be null. 





        

<hr>



### function operator++ 

_Advance to the next alive main component accepted by the optional range filter._ 
```C++
inline FORCE_INLINE Iterator & ecss::ArraysView::Iterator::operator++ () noexcept
```





**Precondition:**

The iterator is not at end. 





        

<hr>



### function operator== 

```C++
inline FORCE_INLINE bool ecss::ArraysView::Iterator::operator== (
    const EndIterator &
) noexcept const
```





**Returns:**

True when this iterator has reached the end position. 





        

<hr>



### function operator== 

```C++
inline FORCE_INLINE bool ecss::ArraysView::Iterator::operator== (
    const Iterator & other
) noexcept const
```





**Returns:**

Whether two iterators from the same view refer to the same position. 





        

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





**Returns:**

True when `it` still refers to an element. 





        

<hr>



### friend operator== 

```C++
inline FORCE_INLINE friend bool ecss::ArraysView::Iterator::operator== (
    const EndIterator endIt,
    const Iterator & it
) noexcept
```





**Returns:**

True when `it` has reached the end position. 





        

<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/Registry.h`

