

# Struct ecss::Threads::PinCounters::WaiterScope



[**ClassList**](annotated.md) **>** [**WaiterScope**](structecss_1_1Threads_1_1PinCounters_1_1WaiterScope.md)



_RAII announce/retract of a blocked waiter; gates the notify syscalls._ 






















## Public Attributes

| Type | Name |
| ---: | :--- |
|  std::atomic&lt; uint32\_t &gt; & | [**waiters**](#variable-waiters)  <br> |
















## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**WaiterScope**](#function-waiterscope-12) (const WaiterScope &) = delete<br> |
|   | [**WaiterScope**](#function-waiterscope-22) (std::atomic&lt; uint32\_t &gt; & w) <br> |
|  WaiterScope & | [**operator=**](#function-operator) (const WaiterScope &) = delete<br> |
|   | [**~WaiterScope**](#function-waiterscope) () <br> |




























## Public Attributes Documentation




### variable waiters 

```C++
std::atomic<uint32_t>& ecss::Threads::PinCounters::WaiterScope::waiters;
```




<hr>
## Public Functions Documentation




### function WaiterScope [1/2]

```C++
WaiterScope::WaiterScope (
    const WaiterScope &
) = delete
```




<hr>



### function WaiterScope [2/2]

```C++
inline explicit WaiterScope::WaiterScope (
    std::atomic< uint32_t > & w
) 
```



@thread\_safety Internally synchronized. Two seq\_cst increments over its lifetime. Announces a blocked waiter _before_ it samples what it is about to wait on, which is what stops a concurrent release from losing the wake-up. 


        

<hr>



### function operator= 

```C++
WaiterScope & WaiterScope::operator= (
    const WaiterScope &
) = delete
```




<hr>



### function ~WaiterScope 

```C++
inline WaiterScope::~WaiterScope () 
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/threads/PinCounters.h`

