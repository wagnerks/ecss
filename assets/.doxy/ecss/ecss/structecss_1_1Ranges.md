

# Struct ecss::Ranges

**template &lt;typename Type&gt;**



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Ranges**](structecss_1_1Ranges.md)






















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
|   | [**Ranges**](#function-ranges-14) () = default<br> |
|   | [**Ranges**](#function-ranges-24) (const Range & range) <br> |
|   | [**Ranges**](#function-ranges-34) (const std::vector&lt; Range &gt; & range) <br> |
|   | [**Ranges**](#function-ranges-44) (const std::vector&lt; Type &gt; & sortedRanges) <br> |
|  FORCE\_INLINE const Range & | [**back**](#function-back) () const<br> |
|  FORCE\_INLINE void | [**clear**](#function-clear) () <br> |
|  FORCE\_INLINE bool | [**contains**](#function-contains) (Type value) const<br> |
|  FORCE\_INLINE bool | [**empty**](#function-empty) () const<br> |
|  void | [**erase**](#function-erase) (EntityId id) <br> |
|  FORCE\_INLINE const Range & | [**front**](#function-front) () const<br> |
|  std::vector&lt; Type &gt; | [**getAll**](#function-getall) () const<br> |
|  void | [**insert**](#function-insert) (EntityId id) <br> |
|  FORCE\_INLINE void | [**mergeIntersections**](#function-mergeintersections) () <br> |
|  FORCE\_INLINE void | [**pop\_back**](#function-pop_back) () <br> |
|  FORCE\_INLINE size\_t | [**size**](#function-size) () const<br> |
|  FORCE\_INLINE Type | [**take**](#function-take) () <br> |


## Public Static Functions

| Type | Name |
| ---: | :--- |
|  int | [**binarySearchInRanges**](#function-binarysearchinranges) (const std::vector&lt; Range &gt; & ranges, EntityId id) <br> |


























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

```C++
ecss::Ranges::Ranges () = default
```




<hr>



### function Ranges [2/4]

```C++
inline ecss::Ranges::Ranges (
    const Range & range
) 
```




<hr>



### function Ranges [3/4]

```C++
inline ecss::Ranges::Ranges (
    const std::vector< Range > & range
) 
```




<hr>



### function Ranges [4/4]

```C++
inline ecss::Ranges::Ranges (
    const std::vector< Type > & sortedRanges
) 
```




<hr>



### function back 

```C++
inline FORCE_INLINE const Range & ecss::Ranges::back () const
```




<hr>



### function clear 

```C++
inline FORCE_INLINE void ecss::Ranges::clear () 
```




<hr>



### function contains 

```C++
inline FORCE_INLINE bool ecss::Ranges::contains (
    Type value
) const
```




<hr>



### function empty 

```C++
inline FORCE_INLINE bool ecss::Ranges::empty () const
```




<hr>



### function erase 

```C++
inline void ecss::Ranges::erase (
    EntityId id
) 
```




<hr>



### function front 

```C++
inline FORCE_INLINE const Range & ecss::Ranges::front () const
```




<hr>



### function getAll 

```C++
inline std::vector< Type > ecss::Ranges::getAll () const
```




<hr>



### function insert 

```C++
inline void ecss::Ranges::insert (
    EntityId id
) 
```




<hr>



### function mergeIntersections 

```C++
inline FORCE_INLINE void ecss::Ranges::mergeIntersections () 
```




<hr>



### function pop\_back 

```C++
inline FORCE_INLINE void ecss::Ranges::pop_back () 
```




<hr>



### function size 

```C++
inline FORCE_INLINE size_t ecss::Ranges::size () const
```




<hr>



### function take 

```C++
inline FORCE_INLINE Type ecss::Ranges::take () 
```




<hr>
## Public Static Functions Documentation




### function binarySearchInRanges 

```C++
static inline int ecss::Ranges::binarySearchInRanges (
    const std::vector< Range > & ranges,
    EntityId id
) 
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/Ranges.h`

