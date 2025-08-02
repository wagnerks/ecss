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

			template <size_t I>
			static constexpr uint32_t get() {
				if constexpr (I == 0) {
					return base;
				}
				else {
					using PrevType = typename std::tuple_element<I - 1, std::tuple<Types...>>::type;
					using CurType = typename std::tuple_element<I, std::tuple<Types...>>::type;
					constexpr uint32_t prev = get<I - 1>();
					return ((prev + sizeof(PrevType) + alignof(CurType) - 1) / alignof(CurType)) * alignof(CurType);
				}
			}

			template <size_t... Is>
			static constexpr std::array<uint32_t, count> make(std::index_sequence<Is...>) {
				return { get<Is>()... };
			}

			static constexpr std::array<uint32_t, count> offsets = make(std::make_index_sequence<count>{});
			static constexpr size_t totalSize = offsets.back() + sizeof(std::tuple_element_t<count - 1, std::tuple<Types...>>);
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

