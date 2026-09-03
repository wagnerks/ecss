

# Class ecss::ArraysView

**template &lt;bool ThreadSafe, typename Allocator, bool Ranged, typename T, typename ... CompTypes&gt;**



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**ArraysView**](classecss_1_1ArraysView.md)



_Iteration over one or more component arrays, driven by the first type named._ [More...](#detailed-description)

* `#include <Registry.h>`















## Classes

| Type | Name |
| ---: | :--- |
| struct | [**EndIterator**](structecss_1_1ArraysView_1_1EndIterator.md) <br>_Sentinel end iterator tag._  |
| class | [**Iterator**](classecss_1_1ArraysView_1_1Iterator.md) <br>_Forward iterator over alive sectors of the main component type._  |






















## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**ArraysView**](#function-arraysview-14) ([**ArraysView**](classecss_1_1ArraysView.md) &&) = delete<br> |
|   | [**ArraysView**](#function-arraysview-24) (const [**ArraysView**](classecss_1_1ArraysView.md) &) = delete<br> |
|   | [**ArraysView**](#function-arraysview-34) ([**Registry**](classecss_1_1Registry.md)&lt; ThreadSafe, Allocator &gt; \* manager) noexcept<br>_Construct a full-range view (Ranged=false specialization)._  |
|   | [**ArraysView**](#function-arraysview-44) ([**Registry**](classecss_1_1Registry.md)&lt; ThreadSafe, Allocator &gt; \* manager, const [**Ranges**](structecss_1_1Ranges.md)&lt; EntityId &gt; & ranges={}) noexcept<br>_Construct a ranged view limited to_ `ranges` _(Ranged=true specialization)._ |
|  FORCE\_INLINE [**Iterator**](classecss_1_1ArraysView_1_1Iterator.md) | [**begin**](#function-begin) () noexcept const<br> |
|  FORCE\_INLINE void | [**each**](#function-each) (Func && func) const<br>_Fast iteration without tuple overhead. Single component: func(T&). Several: func(T&, CompTypes&...)._  |
|  FORCE\_INLINE bool | [**empty**](#function-empty) () noexcept const<br> |
|  FORCE\_INLINE [**EndIterator**](structecss_1_1ArraysView_1_1EndIterator.md) | [**end**](#function-end) () noexcept const<br> |
|  [**ArraysView**](classecss_1_1ArraysView.md) & | [**operator=**](#function-operator) ([**ArraysView**](classecss_1_1ArraysView.md) &&) = delete<br> |
|  [**ArraysView**](classecss_1_1ArraysView.md) & | [**operator=**](#function-operator_1) (const [**ArraysView**](classecss_1_1ArraysView.md) &) = delete<br> |




























## Detailed Description


@thread\_safety Thread-confined  the view object, not the arrays. A view belongs to the thread that opened it; any number of threads may each hold their own over the same arrays at the same time.


Its whole lifetime is a structural hold on every array it names: while it lives, no sector in those arrays may be relocated, so the pointers it hands out stay good. That is also what makes writers wait. [**Registry::clear()**](classecss_1_1Registry.md#function-clear), [**Registry::defragment()**](classecss_1_1Registry.md#function-defragment-12) and an insert landing in the middle of one of these arrays cannot finish until the view is destroyed  from another thread they block, from this one they deadlock. Keep views short, and close one before restructuring what it was reading.


Iterating tells you a component is alive; it does not stop another thread writing its value. 

**See also:** [**Registry::access()**](classecss_1_1Registry.md#function-access) 



    
## Public Functions Documentation




### function ArraysView [1/4]

```C++
ecss::ArraysView::ArraysView (
    ArraysView &&
) = delete
```




<hr>



### function ArraysView [2/4]

```C++
ecss::ArraysView::ArraysView (
    const ArraysView &
) = delete
```



A view is a fixed place, not a value. Its cached begin iterator points into the view's own mRanges, so a copy or a move would leave that iterator addressing the original  dangling the moment the original dies. The structural holds it carries have the same shape of problem as AccessGuard's: they are released against per-thread bookkeeping, so a second owner would release what it never took.


Nothing is lost by this. [**Registry::view()**](classecss_1_1Registry.md#function-view-12) returns a prvalue, so `auto v = reg.view<T>()` and iterating a temporary both go through guaranteed elision and never needed either. 


        

<hr>



### function ArraysView [3/4]

_Construct a full-range view (Ranged=false specialization)._ 
```C++
inline explicit ecss::ArraysView::ArraysView (
    Registry < ThreadSafe, Allocator > * manager
) noexcept
```




<hr>



### function ArraysView [4/4]

_Construct a ranged view limited to_ `ranges` _(Ranged=true specialization)._
```C++
inline explicit ecss::ArraysView::ArraysView (
    Registry < ThreadSafe, Allocator > * manager,
    const Ranges < EntityId > & ranges={}
) noexcept
```





**Parameters:**


* `manager` [**Registry**](classecss_1_1Registry.md) whose component arrays are viewed. 
* `ranges` Half-open entity-id ranges to visit. An empty set visits no entities. 




        

<hr>



### function begin 

```C++
inline FORCE_INLINE Iterator ecss::ArraysView::begin () noexcept const
```





**Returns:**

[**Iterator**](classecss_1_1ArraysView_1_1Iterator.md) to first alive element (or end if empty). 





        

<hr>



### function each 

_Fast iteration without tuple overhead. Single component: func(T&). Several: func(T&, CompTypes&...)._ 
```C++
template<typename Func>
inline FORCE_INLINE void ecss::ArraysView::each (
    Func && func
) const
```



This is a join, and that is the difference from iterating the view with a range-for. Here func takes references and is called, in view iteration order, only for entities that have every named component; one missing secondary skips the entity. A range-for yields (EntityId, T\*, CompTypes\*...) for every entity with the main component, and a secondary it lacks arrives as nullptr for you to test.


Pick [**each()**](classecss_1_1ArraysView.md#function-each) when the entity is only interesting with all of them, and the range-for when a missing component means something to your code. 


        

<hr>



### function empty 

```C++
inline FORCE_INLINE bool ecss::ArraysView::empty () noexcept const
```





**Returns:**

True when [**begin()**](classecss_1_1ArraysView.md#function-begin) equals [**end()**](classecss_1_1ArraysView.md#function-end), otherwise false. 





        

<hr>



### function end 

```C++
inline FORCE_INLINE EndIterator ecss::ArraysView::end () noexcept const
```





**Returns:**

Sentinel end marker. 





        

<hr>



### function operator= 

```C++
ArraysView & ecss::ArraysView::operator= (
    ArraysView &&
) = delete
```




<hr>



### function operator= 

```C++
ArraysView & ecss::ArraysView::operator= (
    const ArraysView &
) = delete
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/Registry.h`

