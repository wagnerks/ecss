

# Struct ecss::PinnedComponent

**template &lt;class T&gt;**



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**PinnedComponent**](structecss_1_1PinnedComponent.md)



_RAII wrapper that pins the sector holding component T and exposes a typed pointer._ [More...](#detailed-description)

* `#include <Registry.h>`





































## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**PinnedComponent**](#function-pinnedcomponent-14) () = default<br> |
|   | [**PinnedComponent**](#function-pinnedcomponent-24) (const [**PinnedComponent**](structecss_1_1PinnedComponent.md) & other) = delete<br> |
|   | [**PinnedComponent**](#function-pinnedcomponent-34) ([**Memory::PinnedSector**](structecss_1_1Memory_1_1PinnedSector.md) && pinnedSector, T \* ptr) <br>_Construct from a pinned sector and component pointer._  |
|   | [**PinnedComponent**](#function-pinnedcomponent-44) ([**PinnedComponent**](structecss_1_1PinnedComponent.md) && other) noexcept<br> |
|  T \* | [**get**](#function-get) () noexcept const<br> |
|   | [**operator bool**](#function-operator-bool) () noexcept const<br> |
|  T & | [**operator\***](#function-operator) () noexcept const<br> |
|  T \* | [**operator-&gt;**](#function-operator-) () noexcept const<br> |
|  [**PinnedComponent**](structecss_1_1PinnedComponent.md) & | [**operator=**](#function-operator_1) (const [**PinnedComponent**](structecss_1_1PinnedComponent.md) & other) = delete<br> |
|  [**PinnedComponent**](structecss_1_1PinnedComponent.md) & | [**operator=**](#function-operator_2) ([**PinnedComponent**](structecss_1_1PinnedComponent.md) && other) noexcept<br> |
|  void | [**release**](#function-release) () <br>_Release the pin early. After this call_ [_**get()**_](structecss_1_1PinnedComponent.md#function-get) _returns nullptr._ |
|   | [**~PinnedComponent**](#function-pinnedcomponent) () <br>_Destructor automatically releases the pin and nulls pointer._  |




























## Detailed Description




**Template parameters:**


* `T` Component type stored in the pinned sector.

Pin semantics (thread-safe build):
* Pin increments a pin counter preventing concurrent structural erase of the sector.
* Releasing (explicitly via [**release()**](structecss_1_1PinnedComponent.md#function-release) or implicitly in destructor) decrements the pin counter.






**Warning:**

Never store the raw pointer `get()` beyond the lifetime of this wrapper. 




**Note:**

In non-thread-safe builds pinning still exists conceptually but can be a no-op. 





    
## Public Functions Documentation




### function PinnedComponent [1/4]

```C++
ecss::PinnedComponent::PinnedComponent () = default
```




<hr>



### function PinnedComponent [2/4]

```C++
ecss::PinnedComponent::PinnedComponent (
    const PinnedComponent & other
) = delete
```




<hr>



### function PinnedComponent [3/4]

_Construct from a pinned sector and component pointer._ 
```C++
inline ecss::PinnedComponent::PinnedComponent (
    Memory::PinnedSector && pinnedSector,
    T * ptr
) 
```





**Parameters:**


* `pinnedSector` Sector pin handle (ownership transferred). 
* `ptr` Pointer to component T in that sector (may be nullptr). 




        

<hr>



### function PinnedComponent [4/4]

```C++
ecss::PinnedComponent::PinnedComponent (
    PinnedComponent && other
) noexcept
```




<hr>



### function get 

```C++
inline T * ecss::PinnedComponent::get () noexcept const
```





**Returns:**

The raw component pointer or nullptr if invalid. 





        

<hr>



### function operator bool 

```C++
inline explicit ecss::PinnedComponent::operator bool () noexcept const
```





**Returns:**

True if a valid component pointer is held. 





        

<hr>



### function operator\* 

```C++
inline T & ecss::PinnedComponent::operator* () noexcept const
```





**Returns:**

Dereferenced component reference (UB if ptr is null � guard with bool()). 





        

<hr>



### function operator-&gt; 

```C++
inline T * ecss::PinnedComponent::operator-> () noexcept const
```





**Returns:**

Operator access forwarding to underlying component pointer. 





        

<hr>



### function operator= 

```C++
PinnedComponent & ecss::PinnedComponent::operator= (
    const PinnedComponent & other
) = delete
```




<hr>



### function operator= 

```C++
PinnedComponent & ecss::PinnedComponent::operator= (
    PinnedComponent && other
) noexcept
```




<hr>



### function release 

_Release the pin early. After this call_ [_**get()**_](structecss_1_1PinnedComponent.md#function-get) _returns nullptr._
```C++
inline void ecss::PinnedComponent::release () 
```




<hr>



### function ~PinnedComponent 

_Destructor automatically releases the pin and nulls pointer._ 
```C++
inline ecss::PinnedComponent::~PinnedComponent () 
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/Registry.h`

