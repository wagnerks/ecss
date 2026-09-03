

# Class ecss::detail::AccessTracker



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**detail**](namespaceecss_1_1detail.md) **>** [**AccessTracker**](classecss_1_1detail_1_1AccessTracker.md)



_Debug-only detector for two threads touching one component type at once._ [More...](#detailed-description)

* `#include <AccessTracker.h>`







































## Public Static Functions

| Type | Name |
| ---: | :--- |
|  void | [**beginRead**](#function-beginread) (ECSType type, const char \* name) <br> |
|  void | [**beginWrite**](#function-beginwrite) (ECSType type, const char \* name) <br> |
|  bool | [**enabled**](#function-enabled) () noexcept<br>_Whether conflicts are watched for. Off until a_ [_**Registry**_](classecss_1_1Registry.md) _turns it on. @thread\_safety Internally synchronized. One relaxed load, which is a plain move on every architecture that matters. Atomic rather than a bare bool because every tracked call reads it while_[_**setEnabled()**_](classecss_1_1detail_1_1AccessTracker.md#function-setenabled) _may be writing, and a torn read there would be a data race in the tool meant to find them._ |
|  void | [**endRead**](#function-endread) (ECSType type) <br> |
|  void | [**endWrite**](#function-endwrite) (ECSType type) <br> |
|  void | [**setEnabled**](#function-setenabled) (bool on) noexcept<br> |


























## Detailed Description


The container guarantees _structure_: an array will not be relocated under an iterator, and a pinned sector will not move or die. It does not guarantee that a component's _value_ is stable while another thread writes it  doing so would mean locking or pinning per element, which costs more than everything the lock-free read paths save.


That leaves a gap the compiler cannot see: a system reading Position while another writes it is a race, and worse, it is a scheduling bug  the frame's result depends on which thread won, so making the memory safe would not make the answer right. This finds it and names it, at the point it happens, instead of leaving it to a sanitizer run or to a bug report about numbers that flicker.


A view is a read scope for as long as it lives; a mutator is a write scope for the duration of its call. Re-entering from the same thread is fine and expected  a system routinely reads what it just wrote.


Compiled out entirely when NDEBUG is set: the whole point is to be free in the build that ships. 

**See also:** [**Registry::setAccessTracking**](classecss_1_1Registry.md#function-setaccesstracking) @thread\_safety Internally synchronized. Every entry point takes the per-type mutex, and the flag is read before anything else, so leaving it off costs a load. [**enabled()**](classecss_1_1detail_1_1AccessTracker.md#function-enabled) is the exception: set it once at startup, not while threads run. The whole class compiles to nothing when NDEBUG is set. 



    
## Public Static Functions Documentation




### function beginRead 

```C++
static inline void ecss::detail::AccessTracker::beginRead (
    ECSType type,
    const char * name
) 
```



@thread\_safety Internally synchronized. Takes the per-type mutex. Checks the enabled flag first, so leaving tracking off costs one load. Compiled out under NDEBUG. 


        

<hr>



### function beginWrite 

```C++
static inline void ecss::detail::AccessTracker::beginWrite (
    ECSType type,
    const char * name
) 
```



@thread\_safety Internally synchronized. Takes the per-type mutex. Checks the enabled flag first, so leaving tracking off costs one load. Compiled out under NDEBUG. 


        

<hr>



### function enabled 

_Whether conflicts are watched for. Off until a_ [_**Registry**_](classecss_1_1Registry.md) _turns it on. @thread\_safety Internally synchronized. One relaxed load, which is a plain move on every architecture that matters. Atomic rather than a bare bool because every tracked call reads it while_[_**setEnabled()**_](classecss_1_1detail_1_1AccessTracker.md#function-setenabled) _may be writing, and a torn read there would be a data race in the tool meant to find them._
```C++
static inline bool ecss::detail::AccessTracker::enabled () noexcept
```




<hr>



### function endRead 

```C++
static inline void ecss::detail::AccessTracker::endRead (
    ECSType type
) 
```



@thread\_safety Internally synchronized. Takes the per-type mutex. Checks the enabled flag first, so leaving tracking off costs one load. Compiled out under NDEBUG. 


        

<hr>



### function endWrite 

```C++
static inline void ecss::detail::AccessTracker::endWrite (
    ECSType type
) 
```



@thread\_safety Internally synchronized. Takes the per-type mutex. Checks the enabled flag first, so leaving tracking off costs one load. Compiled out under NDEBUG. 


        

<hr>



### function setEnabled 

```C++
static inline void ecss::detail::AccessTracker::setEnabled (
    bool on
) noexcept
```



@thread\_safety Internally synchronized. One relaxed store. Still meant for startup: turning tracking on once threads are running cannot show the overlaps that already happened. 


        

<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/AccessTracker.h`

