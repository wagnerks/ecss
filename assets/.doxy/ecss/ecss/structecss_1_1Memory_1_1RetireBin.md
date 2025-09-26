

# Struct ecss::Memory::RetireBin



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md) **>** [**RetireBin**](structecss_1_1Memory_1_1RetireBin.md)










































## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**RetireBin**](#function-retirebin-13) () = default<br> |
|   | [**RetireBin**](#function-retirebin-23) (const RetireBin &) <br> |
|   | [**RetireBin**](#function-retirebin-33) (RetireBin && other) noexcept<br> |
|  void | [**drainAll**](#function-drainall) () <br> |
|  RetireBin & | [**operator=**](#function-operator) (const RetireBin &) <br> |
|  RetireBin & | [**operator=**](#function-operator_1) (RetireBin && other) noexcept<br> |
|  void | [**retire**](#function-retire) (void \* p) <br> |
|   | [**~RetireBin**](#function-retirebin) () <br> |




























## Public Functions Documentation




### function RetireBin [1/3]

```C++
ecss::Memory::RetireBin::RetireBin () = default
```




<hr>



### function RetireBin [2/3]

```C++
inline ecss::Memory::RetireBin::RetireBin (
    const RetireBin &
) 
```




<hr>



### function RetireBin [3/3]

```C++
inline ecss::Memory::RetireBin::RetireBin (
    RetireBin && other
) noexcept
```




<hr>



### function drainAll 

```C++
inline void ecss::Memory::RetireBin::drainAll () 
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



### function retire 

```C++
inline void ecss::Memory::RetireBin::retire (
    void * p
) 
```




<hr>



### function ~RetireBin 

```C++
inline ecss::Memory::RetireBin::~RetireBin () 
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/memory/RetireAllocator.h`

