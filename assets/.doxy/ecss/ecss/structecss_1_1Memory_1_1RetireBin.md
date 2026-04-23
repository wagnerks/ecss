

# Struct ecss::Memory::RetireBin



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md) **>** [**RetireBin**](structecss_1_1Memory_1_1RetireBin.md)



_Deferred memory reclamation bin with grace period support._ [More...](#detailed-description)

* `#include <RetireAllocator.h>`























## Public Static Attributes

| Type | Name |
| ---: | :--- |
|  uint32\_t | [**DEFAULT\_GRACE\_PERIOD**](#variable-default_grace_period)   = `3`<br> |














## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**RetireBin**](#function-retirebin-14) () = default<br> |
|   | [**RetireBin**](#function-retirebin-24) (const [**RetireBin**](structecss_1_1Memory_1_1RetireBin.md) &) <br> |
|   | [**RetireBin**](#function-retirebin-34) ([**RetireBin**](structecss_1_1Memory_1_1RetireBin.md) && other) noexcept<br> |
|   | [**RetireBin**](#function-retirebin-44) (uint32\_t gracePeriod) <br> |
|  void | [**drainAll**](#function-drainall) () <br>_Immediately free all retired memory (use only at safe sync points)_  |
|  uint32\_t | [**getGracePeriod**](#function-getgraceperiod) () const<br> |
|  [**RetireBin**](structecss_1_1Memory_1_1RetireBin.md) & | [**operator=**](#function-operator) (const [**RetireBin**](structecss_1_1Memory_1_1RetireBin.md) &) <br> |
|  [**RetireBin**](structecss_1_1Memory_1_1RetireBin.md) & | [**operator=**](#function-operator_1) ([**RetireBin**](structecss_1_1Memory_1_1RetireBin.md) && other) noexcept<br> |
|  size\_t | [**pendingCount**](#function-pendingcount) () const<br>_Get number of blocks waiting to be freed._  |
|  void | [**retire**](#function-retire) (void \* p) <br>_Queue memory block for deferred freeing._  |
|  void | [**setGracePeriod**](#function-setgraceperiod) (uint32\_t ticks) <br> |
|  size\_t | [**tick**](#function-tick) () <br>_Process one tick of the grace period._  |
|   | [**~RetireBin**](#function-retirebin) () <br> |




























## Detailed Description


Memory blocks are not freed immediately when retired. Instead, they wait for a configurable number of [**tick()**](structecss_1_1Memory_1_1RetireBin.md#function-tick) calls (grace period) before being freed. This allows concurrent readers to safely finish using old memory even after a container reallocation.


Usage patterns:
* Call [**tick()**](structecss_1_1Memory_1_1RetireBin.md#function-tick) once per frame/update cycle to gradually free old memory
* Call [**drainAll()**](structecss_1_1Memory_1_1RetireBin.md#function-drainall) to immediately free everything (use only at safe points)




Default grace period is 3 ticks, which is safe for typical game loops where iterators don't survive across frames. 


    
## Public Static Attributes Documentation




### variable DEFAULT\_GRACE\_PERIOD 

```C++
uint32_t ecss::Memory::RetireBin::DEFAULT_GRACE_PERIOD;
```




<hr>
## Public Functions Documentation




### function RetireBin [1/4]

```C++
ecss::Memory::RetireBin::RetireBin () = default
```




<hr>



### function RetireBin [2/4]

```C++
inline ecss::Memory::RetireBin::RetireBin (
    const RetireBin &
) 
```




<hr>



### function RetireBin [3/4]

```C++
inline ecss::Memory::RetireBin::RetireBin (
    RetireBin && other
) noexcept
```




<hr>



### function RetireBin [4/4]

```C++
inline explicit ecss::Memory::RetireBin::RetireBin (
    uint32_t gracePeriod
) 
```




<hr>



### function drainAll 

_Immediately free all retired memory (use only at safe sync points)_ 
```C++
inline void ecss::Memory::RetireBin::drainAll () 
```




<hr>



### function getGracePeriod 

```C++
inline uint32_t ecss::Memory::RetireBin::getGracePeriod () const
```




<hr>



### function operator= 

```C++
inline RetireBin & ecss::Memory::RetireBin::operator= (
    const RetireBin &
) 
```




<hr>



### function operator= 

```C++
inline RetireBin & ecss::Memory::RetireBin::operator= (
    RetireBin && other
) noexcept
```




<hr>



### function pendingCount 

_Get number of blocks waiting to be freed._ 
```C++
inline size_t ecss::Memory::RetireBin::pendingCount () const
```




<hr>



### function retire 

_Queue memory block for deferred freeing._ 
```C++
inline void ecss::Memory::RetireBin::retire (
    void * p
) 
```




<hr>



### function setGracePeriod 

```C++
inline void ecss::Memory::RetireBin::setGracePeriod (
    uint32_t ticks
) 
```




<hr>



### function tick 

_Process one tick of the grace period._ 
```C++
inline size_t ecss::Memory::RetireBin::tick () 
```



Call this once per frame/update cycle. Memory blocks whose countdown reaches zero will be freed. This is safe to call from any thread.




**Returns:**

Number of blocks freed this tick 





        

<hr>



### function ~RetireBin 

```C++
inline ecss::Memory::RetireBin::~RetireBin () 
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/memory/RetireAllocator.h`

