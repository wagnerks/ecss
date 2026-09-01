#pragma once

#include <atomic>
#include <cstdint>
#include <limits>
#include <array>
#include <type_traits>

#if defined(_MSC_VER)
#  include <intrin.h>
#  define FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#  define FORCE_INLINE inline __attribute__((always_inline))
#else
#  define FORCE_INLINE inline
#endif

namespace ecss {
	/// @brief CPU pause / yield hint for short seqlock retry loops.
	FORCE_INLINE void cpuRelax() noexcept {
#if defined(_MSC_VER)
#  if defined(_M_X64) || defined(_M_IX86)
		_mm_pause();
#  elif defined(_M_ARM64)
		__yield();
#  endif
#else
#  if defined(__x86_64__) || defined(__i386__)
		__builtin_ia32_pause();
#  elif defined(__aarch64__)
		__asm__ __volatile__("yield" ::: "memory");
#  endif
#endif
	}

	using SectorId = uint32_t;
	using EntityId = SectorId;
	using ECSType = uint16_t;

	constexpr SectorId INVALID_ID = std::numeric_limits<SectorId>::max();
	constexpr uint32_t INVALID_IDX = std::numeric_limits<uint32_t>::max();

	namespace types {
		/**
		 * @brief Compile-time guard for state the design assumes is genuinely lock-free.
		 *
		 * A std::atomic whose payload exceeds the platform word size silently falls back to
		 * a lock table inside the standard library. Nothing at the call site says so, and
		 * the cost only shows up as readers that refuse to scale -- which is exactly how a
		 * 16-byte std::atomic<SparseView> ended up serialising every sparse lookup in this
		 * library through a process-global spin lock.
		 *
		 * Rule: every atomic on a hot path gets a static_assert with this next to it.
		 */
		template<typename T>
		inline constexpr bool isLockFreeAtomic = std::atomic<T>::is_always_lock_free;

		/// @brief Empty base for offset calculation when sector has no header.
		struct EmptyBase {};

		template <typename Base, typename... Types>
		struct OffsetArray {
			template<size_t N, size_t A>
			static consteval size_t align_up() noexcept {
				if constexpr((N & (N - 1)) == 0) {
					return (N + A - 1) & ~(A - 1);
				} else {
					return (N + (A - 1)) / A * A;
				}
			}

			static constexpr size_t count = sizeof...(Types);
			
			// For EmptyBase, baseSize is 0; otherwise use aligned sizeof(Base)
			static constexpr size_t baseSize = std::is_empty_v<Base> ? 0 : align_up<sizeof(Base), alignof(Base)>();

			template <size_t I>
			static consteval size_t get() {
				using Tup = std::tuple<Types...>;
				using Cur = std::tuple_element_t<I, Tup>;
				if constexpr (I == 0) {
					if constexpr (std::is_empty_v<Base>) {
						// No base header, start from 0 aligned to first type
						return 0;
					} else {
						return align_up<baseSize, alignof(Cur)>();
					}
				}
				else {
					using Prev = std::tuple_element_t<I - 1, Tup>;
					constexpr size_t prev = get<I - 1>();
					return align_up<prev + sizeof(Prev), alignof(Cur)>();
				}
			}

			template <size_t... Is>
			static consteval std::array<size_t, count> make(std::index_sequence<Is...>) {
				return { get<Is>()... };
			}

			static constexpr uint32_t max_align = []{
				uint32_t m = std::is_empty_v<Base> ? 1 : alignof(Base);
				((m = m < alignof(Types) ? alignof(Types) : m), ...);
				return m;
			}();

			static constexpr std::array<size_t, count> offsets = make(std::make_index_sequence<count>{});
			static constexpr size_t totalSize = align_up<offsets.back() + sizeof(std::tuple_element_t<count - 1, std::tuple<Types...>>), max_align>();

			template<class T, std::size_t Off>
			static consteval void check_one() {
				static_assert(Off % alignof(T) == 0, "component misaligned");
			}

			static consteval void static_checks() {
				[]<std::size_t... Is>(std::index_sequence<Is...>) {
					(check_one<std::tuple_element_t<Is, std::tuple<Types...>>, offsets[Is]>(), ...);
				}(std::make_index_sequence<count>{});
				static_assert(totalSize % max_align == 0, "stride must be multiple of max_align");
			}
		};

		template<typename...>
		constexpr bool areUnique = true;

		template<typename T, typename... Ts>
		constexpr bool areUnique<T, Ts...> = (!std::is_same_v<T, Ts> && ...);

		template <typename T>
		constexpr int getIndex(int x = 0) {	return -1; }

		template<typename T, typename... Ts>
		constexpr int getIndex() { constexpr bool matches[] = { std::is_same_v<T, Ts>... };
			for (size_t i = 0; i < sizeof...(Ts); ++i) if (matches[i]) return static_cast<int>(i); return -1;
		}

		template<typename...>
		constexpr size_t typeIndex = 0;
		template<typename T, typename... Ts>
		constexpr size_t typeIndex<T, Ts...> = getIndex<T, Ts...>();
	}
}

