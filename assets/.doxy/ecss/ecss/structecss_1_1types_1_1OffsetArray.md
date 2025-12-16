

# Struct ecss::types::OffsetArray

**template &lt;typename Base, typename... Types&gt;**



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**types**](namespaceecss_1_1types.md) **>** [**OffsetArray**](structecss_1_1types_1_1OffsetArray.md)




























## Public Static Attributes

| Type | Name |
| ---: | :--- |
|  size\_t | [**baseSize**](#variable-basesize)   = `std::is\_empty\_v&lt;Base&gt; ? 0 : align\_up&lt;sizeof(Base), alignof(Base)&gt;()`<br> |
|  size\_t | [**count**](#variable-count)   = `sizeof...(Types)`<br> |
|  uint32\_t | [**max\_align**](#variable-max_align)   = `/* multi line expression */`<br> |
|  std::array&lt; size\_t, count &gt; | [**offsets**](#variable-offsets)   = `make(std::make\_index\_sequence&lt;count&gt;{})`<br> |
|  size\_t | [**totalSize**](#variable-totalsize)   = `align\_up&lt;offsets.back() + sizeof(std::tuple\_element\_t&lt;count - 1, std::tuple&lt;Types...&gt;&gt;), max\_align&gt;()`<br> |
















## Public Static Functions

| Type | Name |
| ---: | :--- |
|  size\_t | [**align\_up**](#function-align_up) () noexcept<br> |
|  void | [**check\_one**](#function-check_one) () <br> |
|  size\_t | [**get**](#function-get) () <br> |
|  std::array&lt; size\_t, count &gt; | [**make**](#function-make) (std::index\_sequence&lt; Is... &gt;) <br> |
|  void | [**static\_checks**](#function-static_checks) () <br> |


























## Public Static Attributes Documentation




### variable baseSize 

```C++
size_t ecss::types::OffsetArray< Base, Types >::baseSize;
```




<hr>



### variable count 

```C++
size_t ecss::types::OffsetArray< Base, Types >::count;
```




<hr>



### variable max\_align 

```C++
uint32_t ecss::types::OffsetArray< Base, Types >::max_align;
```




<hr>



### variable offsets 

```C++
std::array<size_t, count> ecss::types::OffsetArray< Base, Types >::offsets;
```




<hr>



### variable totalSize 

```C++
size_t ecss::types::OffsetArray< Base, Types >::totalSize;
```




<hr>
## Public Static Functions Documentation




### function align\_up 

```C++
template<size_t N, size_t A>
static inline size_t ecss::types::OffsetArray::align_up () noexcept
```




<hr>



### function check\_one 

```C++
template<class T, std::size_t Off>
static inline void ecss::types::OffsetArray::check_one () 
```




<hr>



### function get 

```C++
template<size_t I>
static inline size_t ecss::types::OffsetArray::get () 
```




<hr>



### function make 

```C++
template<size_t... Is>
static inline std::array< size_t, count > ecss::types::OffsetArray::make (
    std::index_sequence< Is... >
) 
```




<hr>



### function static\_checks 

```C++
static inline void ecss::types::OffsetArray::static_checks () 
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/Types.h`

