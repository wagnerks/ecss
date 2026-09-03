

# Struct ecss::IdSet&lt; Type, true &gt;

**template &lt;typename Type&gt;**



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**IdSet&lt; Type, true &gt;**](structecss_1_1IdSet_3_01Type_00_01true_01_4.md)



_Lock-free live-id set._ [More...](#detailed-description)

* `#include <IdSet.h>`



Inherits the following classes: [ecss::detail::IdSetLayout](structecss_1_1detail_1_1IdSetLayout.md)
















## Public Types inherited from ecss::detail::IdSetLayout

See [ecss::detail::IdSetLayout](structecss_1_1detail_1_1IdSetLayout.md)

| Type | Name |
| ---: | :--- |
| typedef uint64\_t | [**Word**](structecss_1_1detail_1_1IdSetLayout.md#typedef-word)  <br> |












## Public Static Attributes inherited from ecss::detail::IdSetLayout

See [ecss::detail::IdSetLayout](structecss_1_1detail_1_1IdSetLayout.md)

| Type | Name |
| ---: | :--- |
|  Word | [**kFull**](structecss_1_1detail_1_1IdSetLayout.md#variable-kfull)   = `~Word{ 0 }`<br> |
|  size\_t | [**kWordBits**](structecss_1_1detail_1_1IdSetLayout.md#variable-kwordbits)   = `std::numeric\_limits&lt;Word&gt;::digits`<br> |


























## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**IdSet**](#function-idset-12) () = default<br>_Construct an empty id set._  |
|   | [**IdSet**](#function-idset-22) (const [**IdSet**](structecss_1_1IdSet.md) &) = delete<br>_Copying is forbidden because the set owns atomic bitmap storage._  |
|  size\_t | [**byteSize**](#function-bytesize) () const<br> |
|  void | [**clear**](#function-clear) () <br>_Release every allocated id without shrinking retained storage._  |
|  FORCE\_INLINE bool | [**contains**](#function-contains) (Type id) const<br> |
|  bool | [**empty**](#function-empty) () const<br> |
|  void | [**erase**](#function-erase-12) (const Type \* begin, const Type \* end) <br>_Release a run of ids. Ids that fall in the same word are cleared together, so a sorted list costs one operation per 64 ids rather than one per id. Correct for an unsorted list too_  _it simply flushes more often. Each word costs one read-modify-write on a line every other thread's_[_**take()**_](structecss_1_1IdSet_3_01Type_00_01true_01_4.md#function-take-12) _may be scanning, so batching them is worth more here than the instruction count suggests._ |
|  void | [**erase**](#function-erase-22) (Type id) <br>_Release_ `id` _._ |
|  std::vector&lt; Type &gt; | [**getAll**](#function-getall) () const<br> |
|  void | [**insert**](#function-insert) (Type id) <br>_Mark_ `id` _allocated._ |
|  [**IdSet**](structecss_1_1IdSet.md) & | [**operator=**](#function-operator) (const [**IdSet**](structecss_1_1IdSet.md) &) = delete<br>_Copy assignment is forbidden because the set owns atomic bitmap storage._  |
|  void | [**reserve**](#function-reserve) (Type maxId) <br>_Preallocate enough bitmap storage to represent_ `maxId` _._ |
|  size\_t | [**size**](#function-size) () const<br> |
|  Type | [**take**](#function-take-12) () <br>_Allocate and return one free id._  |
|  void | [**take**](#function-take-22) (size\_t count, std::vector&lt; Type &gt; & out) <br>_Allocate_ `count` _ids at once, appending them to_`out` _._ |
|   | [**~IdSet**](#function-idset) () <br>_Destroy the set and its retained tables and blocks._  |






## Public Static Functions inherited from ecss::detail::IdSetLayout

See [ecss::detail::IdSetLayout](structecss_1_1detail_1_1IdSetLayout.md)

| Type | Name |
| ---: | :--- |
|  FORCE\_INLINE Word | [**bitOf**](structecss_1_1detail_1_1IdSetLayout.md#function-bitof) (size\_t id) <br> |
|  FORCE\_INLINE size\_t | [**wordOf**](structecss_1_1detail_1_1IdSetLayout.md#function-wordof) (size\_t id) <br> |


















































## Detailed Description


[**take()**](structecss_1_1IdSet_3_01Type_00_01true_01_4.md#function-take-12) claims a bit with a CAS, [**erase()**](structecss_1_1IdSet_3_01Type_00_01true_01_4.md#function-erase-22) clears one with fetch\_and, [**contains()**](structecss_1_1IdSet_3_01Type_00_01true_01_4.md#function-contains) is a single load. No mutex on any of them: the previous version serialised every entity creation on one registry-global mutex (measured ~560x per-op latency at 32 threads), and the structure underneath was already O(1).


Growth never moves a block. The published table holds _pointers_ to blocks that are allocated once and never relocated, so adding a block cannot lose a bit that another thread is CASing at that moment  which copying a flat array would.




**Warning:**

[**clear()**](structecss_1_1IdSet_3_01Type_00_01true_01_4.md#function-clear) is not safe against concurrent take/erase, and the owner cannot make it so: [**take()**](structecss_1_1IdSet_3_01Type_00_01true_01_4.md#function-take-12) and [**erase()**](structecss_1_1IdSet_3_01Type_00_01true_01_4.md#function-erase-22) are lock-free by design and pass no mutex, so [**Registry**](classecss_1_1Registry.md)'s mEntitiesMutex orders [**clear()**](structecss_1_1IdSet_3_01Type_00_01true_01_4.md#function-clear) against readers of the set and nothing else. Excluding concurrent creation and destruction is the caller's. It costs no memory safety  the words are zeroed in place, never freed  but an id claimed just then can come back free and be handed out twice. Everything else here is safe. @thread\_safety Internally synchronized  and that applies to every member below, with the two exceptions named at the end.


[**take()**](structecss_1_1IdSet_3_01Type_00_01true_01_4.md#function-take-12), [**insert()**](structecss_1_1IdSet_3_01Type_00_01true_01_4.md#function-insert), [**erase()**](structecss_1_1IdSet_3_01Type_00_01true_01_4.md#function-erase-22), [**contains()**](structecss_1_1IdSet_3_01Type_00_01true_01_4.md#function-contains), [**size()**](structecss_1_1IdSet_3_01Type_00_01true_01_4.md#function-size) and [**empty()**](structecss_1_1IdSet_3_01Type_00_01true_01_4.md#function-empty) are lock-free: atomic operations on the bitmap, none of which waits for another thread. take(count, out) claims a whole word of ids per atomic operation rather than one at a time. [**getAll()**](structecss_1_1IdSet_3_01Type_00_01true_01_4.md#function-getall) is lock-free too, and gives a snapshot.


Not lock-free: growing. [**take()**](structecss_1_1IdSet_3_01Type_00_01true_01_4.md#function-take-12) and [**reserve()**](structecss_1_1IdSet_3_01Type_00_01true_01_4.md#function-reserve) add a block when the bitmap runs out, and that takes the growth mutex before publishing a fresh table. Superseded tables are kept rather than freed, so a concurrent reader walking the old one stays valid. [**reserve()**](structecss_1_1IdSet_3_01Type_00_01true_01_4.md#function-reserve) up front keeps that off the hot path.


The exceptions: the constructor and the destructor need exclusive access. The destructor frees every table and block outright.


Results are true at the moment of the call: an id [**contains()**](structecss_1_1IdSet_3_01Type_00_01true_01_4.md#function-contains) reports may be destroyed by another thread immediately afterwards, and a snapshot is stale as soon as it is taken. 


    
## Public Functions Documentation




### function IdSet [1/2]

_Construct an empty id set._ 
```C++
ecss::IdSet< Type, true >::IdSet () = default
```




<hr>



### function IdSet [2/2]

_Copying is forbidden because the set owns atomic bitmap storage._ 
```C++
ecss::IdSet< Type, true >::IdSet (
    const IdSet &
) = delete
```




<hr>



### function byteSize 

```C++
inline size_t ecss::IdSet< Type, true >::byteSize () const
```





**Returns:**

Bytes owned by currently published bitmap blocks, excluding retained old tables. 





        

<hr>



### function clear 

_Release every allocated id without shrinking retained storage._ 
```C++
inline void ecss::IdSet< Type, true >::clear () 
```





**Postcondition:**

[**empty()**](structecss_1_1IdSet_3_01Type_00_01true_01_4.md#function-empty) is true, [**size()**](structecss_1_1IdSet_3_01Type_00_01true_01_4.md#function-size) is zero, and the next [**take()**](structecss_1_1IdSet_3_01Type_00_01true_01_4.md#function-take-12) starts again at id zero. 




**Warning:**

Caller must exclude concurrent [**take()**](structecss_1_1IdSet_3_01Type_00_01true_01_4.md#function-take-12), [**insert()**](structecss_1_1IdSet_3_01Type_00_01true_01_4.md#function-insert), and [**erase()**](structecss_1_1IdSet_3_01Type_00_01true_01_4.md#function-erase-22). 





        

<hr>



### function contains 

```C++
inline FORCE_INLINE bool ecss::IdSet< Type, true >::contains (
    Type id
) const
```





**Returns:**

Whether `id` was allocated at the instant sampled by this call. 





        

<hr>



### function empty 

```C++
inline bool ecss::IdSet< Type, true >::empty () const
```





**Returns:**

Whether the set had no allocated ids in the snapshot observed by [**size()**](structecss_1_1IdSet_3_01Type_00_01true_01_4.md#function-size). 





        

<hr>



### function erase [1/2]

_Release a run of ids. Ids that fall in the same word are cleared together, so a sorted list costs one operation per 64 ids rather than one per id. Correct for an unsorted list too_  _it simply flushes more often. Each word costs one read-modify-write on a line every other thread's_[_**take()**_](structecss_1_1IdSet_3_01Type_00_01true_01_4.md#function-take-12) _may be scanning, so batching them is worth more here than the instruction count suggests._
```C++
inline void ecss::IdSet< Type, true >::erase (
    const Type * begin,
    const Type * end
) 
```




<hr>



### function erase [2/2]

_Release_ `id` _._
```C++
inline void ecss::IdSet< Type, true >::erase (
    Type id
) 
```





**Postcondition:**

contains(id) is false; erasing an unallocated id is a no-op. 





        

<hr>



### function getAll 

```C++
inline std::vector< Type > ecss::IdSet< Type, true >::getAll () const
```





**Returns:**

A snapshot of allocated ids, in ascending order when no mutation overlaps the call. 




**Note:**

A concurrent mutation may make the returned snapshot stale immediately. 





        

<hr>



### function insert 

_Mark_ `id` _allocated._
```C++
inline void ecss::IdSet< Type, true >::insert (
    Type id
) 
```





**Postcondition:**

contains(id) is true; inserting an already allocated id is a no-op. 





        

<hr>



### function operator= 

_Copy assignment is forbidden because the set owns atomic bitmap storage._ 
```C++
IdSet & ecss::IdSet< Type, true >::operator= (
    const IdSet &
) = delete
```




<hr>



### function reserve 

_Preallocate enough bitmap storage to represent_ `maxId` _._
```C++
inline void ecss::IdSet< Type, true >::reserve (
    Type maxId
) 
```





**Postcondition:**

Membership is unchanged; later access to ids through `maxId` needs no bitmap growth. @thread\_safety Internally synchronized; growth may take the growth mutex. 





        

<hr>



### function size 

```C++
inline size_t ecss::IdSet< Type, true >::size () const
```





**Returns:**

A snapshot count of allocated ids. 





        

<hr>



### function take [1/2]

_Allocate and return one free id._ 
```C++
inline Type ecss::IdSet< Type, true >::take () 
```





**Note:**

Concurrent allocations return distinct ids, but their completion order is unspecified. 





        

<hr>



### function take [2/2]

_Allocate_ `count` _ids at once, appending them to_`out` _._
```C++
inline void ecss::IdSet< Type, true >::take (
    size_t count,
    std::vector< Type > & out
) 
```



A free word is claimed whole with a single compare-exchange, so a large batch costs about one atomic per 64 ids rather than one per id  and threads that collide on a word simply move to the next one, which spreads them without needing the striping the single-id path uses.


Ids come out ascending within each word but a batch taken while another thread is also taking may interleave, so the result is not guaranteed globally sorted. 


        

<hr>



### function ~IdSet 

_Destroy the set and its retained tables and blocks._ 
```C++
inline ecss::IdSet< Type, true >::~IdSet () 
```





**Precondition:**

No operation may run concurrently with destruction. 





        

<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/IdSet.h`

