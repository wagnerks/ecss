

# Class ecss::detail::AccessGuard



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**detail**](namespaceecss_1_1detail.md) **>** [**AccessGuard**](classecss_1_1detail_1_1AccessGuard.md)



_Reader-writer lock per component type, held for the length of a system._ [More...](#detailed-description)

* `#include <Access.h>`















## Classes

| Type | Name |
| ---: | :--- |
| struct | [**Entry**](structecss_1_1detail_1_1AccessGuard_1_1Entry.md) <br>_One component type's claim: which lock, and whether for writing._  |






















## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**AccessGuard**](#function-accessguard-14) () = default<br> |
|   | [**AccessGuard**](#function-accessguard-24) ([**AccessGuard**](classecss_1_1detail_1_1AccessGuard.md) &&) = delete<br>_Moving is forbidden because a guard must be released on its acquiring thread._  |
|   | [**AccessGuard**](#function-accessguard-34) (const [**AccessGuard**](classecss_1_1detail_1_1AccessGuard.md) &) = delete<br>_A guard has unique ownership of its component-type claims._  |
|   | [**AccessGuard**](#function-accessguard-44) (std::array&lt; [**Entry**](structecss_1_1detail_1_1AccessGuard_1_1Entry.md), N &gt; claims) <br> |
|  [**AccessGuard**](classecss_1_1detail_1_1AccessGuard.md) & | [**operator=**](#function-operator) ([**AccessGuard**](classecss_1_1detail_1_1AccessGuard.md) &&) = delete<br>_Move assignment is forbidden for the same thread-affinity reason._  |
|  [**AccessGuard**](classecss_1_1detail_1_1AccessGuard.md) & | [**operator=**](#function-operator_1) (const [**AccessGuard**](classecss_1_1detail_1_1AccessGuard.md) &) = delete<br>_Copy assignment is forbidden; claims cannot have two owners._  |
|   | [**~AccessGuard**](#function-accessguard) () <br> |




























## Detailed Description


The container keeps an array's shape safe on its own, but not a component's value: guarding that per element would cost more than the lock-free read paths save. This puts the guarantee where systems actually work  a whole component type at a time  so it is paid once per system per frame rather than once per element. 29.5 ns for one type, 84.8 for three; fifty systems naming three apiece come to 4.2 us in a frame, which is 0.03% of one at sixty a second.


Claims are taken in one call on purpose. Acquiring them one at a time lets two systems take the same pair in opposite orders and stop, so the guard sorts them by type id and locks in that order; every caller therefore agrees on the sequence.


Re-entering from the same thread is allowed and does nothing, because std::shared\_mutex is not recursive and a second shared acquire deadlocks against a waiting writer. Asking to write a type this thread already holds for reading is refused rather than quietly upgraded: there is no atomic upgrade, and doing it in two steps is where the deadlock would come back. @thread\_safety Internally synchronized; blocks by design. Blocking is the point: it holds a reader-writer lock per component type until it dies. The guard object itself belongs to one thread and is neither movable nor copyable.


Name every type a system touches in one call. Taking claims one at a time is how two systems deadlock on the same pair in opposite orders; asked for together they are sorted by type id, so every caller agrees on the order. 


    
## Public Functions Documentation




### function AccessGuard [1/4]

```C++
ecss::detail::AccessGuard::AccessGuard () = default
```



@thread\_safety Thread-confined. The guard belongs to the thread that made it and cannot leave it: see the deleted move below. An empty one holds nothing and releases nothing. 


        

<hr>



### function AccessGuard [2/4]

_Moving is forbidden because a guard must be released on its acquiring thread._ 
```C++
ecss::detail::AccessGuard::AccessGuard (
    AccessGuard &&
) = delete
```




<hr>



### function AccessGuard [3/4]

_A guard has unique ownership of its component-type claims._ 
```C++
ecss::detail::AccessGuard::AccessGuard (
    const AccessGuard &
) = delete
```




<hr>



### function AccessGuard [4/4]

```C++
template<size_t N>
inline explicit ecss::detail::AccessGuard::AccessGuard (
    std::array< Entry , N > claims
) 
```





**Parameters:**


* `claims` one entry per component type, in any order. @thread\_safety Internally synchronized; blocks by design. Waits for each claimed type's reader-writer lock. Claims are sorted by type id before any is taken, so two systems naming the same pair cannot take them in opposite orders. 




        

<hr>



### function operator= 

_Move assignment is forbidden for the same thread-affinity reason._ 
```C++
AccessGuard & ecss::detail::AccessGuard::operator= (
    AccessGuard &&
) = delete
```




<hr>



### function operator= 

_Copy assignment is forbidden; claims cannot have two owners._ 
```C++
AccessGuard & ecss::detail::AccessGuard::operator= (
    const AccessGuard &
) = delete
```




<hr>



### function ~AccessGuard 

```C++
inline ecss::detail::AccessGuard::~AccessGuard () 
```



@thread\_safety Internally synchronized. Runs on the thread that took the claims  the guard is neither movable nor copyable, so it cannot be anywhere else. Releases every lock it actually took, in reverse order, so a nested guard never frees what an outer one still needs. 


        

<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/Access.h`

