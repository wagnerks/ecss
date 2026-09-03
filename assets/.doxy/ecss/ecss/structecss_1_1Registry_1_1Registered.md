

# Struct ecss::Registry::Registered



[**ClassList**](annotated.md) **>** [**Registered**](structecss_1_1Registry_1_1Registered.md)



_Immutable snapshot of the registered arrays, published on registration._ `map` _is indexed by componentTypeId;_`list` _is the de-duplicated array list._






















## Public Attributes

| Type | Name |
| ---: | :--- |
|  const [**Memory::LayoutData**](structecss_1_1Memory_1_1LayoutData.md) \*\* | [**layout**](#variable-layout)   = `nullptr`<br> |
|  [**Memory::SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md)&lt; ThreadSafe, Allocator &gt; \*\* | [**list**](#variable-list)   = `nullptr`<br> |
|  size\_t | [**listCount**](#variable-listcount)   = `0`<br> |
|  [**Memory::SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md)&lt; ThreadSafe, Allocator &gt; \*\* | [**map**](#variable-map)   = `nullptr`<br> |
|  size\_t | [**mapCount**](#variable-mapcount)   = `0`<br> |
|  const [**Memory::SectorLayoutMeta**](structecss_1_1Memory_1_1SectorLayoutMeta.md) \*\* | [**meta**](#variable-meta)   = `nullptr`<br> |












































## Public Attributes Documentation




### variable layout 

```C++
const Memory::LayoutData** ecss::Registry< ThreadSafe, Allocator >::Registered::layout;
```



Per component type, the layout record and the layout it was resolved against. Kept in this snapshot rather than a cache beside it so one acquire load still answers everything, and so they cannot disagree about which registration they belong to. 


        

<hr>



### variable list 

```C++
Memory::SectorsArray<ThreadSafe, Allocator>** ecss::Registry< ThreadSafe, Allocator >::Registered::list;
```




<hr>



### variable listCount 

```C++
size_t ecss::Registry< ThreadSafe, Allocator >::Registered::listCount;
```




<hr>



### variable map 

```C++
Memory::SectorsArray<ThreadSafe, Allocator>** ecss::Registry< ThreadSafe, Allocator >::Registered::map;
```




<hr>



### variable mapCount 

```C++
size_t ecss::Registry< ThreadSafe, Allocator >::Registered::mapCount;
```




<hr>



### variable meta 

```C++
const Memory::SectorLayoutMeta** ecss::Registry< ThreadSafe, Allocator >::Registered::meta;
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/Registry.h`

