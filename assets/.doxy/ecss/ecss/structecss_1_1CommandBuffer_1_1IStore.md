

# Struct ecss::CommandBuffer::IStore



[**ClassList**](annotated.md) **>** [**IStore**](structecss_1_1CommandBuffer_1_1IStore.md)



_Type-erased handle so the buffer can hold one bucket per component type._ 






































## Public Functions

| Type | Name |
| ---: | :--- |
| virtual void | [**apply**](#function-apply) ([**Reg**](classecss_1_1Registry.md) &) = 0<br> |
| virtual void | [**clear**](#function-clear) () = 0<br> |
| virtual bool | [**empty**](#function-empty) () const = 0<br> |
| virtual size\_t | [**size**](#function-size) () const = 0<br> |
| virtual  | [**~IStore**](#function-istore) () = default<br> |




























## Public Functions Documentation




### function apply 

```C++
virtual void IStore::apply (
    Reg &
) = 0
```




<hr>



### function clear 

```C++
virtual void IStore::clear () = 0
```




<hr>



### function empty 

```C++
virtual bool IStore::empty () const = 0
```




<hr>



### function size 

```C++
virtual size_t IStore::size () const = 0
```




<hr>



### function ~IStore 

```C++
virtual IStore::~IStore () = default
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/CommandBuffer.h`

