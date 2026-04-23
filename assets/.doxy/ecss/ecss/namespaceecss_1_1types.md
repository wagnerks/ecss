

# Namespace ecss::types



[**Namespace List**](namespaces.md) **>** [**ecss**](namespaceecss.md) **>** [**types**](namespaceecss_1_1types.md)




















## Classes

| Type | Name |
| ---: | :--- |
| struct | [**EmptyBase**](structecss_1_1types_1_1EmptyBase.md) <br>_Empty base for offset calculation when sector has no header._  |
| struct | [**OffsetArray**](structecss_1_1types_1_1OffsetArray.md) &lt;typename Base, Types&gt;<br> |






## Public Attributes

| Type | Name |
| ---: | :--- |
|  bool | [**areUnique**](#variable-areunique)   = `true`<br> |
|  bool | [**areUnique&lt; T, Ts... &gt;**](#variable-areunique-t-ts)   = `(!std::is\_same\_v&lt;T, Ts&gt; && ...)`<br> |
|  size\_t | [**typeIndex**](#variable-typeindex)   = `0`<br> |
|  size\_t | [**typeIndex&lt; T, Ts... &gt;**](#variable-typeindex-t-ts)   = `getIndex&lt;T, Ts...&gt;()`<br> |
















## Public Functions

| Type | Name |
| ---: | :--- |
|  int | [**getIndex**](#function-getindex) () <br> |
|  int | [**getIndex**](#function-getindex) (int x=0) <br> |




























## Public Attributes Documentation




### variable areUnique 

```C++
bool ecss::types::areUnique;
```




<hr>



### variable areUnique&lt; T, Ts... &gt; 

```C++
bool ecss::types::areUnique< T, Ts... >;
```




<hr>



### variable typeIndex 

```C++
size_t ecss::types::typeIndex;
```




<hr>



### variable typeIndex&lt; T, Ts... &gt; 

```C++
size_t ecss::types::typeIndex< T, Ts... >;
```




<hr>
## Public Functions Documentation




### function getIndex 

```C++
template<typename T, typename... Ts>
int ecss::types::getIndex () 
```




<hr>



### function getIndex 

```C++
template<typename T>
int ecss::types::getIndex (
    int x=0
) 
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/Types.h`

