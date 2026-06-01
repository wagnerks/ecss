

# Struct ecss::Memory::LayoutData::FunctionTable



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md) **>** [**LayoutData**](structecss_1_1Memory_1_1LayoutData.md) **>** [**FunctionTable**](structecss_1_1Memory_1_1LayoutData_1_1FunctionTable.md)


























## Public Attributes

| Type | Name |
| ---: | :--- |
|  void(\* | [**copy**](#variable-copy)  <br> |
|  void(\* | [**destructor**](#variable-destructor)  <br> |
|  void(\* | [**move**](#variable-move)  <br> |












































## Public Attributes Documentation




### variable copy 

```C++
void(* ecss::Memory::LayoutData::FunctionTable::copy) (void *dest, const void *src);
```




<hr>



### variable destructor 

```C++
void(* ecss::Memory::LayoutData::FunctionTable::destructor) (void *src);
```




<hr>



### variable move 

```C++
void(* ecss::Memory::LayoutData::FunctionTable::move) (void *dest, void *src);
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/memory/SectorLayoutMeta.h`

