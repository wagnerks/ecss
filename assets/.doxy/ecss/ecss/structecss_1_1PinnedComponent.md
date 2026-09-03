

# Struct ecss::PinnedComponent

**template &lt;class T&gt;**



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**PinnedComponent**](structecss_1_1PinnedComponent.md)



_RAII wrapper that pins the sector holding component T and exposes a typed pointer._ [More...](#detailed-description)

* `#include <Registry.h>`





































## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**PinnedComponent**](#function-pinnedcomponent-14) () = default<br>_Construct an empty handle._  |
|   | [**PinnedComponent**](#function-pinnedcomponent-24) (const [**PinnedComponent**](structecss_1_1PinnedComponent.md) & other) = delete<br>_Copying is forbidden because a sector pin has one owner._  |
|   | [**PinnedComponent**](#function-pinnedcomponent-34) ([**Memory::PinnedSector**](structecss_1_1Memory_1_1PinnedSector.md) && pinnedSector, T \* ptr) <br>_Construct from a pinned sector and component pointer._  |
|   | [**PinnedComponent**](#function-pinnedcomponent-44) ([**PinnedComponent**](structecss_1_1PinnedComponent.md) && other) noexcept<br>_Transfer the component pointer and its sector pin._  |
|  T \* | [**get**](#function-get) () noexcept const<br> |
|   | [**operator bool**](#function-operator-bool) () noexcept const<br> |
|  T & | [**operator\***](#function-operator) () noexcept const<br> |
|  T \* | [**operator-&gt;**](#function-operator-) () noexcept const<br> |
|  [**PinnedComponent**](structecss_1_1PinnedComponent.md) & | [**operator=**](#function-operator_1) (const [**PinnedComponent**](structecss_1_1PinnedComponent.md) & other) = delete<br>_Copy assignment is forbidden because a sector pin has one owner._  |
|  [**PinnedComponent**](structecss_1_1PinnedComponent.md) & | [**operator=**](#function-operator_2) ([**PinnedComponent**](structecss_1_1PinnedComponent.md) && other) noexcept<br>_Release the current pin and take the pointer and pin from_ `other` _._ |
|  void | [**release**](#function-release) () <br>_Release the pin early. After this call_ [_**get()**_](structecss_1_1PinnedComponent.md#function-get) _returns nullptr. @thread\_safety Thread-confined. Releasing is what unblocks a thread waiting to destroy this entity, so calling it as soon as the pointer is finished with is worth doing._ |
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


@thread\_safety Thread-confined  the handle, not what it points at. One of these belongs to one thread: it is move-only, and its accessors (get, operator-&gt;, operator\*, operator bool, release) are plain reads of members with no synchronization of their own. Do not share one across threads; take a pin per thread instead.


What the handle buys is the opposite direction: while it lives, that sector will not be moved, destroyed or reused by anyone, so the pointer stays good. It also makes other threads wait  destroyComponent and destroyEntity for this entity block until it is released  so hold it for as short a time as the work allows, and never across a frame.


The pin protects the sector's _existence_, not its _value_: another thread may still be writing this component. 

**See also:** [**Registry::access()**](classecss_1_1Registry.md#function-access) 



    
## Public Functions Documentation




### function PinnedComponent [1/4]

_Construct an empty handle._ 
```C++
ecss::PinnedComponent::PinnedComponent () = default
```





**Postcondition:**

[**get()**](structecss_1_1PinnedComponent.md#function-get) == nullptr and operator bool() == false. 





        

<hr>



### function PinnedComponent [2/4]

_Copying is forbidden because a sector pin has one owner._ 
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

_Transfer the component pointer and its sector pin._ 
```C++
ecss::PinnedComponent::PinnedComponent (
    PinnedComponent && other
) noexcept
```





**Postcondition:**

The destination refers to the same component as `other` did before the move. 




**Note:**

The moved-from handle remains valid but its state is unspecified. 





        

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

Dereferenced component reference (UB if ptr is null; guard with bool()). 





        

<hr>



### function operator-&gt; 

```C++
inline T * ecss::PinnedComponent::operator-> () noexcept const
```





**Returns:**

Operator access forwarding to underlying component pointer. 





        

<hr>



### function operator= 

_Copy assignment is forbidden because a sector pin has one owner._ 
```C++
PinnedComponent & ecss::PinnedComponent::operator= (
    const PinnedComponent & other
) = delete
```




<hr>



### function operator= 

_Release the current pin and take the pointer and pin from_ `other` _._
```C++
PinnedComponent & ecss::PinnedComponent::operator= (
    PinnedComponent && other
) noexcept
```





**Postcondition:**

The destination refers to the same component as `other` did before the move. 




**Note:**

The moved-from handle remains valid but its state is unspecified. 





        

<hr>



### function release 

_Release the pin early. After this call_ [_**get()**_](structecss_1_1PinnedComponent.md#function-get) _returns nullptr. @thread\_safety Thread-confined. Releasing is what unblocks a thread waiting to destroy this entity, so calling it as soon as the pointer is finished with is worth doing._
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

