

# Struct ecss::Threads::PinCounters::WriterIntent



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Threads**](namespaceecss_1_1Threads.md) **>** [**PinCounters**](structecss_1_1Threads_1_1PinCounters.md) **>** [**WriterIntent**](structecss_1_1Threads_1_1PinCounters_1_1WriterIntent.md)



_RAII announcement that a writer wants the array to go quiet._ 

* `#include <PinCounters.h>`





















## Public Attributes

| Type | Name |
| ---: | :--- |
|  const [**PinCounters**](structecss_1_1Threads_1_1PinCounters.md) & | [**pins**](#variable-pins)  <br> |
















## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**WriterIntent**](#function-writerintent-12) (const [**PinCounters**](structecss_1_1Threads_1_1PinCounters.md) & p) noexcept<br> |
|   | [**WriterIntent**](#function-writerintent-22) (const [**WriterIntent**](structecss_1_1Threads_1_1PinCounters_1_1WriterIntent.md) &) = delete<br> |
|   | [**~WriterIntent**](#function-writerintent) () <br> |




























## Public Attributes Documentation




### variable pins 

```C++
const PinCounters& ecss::Threads::PinCounters::WriterIntent::pins;
```




<hr>
## Public Functions Documentation




### function WriterIntent [1/2]

```C++
inline explicit ecss::Threads::PinCounters::WriterIntent::WriterIntent (
    const PinCounters & p
) noexcept
```



@thread\_safety Internally synchronized. Two relaxed increments over its lifetime. Announces that a writer is waiting so readers back off; never waits itself. 


        

<hr>



### function WriterIntent [2/2]

```C++
ecss::Threads::PinCounters::WriterIntent::WriterIntent (
    const WriterIntent &
) = delete
```




<hr>



### function ~WriterIntent 

```C++
inline ecss::Threads::PinCounters::WriterIntent::~WriterIntent () 
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/threads/PinCounters.h`

