

# Struct ecss::detail::IdSetLayout



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**detail**](namespaceecss_1_1detail.md) **>** [**IdSetLayout**](structecss_1_1detail_1_1IdSetLayout.md)



_Bit arithmetic shared by both_ [_**IdSet**_](structecss_1_1IdSet.md) _flavours._

* `#include <IdSet.h>`





Inherited by the following classes: [ecss::IdSet&lt; Type, false &gt;](structecss_1_1IdSet_3_01Type_00_01false_01_4.md),  [ecss::IdSet&lt; Type, true &gt;](structecss_1_1IdSet_3_01Type_00_01true_01_4.md)












## Public Types

| Type | Name |
| ---: | :--- |
| typedef uint64\_t | [**Word**](#typedef-word)  <br> |






## Public Static Attributes

| Type | Name |
| ---: | :--- |
|  Word | [**kFull**](#variable-kfull)   = `~Word{ 0 }`<br> |
|  size\_t | [**kWordBits**](#variable-kwordbits)   = `std::numeric\_limits&lt;Word&gt;::digits`<br> |
















## Public Static Functions

| Type | Name |
| ---: | :--- |
|  FORCE\_INLINE Word | [**bitOf**](#function-bitof) (size\_t id) <br> |
|  FORCE\_INLINE size\_t | [**wordOf**](#function-wordof) (size\_t id) <br> |


























## Public Types Documentation




### typedef Word 

```C++
using ecss::detail::IdSetLayout::Word =  uint64_t;
```




<hr>
## Public Static Attributes Documentation




### variable kFull 

```C++
Word ecss::detail::IdSetLayout::kFull;
```




<hr>



### variable kWordBits 

```C++
size_t ecss::detail::IdSetLayout::kWordBits;
```




<hr>
## Public Static Functions Documentation




### function bitOf 

```C++
static inline FORCE_INLINE Word ecss::detail::IdSetLayout::bitOf (
    size_t id
) 
```




<hr>



### function wordOf 

```C++
static inline FORCE_INLINE size_t ecss::detail::IdSetLayout::wordOf (
    size_t id
) 
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/IdSet.h`

