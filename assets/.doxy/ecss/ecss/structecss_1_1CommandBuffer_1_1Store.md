

# Struct ecss::CommandBuffer::Store

**template &lt;typename T&gt;**



[**ClassList**](annotated.md) **>** [**Store**](structecss_1_1CommandBuffer_1_1Store.md)








Inherits the following classes: ecss::CommandBuffer< ThreadSafe, Allocator >::IStore


















## Public Attributes

| Type | Name |
| ---: | :--- |
|  std::vector&lt; uint64\_t &gt; | [**addSeq**](#variable-addseq)  <br> |
|  std::vector&lt; std::pair&lt; EntityId, T &gt; &gt; | [**adds**](#variable-adds)  <br> |
|  std::vector&lt; EntityId &gt; | [**removals**](#variable-removals)  <br> |
|  std::vector&lt; uint64\_t &gt; | [**removeSeq**](#variable-removeseq)  <br> |
















## Public Functions

| Type | Name |
| ---: | :--- |
|  void | [**apply**](#function-apply) ([**Reg**](classecss_1_1Registry.md) & registry) override<br> |
|  void | [**clear**](#function-clear) () override<br> |
|  bool | [**empty**](#function-empty) () override const<br> |
|  size\_t | [**size**](#function-size) () override const<br> |




























## Public Attributes Documentation




### variable addSeq 

```C++
std::vector<uint64_t> ecss::CommandBuffer< ThreadSafe, Allocator >::Store< T >::addSeq;
```




<hr>



### variable adds 

```C++
std::vector<std::pair<EntityId, T> > ecss::CommandBuffer< ThreadSafe, Allocator >::Store< T >::adds;
```



Adds are held in the shape insertBulk already takes, so the common case can hand them straight over. The sequence numbers sit alongside and are only consulted when the same type was both added and removed in one buffer. 


        

<hr>



### variable removals 

```C++
std::vector<EntityId> ecss::CommandBuffer< ThreadSafe, Allocator >::Store< T >::removals;
```




<hr>



### variable removeSeq 

```C++
std::vector<uint64_t> ecss::CommandBuffer< ThreadSafe, Allocator >::Store< T >::removeSeq;
```




<hr>
## Public Functions Documentation




### function apply 

```C++
inline void Store::apply (
    Reg & registry
) override
```




<hr>



### function clear 

```C++
inline void Store::clear () override
```




<hr>



### function empty 

```C++
inline bool Store::empty () override const
```




<hr>



### function size 

```C++
inline size_t Store::size () override const
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/CommandBuffer.h`

