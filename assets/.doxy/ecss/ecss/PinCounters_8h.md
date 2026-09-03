

# File PinCounters.h



[**FileList**](files.md) **>** [**ecss**](dir_194708e763cf312315c6b23555bce86f.md) **>** [**threads**](dir_a9a674ced088cdcac6c51605566c5246.md) **>** [**PinCounters.h**](PinCounters_8h.md)

[Go to the source code of this file](PinCounters_8h_source.md)



* `#include <algorithm>`
* `#include <atomic>`
* `#include <chrono>`
* `#include <cstdio>`
* `#include <cstdlib>`
* `#include <cassert>`
* `#include <cstddef>`
* `#include <cstdint>`
* `#include <mutex>`
* `#include <vector>`
* `#include <map>`
* `#include <utility>`
* `#include <ecss/Types.h>`













## Namespaces

| Type | Name |
| ---: | :--- |
| namespace | [**ecss**](namespaceecss.md) <br> |
| namespace | [**Threads**](namespaceecss_1_1Threads.md) <br> |


## Classes

| Type | Name |
| ---: | :--- |
| struct | [**PinCounters**](structecss_1_1Threads_1_1PinCounters.md) <br>_Per-sector pin tracking & synchronization for safe structural mutations._  |
| struct | [**WriterIntent**](structecss_1_1Threads_1_1PinCounters_1_1WriterIntent.md) <br>_RAII announcement that a writer wants the array to go quiet._  |
| struct | [**SelfWaitDebug**](structecss_1_1Threads_1_1SelfWaitDebug.md) <br>_Debug-only record of what the calling thread is holding, per array._  |



















































------------------------------
The documentation for this class was generated from the following file `ecss/threads/PinCounters.h`

