

# Struct ecss::Memory::PinnedSector



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md) **>** [**PinnedSector**](structecss_1_1Memory_1_1PinnedSector.md)



_RAII pin for a sector to prevent relocation / destruction while in use._ [More...](#detailed-description)

* `#include <SectorsArray.h>`





































## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**PinnedSector**](#function-pinnedsector-14) () = default<br> |
|   | [**PinnedSector**](#function-pinnedsector-24) (const [**PinnedSector**](structecss_1_1Memory_1_1PinnedSector.md) &) = delete<br> |
|   | [**PinnedSector**](#function-pinnedsector-34) (const [**Threads::PinCounters**](structecss_1_1Threads_1_1PinCounters.md) & o, SectorId sid, std::byte \* d, uint32\_t alive) <br> |
|   | [**PinnedSector**](#function-pinnedsector-44) ([**PinnedSector**](structecss_1_1Memory_1_1PinnedSector.md) && other) noexcept<br> |
|  std::byte \* | [**getData**](#function-getdata) () const<br> |
|  SectorId | [**getId**](#function-getid) () const<br> |
|  uint32\_t | [**getIsAlive**](#function-getisalive) () const<br> |
|   | [**operator bool**](#function-operator-bool) () const<br> |
|  [**PinnedSector**](structecss_1_1Memory_1_1PinnedSector.md) & | [**operator=**](#function-operator) (const [**PinnedSector**](structecss_1_1Memory_1_1PinnedSector.md) &) = delete<br> |
|  [**PinnedSector**](structecss_1_1Memory_1_1PinnedSector.md) & | [**operator=**](#function-operator_1) ([**PinnedSector**](structecss_1_1Memory_1_1PinnedSector.md) && other) noexcept<br> |
|  void | [**release**](#function-release) () <br> |
|   | [**~PinnedSector**](#function-pinnedsector) () <br> |




























## Detailed Description


@thread\_safety Thread-confined  the handle, not the array. One of these belongs to one thread; its accessors are plain member reads. Any number of threads may each hold their own pin, on the same sector or not.


What it buys: while it lives, that sector will not be moved, destroyed or reused, so the pointer stays good. What it costs others: a thread destroying or overwriting this sector waits until it is released. Never pin a sector and then destroy it from the same thread  that waits on yourself.


It says nothing about the component's _value_: another thread may be writing it. 

**See also:** [**Registry::access()**](classecss_1_1Registry.md#function-access) 



    
## Public Functions Documentation




### function PinnedSector [1/4]

```C++
ecss::Memory::PinnedSector::PinnedSector () = default
```




<hr>



### function PinnedSector [2/4]

```C++
ecss::Memory::PinnedSector::PinnedSector (
    const PinnedSector &
) = delete
```




<hr>



### function PinnedSector [3/4]

```C++
inline ecss::Memory::PinnedSector::PinnedSector (
    const Threads::PinCounters & o,
    SectorId sid,
    std::byte * d,
    uint32_t alive
) 
```




<hr>



### function PinnedSector [4/4]

```C++
inline ecss::Memory::PinnedSector::PinnedSector (
    PinnedSector && other
) noexcept
```




<hr>



### function getData 

```C++
inline std::byte * ecss::Memory::PinnedSector::getData () const
```




<hr>



### function getId 

```C++
inline SectorId ecss::Memory::PinnedSector::getId () const
```




<hr>



### function getIsAlive 

```C++
inline uint32_t ecss::Memory::PinnedSector::getIsAlive () const
```




<hr>



### function operator bool 

```C++
inline explicit ecss::Memory::PinnedSector::operator bool () const
```




<hr>



### function operator= 

```C++
PinnedSector & ecss::Memory::PinnedSector::operator= (
    const PinnedSector &
) = delete
```




<hr>



### function operator= 

```C++
inline PinnedSector & ecss::Memory::PinnedSector::operator= (
    PinnedSector && other
) noexcept
```




<hr>



### function release 

```C++
inline void ecss::Memory::PinnedSector::release () 
```




<hr>



### function ~PinnedSector 

```C++
inline ecss::Memory::PinnedSector::~PinnedSector () 
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/memory/SectorsArray.h`

