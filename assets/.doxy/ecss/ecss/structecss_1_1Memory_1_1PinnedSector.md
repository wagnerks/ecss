

# Struct ecss::Memory::PinnedSector



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md) **>** [**PinnedSector**](structecss_1_1Memory_1_1PinnedSector.md)



_RAII pin for a sector to prevent relocation / destruction while in use._ 

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

