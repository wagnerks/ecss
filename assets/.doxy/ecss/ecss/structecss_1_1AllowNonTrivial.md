

# Struct ecss::AllowNonTrivial

**template &lt;class T&gt;**



[**ClassList**](annotated.md) **>** [**ecss**](namespaceecss.md) **>** [**AllowNonTrivial**](structecss_1_1AllowNonTrivial.md)



_Declare that a component is knowingly not trivially copyable._ [More...](#detailed-description)

* `#include <Types.h>`



Inherits the following classes: std::false_type






























































## Detailed Description


Specialize to std::true\_type for a component that owns a std::string or a std::vector and always will: 
```C++
template<> struct ecss::AllowNonTrivial<MeshComponent> : std::true_type {};
```



The warning below exists to catch the accidental cases  a stray virtual function, a mutex member, a base class someone added for unrelated reasons  where the cost is paid for nothing. A type that cannot be made trivial has nothing to fix, and a warning nobody can act on is one everybody learns to switch off wholesale. 


    

------------------------------
The documentation for this class was generated from the following file `ecss/Types.h`

