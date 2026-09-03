

# Class ecss::CommandBuffer

**template &lt;bool ThreadSafe, typename Allocator&gt;**



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**CommandBuffer**](classecss_1_1CommandBuffer.md)



_Records structural changes and applies them together at a chosen point._ [More...](#detailed-description)

* `#include <CommandBuffer.h>`





































## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**CommandBuffer**](#function-commandbuffer-13) () = default<br> |
|   | [**CommandBuffer**](#function-commandbuffer-23) ([**CommandBuffer**](classecss_1_1CommandBuffer.md) &&) noexcept<br>_Transfer all recorded operations to another buffer._  |
|   | [**CommandBuffer**](#function-commandbuffer-33) (const [**CommandBuffer**](classecss_1_1CommandBuffer.md) &) = delete<br>_Copying is forbidden; recorded operations have one owning buffer._  |
|  void | [**addComponent**](#function-addcomponent) (EntityId entity, Args &&... args) <br>_Record "give @p entity a T", to be applied by_ [_**apply()**_](classecss_1_1CommandBuffer.md#function-apply) _. Recording the same entity twice for one type keeps the later value, matching what a pair of immediate addComponent calls would have left behind. @thread\_safety Thread-confined. One buffer belongs to one thread; nothing in it takes a lock. Give each recording thread its own._ |
|  void | [**apply**](#function-apply) ([**Reg**](classecss_1_1Registry.md) & registry) <br>_Apply everything recorded, then empty the buffer._  |
|  void | [**clear**](#function-clear) () <br>_Drop everything recorded without applying it. @thread\_safety Thread-confined. One buffer belongs to one thread; nothing in it takes a lock. Give each recording thread its own._  |
|  void | [**destroyComponent**](#function-destroycomponent) (EntityId entity) <br>_Record "take T away from @p entity". @thread\_safety Thread-confined. One buffer belongs to one thread; nothing in it takes a lock. Give each recording thread its own._  |
|  void | [**destroyEntity**](#function-destroyentity) (EntityId entity) <br>_Record "destroy @p entity", across every component type it has. @thread\_safety Thread-confined. One buffer belongs to one thread; nothing in it takes a lock. Give each recording thread its own._  |
|  bool | [**empty**](#function-empty) () const<br> |
|  [**CommandBuffer**](classecss_1_1CommandBuffer.md) & | [**operator=**](#function-operator) ([**CommandBuffer**](classecss_1_1CommandBuffer.md) &&) noexcept<br>_Replace this buffer with the recorded operations owned by_ `other` _._ |
|  [**CommandBuffer**](classecss_1_1CommandBuffer.md) & | [**operator=**](#function-operator_1) (const [**CommandBuffer**](classecss_1_1CommandBuffer.md) &) = delete<br>_Copy assignment is forbidden; recorded operations have one owning buffer._  |
|  size\_t | [**size**](#function-size) () const<br> |




























## Detailed Description


Adding or removing a component, or destroying an entity, changes the shape of an array rather than the value of a component. Those are the only operations the thread-safe build charges for  component values are written straight through the iterator and cost the same either way  and they are the only ones that are illegal while iterating the array they touch. Both problems come from the same place, and both go away if the change is recorded now and applied later:



* one lock and one pass per component type instead of per call, and one sorted merge instead of a shift per insert;
* nothing is applied while a view is open, so the structural change cannot invalidate an iterator, and a thread cannot end up waiting for a hold only it could release.




It is not free, and not always a win. Adding a component to 200000 entities, ns per add:  ascending ids, plain 7.3 18.8 ascending ids, TS 37.1 21.2 shuffled ids, plain 62563.7 73.9 shuffled ids, TS 101611.1 89.7


Against an append in the non-thread-safe build the buffer loses: that path is already a push and recording costs more than doing it. It wins wherever the immediate call would pay for either per-call synchronisation or a middle insert, which is every other row  and ids arriving in ascending order is the exception in gameplay code, not the rule.


Deferral is explicit: the buffer is a separate object with its own verbs, so it is never a surprise that a recorded change is not visible until [**apply()**](classecss_1_1CommandBuffer.md#function-apply). [**Registry**](classecss_1_1Registry.md)'s own addComponent/destroyEntity still take effect immediately.




**Note:**

Ids come from the registry, not the buffer, so an entity created for a recorded add is usable as an id right away  see [**Registry::takeEntities**](classecss_1_1Registry.md#function-takeentities).


@thread\_safety One buffer belongs to one thread. Give each recording thread its own and apply them one after another; the buffer itself takes no locks. 


    
## Public Functions Documentation




### function CommandBuffer [1/3]

```C++
ecss::CommandBuffer::CommandBuffer () = default
```



@thread\_safety Thread-confined. One buffer belongs to one thread; nothing in it takes a lock. Give each recording thread its own. 


        

<hr>



### function CommandBuffer [2/3]

_Transfer all recorded operations to another buffer._ 
```C++
ecss::CommandBuffer::CommandBuffer (
    CommandBuffer &&
) noexcept
```





**Postcondition:**

The destination has the source's former observable state. 




**Note:**

The moved-from buffer remains valid but its state is unspecified. 





        

<hr>



### function CommandBuffer [3/3]

_Copying is forbidden; recorded operations have one owning buffer._ 
```C++
ecss::CommandBuffer::CommandBuffer (
    const CommandBuffer &
) = delete
```




<hr>



### function addComponent 

_Record "give @p entity a T", to be applied by_ [_**apply()**_](classecss_1_1CommandBuffer.md#function-apply) _. Recording the same entity twice for one type keeps the later value, matching what a pair of immediate addComponent calls would have left behind. @thread\_safety Thread-confined. One buffer belongs to one thread; nothing in it takes a lock. Give each recording thread its own._
```C++
template<typename T, typename... Args>
inline void ecss::CommandBuffer::addComponent (
    EntityId entity,
    Args &&... args
) 
```




<hr>



### function apply 

_Apply everything recorded, then empty the buffer._ 
```C++
inline void ecss::CommandBuffer::apply (
    Reg & registry
) 
```



For any one entity and component type the last thing recorded wins, so removing a component and adding it back in the same frame leaves it present, and the reverse leaves it absent  the same answer the immediate calls would have given in that order. Batching is kept: the surviving adds and removals still go out as one call per type.


Entity destruction is applied after all of it and is terminal, so an entity both written to and destroyed in the same frame ends up destroyed regardless of the order the two were recorded in. Recording anything against an entity already destroyed in this buffer is a caller error  its id may already belong to something else. @thread\_safety Thread-confined; blocks. This is the call that touches the registry: it inserts and destroys, so it takes on the contract of those operations. Apply at a point where nothing is iterating the arrays it writes to  that is the whole reason to record into a buffer first. 


        

<hr>



### function clear 

_Drop everything recorded without applying it. @thread\_safety Thread-confined. One buffer belongs to one thread; nothing in it takes a lock. Give each recording thread its own._ 
```C++
inline void ecss::CommandBuffer::clear () 
```




<hr>



### function destroyComponent 

_Record "take T away from @p entity". @thread\_safety Thread-confined. One buffer belongs to one thread; nothing in it takes a lock. Give each recording thread its own._ 
```C++
template<typename T>
inline void ecss::CommandBuffer::destroyComponent (
    EntityId entity
) 
```




<hr>



### function destroyEntity 

_Record "destroy @p entity", across every component type it has. @thread\_safety Thread-confined. One buffer belongs to one thread; nothing in it takes a lock. Give each recording thread its own._ 
```C++
inline void ecss::CommandBuffer::destroyEntity (
    EntityId entity
) 
```




<hr>



### function empty 

```C++
inline bool ecss::CommandBuffer::empty () const
```





**Returns:**

True when nothing is waiting to be applied. @thread\_safety Thread-confined. One buffer belongs to one thread; nothing in it takes a lock. Give each recording thread its own. 





        

<hr>



### function operator= 

_Replace this buffer with the recorded operations owned by_ `other` _._
```C++
CommandBuffer & ecss::CommandBuffer::operator= (
    CommandBuffer &&
) noexcept
```





**Postcondition:**

The destination has `other's` former observable state. 




**Note:**

The moved-from buffer remains valid but its state is unspecified. 





        

<hr>



### function operator= 

_Copy assignment is forbidden; recorded operations have one owning buffer._ 
```C++
CommandBuffer & ecss::CommandBuffer::operator= (
    const CommandBuffer &
) = delete
```




<hr>



### function size 

```C++
inline size_t ecss::CommandBuffer::size () const
```





**Returns:**

How many changes are recorded, for sizing a flush or for diagnostics. @thread\_safety Thread-confined. One buffer belongs to one thread; nothing in it takes a lock. Give each recording thread its own. 





        

<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/CommandBuffer.h`

