

# Struct ecss::detail::AccessGuard::Entry



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**detail**](namespaceecss_1_1detail.md) **>** [**AccessGuard**](classecss_1_1detail_1_1AccessGuard.md) **>** [**Entry**](structecss_1_1detail_1_1AccessGuard_1_1Entry.md)



_One component type's claim: which lock, and whether for writing._ 

* `#include <Access.h>`





















## Public Attributes

| Type | Name |
| ---: | :--- |
|  bool | [**acquired**](#variable-acquired)   = `false`<br>_false when this thread already held it_  |
|  std::shared\_mutex \* | [**mutex**](#variable-mutex)   = `nullptr`<br> |
|  ECSType | [**type**](#variable-type)   = `0`<br> |
|  bool | [**writes**](#variable-writes)   = `false`<br> |












































## Public Attributes Documentation




### variable acquired 

_false when this thread already held it_ 
```C++
bool ecss::detail::AccessGuard::Entry::acquired;
```




<hr>



### variable mutex 

```C++
std::shared_mutex* ecss::detail::AccessGuard::Entry::mutex;
```




<hr>



### variable type 

```C++
ECSType ecss::detail::AccessGuard::Entry::type;
```




<hr>



### variable writes 

```C++
bool ecss::detail::AccessGuard::Entry::writes;
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/Access.h`

