// Prototype: what `import ecss;` would cost a consumer, versus the header and versus a PCH.
//
// Kept because the answer on VS 2022 17.8 (cl 19.38) is "blocked, revisit after an upgrade",
// and a prototype that lives in a gitignored build directory would not survive to be
// revisited. Measured 494 ms per view-using TU against 1 032 ms for the header and 651 ms
// for a PCH of the same header -- so modules win, but only by ~157 ms/TU over one line of
// CMake, and they arrive with two blockers (see below).
//
// Wrapper form rather than a rewrite: the header is parsed once here in the global module
// fragment and the names are re-exported. Cheapest possible migration, so what it saves is
// the upper bound before any real work.
//
// To re-run, from an x64 Native Tools prompt in the repository root:
//
//   set OUT=build-bench\modules
//   mkdir %OUT%
//   cl /nologo /c /EHsc /O2 /DNDEBUG /std:c++latest /I. /interface /TP ^
//      /ifcOutput %OUT%\ecss.ifc /Fo%OUT%\ecss.obj scripts\experiments\modules\ecss.ixx
//   cl /nologo /c /EHsc /O2 /DNDEBUG /std:c++latest /reference ecss=%OUT%\ecss.ifc ^
//      /Fo%OUT%\each_mod.obj scripts\experiments\modules\consumer_each.cpp
//   cl /nologo /c /EHsc /O2 /DNDEBUG /std:c++latest /I. ^
//      /Fo%OUT%\each_hdr.obj scripts\experiments\modules\consumer_each_hdr.cpp
//
// Compare the two object files: they should be within a kilobyte of each other. If the
// module one is missing, the compile failed -- do not time a failed compile.
//
// BLOCKER 1, not fixable in this library. consumer_view.cpp fails with C3643/C3448:
// `for (auto [e, a, b] : reg.view<A, B>())` will not decompose a std::tuple that reached
// the importer through a module. That is the library's primary iteration idiom. each()
// is unaffected.
//
// BLOCKER 2, debug builds only. Without NDEBUG, PinCounters.h instantiates
// std::map<std::pair<const void*, SectorId>, size_t> and the importer cannot find
// operator< for the pair (C2678) -- global module fragment operators are not reachable.
//
// Both are symptoms of the std headers sitting in the global module fragment, which is
// forced here: `import std;` needs 17.10 / cl 19.40, and this toolset is 19.38. Retry the
// whole prototype with `import std;` after upgrading; that is the point at which this
// becomes worth costing out properly.
module;

#include <ecss/Registry.h>

export module ecss;

// A real migration has to audit everything consumers name, not just what these probes use.
export namespace ecss {
	using ecss::Registry;
	using ecss::EntityId;
	using ecss::SectorId;
	using ecss::ECSType;
	using ecss::Ranges;
	using ecss::INVALID_ID;
	using ecss::INVALID_IDX;
}
