

# Struct ecss::Memory::RetireAllocator

**template &lt;class T&gt;**



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md) **>** [**RetireAllocator**](structecss_1_1Memory_1_1RetireAllocator.md)



_Allocator that defers memory reclamation to avoid use-after-free during container reallocation._ [More...](#detailed-description)

* `#include <RetireAllocator.h>`

















## Public Types

| Type | Name |
| ---: | :--- |
| typedef std::true\_type | [**is\_always\_equal**](#typedef-is_always_equal)  <br> |
| typedef std::false\_type | [**propagate\_on\_container\_move\_assignment**](#typedef-propagate_on_container_move_assignment)  <br> |
| typedef T | [**value\_type**](#typedef-value_type)  <br> |




## Public Attributes

| Type | Name |
| ---: | :--- |
|  RetireBin \* | [**bin**](#variable-bin)   = `nullptr`<br> |
















## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**RetireAllocator**](#function-retireallocator-23) (const [**RetireAllocator**](structecss_1_1Memory_1_1RetireAllocator.md)&lt; U &gt; & other) noexcept<br> |
|   | [**RetireAllocator**](#function-retireallocator-33) (RetireBin \* bin) noexcept<br> |
|  T \* | [**allocate**](#function-allocate) (size\_t n) <br> |
|  void | [**deallocate**](#function-deallocate) (T \* p, size\_t n) noexcept<br> |
|  bool | [**operator!=**](#function-operator) (const [**RetireAllocator**](structecss_1_1Memory_1_1RetireAllocator.md)&lt; U &gt; & rhs) noexcept const<br> |
|  bool | [**operator==**](#function-operator_1) (const [**RetireAllocator**](structecss_1_1Memory_1_1RetireAllocator.md)&lt; U &gt; & rhs) noexcept const<br> |




























## Detailed Description


Standard containers like std::vector will call deallocate() on the old buffer immediately after a reallocation. In concurrent scenarios, a reader may still access the old buffer, leading to crashes or undefined behavior. [**RetireAllocator**](structecss_1_1Memory_1_1RetireAllocator.md) solves this by not freeing memory right away: deallocate() places the old block into a RetireBin. The user is then responsible for calling RetireBin::drainAll() at a safe point, when no readers can reference retired buffers anymore.


Typical usage: construct a container with a [**RetireAllocator**](structecss_1_1Memory_1_1RetireAllocator.md) bound to a shared RetireBin. Push-backs that trigger reallocation will queue the old memory into the bin instead of freeing it. Later, at a known quiescent state, the program calls drainAll() to release all retired memory.


This approach prevents reallocation races from invalidating concurrent readers, at the cost of temporarily higher memory usage until retired blocks are drained. 


    
## Public Types Documentation




### typedef is\_always\_equal 

```C++
using ecss::Memory::RetireAllocator< T >::is_always_equal =  std::true_type;
```




<hr>



### typedef propagate\_on\_container\_move\_assignment 

```C++
using ecss::Memory::RetireAllocator< T >::propagate_on_container_move_assignment =  std::false_type;
```




<hr>



### typedef value\_type 

```C++
using ecss::Memory::RetireAllocator< T >::value_type =  T;
```




<hr>
## Public Attributes Documentation




### variable bin 

```C++
RetireBin* ecss::Memory::RetireAllocator< T >::bin;
```




<hr>
## Public Functions Documentation




### function RetireAllocator [2/3]

```C++
template<class U>
inline ecss::Memory::RetireAllocator::RetireAllocator (
    const RetireAllocator < U > & other
) noexcept
```




<hr>



### function RetireAllocator [3/3]

```C++
inline explicit ecss::Memory::RetireAllocator::RetireAllocator (
    RetireBin * bin
) noexcept
```




<hr>



### function allocate 

```C++
inline T * ecss::Memory::RetireAllocator::allocate (
    size_t n
) 
```




<hr>



### function deallocate 

```C++
inline void ecss::Memory::RetireAllocator::deallocate (
    T * p,
    size_t n
) noexcept
```




<hr>



### function operator!= 

```C++
template<class U>
inline bool ecss::Memory::RetireAllocator::operator!= (
    const RetireAllocator < U > & rhs
) noexcept const
```




<hr>



### function operator== 

```C++
template<class U>
inline bool ecss::Memory::RetireAllocator::operator== (
    const RetireAllocator < U > & rhs
) noexcept const
```




<hr>## Friends Documentation





### friend RetireAllocator [1/3]

```C++
template<class U>
struct ecss::Memory::RetireAllocator::RetireAllocator (
    RetireAllocator
) 
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/memory/RetireAllocator.h`

