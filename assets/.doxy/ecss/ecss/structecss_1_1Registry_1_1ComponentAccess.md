

# Struct ecss::Registry::ComponentAccess



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Registry**](classecss_1_1Registry.md) **>** [**ComponentAccess**](structecss_1_1Registry_1_1ComponentAccess.md)



_The array holding T together with T's layout record, from one snapshot load._ 

* `#include <Registry.h>`





















## Public Attributes

| Type | Name |
| ---: | :--- |
|  [**Memory::SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md)&lt; ThreadSafe, Allocator &gt; \* | [**array**](#variable-array)   = `nullptr`<br> |
|  const [**Memory::LayoutData**](structecss_1_1Memory_1_1LayoutData.md) \* | [**layout**](#variable-layout)   = `nullptr`<br> |
















## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**operator bool**](#function-operator-bool) () noexcept const<br> |




























## Public Attributes Documentation




### variable array 

```C++
Memory::SectorsArray<ThreadSafe, Allocator>* ecss::Registry< ThreadSafe, Allocator >::ComponentAccess::array;
```




<hr>



### variable layout 

```C++
const Memory::LayoutData* ecss::Registry< ThreadSafe, Allocator >::ComponentAccess::layout;
```




<hr>
## Public Functions Documentation




### function operator bool 

```C++
inline explicit ecss::Registry::ComponentAccess::operator bool () noexcept const
```





**Returns:**

True when an array was resolved; false for an empty lookup result. 





        

<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/Registry.h`

