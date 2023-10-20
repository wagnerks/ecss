#pragma once

#include <cstdint>
#include <limits>

#include <type_traits>

namespace ecss {
	using SectorId = uint32_t;
	using ECSType = uint16_t;

	constexpr SectorId INVALID_ID = std::numeric_limits<SectorId>::max();

	namespace types {
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

			static constexpr std::size_t count() {//todo check it works
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

