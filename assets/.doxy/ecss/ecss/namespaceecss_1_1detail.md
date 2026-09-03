

# Namespace ecss::detail



[**Namespace List**](namespaces.md) **>** [**ecss**](namespaceecss.md) **>** [**detail**](namespaceecss_1_1detail.md)



_Iterable view over entities with one main component and optional additional components._ [More...](#detailed-description)
















## Classes

| Type | Name |
| ---: | :--- |
| class | [**AccessGuard**](classecss_1_1detail_1_1AccessGuard.md) <br>_Reader-writer lock per component type, held for the length of a system._  |
| class | [**AccessScope**](classecss_1_1detail_1_1AccessScope.md) <br>_RAII claim on one component type, for the tracker to see._  |
| class | [**AccessTracker**](classecss_1_1detail_1_1AccessTracker.md) <br>_Debug-only detector for two threads touching one component type at once._  |
| struct | [**IdSetLayout**](structecss_1_1detail_1_1IdSetLayout.md) <br>_Bit arithmetic shared by both_ [_**IdSet**_](structecss_1_1IdSet.md) _flavours._ |
| struct | [**NonTrivialComponent**](structecss_1_1detail_1_1NonTrivialComponent.md) &lt;class T&gt;<br>_Instantiated for nothing but its own deprecation warning._  |






















## Public Functions

| Type | Name |
| ---: | :--- |
|  const char \* | [**accessTypeName**](#function-accesstypename) () noexcept<br>_Name a component type for a tracker message. Debug only; empty otherwise._  |
|  void | [**checkTriviality**](#function-checktriviality) () noexcept<br>_Say once, per component type, that this one costs its array the fast paths._  |
|  FORCE\_INLINE void | [**forEachAliveSlot**](#function-foreachaliveslot) (void \*const \* chunks, size\_t numChunks, size\_t size, const uint32\_t \* isAliveData, uint32\_t aliveMask, size\_t stride, size\_t chunkCapacity, Callback && callback) <br>_Walk the alive slots of one array, handing each slot's base address to a callback._  |
|  [**AccessScope**](classecss_1_1detail_1_1AccessScope.md) | [**readScope**](#function-readscope) (ECSType type) <br>_Claim_ `type` _for reading until the returned scope dies._ |
|  TrivialityReporter & | [**trivialityReporter**](#function-trivialityreporter) () noexcept<br>_The installed reporter. Prints one line to stderr until someone replaces it._  |
|  std::string\_view | [**typeName**](#function-typename) () noexcept<br>_The component's name, carved out of the compiler's own signature for this function. No RTTI, and unlike typeid(T).name() it is readable on GCC too._  |
|  [**AccessScope**](classecss_1_1detail_1_1AccessScope.md) | [**writeScope**](#function-writescope) (ECSType type) <br>_Claim_ `type` _for writing until the returned scope dies._ |




























## Detailed Description




**Template parameters:**


* `ThreadSafe` Mirrors [**Registry**](classecss_1_1Registry.md) thread-safe flag (affects pinning). 
* `Allocator` Allocator used by sectors. 
* `Ranged` Whether this view limits iteration to provided ranges. 
* `T` Main component type (drives iteration order). 
* `CompTypes` Additional component types optionally retrieved per entity.

Semantics:
* Iterates only sectors where main component T is alive.
* For each entity id, returns pointers (T\*, optional others may be nullptr if absent).
* In ranged mode, skips entities outside the filter by jumping to the next range start.




Thread safety:
* ThreadSafe=true: Back sector pinning ensures iteration upper bound stability.
* Non-main components may be null if not present or not grouped in same array.






**Warning:**

Do not cache raw pointers across mutating frames unless externally synchronized. 





    
## Public Functions Documentation




### function accessTypeName 

_Name a component type for a tracker message. Debug only; empty otherwise._ 
```C++
template<typename T>
inline const char * ecss::detail::accessTypeName () noexcept
```




<hr>



### function checkTriviality 

_Say once, per component type, that this one costs its array the fast paths._ 
```C++
template<class T>
void ecss::detail::checkTriviality () noexcept
```



Two channels, because neither alone reaches everyone. The compile-time warning is the better one  it names the type without running anything  but it is silenced for any consumer that pulls ecss in as a _system_ header, which every CMake project using target\_precompile\_headers does: the generated PCH opens with `#pragma system_header`, and a warning whose location is inside it never reaches the build log. So the same condition also reports once at runtime, when the array's layout is built. That costs one branch per array creation and nothing on any hot path.


Define ECSS\_NO\_TRIVIALITY\_WARNINGS to remove both; setTrivialityReporter(nullptr) removes only the runtime half.




**Note:**

MSVC reports C4996 at /W3 and above; /W1 (bare `cl` with no flags) hides it, CMake and MSBuild projects default to /W3. GCC and clang report it at any level. 





        

<hr>



### function forEachAliveSlot 

_Walk the alive slots of one array, handing each slot's base address to a callback._ 
```C++
template<bool ThreadSafe, class Callback>
FORCE_INLINE void ecss::detail::forEachAliveSlot (
    void *const * chunks,
    size_t numChunks,
    size_t size,
    const uint32_t * isAliveData,
    uint32_t aliveMask,
    size_t stride,
    size_t chunkCapacity,
    Callback && callback
) 
```



Outside [**ArraysView**](classecss_1_1ArraysView.md) on purpose. The body says nothing about component types  they reach it as runtime offsets inside ctx  yet as a static member it was instantiated once per view, so a translation unit opening 48 different views compiled 48 copies of the same loop. Parameterised on ThreadSafe alone, there are at most two.


Walk the live slots of a chunked array, calling `callback` with each.


The callback is a template parameter rather than a function pointer, and that is the whole point: with a pointer the compiler cannot see through the call, so it inlined nothing, unrolled nothing and vectorized nothing, and every element paid an indirect call. Measured over a million single-component sectors, the pointer form ran at 1.86 ms against 0.82 for this one  same elements visited, same checksum. @thread\_safety Internally synchronized. Reads a snapshot the caller already holds. 


        

<hr>



### function readScope 

_Claim_ `type` _for reading until the returned scope dies._
```C++
template<typename T>
inline AccessScope ecss::detail::readScope (
    ECSType type
) 
```





**See also:** [**AccessTracker**](classecss_1_1detail_1_1AccessTracker.md) 



        

<hr>



### function trivialityReporter 

_The installed reporter. Prints one line to stderr until someone replaces it._ 
```C++
inline TrivialityReporter & ecss::detail::trivialityReporter () noexcept
```




<hr>



### function typeName 

_The component's name, carved out of the compiler's own signature for this function. No RTTI, and unlike typeid(T).name() it is readable on GCC too._ 
```C++
template<class T>
std::string_view ecss::detail::typeName () noexcept
```




<hr>



### function writeScope 

_Claim_ `type` _for writing until the returned scope dies._
```C++
template<typename T>
inline AccessScope ecss::detail::writeScope (
    ECSType type
) 
```





**See also:** [**AccessTracker**](classecss_1_1detail_1_1AccessTracker.md) 



        

<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/Access.h`

