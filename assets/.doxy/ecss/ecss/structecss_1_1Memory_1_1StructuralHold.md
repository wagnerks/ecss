

# Struct ecss::Memory::StructuralHold



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md) **>** [**StructuralHold**](structecss_1_1Memory_1_1StructuralHold.md)



_RAII structural hold: while one is alive, no sector in the array may be relocated._ [More...](#detailed-description)

* `#include <SectorsArray.h>`





































## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**StructuralHold**](#function-structuralhold-14) () = default<br> |
|   | [**StructuralHold**](#function-structuralhold-24) (const [**StructuralHold**](structecss_1_1Memory_1_1StructuralHold.md) &) = delete<br> |
|   | [**StructuralHold**](#function-structuralhold-34) (const [**Threads::PinCounters**](structecss_1_1Threads_1_1PinCounters.md) & counters) <br> |
|   | [**StructuralHold**](#function-structuralhold-44) ([**StructuralHold**](structecss_1_1Memory_1_1StructuralHold.md) && other) noexcept<br> |
|   | [**operator bool**](#function-operator-bool) () const<br> |
|  [**StructuralHold**](structecss_1_1Memory_1_1StructuralHold.md) & | [**operator=**](#function-operator) (const [**StructuralHold**](structecss_1_1Memory_1_1StructuralHold.md) &) = delete<br> |
|  [**StructuralHold**](structecss_1_1Memory_1_1StructuralHold.md) & | [**operator=**](#function-operator_1) ([**StructuralHold**](structecss_1_1Memory_1_1StructuralHold.md) && other) noexcept<br> |
|  void | [**release**](#function-release) () <br> |
|   | [**~StructuralHold**](#function-structuralhold) () <br> |




























## Detailed Description


@thread\_safety Thread-confined  the handle, not the array. Holds are counted per thread, so holders on different threads do not even share a cache line; a hold may be released by a different thread than took it, which is why the shard travels inside the object.


While one is outstanding, everything that relocates sectors waits: clear, defragment, a middle insert, copy and move assignment. A view keeps one for its whole life, so those calls cannot finish while a view is open  from another thread they block, from this one they deadlock.


Weaker and cheaper than a pin. A pin says "leave this sector alone" and is counted per sector; a hold says "do not compact the array" and is counted per thread, so holders on different threads do not share a cache line. Iteration needs the second, not the first  which is why views used to pin the back sector and made every thread contend on it. 


    
## Public Functions Documentation




### function StructuralHold [1/4]

```C++
ecss::Memory::StructuralHold::StructuralHold () = default
```




<hr>



### function StructuralHold [2/4]

```C++
ecss::Memory::StructuralHold::StructuralHold (
    const StructuralHold &
) = delete
```




<hr>



### function StructuralHold [3/4]

```C++
inline explicit ecss::Memory::StructuralHold::StructuralHold (
    const Threads::PinCounters & counters
) 
```




<hr>



### function StructuralHold [4/4]

```C++
inline ecss::Memory::StructuralHold::StructuralHold (
    StructuralHold && other
) noexcept
```




<hr>



### function operator bool 

```C++
inline explicit ecss::Memory::StructuralHold::operator bool () const
```




<hr>



### function operator= 

```C++
StructuralHold & ecss::Memory::StructuralHold::operator= (
    const StructuralHold &
) = delete
```




<hr>



### function operator= 

```C++
inline StructuralHold & ecss::Memory::StructuralHold::operator= (
    StructuralHold && other
) noexcept
```




<hr>



### function release 

```C++
inline void ecss::Memory::StructuralHold::release () 
```




<hr>



### function ~StructuralHold 

```C++
inline ecss::Memory::StructuralHold::~StructuralHold () 
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/memory/SectorsArray.h`

