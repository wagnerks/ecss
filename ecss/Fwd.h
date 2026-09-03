#pragma once
/**
 * @file
 * @brief The id types, and nothing that costs anything to include.
 *
 * Most headers that name something from ecss name only an id: a component recording which
 * entity owns it, a system interface taking a list of entities to process, a renderer
 * keying a cache. None of them touch a Registry, and none of them should pay for one.
 *
 * Types.h cannot be that header. It carries the sector offset arithmetic, which wants
 * <array> and <tuple>, and the seqlock helpers, which want <atomic> and (on MSVC)
 * <intrin.h>. That came to 55k preprocessed lines for a caller that wanted a uint32_t.
 *
 * Include this when an id is all that is needed; Types.h pulls it in, so everything that
 * already includes Types.h is unaffected.
 */

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

	/// @brief Reserved id standing for "no sector".
	constexpr SectorId INVALID_ID = static_cast<SectorId>(~SectorId{ 0 });

	/// @brief Reserved linear index standing for "not present".
	constexpr uint32_t INVALID_IDX = static_cast<uint32_t>(~uint32_t{ 0 });
}
