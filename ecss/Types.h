#pragma once

#include <array>
#include <atomic>
#include <cstdio>
#include <string_view>
#include <tuple>
#include <type_traits>

#include <ecss/Fwd.h>

#if defined(_MSC_VER)
#  include <intrin.h>
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

	/**
	 * @brief Declare that a component is knowingly not trivially copyable.
	 *
	 * Specialize to std::true_type for a component that owns a std::string or a std::vector
	 * and always will:
	 * @code
	 * template<> struct ecss::AllowNonTrivial<MeshComponent> : std::true_type {};
	 * @endcode
	 *
	 * The warning below exists to catch the accidental cases -- a stray virtual function, a
	 * mutex member, a base class someone added for unrelated reasons -- where the cost is
	 * paid for nothing. A type that cannot be made trivial has nothing to fix, and a warning
	 * nobody can act on is one everybody learns to switch off wholesale.
	 */
	template<class T>
	struct AllowNonTrivial : std::false_type {};

	/// @brief Receives the name of every component that costs its array the trivial fast paths.
	using TrivialityReporter = void (*)(std::string_view typeName);

	namespace detail {
		/**
		 * @brief The component's name, carved out of the compiler's own signature for this
		 *        function. No RTTI, and unlike typeid(T).name() it is readable on GCC too.
		 */
		template<class T>
		constexpr std::string_view typeName() noexcept {
#if defined(_MSC_VER)
			constexpr std::string_view signature = __FUNCSIG__;
			constexpr std::string_view open = "typeName<";
			auto begin = signature.find(open) + open.size();
			const auto end = signature.rfind('>');
			// MSVC spells out the class-key; nobody wants to read "struct" in a diagnostic.
			for (const std::string_view key : { "struct ", "class ", "enum ", "union " }) {
				if (signature.compare(begin, key.size(), key) == 0) { begin += key.size(); break; }
			}
#else
			constexpr std::string_view signature = __PRETTY_FUNCTION__;
			const auto begin = signature.find("T = ") + 4;
			// clang ends the clause at the bracket ("[T = Heavy]"); gcc lists the other
			// substitutions after a semicolon ("[with T = Heavy; std::string_view = ...]").
			const auto semicolon = signature.find(';', begin);
			const auto end = semicolon != std::string_view::npos ? semicolon : signature.rfind(']');
#endif
			return begin < end ? signature.substr(begin, end - begin) : signature;
		}

		/// @brief The installed reporter. Prints one line to stderr until someone replaces it.
		inline TrivialityReporter& trivialityReporter() noexcept {
			static TrivialityReporter reporter = [](std::string_view name) {
				std::fprintf(stderr,
					"ecss: component '%.*s' is not trivially copyable, so its array gives up the\n"
					"      raw-bytes paths: every middle insert, defragment and copy runs a move\n"
					"      constructor plus a destructor per element instead of one memmove.\n"
					"      Usually a virtual function (often inherited) or a mutex member. If it is\n"
					"      deliberate, specialize ecss::AllowNonTrivial<T> for it.\n",
					static_cast<int>(name.size()), name.data());
				std::fflush(stderr);
			};
			return reporter;
		}
	}

	/// @brief Route the triviality report into your own log, or pass nullptr to silence it.
	inline void setTrivialityReporter(TrivialityReporter reporter) noexcept {
		detail::trivialityReporter() = reporter;
	}

	namespace detail {
		/**
		 * @brief Instantiated for nothing but its own deprecation warning.
		 *
		 * A class template rather than a function: MSVC prints one C4996 per distinct
		 * specialization and names it (`NonTrivialComponent<MeshComponent>`), while a
		 * deprecated *function* template collapses to a single warning per source line, so a
		 * translation unit registering ten components would only ever confess to the first.
		 */
		template<class T>
		struct [[deprecated("ecss: this component is not trivially copyable, so its array gives up the "
		                    "raw-bytes paths -- every middle insert, defragment and copy runs a move "
		                    "constructor plus a destructor per element instead of one memmove. Usually "
		                    "the cause is a virtual function (often an inherited base) or a mutex "
		                    "member. If it is deliberate, specialize ecss::AllowNonTrivial<T> for it.")]]
		NonTrivialComponent {};

		/**
		 * @brief Say once, per component type, that this one costs its array the fast paths.
		 *
		 * Two channels, because neither alone reaches everyone. The compile-time warning is the
		 * better one -- it names the type without running anything -- but it is silenced for any
		 * consumer that pulls ecss in as a *system* header, which every CMake project using
		 * target_precompile_headers does: the generated PCH opens with `#pragma system_header`,
		 * and a warning whose location is inside it never reaches the build log. So the same
		 * condition also reports once at runtime, when the array's layout is built. That costs
		 * one branch per array creation and nothing on any hot path.
		 *
		 * Define ECSS_NO_TRIVIALITY_WARNINGS to remove both; setTrivialityReporter(nullptr)
		 * removes only the runtime half.
		 *
		 * @note MSVC reports C4996 at /W3 and above; /W1 (bare `cl` with no flags) hides it,
		 *       CMake and MSBuild projects default to /W3. GCC and clang report it at any level.
		 */
		template<class T>
		void checkTriviality() noexcept {
#ifndef ECSS_NO_TRIVIALITY_WARNINGS
			if constexpr (!std::is_trivially_copyable_v<T> && !AllowNonTrivial<T>::value) {
				[[maybe_unused]] NonTrivialComponent<T> diagnostic{};

				[[maybe_unused]] static const bool reported = [] {
					if (const auto reporter = trivialityReporter()) { reporter(typeName<T>()); }
					return true;
				}();
			}
#endif
		}
	}
}

