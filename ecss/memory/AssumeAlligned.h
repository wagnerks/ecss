#pragma once
#include <cstddef>
#include <cstdint>

// ecss::assume_aligned<N>(p) — как std::assume_aligned
namespace ecss {

template <std::size_t N, class T>
inline T* assume_aligned(T* p) noexcept {
#if defined(__cpp_lib_assume_aligned) && __cpp_lib_assume_aligned >= 201811L
    #include <memory>
    return std::assume_aligned<N>(p);
#elif defined(__has_builtin)
    #if __has_builtin(__builtin_assume_aligned)
        return reinterpret_cast<T*>(__builtin_assume_aligned(p, N));
    #endif
#endif
#ifdef _MSC_VER
    __assume((reinterpret_cast<std::uintptr_t>(p) & (N - 1)) == 0);
#endif
#ifndef NDEBUG
    if ((reinterpret_cast<std::uintptr_t>(p) & (N - 1)) != 0) {
        __builtin_trap();
    }
#endif
    return p;
}

template <std::size_t N, class T>
inline const T* assume_aligned(const T* p) noexcept { return const_cast<const T*>(assume_aligned<N>(const_cast<T*>(p))); }

} // namespace ecss