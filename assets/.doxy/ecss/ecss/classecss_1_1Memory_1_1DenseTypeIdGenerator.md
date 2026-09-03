

# Class ecss::Memory::DenseTypeIdGenerator



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md) **>** [**DenseTypeIdGenerator**](classecss_1_1Memory_1_1DenseTypeIdGenerator.md)



_Dense sequential type ID generator for efficient array indexing._ [More...](#detailed-description)

* `#include <Reflection.h>`







































## Public Static Functions

| Type | Name |
| ---: | :--- |
|  ECSType | [**getCount**](#function-getcount) () noexcept<br> |
|  FORCE\_INLINE ECSType | [**getId**](#function-getid) () noexcept<br> |
|  FORCE\_INLINE ECSType | [**getTypeId**](#function-gettypeid) () noexcept<br> |


























## Detailed Description


Assigns sequential IDs (0, 1, 2, ...) to types on first use. Thread-safe initialization, then lock-free reads. IDs are global - same type gets same ID across all [**Registry**](classecss_1_1Registry.md) instances. 


    
## Public Static Functions Documentation




### function getCount 

```C++
static inline ECSType ecss::Memory::DenseTypeIdGenerator::getCount () noexcept
```



@thread\_safety Internally synchronized. One relaxed load. A moving target: another thread naming a new type raises it. 


        

<hr>



### function getId 

```C++
template<typename T>
static inline FORCE_INLINE ECSType ecss::Memory::DenseTypeIdGenerator::getId () noexcept
```



@thread\_safety Internally synchronized. A function-local static, so the first caller for a type assigns the id and every other thread waits for that initialization  the language guarantees it. Afterwards it is a plain read of a constant. 


        

<hr>



### function getTypeId 

```C++
template<typename T>
static inline FORCE_INLINE ECSType ecss::Memory::DenseTypeIdGenerator::getTypeId () noexcept
```



@thread\_safety Internally synchronized. Forwards to [**getId()**](classecss_1_1Memory_1_1DenseTypeIdGenerator.md#function-getid) after stripping cv and reference, so const T, T& and T share one id. 


        

<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/memory/Reflection.h`

