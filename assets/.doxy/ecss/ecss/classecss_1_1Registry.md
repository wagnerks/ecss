

# Class ecss::Registry

**template &lt;bool ThreadSafe, typename Allocator&gt;**



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Registry**](classecss_1_1Registry.md)



_Central ECS registry that owns component sector arrays, entities and iteration utilities._ [More...](#detailed-description)

* `#include <Registry.h>`















## Classes

| Type | Name |
| ---: | :--- |
| struct | [**ComponentAccess**](structecss_1_1Registry_1_1ComponentAccess.md) <br>_The array holding T together with T's layout record, from one snapshot load._  |






















## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**Registry**](#function-registry-24) () noexcept<br>_Default construct an empty registry (no arrays allocated until first use). @thread\_safety Internally synchronized, vacuously: nothing can name the object until the constructor returns, so there is nothing to race with._  |
|   | [**Registry**](#function-registry-34) (const [**Registry**](classecss_1_1Registry.md) & other) noexcept<br>[_**Registry**_](classecss_1_1Registry.md) _instances cannot be copied; they uniquely own their arrays and entity ids._ |
|   | [**Registry**](#function-registry-44) ([**Registry**](classecss_1_1Registry.md) && other) noexcept<br>[_**Registry**_](classecss_1_1Registry.md) _instances cannot be moved because views and handles may refer to their address._ |
|  [**detail::AccessGuard**](classecss_1_1detail_1_1AccessGuard.md) | [**access**](#function-access) () <br>_Watch for two threads touching one component type at once (debug builds)._  |
|  FORCE\_INLINE T \* | [**addComponent**](#function-addcomponent) (EntityId entity, Args &&... args) noexcept<br>_Add or overwrite a component T for an entity._  |
|  void | [**addComponents**](#function-addcomponents) (Func && func) <br>_Bulk add components from a generator, inserted as one batch._  |
|  bool | [**autoMaintenance**](#function-automaintenance) () noexcept const<br> |
|  void | [**clear**](#function-clear) () noexcept<br>_Clear all component arrays and remove all entities._  |
|  void | [**clearAsync**](#function-clearasync) () noexcept<br>_Ask every array to clear itself at the next safe point, instead of now._  |
|  FORCE\_INLINE bool | [**contains**](#function-contains) (EntityId entityId) noexcept const<br> |
|  void | [**defragment**](#function-defragment-12) () noexcept<br>_Defragment all arrays (compacts fragmented dead slots)._  |
|  FORCE\_INLINE void | [**defragment**](#function-defragment-22) () noexcept<br>_Defragment the container for component T (if it exists). @thread\_safety Internally synchronized; blocks. Waits until this one array carries no pins and no open views._  |
|  void | [**destroyComponent**](#function-destroycomponent-12) (EntityId entity) noexcept<br>_Destroy component T for a single entity (does nothing if not present)._  |
|  void | [**destroyComponent**](#function-destroycomponent-22) (std::vector&lt; EntityId &gt; & entities) noexcept<br>_Destroy component T for a batch of entities._  |
|  void | [**destroyEntities**](#function-destroyentities) (std::vector&lt; EntityId &gt; & entities) noexcept<br>_Destroy a batch of entities and all their components (sequential per-array)._  |
|  void | [**destroyEntity**](#function-destroyentity) (EntityId entityId) noexcept<br>_Destroy a single entity and all of its components._  |
|  void | [**forEachAsync**](#function-foreachasync) (const std::vector&lt; EntityId &gt; & entities, Func && func) noexcept<br>_Apply a function to each entity in a list, pinning the requested component types._  |
|  FORCE\_INLINE std::vector&lt; EntityId &gt; | [**getAllEntities**](#function-getallentities) () noexcept const<br>_Snapshot all entity ids (copy). @thread\_safety Internally synchronized. Snapshot: correct when taken, stale as soon as another thread takes or destroys an id._  |
|  FORCE\_INLINE [**ComponentAccess**](structecss_1_1Registry_1_1ComponentAccess.md) | [**getComponentAccess**](#function-getcomponentaccess) () noexcept<br>_Resolve T's array and layout in a single lookup._  |
|  [**Memory::SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md)&lt; ThreadSafe, Allocator &gt; \* | [**getComponentContainer**](#function-getcomponentcontainer) () noexcept<br>_Get (or lazily create) the sectors container for component T._  |
|  FORCE\_INLINE bool | [**hasComponent**](#function-hascomponent) (EntityId entity) noexcept<br>_Check if an entity has a live component T._  |
|  FORCE\_INLINE void | [**insert**](#function-insert-12) (const [**Memory::SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md)&lt; TS, Alloc &gt; & array) noexcept<br>_Copy-in an externally built sectors array for component T._  |
|  FORCE\_INLINE void | [**insert**](#function-insert-22) ([**Memory::SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md)&lt; TS, Alloc &gt; && array) noexcept<br>_Move-in an externally built sectors array for component T. @thread\_safety Internally synchronized; blocks. Replaces the whole array, so it waits until the destination carries no pins and no open views._  |
|  void | [**insertBulk**](#function-insertbulk) (It first, It last) <br>_Bulk insert of component T for a batch of (entity, value) pairs._  |
|  FORCE\_INLINE [**ComponentAccess**](structecss_1_1Registry_1_1ComponentAccess.md) | [**lookupComponentAccess**](#function-lookupcomponentaccess) () noexcept<br>_One snapshot load; empty if T has no array yet._  |
|  [**Registry**](classecss_1_1Registry.md) & | [**operator=**](#function-operator) (const [**Registry**](classecss_1_1Registry.md) & other) noexcept<br>[_**Registry**_](classecss_1_1Registry.md) _instances cannot be copy-assigned._ |
|  [**Registry**](classecss_1_1Registry.md) & | [**operator=**](#function-operator_1) ([**Registry**](classecss_1_1Registry.md) && other) noexcept<br>[_**Registry**_](classecss_1_1Registry.md) _instances cannot be move-assigned._ |
|  [**PinnedComponent**](structecss_1_1PinnedComponent.md)&lt; T &gt; | [**pinComponent**](#function-pincomponent) (EntityId entity) noexcept<br>_Pin component T for an entity (thread-safe build only)._  |
|  void | [**registerArray**](#function-registerarray) (uint32\_t capacity=0, Allocator allocator={}) noexcept<br>_Explicitly register (group) component types into a shared sectors array._  |
|  FORCE\_INLINE void | [**reserve**](#function-reserve) (uint32\_t newCapacity) noexcept<br>_Reserve capacity (in sectors array units) for each listed component type._  |
|  void | [**setAccessTracking**](#function-setaccesstracking) (bool enabled) noexcept<br> |
|  void | [**setAutoMaintenance**](#function-setautomaintenance) (bool enabled) noexcept<br>_Let views carry out the maintenance_ [_**update()**_](classecss_1_1Registry.md#function-update-12) _does, for the arrays they touch._ |
|  FORCE\_INLINE void | [**setDefragmentThreshold**](#function-setdefragmentthreshold) (float threshold) <br>_Set defragment threshold for component T container. @thread\_safety Internally synchronized. One relaxed store; it changes when compaction is requested, never compaction itself._  |
|  void | [**setRetireGracePeriod**](#function-setretiregraceperiod) (uint32\_t ticks) noexcept<br>_Set the grace period (in ticks) before retired memory is freed._  |
|  FORCE\_INLINE void | [**takeEntities**](#function-takeentities) (size\_t count, std::vector&lt; EntityId &gt; & out) noexcept<br>_Allocate_ `count` _entity ids in one pass, appending them to_`out` _._ |
|  FORCE\_INLINE EntityId | [**takeEntity**](#function-takeentity) () noexcept<br>_Allocate (take) a new entity id._  |
|  size\_t | [**tick**](#function-tick) () noexcept<br>_Process one tick of the grace period for retired memory._  |
|  void | [**update**](#function-update-12) (bool withDefragment=true) noexcept<br>_Maintenance pass (thread-safe build): process deferred erases, free retired memory, and optionally defragment._  |
|  void | [**update**](#function-update-22) (bool withDefragment=true) noexcept<br>_Maintenance pass (non-thread-safe build): optionally defragment arrays immediately._  |
|  FORCE\_INLINE auto | [**view**](#function-view-12) () noexcept<br>_Create a full-range iterable view over all entities with the main component._  |
|  FORCE\_INLINE auto | [**view**](#function-view-22) (const [**Ranges**](structecss_1_1Ranges.md)&lt; EntityId &gt; & ranges) noexcept<br>_Create an iterable view limited to given entity ranges._  |
|  void | [**warnIfGroupingIgnored**](#function-warnifgroupingignored) () noexcept const<br>_Say so when_ [_**registerArray()**_](classecss_1_1Registry.md#function-registerarray) _was asked to group types that are already apart._ |
|  void | [**warnPartialGrouping**](#function-warnpartialgrouping) () noexcept const<br>_Say so when only some of the named types already have arrays._  |
|   | [**~Registry**](#function-registry) () noexcept<br>_Destroys all component arrays (each SectorsArray is deleted)._  |


## Public Static Functions

| Type | Name |
| ---: | :--- |
|  FORCE\_INLINE ECSType | [**componentTypeId**](#function-componenttypeid) () noexcept<br>_Get a stable numeric type id for component T._  |


























## Detailed Description




**Template parameters:**


* `ThreadSafe` If true, operations use internal locks / pin counters for safe concurrent access. 
* `Allocator` Allocator used by SectorsArray (defaults to chunked allocator).

Responsibilities:
* Entity lifecycle (allocate / erase ids).
* Lazily create or explicitly register component arrays (can group types).
* Component add / overwrite / remove (single or batch).
* Bulk entity destruction with all their components.
* Iteration via [**ArraysView**](classecss_1_1ArraysView.md) over one or more component types.




Thread safety. Every public member carries an @thread\_safety line. It answers one question  may two threads call this on the same object at once  with one of three answers, and adds "; blocks" when the call waits:



* "Internally synchronized." Call it from any number of threads at once.
* "Thread-confined." The object belongs to one thread and holds no lock, exactly as a std::vector does. Do not add a mutex around it  give each thread its own. Views, pins, access guards and command buffers are all this.
* "Caller must ensure exclusive access." A _shared_ object with no synchronization of its own. Here you do have to supply the ordering: a lock, or a place in the frame where nothing else is running. Setup switches and raw lock accessors live here.




The difference matters because the fix is opposite. The first says stop sharing it; the second says guard it.



* "Not applicable (single-threaded build)." Only exists when ThreadSafe == false.




"; blocks" is separate from all three, because waiting and being callable concurrently are different questions  a destructor waits for in-flight readers and still must not race with new ones. It means one specific thing: the call waits for a pin, a hold or a view to be released, which is state a _reader_ controls. That is the wait worth warning about, because a caller can be the reader it is waiting for. Ordinary contention for a mutex between writers is not marked  almost every synchronized call has some, and flagging it would say nothing. Where it appears, what is waited for is named. Two widths:
* one sector  waits until that entity's sector carries no pins. Only a pin on that same entity delays it.
* the whole array  waits until the array carries no pins _and_ no open views. Every [**ArraysView**](classecss_1_1ArraysView.md) holds one for as long as it lives, so anything in this group cannot finish while a view on that array is open. From the thread that opened the view it is a deadlock; from another thread it finishes when the view closes, and a thread iterating in a tight loop can hold it off indefinitely. Nothing here is enforced at compile time. Debug builds assert on the same-thread case; release builds simply block.




All of the above is about the _shape_ of an array. A component's _value_ is a separate question the container does not answer: reading one while another thread writes it is the caller's to arrange. 

**See also:** [**access()**](classecss_1_1Registry.md#function-access), [**setAccessTracking()**](classecss_1_1Registry.md#function-setaccesstracking)
Performance notes:
* Component insertion is O(1) amortized (sector-based).
* hasComponent is O(1) (sector lookup + bit test).
* destroyEntities (sequential version here) visits each array =&gt; O(A \* log N) for sorting per array prep.






**Warning:**

Entity ids are reused; do not cache them beyond system boundaries without validation. 




**Note:**

Use [**reserve&lt;Components...&gt;()**](classecss_1_1Registry.md#function-reserve) to pre-allocate sector capacity and reduce reallocations. 





    
## Public Functions Documentation




### function Registry [2/4]

_Default construct an empty registry (no arrays allocated until first use). @thread\_safety Internally synchronized, vacuously: nothing can name the object until the constructor returns, so there is nothing to race with._ 
```C++
inline ecss::Registry::Registry () noexcept
```




<hr>



### function Registry [3/4]

[_**Registry**_](classecss_1_1Registry.md) _instances cannot be copied; they uniquely own their arrays and entity ids._
```C++
ecss::Registry::Registry (
    const Registry & other
) noexcept
```




<hr>



### function Registry [4/4]

[_**Registry**_](classecss_1_1Registry.md) _instances cannot be moved because views and handles may refer to their address._
```C++
ecss::Registry::Registry (
    Registry && other
) noexcept
```




<hr>



### function access 

_Watch for two threads touching one component type at once (debug builds)._ 
```C++
template<typename... Claims>
inline detail::AccessGuard ecss::Registry::access () 
```



The container keeps an array's _shape_ safe on its own: it will not be relocated under an iterator, and a pinned sector will not move or die. A component's _value_ is a different matter  guarding that would mean locking or pinning per element, which costs more than the lock-free read paths save. So a system reading Position while another writes it is a race the container cannot see.


With this on, it is seen: the first overlap aborts and names the type and the two threads. A view counts as reading its component types for as long as it lives, and a mutator as writing one for the duration of the call; re-entering from the same thread is fine, since a system routinely reads what it just wrote.


Worth leaving on for the whole of development. It compiles to nothing when NDEBUG is set, so there is nothing to turn off before shipping.


Claim component types for the length of a system, so two of them cannot touch the same type at once.



```C++
auto access = reg.access<Read<Position>, Write<Velocity>>();
for (auto [e, p, v] : reg.view<Position, Velocity>()) {
    v->dx += p->x;          // inside, everything runs at full speed as before
}
```



The container keeps an array's shape safe by itself; it does not keep a component's value stable while another thread writes it, and guarding that per element would cost more than the lock-free read paths save. This puts the guarantee at the granularity systems actually work at  a whole component type  so it is paid once per system rather than once per element. A reader-writer lock per type: 29.5 ns for one, 84.8 for three, against 27 ns to pin a single component  and pinning would have to happen per element rather than per system.


Name every type the system touches in one call. Taking them one at a time lets two systems claim the same pair in opposite orders and stop; asked for together, they are sorted by type id so every caller agrees on the order.


Nesting from the same thread is fine and does nothing. Asking to write a type this thread already reads is refused in debug rather than upgraded, since there is no atomic upgrade and doing it in two steps brings the deadlock back.


Nothing forces its use: a system that knows it is the only one touching a type can skip it and lose nothing. [**setAccessTracking()**](classecss_1_1Registry.md#function-setaccesstracking) is what finds the ones that were wrong to skip. @thread\_safety Internally synchronized; blocks by design. This is the one entry point whose whole job is to wait: it takes a reader-writer lock per component type and holds it until the guard dies. Name every type the system touches in one call  claiming them one at a time is how two systems deadlock. 


        

<hr>



### function addComponent 

_Add or overwrite a component T for an entity._ 
```C++
template<class T, class ... Args>
inline FORCE_INLINE T * ecss::Registry::addComponent (
    EntityId entity,
    Args &&... args
) noexcept
```





**Template parameters:**


* `T` Component type. 
* `Args` Constructor argument types for T. 



**Parameters:**


* `entity` Entity id (also used logically as sector id). 
* `args` Construction / assignment arguments. 



**Returns:**

Pointer to the stored component. 




**Note:**

Overwrites existing component instance (destructive assign semantics inside sector).


Add or overwrite component T on an entity. 

**Warning:**

Illegal while this thread holds a view or a pin on T's array, unless the id is above every id already stored: any other position shifts existing sectors, which would invalidate the live iterator. Debug builds assert; release builds hang. See the SectorsArray class documentation. 




**Note:**

Adding M components with ids that are not ascending costs O(M\*N) this way. Use [**insertBulk()**](classecss_1_1Registry.md#function-insertbulk) or [**addComponents()**](classecss_1_1Registry.md#function-addcomponents) for a batch  they merge in one pass. @thread\_safety Internally synchronized; blocks. An id above everything already stored appends and waits for nothing. An id that lands in the middle has to shift the sectors after it, so it waits for the whole array to carry no pins and no open views  including a view this thread has open, which deadlocks. Feed ids in ascending order, or use [**insertBulk()**](classecss_1_1Registry.md#function-insertbulk), to stay on the fast path.


To add from inside a loop over a view, record into an [**ecss::CommandBuffer**](classecss_1_1CommandBuffer.md) and apply it once the loop is done. 


        

<hr>



### function addComponents 

_Bulk add components from a generator, inserted as one batch._ 
```C++
template<class T, typename Func>
inline void ecss::Registry::addComponents (
    Func && func
) 
```





**Template parameters:**


* `T` Component type. 
* `Func` Callable returning std::pair&lt;EntityId,T&gt;. Return {INVALID\_ID, {}} to stop. 



**Parameters:**


* `func` Generator invoked repeatedly until it signals the end. 



**Note:**

Ids may be emitted in any order. The batch is collected, then merged in a single pass under one write lock. @thread\_safety Internally synchronized; blocks in the thread-safe build. Same contract as [**addComponent()**](classecss_1_1Registry.md#function-addcomponent): an ascending run appends, anything landing in the middle waits for the array to carry no pins and no open views. Not applicable in the plain build, which has neither. 





        

<hr>



### function autoMaintenance 

```C++
inline bool ecss::Registry::autoMaintenance () noexcept const
```





**Returns:**

Whether views maintain the arrays they open. 




**See also:** [**setAutoMaintenance**](classecss_1_1Registry.md#function-setautomaintenance) @thread\_safety Internally synchronized. One relaxed load in the thread-safe build, a plain one otherwise. 



        

<hr>



### function clear 

_Clear all component arrays and remove all entities._ 
```C++
inline void ecss::Registry::clear () noexcept
```





**Note:**

Does not shrink capacity. 




**Postcondition:**

contains(id)==false for any previously allocated entity. @thread\_safety Internally synchronized; blocks on every array. Each one waits until it carries no pins and no open views. A view open on another thread holds this up for as long as it lives; a view open on this thread deadlocks.


[**clearAsync()**](classecss_1_1Registry.md#function-clearasync) is the deferred form, safe to call from anywhere. 


        

<hr>



### function clearAsync 

_Ask every array to clear itself at the next safe point, instead of now._ 
```C++
inline void ecss::Registry::clearAsync () noexcept
```



The deferred counterpart to [**clear()**](classecss_1_1Registry.md#function-clear). Records the wish and returns, so unlike [**clear()**](classecss_1_1Registry.md#function-clear) it is safe to call from anywhere, including from inside a loop over a view. Each array performs it the first time [**update()**](classecss_1_1Registry.md#function-update-12) finds it free.


Entity ids are held until the components are actually gone, and released by the [**update()**](classecss_1_1Registry.md#function-update-12) that finds every array done. Releasing them at once would be cheap and wrong: [**takeEntity()**](classecss_1_1Registry.md#function-takeentity) would hand one straight back while the sector naming it is still alive, and the new entity would read the old one's components as its own. So [**contains()**](classecss_1_1Registry.md#function-contains) keeps reporting them until the clear has really happened.




**Note:**

Asked for, not promised. 




**See also:** [**clear()**](classecss_1_1Registry.md#function-clear), [**update()**](classecss_1_1Registry.md#function-update-12) @thread\_safety Internally synchronized. One relaxed store per array plus the id set; nothing is waited for. 



        

<hr>



### function contains 

```C++
inline FORCE_INLINE bool ecss::Registry::contains (
    EntityId entityId
) noexcept const
```





**Returns:**

True if registry currently owns entityId. 




**Returns:**

True if the registry currently owns entityId. Lock-free: a single load. @thread\_safety Internally synchronized. Lock-free read of the id set. 





        

<hr>



### function defragment [1/2]

_Defragment all arrays (compacts fragmented dead slots)._ 
```C++
inline void ecss::Registry::defragment () noexcept
```





**Note:**

Can be expensive if many arrays are large; schedule during low frame-load moments. @thread\_safety Internally synchronized; blocks on every array. Compaction moves sectors, so each array waits until it carries no pins and no open views. [**update()**](classecss_1_1Registry.md#function-update-12) is the deferred form: it skips busy arrays and picks them up next time, which makes it safe to call from inside a loop over a view. 





        

<hr>



### function defragment [2/2]

_Defragment the container for component T (if it exists). @thread\_safety Internally synchronized; blocks. Waits until this one array carries no pins and no open views._ 
```C++
template<typename T>
inline FORCE_INLINE void ecss::Registry::defragment () noexcept
```





**See also:** [**defragment()**](classecss_1_1Registry.md#function-defragment-12) 



        

<hr>



### function destroyComponent [1/2]

_Destroy component T for a single entity (does nothing if not present)._ 
```C++
template<class T>
inline void ecss::Registry::destroyComponent (
    EntityId entity
) noexcept
```





**Template parameters:**


* `T` Component type. 



**Parameters:**


* `entity` Entity id. @complexity O(1). @thread\_safety Internally synchronized; blocks, but only on this one sector. It waits for that entity's sector to carry no pins; pins on other entities and views over the array do not delay it. Holding a pin to the component being destroyed, on this thread, deadlocks. 




        

<hr>



### function destroyComponent [2/2]

_Destroy component T for a batch of entities._ 
```C++
template<class T>
inline void ecss::Registry::destroyComponent (
    std::vector< EntityId > & entities
) noexcept
```





**Template parameters:**


* `T` Component type. 



**Parameters:**


* `entities` Entity id list (will be sorted and truncated to valid sector capacity). 



**Note:**

Modifies the input vector (sorting, trimming out-of-range ids). 




**Warning:**

Pins are waited if thread-safe; call outside tight critical paths if possible. @thread\_safety Internally synchronized; blocks, but only on the named sectors. Waits for each listed entity to carry no pins; unrelated entities and open views do not delay it. 





        

<hr>



### function destroyEntities 

_Destroy a batch of entities and all their components (sequential per-array)._ 
```C++
inline void ecss::Registry::destroyEntities (
    std::vector< EntityId > & entities
) noexcept
```





**Parameters:**


* `entities` List of entities (not modified). 



**Note:**

Safe to call while other threads query (ThreadSafe=true). 




**Warning:**

No parallelization here to avoid thread lifetime complexity.


Destroy a batch of entities across every registered array. 

**Parameters:**


* `entities` Ids to destroy. Sorted ascending is the cheap case; any other order is sorted in place first, since destroyInArray binary-searches this range to trim ids past each array's sparse map. 



**Note:**

Far cheaper than a loop of [**destroyEntity()**](classecss_1_1Registry.md#function-destroyentity): one lock and one pass per array rather than per entity  14.3 ns per entity against 74.4. @thread\_safety Internally synchronized; blocks, but only on the named entities' sectors. 





        

<hr>



### function destroyEntity 

_Destroy a single entity and all of its components._ 
```C++
inline void ecss::Registry::destroyEntity (
    EntityId entityId
) noexcept
```





**Parameters:**


* `entityId` Entity to remove (ignored if not owned). @complexity O(A) with A = number of component arrays. @thread\_safety Internally synchronized; blocks, but only on this entity's sectors. Each array holding the entity is asked to destroy in place, which waits for that one sector to carry no pins. Arrays that do not hold it are skipped without taking their lock. A pin this thread holds on the entity deadlocks. 




        

<hr>



### function forEachAsync 

_Apply a function to each entity in a list, pinning the requested component types._ 
```C++
template<typename... Components, typename Func>
inline void ecss::Registry::forEachAsync (
    const std::vector< EntityId > & entities,
    Func && func
) noexcept
```





**Template parameters:**


* `Components` Component types to pin. 
* `Func` Callable signature: void(EntityId, Components\*...). 



**Parameters:**


* `entities` Entity ids to process. 
* `func` Function invoked per entity. 



**Note:**

Skips entities missing any main pinned component (pointer passed may be nullptr for non-main).




**Warning:**

Despite the name, this starts no threads. It is a loop over the list on the calling thread, pinning each entity's components in turn. The name and this contract described workers with a view each, which was never what it did. To spread the work, slice the list yourself and call this from each thread  which is safe, and is what the pinning is for. @thread\_safety Internally synchronized. Pins each entity's components for the length of one call to func. The usual rule applies inside func: no structural change to an array being iterated. 





        

<hr>



### function getAllEntities 

_Snapshot all entity ids (copy). @thread\_safety Internally synchronized. Snapshot: correct when taken, stale as soon as another thread takes or destroys an id._ 
```C++
inline FORCE_INLINE std::vector< EntityId > ecss::Registry::getAllEntities () noexcept const
```




<hr>



### function getComponentAccess 

_Resolve T's array and layout in a single lookup._ 
```C++
template<class T>
inline FORCE_INLINE ComponentAccess ecss::Registry::getComponentAccess () noexcept
```



The layout was resolved when the array was registered, so the per-call scan over the layout's type tokens disappears  that scan is why a query against a grouped array grew more expensive the more component types the group held.


The cached record is checked, not assumed: [**Registry::insert**](classecss_1_1Registry.md#function-insert-12) can still repoint an array at a different layout, and the recorded one would then describe the wrong sector shape. Comparing the layout the array reports now against the one this entry was built from costs a single load and compare  measured at 0.2-0.35 ns against a 2-6 ns saving  and turns a stale entry into a slow path rather than a wrong answer. @thread\_safety Internally synchronized. Registers the array on first use, which publishes a new snapshot; the old one is retired, not freed, so a concurrent reader walking it is safe. 


        

<hr>



### function getComponentContainer 

_Get (or lazily create) the sectors container for component T._ 
```C++
template<class T>
inline Memory::SectorsArray < ThreadSafe, Allocator > * ecss::Registry::getComponentContainer () noexcept
```





**Template parameters:**


* `T` Component type. 



**Returns:**

Pointer to container holding (possibly grouped) T. 




**Note:**

Will implicitly register a single-type array if not pre-registered. @thread\_safety Internally synchronized. Returns the array, and the array outlives every call on this registry, so the pointer stays valid. What you then call on it carries its own contract  see SectorsArray. 





        

<hr>



### function hasComponent 

_Check if an entity has a live component T._ 
```C++
template<class T>
inline FORCE_INLINE bool ecss::Registry::hasComponent (
    EntityId entity
) noexcept
```





**Template parameters:**


* `T` Component type. 



**Parameters:**


* `entity` Entity id. 



**Returns:**

True if the component exists and is alive; false otherwise. @complexity O(1). @thread\_safety Internally synchronized. Lock-free once the type is registered: the slot lookup and the liveness word both come from published snapshots. The very first call for a type is not  [**getComponentAccess()**](classecss_1_1Registry.md#function-getcomponentaccess) registers the array under a unique lock  so touch each type once at startup if that matters. Tells you the component exists, not that its value is stable: another thread may be writing it. 




**See also:** [**access()**](classecss_1_1Registry.md#function-access) 



        

<hr>



### function insert [1/2]

_Copy-in an externally built sectors array for component T._ 
```C++
template<typename T, bool TS, typename Alloc>
inline FORCE_INLINE void ecss::Registry::insert (
    const Memory::SectorsArray < TS, Alloc > & array
) noexcept
```





**Warning:**

The source must have been built over the same component types in the same order as T's registered array. [A, B] and [B, A] are different layouts, and so are [A] and [A, B]. A mismatch leaves this registry unchanged (and asserts in debug) rather than repointing the array at a foreign layout: the sector size and the liveness bits would no longer describe the bytes, and the registry would still route B to a different array anyway. @thread\_safety Internally synchronized; blocks. Replaces the whole array, so it waits until the destination carries no pins and no open views. 





        

<hr>



### function insert [2/2]

_Move-in an externally built sectors array for component T. @thread\_safety Internally synchronized; blocks. Replaces the whole array, so it waits until the destination carries no pins and no open views._ 
```C++
template<typename T, bool TS, typename Alloc>
inline FORCE_INLINE void ecss::Registry::insert (
    Memory::SectorsArray < TS, Alloc > && array
) noexcept
```




<hr>



### function insertBulk 

_Bulk insert of component T for a batch of (entity, value) pairs._ 
```C++
template<class T, class It>
inline void ecss::Registry::insertBulk (
    It first,
    It last
) 
```





**Template parameters:**


* `T` Component type. 
* `It` Iterator over pair-like {EntityId, T}. 



**Parameters:**


* `first` Range of pairs. Ids may be in any order and may fall anywhere in the range already stored; an id already present is overwritten. 



**Note:**

Amortizes per-element overhead (existence check, insert-position search, view publish) and, in the TS build, the write lock and pin wait across the whole range. Prefer this to a loop of [**addComponent()**](classecss_1_1Registry.md#function-addcomponent) whenever the ids are not ascending: the loop costs O(M\*N), this sorts once and merges in a single pass. @thread\_safety Internally synchronized; blocks. A batch entirely above what is stored is appended and waits for nothing. Otherwise the batch is merged, which moves existing sectors, and that waits for the array to carry no pins and no open views  from a thread holding a view on this array, a deadlock.


To add from inside a loop over a view, record into an [**ecss::CommandBuffer**](classecss_1_1CommandBuffer.md) and apply it once the loop is done. 


        

<hr>



### function lookupComponentAccess 

_One snapshot load; empty if T has no array yet._ 
```C++
template<class T>
inline FORCE_INLINE ComponentAccess ecss::Registry::lookupComponentAccess () noexcept
```





**See also:** [**getComponentAccess**](classecss_1_1Registry.md#function-getcomponentaccess) @thread\_safety Internally synchronized. Lock-free; returns a null [**access**](classecss_1_1Registry.md#function-access) if the type has not been registered rather than registering it. 



        

<hr>



### function operator= 

[_**Registry**_](classecss_1_1Registry.md) _instances cannot be copy-assigned._
```C++
Registry & ecss::Registry::operator= (
    const Registry & other
) noexcept
```




<hr>



### function operator= 

[_**Registry**_](classecss_1_1Registry.md) _instances cannot be move-assigned._
```C++
Registry & ecss::Registry::operator= (
    Registry && other
) noexcept
```




<hr>



### function pinComponent 

_Pin component T for an entity (thread-safe build only)._ 
```C++
template<class T>
inline PinnedComponent < T > ecss::Registry::pinComponent (
    EntityId entity
) noexcept
```





**Template parameters:**


* `T` Component type. 



**Parameters:**


* `entity` Entity id. 



**Returns:**

[**PinnedComponent&lt;T&gt;**](structecss_1_1PinnedComponent.md) (empty if component missing). 




**Note:**

The returned object must not outlive concurrent modification epochs. @thread\_safety Internally synchronized. The pin is the point: while it is held, that one sector will not be moved, destroyed or reused, so the pointer stays good. It also delays anything that needs that sector  destroyComponent and destroyEntity for the same entity wait for it, and holding a pin while destroying what it points at deadlocks. Keep it short. 





        

<hr>



### function registerArray 

_Explicitly register (group) component types into a shared sectors array._ 
```C++
template<typename... ComponentTypes>
inline void ecss::Registry::registerArray (
    uint32_t capacity=0,
    Allocator allocator={}
) noexcept
```





**Template parameters:**


* `ComponentTypes` Component types to co-locate. 



**Parameters:**


* `capacity` Initial reserve (optional). 
* `allocator` Allocator instance to move. 



**Note:**

All types must either all be new or already co-grouped; partial mixes assert. 




**Warning:**

Call before first implicit access to any of the grouped types. @thread\_safety Internally synchronized. Publishes a new array snapshot; the superseded one is retired rather than freed, so a reader still walking it is safe. Grouping decides the memory layout, so do it at startup, before anything stores a component of these types. 





        

<hr>



### function reserve 

_Reserve capacity (in sectors array units) for each listed component type._ 
```C++
template<class... Components>
inline FORCE_INLINE void ecss::Registry::reserve (
    uint32_t newCapacity
) noexcept
```





**Template parameters:**


* `Components` Component types to reserve for. 



**Parameters:**


* `newCapacity` Target capacity (implementation may round up). @thread\_safety Internally synchronized. Takes each array's write lock to grow it, which is brief, and does not wait for pins or views: growth adds chunks and moves no sector. Worth doing before the threads start, to keep it off the frame. 




        

<hr>



### function setAccessTracking 

```C++
inline void ecss::Registry::setAccessTracking (
    bool enabled
) noexcept
```



@thread\_safety Internally synchronized. One relaxed store to a process-wide atomic flag, so calling it while threads run is not a race  but it is still a startup switch: turning tracking on late cannot show the overlaps that already happened. Compiled out entirely when NDEBUG is set. 


        

<hr>



### function setAutoMaintenance 

_Let views carry out the maintenance_ [_**update()**_](classecss_1_1Registry.md#function-update-12) _does, for the arrays they touch._
```C++
inline void ecss::Registry::setAutoMaintenance (
    bool enabled
) noexcept
```



With this on there is no call to place in the frame: opening a view first gives its arrays the pass [**update()**](classecss_1_1Registry.md#function-update-12) would have given them  retire the memory whose grace period has run out, apply deferred erases, and compact if the array asked for it and is free at that moment. A busy array is skipped, exactly as in [**update()**](classecss_1_1Registry.md#function-update-12), so this never blocks and never changes what iteration sees.


Compaction is done where it pays: right before the iteration that benefits from it. The total work is the same either way  an array wants compacting once, and once done it stops asking  so this moves the cost rather than adding it.


Off by default: opening a view is a read, and doing structural work inside one should be asked for rather than assumed. Set it once at startup, before other threads exist. [**update()**](classecss_1_1Registry.md#function-update-12) keeps working and stays the way to control when the work lands. @thread\_safety Internally synchronized. One relaxed store in the thread-safe build, a plain one otherwise. Still worth doing at startup: flipping it mid-frame changes what opening a view does, so two threads can disagree about whether their views maintain. 


        

<hr>



### function setDefragmentThreshold 

_Set defragment threshold for component T container. @thread\_safety Internally synchronized. One relaxed store; it changes when compaction is requested, never compaction itself._ 
```C++
template<typename T>
inline FORCE_INLINE void ecss::Registry::setDefragmentThreshold (
    float threshold
) 
```




<hr>



### function setRetireGracePeriod 

_Set the grace period (in ticks) before retired memory is freed._ 
```C++
inline void ecss::Registry::setRetireGracePeriod (
    uint32_t ticks
) noexcept
```



Higher values = safer but more memory usage. Lower values = less memory but risk of use-after-free if iterators live long.


Default is 3 ticks, which is safe for typical game loops where iterators don't survive across frames.




**Note:**

The non-thread-safe build fixes this at zero and ignores the setter.




**Parameters:**


* `ticks` Number of [**tick()**](classecss_1_1Registry.md#function-tick) calls before memory is freed @thread\_safety Internally synchronized. The period each bin holds is an atomic, and a block captures its own countdown when it is retired, so lowering the period never shortens the life of something already queued. Zero is the one value that would be unsafe here and it is refused: see below. 




        

<hr>



### function takeEntities 

_Allocate_ `count` _entity ids in one pass, appending them to_`out` _._
```C++
inline FORCE_INLINE void ecss::Registry::takeEntities (
    size_t count,
    std::vector< EntityId > & out
) noexcept
```



Prefer this to a loop of [**takeEntity()**](classecss_1_1Registry.md#function-takeentity) when streaming a region in: the bitmap is walked once instead of once per id, and in the thread-safe build a free word of 64 ids is claimed with a single atomic. @thread\_safety Internally synchronized. Lock-free, and claims a whole word of ids per atomic operation rather than one at a time. 


        

<hr>



### function takeEntity 

_Allocate (take) a new entity id._ 
```C++
inline FORCE_INLINE EntityId ecss::Registry::takeEntity () noexcept
```



Allocate (take) a new entity id. 

**Note:**

Lock-free in the thread-safe build: the id bitmap claims a bit with a CAS. Serialising this on the registry mutex cost ~560x per-op latency at 32 threads. @thread\_safety Internally synchronized. Lock-free claim out of the id set. 





        

<hr>



### function tick 

_Process one tick of the grace period for retired memory._ 
```C++
inline size_t ecss::Registry::tick () noexcept
```



Call this once per frame/update cycle. Memory blocks that have waited the full grace period (default 3 ticks) will be freed.


This is safe to call while iterators may be active in other threads - only sufficiently old memory (older than grace period) will be freed.




**Note:**

In non-thread-safe mode memory is freed as it is released, so this is a no-op returning zero. It stays callable in both modes.




**Returns:**

Total number of memory blocks freed across all arrays @thread\_safety Internally synchronized. Advances the grace-period counters and frees what has expired; takes each bin's mutex only when it has something in it, and never waits for a reader. 





        

<hr>



### function update [1/2]

_Maintenance pass (thread-safe build): process deferred erases, free retired memory, and optionally defragment._ 
```C++
inline void ecss::Registry::update (
    bool withDefragment=true
) noexcept
```



Safe to call from anywhere, including from inside a loop over a view: nothing here waits. Deferred erases only destroy components in place, and compaction is attempted rather than awaited  an array that something is iterating right now is left for the next call. Calling it more than once a frame is harmless; calling it at a quiet point simply means more of the work lands on the first try.


To compact regardless of who is iterating, and to wait for them, call SectorsArray::defragment() on the array directly. 

**Parameters:**


* `withDefragment` If true, arrays that exceed thresholds may compact themselves. 



**Note:**

Recommended to call once per frame at a stable synchronization point. 




**Note:**

Automatically frees retired memory that has passed the grace period (default 3 ticks). @thread\_safety Internally synchronized. Nothing here waits, so this is the one maintenance entry point that is safe to call from inside a loop over a view. [**clear()**](classecss_1_1Registry.md#function-clear), [**defragment()**](classecss_1_1Registry.md#function-defragment-12) and the array's own [**defragment()**](classecss_1_1Registry.md#function-defragment-12) are not. 





        

<hr>



### function update [2/2]

_Maintenance pass (non-thread-safe build): optionally defragment arrays immediately._ 
```C++
inline void ecss::Registry::update (
    bool withDefragment=true
) noexcept
```





**Parameters:**


* `withDefragment` If true, compacts arrays that request it. @thread\_safety Not applicable (single-threaded build). 




        

<hr>



### function view [1/2]

_Create a full-range iterable view over all entities with the main component._ 
```C++
template<typename... Components>
inline FORCE_INLINE auto ecss::Registry::view () noexcept
```





**Template parameters:**


* `Components` Component types to access; first drives iteration. 



**Returns:**

[**ArraysView**](classecss_1_1ArraysView.md) instance (full range). @thread\_safety Internally synchronized. Any number of threads may iterate at once. The view holds the arrays it names for as long as it lives, which is what keeps sectors from moving underneath it  and equally what makes [**clear()**](classecss_1_1Registry.md#function-clear), [**defragment()**](classecss_1_1Registry.md#function-defragment-12) and a middle insert wait. Keep views short, and do not open one around a structural change to the same array. 





        

<hr>



### function view [2/2]

_Create an iterable view limited to given entity ranges._ 
```C++
template<typename... Components>
inline FORCE_INLINE auto ecss::Registry::view (
    const Ranges < EntityId > & ranges
) noexcept
```





**Template parameters:**


* `Components` Component types to fetch; first drives iteration order. 



**Parameters:**


* `ranges` Half-open entity ranges. 



**Returns:**

[**ArraysView**](classecss_1_1ArraysView.md) instance (ranged iteration). @thread\_safety Internally synchronized. Any number of threads may iterate at once. The view holds the arrays it names for as long as it lives, which is what keeps sectors from moving underneath it  and equally what makes [**clear()**](classecss_1_1Registry.md#function-clear), [**defragment()**](classecss_1_1Registry.md#function-defragment-12) and a middle insert wait. Keep views short, and do not open one around a structural change to the same array. 





        

<hr>



### function warnIfGroupingIgnored 

_Say so when_ [_**registerArray()**_](classecss_1_1Registry.md#function-registerarray) _was asked to group types that are already apart._
```C++
template<class... ComponentTypes>
inline void ecss::Registry::warnIfGroupingIgnored () noexcept const
```



Registering the same group twice is genuinely idempotent and should say nothing. Asking to group types that already live in separate arrays is a different thing: a component's layout is fixed when its array is created, so the request cannot be honoured and never will be. It used to return without a word, which meant a [**registerArray()**](classecss_1_1Registry.md#function-registerarray) written one line too late  after the [**addComponent()**](classecss_1_1Registry.md#function-addcomponent) that registered the type implicitly  looked exactly like one that worked.


Define ECSS\_NO\_GROUPING\_WARNINGS to silence it. @thread\_safety Caller must ensure exclusive access. Reads the live map, so it belongs under componentsArrayMapMutex in the thread-safe build. 


        

<hr>



### function warnPartialGrouping 

_Say so when only some of the named types already have arrays._ 
```C++
template<class... ComponentTypes>
inline void ecss::Registry::warnPartialGrouping () noexcept const
```



The assert beside this one is compiled out of a release build, which left the call returning in silence there  the same way the already-grouped case did, and just as misleading. 

**See also:** [**warnIfGroupingIgnored**](classecss_1_1Registry.md#function-warnifgroupingignored) @thread\_safety Caller must ensure exclusive [**access**](classecss_1_1Registry.md#function-access). Reads the live map. 



        

<hr>



### function ~Registry 

_Destroys all component arrays (each SectorsArray is deleted)._ 
```C++
inline ecss::Registry::~Registry () noexcept
```



@thread\_safety Internally synchronized; blocks. Deleting an array clears it, and clearing waits until that array carries no pins and no open views, so a view open on another thread stalls this destructor for as long as it lives  in-flight readers are waited for, not run over.


That is a synchronization guarantee, not a lifetime one. Nothing may _start_ using the registry once destruction has begun; the waiting covers the readers already inside, and cannot cover the ones that arrive afterwards. 


        

<hr>
## Public Static Functions Documentation




### function componentTypeId 

_Get a stable numeric type id for component T._ 
```C++
template<typename T>
static inline FORCE_INLINE ECSType ecss::Registry::componentTypeId () noexcept
```





**Template parameters:**


* `T` Component type. @thread\_safety Internally synchronized. The id is assigned once, by whichever thread asks first, and never changes afterwards. Ids come from one process-wide counter, so two registries agree on the number for a type. 




        

<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/Registry.h`

