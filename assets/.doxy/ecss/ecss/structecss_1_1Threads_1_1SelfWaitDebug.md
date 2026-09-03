

# Struct ecss::Threads::SelfWaitDebug



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Threads**](namespaceecss_1_1Threads.md) **>** [**SelfWaitDebug**](structecss_1_1Threads_1_1SelfWaitDebug.md)



_Debug-only record of what the calling thread is holding, per array._ [More...](#detailed-description)

* `#include <PinCounters.h>`





















## Public Attributes

| Type | Name |
| ---: | :--- |
|  std::map&lt; const void \*, size\_t &gt; | [**holds**](#variable-holds)  <br>_array -&gt; holds taken here_  |
|  std::map&lt; std::pair&lt; const void \*, SectorId &gt;, size\_t &gt; | [**pins**](#variable-pins)  <br>_(array, sector) -&gt; pins_  |


















## Public Static Functions

| Type | Name |
| ---: | :--- |
|  void | [**addHold**](#function-addhold) (const void \* owner) <br> |
|  void | [**addPin**](#function-addpin) (const void \* owner, SectorId id) <br> |
|  [**SelfWaitDebug**](structecss_1_1Threads_1_1SelfWaitDebug.md) & | [**current**](#function-current) () <br> |
|  void | [**dropHold**](#function-drophold) (const void \* owner) <br> |
|  void | [**dropPin**](#function-droppin) (const void \* owner, SectorId id) <br> |
|  bool | [**holdsAnythingOn**](#function-holdsanythingon) (const void \* owner) <br> |
|  bool | [**holdsPinOn**](#function-holdspinon) (const void \* owner, SectorId id) <br> |


























## Detailed Description


Structural changes are not allowed while a view or pin on the same array is alive  relocating sectors would invalidate the iterator that is reading them. Writers enforce that by waiting for the pins and holds to drain, which is correct against other threads and unsatisfiable against yourself: the thread blocks on a condition only it could clear, and hangs forever with no diagnosis.


These maps let the wait primitives recognise that case and abort with the reason attached. They exist only in debug builds; release keeps the original code exactly, because maintaining them would tax the view hot path (hold acquire/release runs at ~199M pairs per second and is what makes iteration cheap). 


    
## Public Attributes Documentation




### variable holds 

_array -&gt; holds taken here_ 
```C++
std::map<const void*, size_t> ecss::Threads::SelfWaitDebug::holds;
```




<hr>



### variable pins 

_(array, sector) -&gt; pins_ 
```C++
std::map<std::pair<const void*, SectorId>, size_t> ecss::Threads::SelfWaitDebug::pins;
```




<hr>
## Public Static Functions Documentation




### function addHold 

```C++
static inline void ecss::Threads::SelfWaitDebug::addHold (
    const void * owner
) 
```



@thread\_safety Internally synchronized. Per-thread state: each thread sees only its own map, so there is nothing to share and nothing to lock. Debug builds only. 


        

<hr>



### function addPin 

```C++
static inline void ecss::Threads::SelfWaitDebug::addPin (
    const void * owner,
    SectorId id
) 
```



@thread\_safety Internally synchronized. Per-thread state: each thread sees only its own map, so there is nothing to share and nothing to lock. Debug builds only. 


        

<hr>



### function current 

```C++
static inline SelfWaitDebug & ecss::Threads::SelfWaitDebug::current () 
```




<hr>



### function dropHold 

```C++
static inline void ecss::Threads::SelfWaitDebug::dropHold (
    const void * owner
) 
```



@thread\_safety Internally synchronized. Per-thread state: each thread sees only its own map, so there is nothing to share and nothing to lock. Debug builds only. 


        

<hr>



### function dropPin 

```C++
static inline void ecss::Threads::SelfWaitDebug::dropPin (
    const void * owner,
    SectorId id
) 
```



@thread\_safety Internally synchronized. Per-thread state: each thread sees only its own map, so there is nothing to share and nothing to lock. Debug builds only. 


        

<hr>



### function holdsAnythingOn 

```C++
static inline bool ecss::Threads::SelfWaitDebug::holdsAnythingOn (
    const void * owner
) 
```





**Returns:**

true if this thread holds anything at all on `owner`. @thread\_safety Internally synchronized. Per-thread state: each thread sees only its own map, so there is nothing to share and nothing to lock. Debug builds only. 





        

<hr>



### function holdsPinOn 

```C++
static inline bool ecss::Threads::SelfWaitDebug::holdsPinOn (
    const void * owner,
    SectorId id
) 
```



@thread\_safety Internally synchronized. Per-thread state: each thread sees only its own map, so there is nothing to share and nothing to lock. Debug builds only. 


        

<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/threads/PinCounters.h`

