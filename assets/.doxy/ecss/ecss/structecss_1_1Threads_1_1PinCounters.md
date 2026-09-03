

# Struct ecss::Threads::PinCounters



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**Threads**](namespaceecss_1_1Threads.md) **>** [**PinCounters**](structecss_1_1Threads_1_1PinCounters.md)



_Per-sector pin tracking & synchronization for safe structural mutations._ [More...](#detailed-description)

* `#include <PinCounters.h>`















## Classes

| Type | Name |
| ---: | :--- |
| struct | [**WriterIntent**](structecss_1_1Threads_1_1PinCounters_1_1WriterIntent.md) <br>_RAII announcement that a writer wants the array to go quiet._  |






















## Public Functions

| Type | Name |
| ---: | :--- |
|   | [**PinCounters**](#function-pincounters-12) () = default<br> |
|   | [**PinCounters**](#function-pincounters-22) (const [**PinCounters**](structecss_1_1Threads_1_1PinCounters.md) &) = delete<br> |
|  uint32\_t | [**acquireHold**](#function-acquirehold) () noexcept const<br>_Take a structural hold: no sector may be relocated while one is outstanding._  |
|  FORCE\_INLINE bool | [**anyHold**](#function-anyhold) () noexcept const<br> |
|  FORCE\_INLINE bool | [**anyPin**](#function-anypin) () noexcept const<br>_True if any structural hold is outstanding. Sums the shards, so it is deliberately not something to spin on_  _waitUntilQuiescent samples it once per wait, not in a tight loop. @thread\_safety Internally synchronized. Sums the shards, so it is a handful of loads rather than one. Deliberately not something to spin on._ |
|  bool | [**canMoveSector**](#function-canmovesector) (SectorId sectorId) const<br>_Exact test: may sector_ `sectorId` _be destroyed / overwritten in place?_ |
|  FORCE\_INLINE bool | [**hasAnyPinnedSector**](#function-hasanypinnedsector) () noexcept const<br>_True if any sector on this array carries a pin. Says nothing about holds._  |
|  FORCE\_INLINE bool | [**hasAnyPins**](#function-hasanypins) () noexcept const<br>_True if any sector is currently pinned (exact)._  |
|  FORCE\_INLINE bool | [**isArrayLocked**](#function-isarraylocked) () const<br>_Alias of_ [_**hasAnyPins()**_](structecss_1_1Threads_1_1PinCounters.md#function-hasanypins) _: no sector may be relocated while this is true. @thread\_safety Internally synchronized. Alias of_[_**hasAnyPins()**_](structecss_1_1Threads_1_1PinCounters.md#function-hasanypins) _._ |
|  FORCE\_INLINE bool | [**isPinned**](#function-ispinned) (SectorId id) const<br>_Test whether a sector presently has a non-zero pin counter. @thread\_safety Internally synchronized. One acquire load; true at the moment of the call._  |
|  [**PinCounters**](structecss_1_1Threads_1_1PinCounters.md) & | [**operator=**](#function-operator) (const [**PinCounters**](structecss_1_1Threads_1_1PinCounters.md) &) = delete<br> |
|  void | [**pin**](#function-pin) (SectorId id) <br>_Increment the pin counter for sector id._  |
|  void | [**releaseHold**](#function-releasehold) (uint32\_t shard) noexcept const<br> |
|  void | [**reserve**](#function-reserve) (SectorId maxId) <br>_Pre-allocate counter blocks covering ids up to and including_ `maxId` _. @thread\_safety Internally synchronized. Touches the counter for maxId, which allocates the blocks up to it under the growth mutex. Doing it up front keeps that allocation off the first pin._ |
|  void | [**unpin**](#function-unpin) (SectorId id) <br>_Decrement the pin counter for sector id; wakes writers on the last unpin._  |
|  void | [**waitForWritersToPass**](#function-waitforwriterstopass) () noexcept const<br>_Block until no writer is waiting on this array._  |
|  void | [**waitUntilChangeable**](#function-waituntilchangeable) (SectorId sid) const<br>_Block until sector_ `sid` _carries no pins._ |
|  void | [**waitUntilQuiescent**](#function-waituntilquiescent) () const<br>_Block until no sector at all is pinned._  |
|  FORCE\_INLINE bool | [**writersWaiting**](#function-writerswaiting) () noexcept const<br>_True while a writer is waiting for pins to drain._  |
|   | [**~PinCounters**](#function-pincounters) () <br> |


## Public Static Functions

| Type | Name |
| ---: | :--- |
|  void | [**addOutstanding**](#function-addoutstanding) () noexcept<br>_@thread\_safety Internally synchronized. Thread-local._  |
|  void | [**dropOutstanding**](#function-dropoutstanding) () noexcept<br>_@thread\_safety Internally synchronized. Thread-local; clamped, see_ [_**threadOutstanding()**_](structecss_1_1Threads_1_1PinCounters.md#function-threadoutstanding) _._ |
|  void | [**reportStuckWait**](#function-reportstuckwait) (const char \* what) noexcept<br>_Report a wait that never ended, and stop._  |
|  bool | [**threadHoldsNothing**](#function-threadholdsnothing) () noexcept<br>_True when this thread holds no pin and no hold anywhere. @thread\_safety Internally synchronized. Thread-local._  |
|  int & | [**threadOutstanding**](#function-threadoutstanding) () noexcept<br>_How many pins and holds this thread has outstanding, on any array._  |
|  std::chrono::steady\_clock::time\_point | [**waitDeadline**](#function-waitdeadline) (bool & bounded) noexcept<br>_The deadline a wait starting now should honour._  |
|  bool | [**waitOrDeadline**](#function-waitordeadline) (const std::atomic&lt; T &gt; & word, T expected, std::chrono::steady\_clock::time\_point deadline, bool bounded) noexcept<br>_Wait for_ `word` _to stop reading_`expected` _, or give up at the deadline._ |
|  std::atomic&lt; uint32\_t &gt; & | [**waitTimeoutSeconds**](#function-waittimeoutseconds) () noexcept<br>_How long a structural wait may run before it is treated as a deadlock._  |


























## Detailed Description


Two predicates, both exact  neither can ever report "safe" while a pin is live:
* isPinned(id) / waitUntilChangeable(id): that one sector is not in use. Sufficient before destroying or overwriting that sector _in place_.
* [**hasAnyPins()**](structecss_1_1Threads_1_1PinCounters.md#function-hasanypins) / [**waitUntilQuiescent()**](structecss_1_1Threads_1_1PinCounters.md#function-waituntilquiescent): no sector is in use at all. Required before _relocating_ sectors (middle-insert shift, defragment, clear, copy, move), because relocation moves sectors the caller never named.




Why there is no longer a "highest pinned id": The previous design kept maxPinnedSector, recomputed from a hierarchical bit mask over pinned ids, and let a writer proceed when its target id was above it. That value could be under-reported: the bitmask clears a parent bit before re-checking the child word, so a concurrent highestSet() could miss a whole subtree and return a lower id  or -1  while sectors were still pinned, and the writer then relocated or destroyed pinned data. Both predicates below are single atomic loads over counters that were already maintained exactly, so the correct version is also the cheaper one: pin/unpin no longer touch the bitmask (whose ensurePath() took a global unique\_lock on every first pin) and no longer walk it to recompute a maximum.


Invariants:
* A sector is "pinned" while its counter &gt; 0.
* the pin shards sum to the number of distinct sectors whose counter &gt; 0.
* that sum is 0 &lt;=&gt; every per-sector counter is 0.




Locking: [**pin()**](structecss_1_1Threads_1_1PinCounters.md#function-pin)/unpin() take no locks at all. The counter block table grows under a mutex and is published as an immutable snapshot, so lookup is lock-free.


Ordering: pins are always taken while holding at least the owning array shared lock, and writers test these predicates under its unique lock, so the array mutex supplies the happens-before between "reader pinned" and "writer looked"; the atomics here only need to be individually coherent. [**unpin()**](structecss_1_1Threads_1_1PinCounters.md#function-unpin) runs without any lock, which is why the wait primitives use atomic wait/notify. @thread\_safety Internally synchronized. Every member is atomic and takes no lock; readers pin and hold without holding the array's mutex at all, which is why writers publish a structural epoch and re-check rather than trusting the lock.


The wait members are the exception and block by design:
* waitUntilChangeable(id) waits for one sector's pins.
* [**waitUntilQuiescent()**](structecss_1_1Threads_1_1PinCounters.md#function-waituntilquiescent) waits for every pin and every hold on the array. Neither can be satisfied by the thread that is itself holding what they wait for; debug builds assert on that, release builds hang. 

**See also:** SectorsArray 





    
## Public Functions Documentation




### function PinCounters [1/2]

```C++
ecss::Threads::PinCounters::PinCounters () = default
```




<hr>



### function PinCounters [2/2]

```C++
ecss::Threads::PinCounters::PinCounters (
    const PinCounters &
) = delete
```




<hr>



### function acquireHold 

_Take a structural hold: no sector may be relocated while one is outstanding._ 
```C++
inline uint32_t ecss::Threads::PinCounters::acquireHold () noexcept const
```



This is a different question from "is this sector busy". A view needs the array not to be compacted underneath it; it does not care which sector it names. It used to express that by pinning the back sector, so every view on every thread contended on one sector counter. Holds are keyed by thread instead, so they spread: measured 25.7M -&gt; 199M acquire/release pairs per second at four threads.




**Returns:**

the shard to hand back to [**releaseHold()**](structecss_1_1Threads_1_1PinCounters.md#function-releasehold). @thread\_safety Internally synchronized. One atomic increment on a per-thread shard, so holders on different threads do not share a cache line. Never waits. 





        

<hr>



### function anyHold 

```C++
inline FORCE_INLINE bool ecss::Threads::PinCounters::anyHold () noexcept const
```




<hr>



### function anyPin 

_True if any structural hold is outstanding. Sums the shards, so it is deliberately not something to spin on_  _waitUntilQuiescent samples it once per wait, not in a tight loop. @thread\_safety Internally synchronized. Sums the shards, so it is a handful of loads rather than one. Deliberately not something to spin on._
```C++
inline FORCE_INLINE bool ecss::Threads::PinCounters::anyPin () noexcept const
```



True if any sector on this array carries a pin.


Inexact by construction, in the safe direction: a pin taken while the loop is midway through can be missed. The structural epoch is what closes that  a writer publishes it before asking, and a pinner re-reads it after pinning and starts over if it moved. anyHold() is inexact the same way, for the same reason. @thread\_safety Internally synchronized. One load per shard. 


        

<hr>



### function canMoveSector 

_Exact test: may sector_ `sectorId` _be destroyed / overwritten in place?_
```C++
inline bool ecss::Threads::PinCounters::canMoveSector (
    SectorId sectorId
) const
```





**Note:**

Says nothing about _other_ sectors. Anything that relocates sectors must use [**hasAnyPins()**](structecss_1_1Threads_1_1PinCounters.md#function-hasanypins) / [**waitUntilQuiescent()**](structecss_1_1Threads_1_1PinCounters.md#function-waituntilquiescent) instead. @thread\_safety Internally synchronized. One seq\_cst load. Meaningful only after the caller has published its structural epoch; before that it is a stale sample. 





        

<hr>



### function hasAnyPinnedSector 

_True if any sector on this array carries a pin. Says nothing about holds._ 
```C++
inline FORCE_INLINE bool ecss::Threads::PinCounters::hasAnyPinnedSector () noexcept const
```



An empty sum across the pin shards is exactly "every per-sector counter is zero", so a writer that only changes named sectors in place can ask this one question instead of asking about each id it named. Not enough to relocate anything  that needs [**hasAnyPins()**](structecss_1_1Threads_1_1PinCounters.md#function-hasanypins), which counts holds too. @thread\_safety Internally synchronized. One seq\_cst load. 


        

<hr>



### function hasAnyPins 

_True if any sector is currently pinned (exact)._ 
```C++
inline FORCE_INLINE bool ecss::Threads::PinCounters::hasAnyPins () noexcept const
```



The relocation gate: true if anything at all forbids moving sectors. @thread\_safety Internally synchronized. One load plus anyHold(). This is the gate for relocating sectors, so it errs by over-reporting and never the other way. 


        

<hr>



### function isArrayLocked 

_Alias of_ [_**hasAnyPins()**_](structecss_1_1Threads_1_1PinCounters.md#function-hasanypins) _: no sector may be relocated while this is true. @thread\_safety Internally synchronized. Alias of_[_**hasAnyPins()**_](structecss_1_1Threads_1_1PinCounters.md#function-hasanypins) _._
```C++
inline FORCE_INLINE bool ecss::Threads::PinCounters::isArrayLocked () const
```




<hr>



### function isPinned 

_Test whether a sector presently has a non-zero pin counter. @thread\_safety Internally synchronized. One acquire load; true at the moment of the call._ 
```C++
inline FORCE_INLINE bool ecss::Threads::PinCounters::isPinned (
    SectorId id
) const
```




<hr>



### function operator= 

```C++
PinCounters & ecss::Threads::PinCounters::operator= (
    const PinCounters &
) = delete
```




<hr>



### function pin 

_Increment the pin counter for sector id._ 
```C++
inline void ecss::Threads::PinCounters::pin (
    SectorId id
) 
```





**Parameters:**


* `id` Sector id (!= INVALID\_ID). @thread\_safety Internally synchronized. Lock-free: atomic increments, no lock taken and no wait. May grow the counter table on a first touch of a high id, and that growth is itself synchronized. Publishes before it checks, so a writer that bumped its epoch first is always seen. 




        

<hr>



### function releaseHold 

```C++
inline void ecss::Threads::PinCounters::releaseHold (
    uint32_t shard
) noexcept const
```





**Parameters:**


* `shard` the value returned by [**acquireHold()**](structecss_1_1Threads_1_1PinCounters.md#function-acquirehold); a hold may be released by a different thread than took it, so the shard travels with the holder. @thread\_safety Internally synchronized. Atomic decrement plus a generation bump, and a wake only when a waiter has announced itself. May be called from a different thread than took the hold  the shard travels with the holder. 




        

<hr>



### function reserve 

_Pre-allocate counter blocks covering ids up to and including_ `maxId` _. @thread\_safety Internally synchronized. Touches the counter for maxId, which allocates the blocks up to it under the growth mutex. Doing it up front keeps that allocation off the first pin._
```C++
inline void ecss::Threads::PinCounters::reserve (
    SectorId maxId
) 
```




<hr>



### function unpin 

_Decrement the pin counter for sector id; wakes writers on the last unpin._ 
```C++
inline void ecss::Threads::PinCounters::unpin (
    SectorId id
) 
```





**Parameters:**


* `id` Sector id. @thread\_safety Internally synchronized. Lock-free: atomic decrements, plus a wake for any waiter that announced itself. Skips the wake syscall when provably nobody is blocked  the announce/sample handshake is seq\_cst on both sides so a waiter that committed to blocking cannot be missed. 




        

<hr>



### function waitForWritersToPass 

_Block until no writer is waiting on this array._ 
```C++
inline void ecss::Threads::PinCounters::waitForWritersToPass () noexcept const
```



The counterpart to the bounded yield: a reader that holds nothing can afford to wait properly, and that is what lets a writer ever reach quiescence when readers are opening views back to back. A thread that already holds a pin or a hold must NOT come here  the writer may be waiting for exactly that. @thread\_safety Internally synchronized; blocks until the writers pass. 


        

<hr>



### function waitUntilChangeable 

_Block until sector_ `sid` _carries no pins._
```C++
inline void ecss::Threads::PinCounters::waitUntilChangeable (
    SectorId sid
) const
```





**Warning:**

Covers only `sid`. Use [**waitUntilQuiescent()**](structecss_1_1Threads_1_1PinCounters.md#function-waituntilquiescent) before relocating sectors. @thread\_safety Internally synchronized; blocks on one sector. Returns at once when that sector carries no pins, which is the common case and costs one load. Waits otherwise  and cannot be satisfied by the thread that holds the pin itself. Debug builds assert on that; release builds hang. 





        

<hr>



### function waitUntilQuiescent 

_Block until no sector at all is pinned._ 
```C++
inline void ecss::Threads::PinCounters::waitUntilQuiescent () const
```





**Note:**

Required before any operation that moves sectors between linear indices. @thread\_safety Internally synchronized; blocks on the whole array. Waits for every pin and every hold, so a single open view anywhere holds it. From the thread that owns that view it never returns. Debug builds assert; release builds hang. 





        

<hr>



### function writersWaiting 

_True while a writer is waiting for pins to drain._ 
```C++
inline FORCE_INLINE bool ecss::Threads::PinCounters::writersWaiting () noexcept const
```



Readers consult this and yield a bounded number of times before pinning again. Without it a handful of threads that pin in a loop keep the array permanently non-quiescent and structural writes never run  deferred erases pile up and compaction never happens. The yield is bounded on purpose: a reader may already hold a pin the writer is waiting for, and blocking here would deadlock. @thread\_safety Internally synchronized. One relaxed load. A hint for readers to yield, not a gate: it is allowed to be stale in either direction. 


        

<hr>



### function ~PinCounters 

```C++
inline ecss::Threads::PinCounters::~PinCounters () 
```



@thread\_safety Caller must ensure exclusive access. Frees the counter tables and blocks outright. Nothing may be pinning or holding, and nothing may call get() afterwards. 


        

<hr>
## Public Static Functions Documentation




### function addOutstanding 

_@thread\_safety Internally synchronized. Thread-local._ 
```C++
static inline void ecss::Threads::PinCounters::addOutstanding () noexcept
```




<hr>



### function dropOutstanding 

_@thread\_safety Internally synchronized. Thread-local; clamped, see_ [_**threadOutstanding()**_](structecss_1_1Threads_1_1PinCounters.md#function-threadoutstanding) _._
```C++
static inline void ecss::Threads::PinCounters::dropOutstanding () noexcept
```




<hr>



### function reportStuckWait 

_Report a wait that never ended, and stop._ 
```C++
static inline void ecss::Threads::PinCounters::reportStuckWait (
    const char * what
) noexcept
```





**See also:** [**waitTimeoutSeconds**](structecss_1_1Threads_1_1PinCounters.md#function-waittimeoutseconds) 



        

<hr>



### function threadHoldsNothing 

_True when this thread holds no pin and no hold anywhere. @thread\_safety Internally synchronized. Thread-local._ 
```C++
static inline bool ecss::Threads::PinCounters::threadHoldsNothing () noexcept
```




<hr>



### function threadOutstanding 

_How many pins and holds this thread has outstanding, on any array._ 
```C++
static inline int & ecss::Threads::PinCounters::threadOutstanding () noexcept
```



Consulted for one question only  may I block here without waiting for myself  so it is built to err upward. A handle released by a different thread than took it leaves the taker's count high, which costs that thread a chance to block and never the other way round; the release side clamps at zero for the same reason. @thread\_safety Internally synchronized. Thread-local: no shared state at all. 


        

<hr>



### function waitDeadline 

_The deadline a wait starting now should honour._ 
```C++
static inline std::chrono::steady_clock::time_point ecss::Threads::PinCounters::waitDeadline (
    bool & bounded
) noexcept
```




<hr>



### function waitOrDeadline 

_Wait for_ `word` _to stop reading_`expected` _, or give up at the deadline._
```C++
template<class T>
static inline bool ecss::Threads::PinCounters::waitOrDeadline (
    const std::atomic< T > & word,
    T expected,
    std::chrono::steady_clock::time_point deadline,
    bool bounded
) noexcept
```





**Returns:**

false if the deadline passed with the value unchanged.


std::atomic::wait has no timed form, and the case worth catching is exactly the one where no notify is ever coming  so this spins, then yields, then polls. The spin covers the common handover, which is microseconds; the poll costs one wakeup per millisecond on a wait that was going to be long anyway. 


        

<hr>



### function waitTimeoutSeconds 

_How long a structural wait may run before it is treated as a deadlock._ 
```C++
static inline std::atomic< uint32_t > & ecss::Threads::PinCounters::waitTimeoutSeconds () noexcept
```



A writer waiting for quiescence cannot be satisfied by the thread that is itself holding a view on the array  it waits for a condition only it could clear. Debug builds assert on that; release builds used to simply stop, with no output and no stack worth reading, which is the least diagnosable failure a library can have.


So the wait is bounded. Nothing legitimate takes anywhere near this long: the longest honest wait is one frame's worth of open views. Set it to zero to wait forever, which restores the old behaviour. @thread\_safety Internally synchronized. One relaxed store; set it at startup. 


        

<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/threads/PinCounters.h`

