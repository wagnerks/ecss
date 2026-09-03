

# File AccessTracker.h



[**FileList**](files.md) **>** [**ecss**](dir_194708e763cf312315c6b23555bce86f.md) **>** [**AccessTracker.h**](AccessTracker_8h.md)

[Go to the source code of this file](AccessTracker_8h_source.md)



* `#include <ecss/Fwd.h>`
* `#include <cassert>`
* `#include <cstdio>`
* `#include <map>`
* `#include <atomic>`
* `#include <mutex>`
* `#include <thread>`
* `#include <typeinfo>`
* `#include <utility>`













## Namespaces

| Type | Name |
| ---: | :--- |
| namespace | [**ecss**](namespaceecss.md) <br> |
| namespace | [**detail**](namespaceecss_1_1detail.md) <br>_Iterable view over entities with one main component and optional additional components._  |


## Classes

| Type | Name |
| ---: | :--- |
| class | [**AccessScope**](classecss_1_1detail_1_1AccessScope.md) <br>_RAII claim on one component type, for the tracker to see._  |
| class | [**AccessTracker**](classecss_1_1detail_1_1AccessTracker.md) <br>_Debug-only detector for two threads touching one component type at once._  |



















































------------------------------
The documentation for this class was generated from the following file `ecss/AccessTracker.h`

