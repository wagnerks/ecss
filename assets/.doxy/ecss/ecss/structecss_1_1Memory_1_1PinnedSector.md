

# Struct ecss::Memory::PinnedSector



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md) **>** [**PinnedSector**](structecss_1_1Memory_1_1PinnedSector.md)



_RAII pin for a sector to prevent relocation / destruction while in use._ [More...](#detailed-description)

* `#include <SectorsArray.h>`





































## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**PinnedSector**](#function-pinnedsector-14) () = default<br> |
|   | [**PinnedSector**](#function-pinnedsector-24) (const [**PinnedSector**](structecss_1_1Memory_1_1PinnedSector.md) &) = delete<br> |
|   | [**PinnedSector**](#function-pinnedsector-34) (const [**Threads::PinCounters**](structecss_1_1Threads_1_1PinCounters.md) & o, [**Sector**](structecss_1_1Memory_1_1Sector.md) \* s, SectorId sid) <br>_Pin a sector._  |
|   | [**PinnedSector**](#function-pinnedsector-44) ([**PinnedSector**](structecss_1_1Memory_1_1PinnedSector.md) && other) noexcept<br> |
|  [**Sector**](structecss_1_1Memory_1_1Sector.md) \* | [**get**](#function-get) () const<br> |
|  SectorId | [**getId**](#function-getid) () const<br> |
|   | [**operator bool**](#function-operator-bool) () const<br> |
|  [**Sector**](structecss_1_1Memory_1_1Sector.md) \* | [**operator-&gt;**](#function-operator-) () const<br> |
|  [**PinnedSector**](structecss_1_1Memory_1_1PinnedSector.md) & | [**operator=**](#function-operator) (const [**PinnedSector**](structecss_1_1Memory_1_1PinnedSector.md) &) = delete<br> |
|  [**PinnedSector**](structecss_1_1Memory_1_1PinnedSector.md) & | [**operator=**](#function-operator_1) ([**PinnedSector**](structecss_1_1Memory_1_1PinnedSector.md) && other) noexcept<br> |
|  void | [**release**](#function-release) () <br>_Manually release pin (safe to call multiple times)._  |
|   | [**~PinnedSector**](#function-pinnedsector) () <br> |




























## Detailed Description


Life-cycle:
* On construction, increments pin counter for the sector id (if valid).
* On move, transfers pin ownership.
* On destruction or [**release()**](structecss_1_1Memory_1_1PinnedSector.md#function-release), decrements the pin counter.






**Warning:**

Do NOT keep a [**PinnedSector**](structecss_1_1Memory_1_1PinnedSector.md) beyond the lifetime of its originating [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md). 




**Note:**

A default-constructed (or moved-from) instance is considered "empty". 





    
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

_Pin a sector._ 
```C++
inline ecss::Memory::PinnedSector::PinnedSector (
    const Threads::PinCounters & o,
    Sector * s,
    SectorId sid
) 
```





**Parameters:**


* `o` Pin counters container (PinCounters instance). 
* `s` Raw sector pointer (may be nullptr). 
* `sid` [**Sector**](structecss_1_1Memory_1_1Sector.md) id (must not be INVALID\_ID if s != nullptr). 




        

<hr>



### function PinnedSector [4/4]

```C++
inline ecss::Memory::PinnedSector::PinnedSector (
    PinnedSector && other
) noexcept
```




<hr>



### function get 

```C++
inline Sector * ecss::Memory::PinnedSector::get () const
```





**Returns:**

Raw sector pointer or nullptr. 





        

<hr>



### function getId 

```C++
inline SectorId ecss::Memory::PinnedSector::getId () const
```





**Returns:**

Pinned sector id (INVALID\_ID when empty). 





        

<hr>



### function operator bool 

```C++
inline explicit ecss::Memory::PinnedSector::operator bool () const
```





**Returns:**

True if non-empty pin. 





        

<hr>



### function operator-&gt; 

```C++
inline Sector * ecss::Memory::PinnedSector::operator-> () const
```





**Returns:**

Raw sector pointer for member access. 





        

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

_Manually release pin (safe to call multiple times)._ 
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

