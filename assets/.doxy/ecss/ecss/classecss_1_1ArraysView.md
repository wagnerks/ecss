

# Class ecss::ArraysView

**template &lt;bool ThreadSafe, typename Allocator, bool Ranged, typename T, typename ... CompTypes&gt;**



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**ArraysView**](classecss_1_1ArraysView.md)



_Iterable view over entities with one main component and optional additional components._ [More...](#detailed-description)

* `#include <Registry.h>`















## Classes

| Type | Name |
| ---: | :--- |
| struct | [**EndIterator**](structecss_1_1ArraysView_1_1EndIterator.md) <br>_Sentinel end iterator tag._  |
| class | [**Iterator**](classecss_1_1ArraysView_1_1Iterator.md) <br>_Forward iterator over alive sectors of the main component type._  |






















## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**ArraysView**](#function-arraysview-12) ([**Registry**](classecss_1_1Registry.md)&lt; ThreadSafe, Allocator &gt; \* manager) noexcept<br>_Construct a full-range view (Ranged=false specialization)._  |
|   | [**ArraysView**](#function-arraysview-22) ([**Registry**](classecss_1_1Registry.md)&lt; ThreadSafe, Allocator &gt; \* manager, const Ranges&lt; EntityId &gt; & ranges={}) noexcept<br> |
|  FORCE\_INLINE [**Iterator**](classecss_1_1ArraysView_1_1Iterator.md) | [**begin**](#function-begin) () noexcept const<br> |
|  FORCE\_INLINE void | [**each**](#function-each) (Func && func) const<br>_Fast iteration without tuple overhead. Single component: func(T&), Multi component grouped: func(T&, CompTypes&...)_  |
|  FORCE\_INLINE bool | [**empty**](#function-empty) () noexcept const<br> |
|  FORCE\_INLINE [**EndIterator**](structecss_1_1ArraysView_1_1EndIterator.md) | [**end**](#function-end) () noexcept const<br> |




























## Detailed Description




**Template parameters:**


* `ThreadSafe` Mirrors [**Registry**](classecss_1_1Registry.md) thread-safe flag (affects pinning). 
* `Allocator` Allocator used by sectors. 
* `Ranged` Whether this view limits iteration to provided ranges. 
* `T` Main component type (drives iteration order). 
* `CompTypes` Additional component types optionally retrieved per entity.

Semantics:
* Iterates only sectors where main component T is alive.
* For each entity id, returns pointers (T\*, optional others may be nullptr if absent).
* In ranged mode, entity ranges are translated to nearest sector indices (clamped).




Thread safety:
* ThreadSafe=true: Back sector pinning ensures iteration upper bound stability.
* Non-main components may be null if not present or not grouped in same array.






**Warning:**

Do not cache raw pointers across mutating frames unless externally synchronized. 





    
## Public Functions Documentation




### function ArraysView [1/2]

_Construct a full-range view (Ranged=false specialization)._ 
```C++
inline explicit ecss::ArraysView::ArraysView (
    Registry < ThreadSafe, Allocator > * manager
) noexcept
```




<hr>



### function ArraysView [2/2]

```C++
inline explicit ecss::ArraysView::ArraysView (
    Registry < ThreadSafe, Allocator > * manager,
    const Ranges< EntityId > & ranges={}
) noexcept
```




<hr>



### function begin 

```C++
inline FORCE_INLINE Iterator ecss::ArraysView::begin () noexcept const
```





**Returns:**

[**Iterator**](classecss_1_1ArraysView_1_1Iterator.md) to first alive element (or end if empty). 





        

<hr>



### function each 

_Fast iteration without tuple overhead. Single component: func(T&), Multi component grouped: func(T&, CompTypes&...)_ 
```C++
template<typename Func>
inline FORCE_INLINE void ecss::ArraysView::each (
    Func && func
) const
```




<hr>



### function empty 

```C++
inline FORCE_INLINE bool ecss::ArraysView::empty () noexcept const
```




<hr>



### function end 

```C++
inline FORCE_INLINE EndIterator ecss::ArraysView::end () noexcept const
```





**Returns:**

Sentinel end marker. 





        

<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/Registry.h`

