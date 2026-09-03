

# Class ecss::detail::AccessScope



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**detail**](namespaceecss_1_1detail.md) **>** [**AccessScope**](classecss_1_1detail_1_1AccessScope.md)



_RAII claim on one component type, for the tracker to see._ [More...](#detailed-description)

* `#include <AccessTracker.h>`





































## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**AccessScope**](#function-accessscope-14) () = default<br> |
|   | [**AccessScope**](#function-accessscope-24) ([**AccessScope**](classecss_1_1detail_1_1AccessScope.md) && other) noexcept<br> |
|   | [**AccessScope**](#function-accessscope-34) (const [**AccessScope**](classecss_1_1detail_1_1AccessScope.md) &) = delete<br> |
|   | [**AccessScope**](#function-accessscope-44) (ECSType type, const char \* name, bool forWriting) <br> |
|  [**AccessScope**](classecss_1_1detail_1_1AccessScope.md) & | [**operator=**](#function-operator) ([**AccessScope**](classecss_1_1detail_1_1AccessScope.md) && other) noexcept<br> |
|  [**AccessScope**](classecss_1_1detail_1_1AccessScope.md) & | [**operator=**](#function-operator_1) (const [**AccessScope**](classecss_1_1detail_1_1AccessScope.md) &) = delete<br> |
|   | [**~AccessScope**](#function-accessscope) () <br> |




























## Detailed Description


Movable and default-constructible so a view can keep an array of them, one per component type it names, without knowing the count until instantiation. A moved-from or default-constructed scope releases nothing. 


    
## Public Functions Documentation




### function AccessScope [1/4]

```C++
ecss::detail::AccessScope::AccessScope () = default
```




<hr>



### function AccessScope [2/4]

```C++
inline ecss::detail::AccessScope::AccessScope (
    AccessScope && other
) noexcept
```




<hr>



### function AccessScope [3/4]

```C++
ecss::detail::AccessScope::AccessScope (
    const AccessScope &
) = delete
```




<hr>



### function AccessScope [4/4]

```C++
inline ecss::detail::AccessScope::AccessScope (
    ECSType type,
    const char * name,
    bool forWriting
) 
```




<hr>



### function operator= 

```C++
inline AccessScope & ecss::detail::AccessScope::operator= (
    AccessScope && other
) noexcept
```




<hr>



### function operator= 

```C++
AccessScope & ecss::detail::AccessScope::operator= (
    const AccessScope &
) = delete
```




<hr>



### function ~AccessScope 

```C++
inline ecss::detail::AccessScope::~AccessScope () 
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/AccessTracker.h`

