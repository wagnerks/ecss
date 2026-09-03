

# Struct ecss::IdSet&lt; Type, false &gt;

**template &lt;typename Type&gt;**



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**IdSet&lt; Type, false &gt;**](structecss_1_1IdSet_3_01Type_00_01false_01_4.md)



[More...](#detailed-description)

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
|  size\_t | [**byteSize**](#function-bytesize) () const<br> |
|  void | [**clear**](#function-clear) () <br>_Release every allocated id without shrinking retained storage._  |
|  FORCE\_INLINE bool | [**contains**](#function-contains) (Type id) const<br> |
|  bool | [**empty**](#function-empty) () const<br> |
|  void | [**erase**](#function-erase-12) (const Type \* begin, const Type \* end) <br>_Release a run of ids. Ids that fall in the same word are cleared together, so a sorted list costs one operation per 64 ids rather than one per id. Correct for an unsorted list too_  _it simply flushes more often._ |
|  void | [**erase**](#function-erase-22) (Type id) <br>_Release_ `id` _. No-op if it is not allocated._ |
|  std::vector&lt; Type &gt; | [**getAll**](#function-getall) () const<br>_Every allocated id, ascending._  |
|  void | [**insert**](#function-insert) (Type id) <br>_Mark_ `id` _allocated. No-op if it already is._ |
|  void | [**reserve**](#function-reserve) (Type maxId) <br>_Preallocate enough bitmap storage to represent_ `maxId` _._ |
|  size\_t | [**size**](#function-size) () const<br> |
|  Type | [**take**](#function-take-12) () <br>_Allocate and return the lowest free id._  |
|  void | [**take**](#function-take-22) (size\_t count, std::vector&lt; Type &gt; & out) <br>_Allocate_ `count` _ids at once, appending them to_`out` _in ascending order._ |






## Public Static Functions inherited from ecss::detail::IdSetLayout

See [ecss::detail::IdSetLayout](structecss_1_1detail_1_1IdSetLayout.md)

| Type | Name |
| ---: | :--- |
|  FORCE\_INLINE Word | [**bitOf**](structecss_1_1detail_1_1IdSetLayout.md#function-bitof) (size\_t id) <br> |
|  FORCE\_INLINE size\_t | [**wordOf**](structecss_1_1detail_1_1IdSetLayout.md#function-wordof) (size\_t id) <br> |


















































## Detailed Description


@thread\_safety Not applicable (single-threaded build)  and that applies to every member below. There is no synchronization anywhere in this specialization: every one of them needs exclusive access. 


    
## Public Functions Documentation




### function byteSize 

```C++
inline size_t ecss::IdSet< Type, false >::byteSize () const
```





**Returns:**

Bytes retained by the bitmap's allocated word capacity. 





        

<hr>



### function clear 

_Release every allocated id without shrinking retained storage._ 
```C++
inline void ecss::IdSet< Type, false >::clear () 
```





**Postcondition:**

[**empty()**](structecss_1_1IdSet_3_01Type_00_01false_01_4.md#function-empty) is true, [**size()**](structecss_1_1IdSet_3_01Type_00_01false_01_4.md#function-size) is zero, and the next [**take()**](structecss_1_1IdSet_3_01Type_00_01false_01_4.md#function-take-12) starts again at id zero. 





        

<hr>



### function contains 

```C++
inline FORCE_INLINE bool ecss::IdSet< Type, false >::contains (
    Type id
) const
```





**Returns:**

True exactly when `id` is currently allocated in this set. 





        

<hr>



### function empty 

```C++
inline bool ecss::IdSet< Type, false >::empty () const
```





**Returns:**

True exactly when [**size()**](structecss_1_1IdSet_3_01Type_00_01false_01_4.md#function-size) is zero. 





        

<hr>



### function erase [1/2]

_Release a run of ids. Ids that fall in the same word are cleared together, so a sorted list costs one operation per 64 ids rather than one per id. Correct for an unsorted list too_  _it simply flushes more often._
```C++
inline void ecss::IdSet< Type, false >::erase (
    const Type * begin,
    const Type * end
) 
```




<hr>



### function erase [2/2]

_Release_ `id` _. No-op if it is not allocated._
```C++
inline void ecss::IdSet< Type, false >::erase (
    Type id
) 
```




<hr>



### function getAll 

_Every allocated id, ascending._ 
```C++
inline std::vector< Type > ecss::IdSet< Type, false >::getAll () const
```




<hr>



### function insert 

_Mark_ `id` _allocated. No-op if it already is._
```C++
inline void ecss::IdSet< Type, false >::insert (
    Type id
) 
```




<hr>



### function reserve 

_Preallocate enough bitmap storage to represent_ `maxId` _._
```C++
inline void ecss::IdSet< Type, false >::reserve (
    Type maxId
) 
```





**Postcondition:**

Membership is unchanged; later access to ids through `maxId` needs no bitmap growth. 





        

<hr>



### function size 

```C++
inline size_t ecss::IdSet< Type, false >::size () const
```





**Returns:**

Number of allocated ids. Counted, not cached: keeping a counter would mean one more contended write per take/erase in the thread-safe flavour, and this is never on a hot path. 





        

<hr>



### function take [1/2]

_Allocate and return the lowest free id._ 
```C++
inline Type ecss::IdSet< Type, false >::take () 
```




<hr>



### function take [2/2]

_Allocate_ `count` _ids at once, appending them to_`out` _in ascending order._
```C++
inline void ecss::IdSet< Type, false >::take (
    size_t count,
    std::vector< Type > & out
) 
```



One walk of the bitmap instead of one per id: each word is loaded once and every free bit in it is taken before moving on, where a loop of [**take()**](structecss_1_1IdSet_3_01Type_00_01false_01_4.md#function-take-12) re-reads the hint and rescans from it every time. 


        

<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/IdSet.h`

