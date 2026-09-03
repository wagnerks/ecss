

# File Access.h



[**FileList**](files.md) **>** [**ecss**](dir_194708e763cf312315c6b23555bce86f.md) **>** [**Access.h**](Access_8h.md)

[Go to the source code of this file](Access_8h_source.md)



* `#include <algorithm>`
* `#include <array>`
* `#include <cassert>`
* `#include <shared_mutex>`
* `#include <stdexcept>`
* `#include <utility>`
* `#include <vector>`
* `#include <ecss/Fwd.h>`













## Namespaces

| Type | Name |
| ---: | :--- |
| namespace | [**ecss**](namespaceecss.md) <br> |
| namespace | [**detail**](namespaceecss_1_1detail.md) <br>_Iterable view over entities with one main component and optional additional components._  |


## Classes

| Type | Name |
| ---: | :--- |
| struct | [**Read**](structecss_1_1Read.md) &lt;typename T&gt;<br>_Claim a component type for reading._  |
| struct | [**Write**](structecss_1_1Write.md) &lt;typename T&gt;<br>_Claim a component type for writing._  |
| class | [**AccessGuard**](classecss_1_1detail_1_1AccessGuard.md) <br>_Reader-writer lock per component type, held for the length of a system._  |
| struct | [**Entry**](structecss_1_1detail_1_1AccessGuard_1_1Entry.md) <br>_One component type's claim: which lock, and whether for writing._  |



















































------------------------------
The documentation for this class was generated from the following file `ecss/Access.h`

