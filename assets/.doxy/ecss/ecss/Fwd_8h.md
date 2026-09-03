

# File Fwd.h



[**FileList**](files.md) **>** [**ecss**](dir_194708e763cf312315c6b23555bce86f.md) **>** [**Fwd.h**](Fwd_8h.md)

[Go to the source code of this file](Fwd_8h_source.md)

_The id types, and nothing that costs anything to include._ [More...](#detailed-description)

* `#include <cstdint>`













## Namespaces

| Type | Name |
| ---: | :--- |
| namespace | [**ecss**](namespaceecss.md) <br> |



















































## Macros

| Type | Name |
| ---: | :--- |
| define  | [**FORCE\_INLINE**](Fwd_8h.md#define-force_inline)  `inline`<br> |

## Detailed Description


Most headers that name something from ecss name only an id: a component recording which entity owns it, a system interface taking a list of entities to process, a renderer keying a cache. None of them touch a Registry, and none of them should pay for one.


Types.h cannot be that header. It carries the sector offset arithmetic, which wants &lt;array&gt; and &lt;tuple&gt;, and the seqlock helpers, which want &lt;atomic&gt; and (on MSVC) &lt;intrin.h&gt;. That came to 55k preprocessed lines for a caller that wanted a uint32\_t.


Include this when an id is all that is needed; Types.h pulls it in, so everything that already includes Types.h is unaffected. 


    
## Macro Definition Documentation





### define FORCE\_INLINE 

```C++
#define FORCE_INLINE `inline`
```




<hr>

------------------------------
The documentation for this class was generated from the following file `ecss/Fwd.h`

