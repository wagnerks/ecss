

# Class ecss::Registry

**template &lt;bool ThreadSafe, typename Allocator&gt;**



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Registry**](classecss_1_1Registry.md)



_Central ECS registry that owns component sector arrays, entities and iteration utilities._ [More...](#detailed-description)

* `#include <Registry.h>`





































## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**Registry**](#function-registry-24) () noexcept<br>_Default construct an empty registry (no arrays allocated until first use)._  |
|   | [**Registry**](#function-registry-34) (const [**Registry**](classecss_1_1Registry.md) & other) noexcept<br> |
|   | [**Registry**](#function-registry-44) ([**Registry**](classecss_1_1Registry.md) && other) noexcept<br> |
|  FORCE\_INLINE T \* | [**addComponent**](#function-addcomponent) (EntityId entity, Args &&... args) noexcept<br>_Add or overwrite a component T for an entity._  |
|  void | [**addComponents**](#function-addcomponents) (Func && func) <br>_Bulk add components via generator functor under a single write lock (thread-safe build)._  |
|  void | [**clear**](#function-clear) () noexcept<br>_Clear all component arrays and remove all entities._  |
|  FORCE\_INLINE bool | [**contains**](#function-contains) (EntityId entityId) noexcept const<br> |
|  void | [**defragment**](#function-defragment-12) () noexcept<br>_Defragment all arrays (compacts fragmented dead slots)._  |
|  FORCE\_INLINE void | [**defragment**](#function-defragment-22) () noexcept<br>_Defragment the container for component T (if it exists)._  |
|  void | [**destroyComponent**](#function-destroycomponent-12) (EntityId entity) noexcept<br>_Destroy component T for a single entity (does nothing if not present)._  |
|  void | [**destroyComponent**](#function-destroycomponent-22) (std::vector&lt; EntityId &gt; & entities) noexcept<br>_Destroy component T for a batch of entities._  |
|  void | [**destroyEntities**](#function-destroyentities) (std::vector&lt; EntityId &gt; & entities) noexcept<br>_Destroy a batch of entities and all their components (sequential per-array)._  |
|  void | [**destroyEntity**](#function-destroyentity) (EntityId entityId) noexcept<br>_Destroy a single entity and all of its components._  |
|  void | [**forEachAsync**](#function-foreachasync) (const std::vector&lt; EntityId &gt; & entities, Func && func) noexcept<br>_Apply a function to each entity in a list, pinning requested component types (thread-safe build)._  |
|  FORCE\_INLINE std::vector&lt; EntityId &gt; | [**getAllEntities**](#function-getallentities) () noexcept const<br>_Snapshot all entity ids (copy)._  |
|  [**Memory::SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md)&lt; ThreadSafe, Allocator &gt; \* | [**getComponentContainer**](#function-getcomponentcontainer) () noexcept<br>_Get (or lazily create) the sectors container for component T._  |
|  FORCE\_INLINE bool | [**hasComponent**](#function-hascomponent) (EntityId entity) noexcept<br>_Check if an entity has a live component T._  |
|  FORCE\_INLINE void | [**insert**](#function-insert-12) (const [**Memory::SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md)&lt; TS, Alloc &gt; & array) noexcept<br>_Copy-in an externally built sectors array for component T._  |
|  FORCE\_INLINE void | [**insert**](#function-insert-22) ([**Memory::SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md)&lt; TS, Alloc &gt; && array) noexcept<br>_Move-in an externally built sectors array for component T._  |
|  [**Registry**](classecss_1_1Registry.md) & | [**operator=**](#function-operator) (const [**Registry**](classecss_1_1Registry.md) & other) noexcept<br> |
|  [**Registry**](classecss_1_1Registry.md) & | [**operator=**](#function-operator_1) ([**Registry**](classecss_1_1Registry.md) && other) noexcept<br> |
|  [**PinnedComponent**](structecss_1_1PinnedComponent.md)&lt; T &gt; | [**pinComponent**](#function-pincomponent) (EntityId entity) noexcept<br>_Pin component T for an entity (thread-safe build only)._  |
|  void | [**registerArray**](#function-registerarray) (uint32\_t capacity=0, Allocator allocator={}) noexcept<br>_Explicitly register (group) component types into a shared sectors array._  |
|  FORCE\_INLINE void | [**reserve**](#function-reserve) (uint32\_t newCapacity) noexcept<br>_Reserve capacity (in sectors array units) for each listed component type._  |
|  FORCE\_INLINE void | [**setDefragmentThreshold**](#function-setdefragmentthreshold) (float threshold) <br>_Set defragment threshold for component T container._  |
|  FORCE\_INLINE EntityId | [**takeEntity**](#function-takeentity) () noexcept<br>_Allocate (take) a new entity id._  |
|  void | [**update**](#function-update-12) (bool withDefragment=true) noexcept<br>_Maintenance pass (thread-safe build): process deferred erases and optionally defragment._  |
|  void | [**update**](#function-update-22) (bool withDefragment=true) noexcept<br>_Maintenance pass (non-thread-safe build): optionally defragment arrays immediately._  |
|  FORCE\_INLINE auto | [**view**](#function-view-12) () noexcept<br>_Create a full-range iterable view over all entities with the main component._  |
|  FORCE\_INLINE auto | [**view**](#function-view-22) (const Ranges&lt; EntityId &gt; & ranges) noexcept<br>_Create an iterable view limited to given entity ranges._  |
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




Thread safety modes:
* ThreadSafe == true:
  * Shared mutex protects component array mapping and entity container.
  * Pin counters block structural mutation while components are in use.


* ThreadSafe == false:
  * No internal synchronization � single-threaded performance focus.






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

_Default construct an empty registry (no arrays allocated until first use)._ 
```C++
ecss::Registry::Registry () noexcept
```




<hr>



### function Registry [3/4]

```C++
ecss::Registry::Registry (
    const Registry & other
) noexcept
```




<hr>



### function Registry [4/4]

```C++
ecss::Registry::Registry (
    Registry && other
) noexcept
```




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





        

<hr>



### function addComponents 

_Bulk add components via generator functor under a single write lock (thread-safe build)._ 
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


* `func` Generator invoked repeatedly while lock is held. 



**Note:**

Optimizes many insertions by avoiding enter/leave lock per element. 





        

<hr>



### function clear 

_Clear all component arrays and remove all entities._ 
```C++
inline void ecss::Registry::clear () noexcept
```





**Note:**

Does not shrink capacity. 




**Postcondition:**

contains(id)==false for any previously allocated entity. 





        

<hr>



### function contains 

```C++
inline FORCE_INLINE bool ecss::Registry::contains (
    EntityId entityId
) noexcept const
```





**Returns:**

True if registry currently owns entityId. 





        

<hr>



### function defragment [1/2]

_Defragment all arrays (compacts fragmented dead slots)._ 
```C++
inline void ecss::Registry::defragment () noexcept
```





**Note:**

Can be expensive if many arrays large � schedule during low frame-load moments. 





        

<hr>



### function defragment [2/2]

_Defragment the container for component T (if it exists)._ 
```C++
template<typename T>
inline FORCE_INLINE void ecss::Registry::defragment () noexcept
```




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


* `entity` Entity id. @complexity O(1). 




        

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

Pins are waited if thread-safe; call outside tight critical paths if possible. 





        

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





        

<hr>



### function destroyEntity 

_Destroy a single entity and all of its components._ 
```C++
inline void ecss::Registry::destroyEntity (
    EntityId entityId
) noexcept
```





**Parameters:**


* `entityId` Entity to remove (ignored if not owned). @complexity O(A) with A = number of component arrays. 




        

<hr>



### function forEachAsync 

_Apply a function to each entity in a list, pinning requested component types (thread-safe build)._ 
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





        

<hr>



### function getAllEntities 

_Snapshot all entity ids (copy)._ 
```C++
inline FORCE_INLINE std::vector< EntityId > ecss::Registry::getAllEntities () noexcept const
```




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

Will implicitly register a single-type array if not pre-registered. 





        

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

True if the component exists and is alive; false otherwise. @complexity O(1). @thread\_safety Locking/pinning applied if ThreadSafe=true. 





        

<hr>



### function insert [1/2]

_Copy-in an externally built sectors array for component T._ 
```C++
template<typename T, bool TS, typename Alloc>
inline FORCE_INLINE void ecss::Registry::insert (
    const Memory::SectorsArray < TS, Alloc > & array
) noexcept
```




<hr>



### function insert [2/2]

_Move-in an externally built sectors array for component T._ 
```C++
template<typename T, bool TS, typename Alloc>
inline FORCE_INLINE void ecss::Registry::insert (
    Memory::SectorsArray < TS, Alloc > && array
) noexcept
```




<hr>



### function operator= 

```C++
Registry & ecss::Registry::operator= (
    const Registry & other
) noexcept
```




<hr>



### function operator= 

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

The returned object must not outlive concurrent modification epochs. 





        

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

Call before first implicit access to any of the grouped types. 





        

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


* `newCapacity` Target capacity (implementation may round up). 




        

<hr>



### function setDefragmentThreshold 

_Set defragment threshold for component T container._ 
```C++
template<typename T>
inline FORCE_INLINE void ecss::Registry::setDefragmentThreshold (
    float threshold
) 
```




<hr>



### function takeEntity 

_Allocate (take) a new entity id._ 
```C++
inline FORCE_INLINE EntityId ecss::Registry::takeEntity () noexcept
```




<hr>



### function update [1/2]

_Maintenance pass (thread-safe build): process deferred erases and optionally defragment._ 
```C++
inline void ecss::Registry::update (
    bool withDefragment=true
) noexcept
```





**Parameters:**


* `withDefragment` If true, arrays that exceed thresholds may compact themselves. 



**Note:**

Recommended to call once per frame at a stable synchronization point. @thread\_safety Internally synchronized. 





        

<hr>



### function update [2/2]

_Maintenance pass (non-thread-safe build): optionally defragment arrays immediately._ 
```C++
inline void ecss::Registry::update (
    bool withDefragment=true
) noexcept
```





**Parameters:**


* `withDefragment` If true, compacts arrays that request it. @thread\_safety Caller must ensure exclusive access. 




        

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

[**ArraysView**](classecss_1_1ArraysView.md) instance (full range). 





        

<hr>



### function view [2/2]

_Create an iterable view limited to given entity ranges._ 
```C++
template<typename... Components>
inline FORCE_INLINE auto ecss::Registry::view (
    const Ranges< EntityId > & ranges
) noexcept
```





**Template parameters:**


* `Components` Component types to fetch; first drives iteration order. 



**Parameters:**


* `ranges` Half-open entity ranges. 



**Returns:**

[**ArraysView**](classecss_1_1ArraysView.md) instance (ranged iteration). 





        

<hr>



### function ~Registry 

_Destroys all component arrays (each SectorsArray is deleted)._ 
```C++
inline ecss::Registry::~Registry () noexcept
```




<hr>
## Public Static Functions Documentation




### function componentTypeId 

_Get a stable numeric type id for component T._ 
```C++
template<typename T>
static inline FORCE_INLINE ECSType ecss::Registry::componentTypeId () noexcept
```





**Template parameters:**


* `T` Component type. 




        

<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/Registry.h`

