

# File Fwd.h

[**File List**](files.md) **>** [**ecss**](dir_194708e763cf312315c6b23555bce86f.md) **>** [**Fwd.h**](Fwd_8h.md)

[Go to the documentation of this file](Fwd_8h.md)


```C++
#pragma once

#include <cstdint>

// Definable from the build so it can be turned down to a plain inline. Forced inlining is
// what makes the hot paths hot, but it also means a caller's function grows by everything
// it touches, and MSVC's optimiser is superlinear in function size -- a translation unit
// that concentrated 64 component operations in one function spent 9 of its 11 seconds in
// the back end. Overriding is the way to find out whether that is what a slow TU is hitting.
#ifndef FORCE_INLINE
#  if defined(_MSC_VER)
#    define FORCE_INLINE __forceinline
#  elif defined(__GNUC__) || defined(__clang__)
#    define FORCE_INLINE inline __attribute__((always_inline))
#  else
#    define FORCE_INLINE inline
#  endif
#endif

namespace ecss {
    using SectorId = uint32_t;
    using EntityId = SectorId;
    using ECSType = uint16_t;

    constexpr SectorId INVALID_ID = static_cast<SectorId>(~SectorId{ 0 });

    constexpr uint32_t INVALID_IDX = static_cast<uint32_t>(~uint32_t{ 0 });
}
```


