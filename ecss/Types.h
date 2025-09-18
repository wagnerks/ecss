#pragma once

#include <cstdint>
#include <limits>
#include <array>
#include <type_traits>

#if defined(_MSC_VER)
#  define FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#  define FORCE_INLINE inline __attribute__((always_inline))
#else
#  define FORCE_INLINE inline
#endif

namespace ecss {
	using SectorId = uint32_t;
	using EntityId = SectorId;
	using ECSType = uint16_t;

	constexpr SectorId INVALID_ID = std::numeric_limits<SectorId>::max();

	namespace types {
		template <typename Base, typename... Types>
		struct OffsetArray {
			static constexpr uint32_t align_up(uint32_t n, uint32_t a) noexcept {
				return (n + (a - 1)) / a * a;
			}

			static constexpr size_t count = sizeof...(Types);
			static constexpr size_t baseSize = align_up(sizeof(Base), alignof(Base));

			template <size_t I>
			static consteval uint32_t get() {
				using Tup = std::tuple<Types...>;
				using Cur = std::tuple_element_t<I, Tup>;
				if constexpr (I == 0) {
					return align_up(baseSize, alignof(Cur));
				}
				else {
					using Prev = std::tuple_element_t<I - 1, Tup>;
					constexpr size_t prev = get<I - 1>();
					return align_up(prev + sizeof(Prev), alignof(Cur));
				}
			}

			template <size_t... Is>
			static consteval std::array<uint32_t, count> make(std::index_sequence<Is...>) {
				return { get<Is>()... };
			}

			static constexpr uint32_t max_align = []{
				uint32_t m = alignof(Base);
				((m = m < alignof(Types) ? alignof(Types) : m), ...);
				return m;
			}();

			static constexpr std::array<uint32_t, count> offsets = make(std::make_index_sequence<count>{});
			static constexpr size_t totalSize = align_up(offsets.back() + sizeof(std::tuple_element_t<count - 1, std::tuple<Types...>>), max_align);

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

		template <typename T>
		struct Base {};

		template <typename... Ts>
		struct TypeSet : Base<Ts>... {
			template<typename T>
			constexpr auto operator+(Base<T>) {
				if constexpr (std::is_base_of_v<Base<T>, TypeSet>) {
					return TypeSet{};
				}
				else {
					return TypeSet<Ts..., T>{};
				}
			}

			static constexpr std::size_t count() {
				return sizeof...(Ts);
			}
		};

		template<typename... Ts>
		constexpr bool areUnique() {
			constexpr auto set = (TypeSet<>{} + ... + Base<Ts>{});
			return set.count() == sizeof...(Ts);
		}

		template <typename T>
		constexpr int getIndex(int x = 0)
		{
			return -1;
		}

		template <typename T, typename U, typename ...Ts>
		constexpr int getIndex(int x = 0)
		{
			if constexpr (std::is_same_v<T, U>)
			{
				return x;
			}
			else
			{
				return getIndex<T, Ts...>(x + 1);
			}
		}
	}
}

