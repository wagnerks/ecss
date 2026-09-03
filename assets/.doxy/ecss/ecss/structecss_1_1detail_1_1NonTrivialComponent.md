

# Struct ecss::detail::NonTrivialComponent

**template &lt;class T&gt;**



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**detail**](namespaceecss_1_1detail.md) **>** [**NonTrivialComponent**](structecss_1_1detail_1_1NonTrivialComponent.md)



_Instantiated for nothing but its own deprecation warning._ [More...](#detailed-description)

* `#include <Types.h>`

































































## Detailed Description


A class template rather than a function: MSVC prints one C4996 per distinct specialization and names it (`NonTrivialComponent <MeshComponent>`), while a deprecated _function_ template collapses to a single warning per source line, so a translation unit registering ten components would only ever confess to the first. 


    

------------------------------
The documentation for this class was generated from the following file `ecss/Types.h`

