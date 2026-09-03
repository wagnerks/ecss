

# Struct ecss::Ranges

**template &lt;typename Type&gt;**



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Ranges**](structecss_1_1Ranges.md)



[More...](#detailed-description)

* `#include <Ranges.h>`

















## Public Types

| Type | Name |
| ---: | :--- |
| typedef std::pair&lt; Type, Type &gt; | [**Range**](#typedef-range)  <br> |




## Public Attributes

| Type | Name |
| ---: | :--- |
|  std::vector&lt; Range &gt; | [**ranges**](#variable-ranges)  <br> |
















## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**Ranges**](#function-ranges-14) () = default<br>_Construct an empty set of allocated values._  |
|   | [**Ranges**](#function-ranges-24) (const Range & range) <br>_Construct from one half-open range. An empty or reversed range produces an empty object._  |
|   | [**Ranges**](#function-ranges-34) (const std::vector&lt; Range &gt; & range) <br>_Construct from sorted half-open ranges and normalize them._  |
|   | [**Ranges**](#function-ranges-44) (const std::vector&lt; Type &gt; & sortedRanges) <br>_Construct from individual allocated values and compress consecutive values._  |
|  FORCE\_INLINE const Range & | [**back**](#function-back) () const<br> |
|  FORCE\_INLINE void | [**clear**](#function-clear) () <br>_Remove all stored ranges._  |
|  FORCE\_INLINE bool | [**contains**](#function-contains) (Type value) const<br> |
|  FORCE\_INLINE bool | [**empty**](#function-empty) () const<br> |
|  void | [**erase**](#function-erase) (Type id) <br>_Release_ `id` _while preserving normalized range order._ |
|  FORCE\_INLINE const Range & | [**front**](#function-front) () const<br> |
|  std::vector&lt; Type &gt; | [**getAll**](#function-getall) () const<br> |
|  void | [**insert**](#function-insert) (Type id) <br>_Mark_ `id` _allocated while preserving normalized range order._ |
|  FORCE\_INLINE void | [**mergeIntersections**](#function-mergeintersections) () <br>_Normalize the list in place: drop the degenerate, coalesce the rest._  |
|  FORCE\_INLINE bool | [**nextStartAfter**](#function-nextstartafter) (Type id, Type & out) const<br>_Start of the first range with first &gt;_ `id` _, if any._ |
|  FORCE\_INLINE void | [**pop\_back**](#function-pop_back) () <br>_Remove the last normalized range._  |
|  FORCE\_INLINE size\_t | [**size**](#function-size) () const<br> |
|  FORCE\_INLINE Type | [**take**](#function-take) () <br>_Allocate and return the lowest value not already represented by the ranges._  |
|  std::pair&lt; Type, Type &gt; | [**takeBlock**](#function-takeblock) (Type maxCount) <br>_Allocate up to_ `maxCount` _contiguous values in one step._ |


## Public Static Functions

| Type | Name |
| ---: | :--- |
|  int | [**binarySearchInRanges**](#function-binarysearchinranges) (const std::vector&lt; Range &gt; & ranges, Type id) <br>_Find the normalized half-open range containing_ `id` _._ |


























## Detailed Description


@thread\_safety Thread-confined  and that applies to every member below, without exception. This is a plain sorted container of id ranges with no synchronization anywhere in it: [**take()**](structecss_1_1Ranges.md#function-take), [**takeBlock()**](structecss_1_1Ranges.md#function-takeblock), [**insert()**](structecss_1_1Ranges.md#function-insert) and [**erase()**](structecss_1_1Ranges.md#function-erase) mutate, the rest read, and none of them is safe against a concurrent mutation. Build one, then hand it to view(); do not touch it again while a view built from it is still walking. 


    
## Public Types Documentation




### typedef Range 

```C++
using ecss::Ranges< Type >::Range =  std::pair<Type, Type>;
```




<hr>
## Public Attributes Documentation




### variable ranges 

```C++
std::vector<Range> ecss::Ranges< Type >::ranges;
```




<hr>
## Public Functions Documentation




### function Ranges [1/4]

_Construct an empty set of allocated values._ 
```C++
ecss::Ranges::Ranges () = default
```




<hr>



### function Ranges [2/4]

_Construct from one half-open range. An empty or reversed range produces an empty object._ 
```C++
inline ecss::Ranges::Ranges (
    const Range & range
) 
```




<hr>



### function Ranges [3/4]

_Construct from sorted half-open ranges and normalize them._ 
```C++
inline ecss::Ranges::Ranges (
    const std::vector< Range > & range
) 
```





**Parameters:**


* `range` [**Ranges**](structecss_1_1Ranges.md) whose valid (`first < second`) subsequence is ordered by `first`. Empty and reversed ranges are ignored. Overlapping and touching ranges are coalesced. 




        

<hr>



### function Ranges [4/4]

_Construct from individual allocated values and compress consecutive values._ 
```C++
inline ecss::Ranges::Ranges (
    const std::vector< Type > & sortedRanges
) 
```





**Parameters:**


* `sortedRanges` Values in nondecreasing order. Duplicates are ignored. 



**Precondition:**

Every value is nonnegative, less than `std::numeric_limits<Type>::max()`, and the sequence is sorted. The maximum value is reserved because ranges use an exclusive end. 





        

<hr>



### function back 

```C++
inline FORCE_INLINE const Range & ecss::Ranges::back () const
```





**Returns:**

Last normalized range. 




**Precondition:**

[**empty()**](structecss_1_1Ranges.md#function-empty) is false. 





        

<hr>



### function clear 

_Remove all stored ranges._ 
```C++
inline FORCE_INLINE void ecss::Ranges::clear () 
```





**Postcondition:**

[**empty()**](structecss_1_1Ranges.md#function-empty) is true. 





        

<hr>



### function contains 

```C++
inline FORCE_INLINE bool ecss::Ranges::contains (
    Type value
) const
```





**Returns:**

True when `value` belongs to one of the stored half-open ranges. 





        

<hr>



### function empty 

```C++
inline FORCE_INLINE bool ecss::Ranges::empty () const
```





**Returns:**

True exactly when no ranges are stored. 





        

<hr>



### function erase 

_Release_ `id` _while preserving normalized range order._
```C++
inline void ecss::Ranges::erase (
    Type id
) 
```





**Postcondition:**

contains(id) is false; erasing a value not represented is a no-op. 





        

<hr>



### function front 

```C++
inline FORCE_INLINE const Range & ecss::Ranges::front () const
```





**Returns:**

First normalized range. 




**Precondition:**

[**empty()**](structecss_1_1Ranges.md#function-empty) is false. 





        

<hr>



### function getAll 

```C++
inline std::vector< Type > ecss::Ranges::getAll () const
```





**Returns:**

Every represented value in ascending order, with no duplicates. 





        

<hr>



### function insert 

_Mark_ `id` _allocated while preserving normalized range order._
```C++
inline void ecss::Ranges::insert (
    Type id
) 
```





**Postcondition:**

contains(id) is true; inserting an already represented id is a no-op. 





        

<hr>



### function mergeIntersections 

_Normalize the list in place: drop the degenerate, coalesce the rest._ 
```C++
inline FORCE_INLINE void ecss::Ranges::mergeIntersections () 
```



Empty and reversed ranges are removed first, and that has to come first: the fold below compares one range's end against the next one's start, and a reversed pair would corrupt that comparison rather than simply being skipped by it. 


        

<hr>



### function nextStartAfter 

_Start of the first range with first &gt;_ `id` _, if any._
```C++
inline FORCE_INLINE bool ecss::Ranges::nextStartAfter (
    Type id,
    Type & out
) const
```




<hr>



### function pop\_back 

_Remove the last normalized range._ 
```C++
inline FORCE_INLINE void ecss::Ranges::pop_back () 
```





**Precondition:**

[**empty()**](structecss_1_1Ranges.md#function-empty) is false. 





        

<hr>



### function size 

```C++
inline FORCE_INLINE size_t ecss::Ranges::size () const
```





**Returns:**

Number of normalized ranges, not the number of individual values. 





        

<hr>



### function take 

_Allocate and return the lowest value not already represented by the ranges._ 
```C++
inline FORCE_INLINE Type ecss::Ranges::take () 
```





**Postcondition:**

The returned value is contained by this object. 




**Exception:**


* `std::overflow_error` No value below `std::numeric_limits<Type>::max()` is free. 




        

<hr>



### function takeBlock 

_Allocate up to_ `maxCount` _contiguous values in one step._
```C++
inline std::pair< Type, Type > ecss::Ranges::takeBlock (
    Type maxCount
) 
```





**Returns:**

`{first, count}`: the first allocated value and how many were actually taken. `count` may be less than `maxCount` when the chosen free gap is smaller. Returns `{0, 0}` without changing the object when `maxCount` is zero or no representable value remains. `std::numeric_limits<Type>::max()` is reserved as the exclusive endpoint and is never allocated.


Batching exists so a caller can amortise whatever lock guards this container over many allocations instead of paying it per value. 


        

<hr>
## Public Static Functions Documentation




### function binarySearchInRanges 

_Find the normalized half-open range containing_ `id` _._
```C++
static inline int ecss::Ranges::binarySearchInRanges (
    const std::vector< Range > & ranges,
    Type id
) 
```





**Returns:**

Its zero-based index, or -1 when no range contains `id`. 




**Precondition:**

`ranges` is ordered by start and contains no overlaps. 





        

<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/Ranges.h`

