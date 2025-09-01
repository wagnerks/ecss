#pragma once

#include <cstdint>
#include <limits>
#include <array>
#include <type_traits>

namespace ecss {
	using SectorId = uint32_t;
	using EntityId = SectorId;
	using ECSType = uint16_t;

	constexpr SectorId INVALID_ID = std::numeric_limits<SectorId>::max();

	namespace types {
		template <size_t base, typename... Types>
		struct OffsetArray {
			static constexpr size_t count = sizeof...(Types);

			static constexpr std::size_t align_up(std::size_t n, std::size_t a) noexcept {
				return (n + (a - 1)) / a * a; 
			}

			template <size_t I>
			static constexpr uint32_t get() {
				using Tup = std::tuple<Types...>;
				using Cur = std::tuple_element_t<I, Tup>;
				if constexpr (I == 0) {
					return static_cast<uint32_t>(align_up(base, alignof(Cur)));
				}
				else {
					using Prev = std::tuple_element_t<I - 1, Tup>;
					constexpr uint32_t prev = get<I - 1>();
					return static_cast<uint32_t>(align_up(prev + sizeof(Prev), alignof(Cur)));
				}
			}

			template <size_t... Is>
			static constexpr std::array<uint32_t, count> make(std::index_sequence<Is...>) {
				return { get<Is>()... };
			}

			static constexpr std::array<uint32_t, count> offsets = make(std::make_index_sequence<count>{});
			static constexpr size_t totalSize =
				offsets.back() + sizeof(std::tuple_element_t<count - 1, std::tuple<Types...>>);

			static constexpr size_t sector_align = base;
			static constexpr size_t stride = align_up(totalSize, sector_align);
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

