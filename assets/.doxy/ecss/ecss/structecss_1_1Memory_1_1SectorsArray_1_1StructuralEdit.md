

# Struct ecss::Memory::SectorsArray::StructuralEdit



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Memory**](namespaceecss_1_1Memory.md) **>** [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) **>** [**StructuralEdit**](structecss_1_1Memory_1_1SectorsArray_1_1StructuralEdit.md)



_RAII publication of "sector storage is changing" around a writer body._ [More...](#detailed-description)

* `#include <SectorsArray.h>`





















## Public Attributes

| Type | Name |
| ---: | :--- |
|  const [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) & | [**arr**](#variable-arr)  <br> |
















## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**StructuralEdit**](#function-structuraledit-12) (const [**SectorsArray**](classecss_1_1Memory_1_1SectorsArray.md) & owner) noexcept<br> |
|   | [**StructuralEdit**](#function-structuraledit-22) (const [**StructuralEdit**](structecss_1_1Memory_1_1SectorsArray_1_1StructuralEdit.md) &) = delete<br> |
|  [**StructuralEdit**](structecss_1_1Memory_1_1SectorsArray_1_1StructuralEdit.md) & | [**operator=**](#function-operator) (const [**StructuralEdit**](structecss_1_1Memory_1_1SectorsArray_1_1StructuralEdit.md) &) = delete<br> |
|   | [**~StructuralEdit**](#function-structuraledit) () <br> |




























## Detailed Description


Pins are taken with no lock at all, so a pin can be published just after a writer has checked that nothing is pinned. Both sides therefore publish before they check: the reader pins and then re-reads the epoch, the writer bumps the epoch and then re-reads the pin state. Whichever went second sees the other, so a validated pin can never be live across a structural change, and a writer never relocates or destroys a sector that a reader has already committed to. 


    
## Public Attributes Documentation




### variable arr 

```C++
const SectorsArray& ecss::Memory::SectorsArray< ThreadSafe, Allocator >::StructuralEdit::arr;
```




<hr>
## Public Functions Documentation




### function StructuralEdit [1/2]

```C++
inline explicit ecss::Memory::SectorsArray::StructuralEdit::StructuralEdit (
    const SectorsArray & owner
) noexcept
```




<hr>



### function StructuralEdit [2/2]

```C++
ecss::Memory::SectorsArray::StructuralEdit::StructuralEdit (
    const StructuralEdit &
) = delete
```




<hr>



### function operator= 

```C++
StructuralEdit & ecss::Memory::SectorsArray::StructuralEdit::operator= (
    const StructuralEdit &
) = delete
```




<hr>



### function ~StructuralEdit 

```C++
inline ecss::Memory::SectorsArray::StructuralEdit::~StructuralEdit () 
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/memory/SectorsArray.h`

