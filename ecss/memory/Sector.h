#pragma once

#include <cstdint>
#include <typeindex>

#include "Reflection.h"
#include "stdint.h"

#include "../Types.h"
#include "../contiguousMap.h"

namespace ecss::Memory {
	namespace Dummy
	{
		struct Sector { SectorId id; uint32_t isAliveData; };
		inline constexpr size_t sectorSize = (sizeof(Dummy::Sector) + alignof(Dummy::Sector) - 1) / alignof(Dummy::Sector) * alignof(Dummy::Sector);
	}
	

	struct LayoutData
	{
		size_t typeHash = 0;
		uint32_t offset = 0;
		uint16_t index = 0;
		ReflectionHelper::FunctionTable functionTable;
		bool isTrivial = 0;
	};

	struct ISectorLayoutMeta {
		class Iterator {
		public:
			using iterator_category = std::forward_iterator_tag;
			using value_type = const LayoutData;
			using difference_type = std::ptrdiff_t;
			using pointer = value_type*;
			using reference = value_type&;

			Iterator(const ISectorLayoutMeta* layoutMeta, uint8_t idx) : layoutsArray(const_cast<LayoutData*>(layoutMeta->getLayouts()) + idx) {}

			Iterator& operator++() { ++layoutsArray; return *this; }
			Iterator operator++(int) { Iterator tmp = *this; ++(*this); return tmp; }

			bool operator==(const Iterator& other) const { return layoutsArray == other.layoutsArray; }
			bool operator!=(const Iterator& other) const { return !(*this == other); }

			reference& operator*() const { return *layoutsArray; }
			reference& operator->() const { return *layoutsArray; }

		private:
			LayoutData* layoutsArray;
		};

		Iterator begin() const { return { this, 0 }; }
		Iterator end() const { return { this, getTypesCount() }; }

	public:
		virtual ~ISectorLayoutMeta() = default;
		virtual uint16_t getTotalSize() const = 0;

		template<typename T>
		inline const LayoutData& getLayoutData() const { return getLayoutData(getIndex<T>()); }
		virtual const LayoutData& getLayoutData(uint8_t idx) const = 0;

	public:
		template<typename T>
		inline constexpr static size_t TypeId() { return TypeIdImpl<std::remove_const_t<std::remove_pointer_t<std::remove_reference_t<T>>>>(); }

	private:
		template<typename T>
		inline constexpr static size_t TypeIdImpl() {
			static char tag;
			return reinterpret_cast<size_t>(&tag);
		}
		template<typename T>
		inline uint8_t getIndex() const { return getIndexByType(TypeId<T>()); }

		virtual const LayoutData* getLayouts() const = 0;

		virtual uint8_t getIndexByType(size_t) const = 0;
		virtual uint8_t getTypesCount() const = 0;
	};

	template<typename U>
	inline void initLayoutData(LayoutData& data, uint16_t& index, uint32_t offset) {
		static_assert(std::is_move_constructible_v<U>, "Type must be move-constructible for use in SectorsArray");

		data.typeHash = ISectorLayoutMeta::TypeId<U>();
		data.offset = offset;
		data.index = index++;
		data.isTrivial = std::is_trivial_v<U>;

		data.functionTable.move = [](void* dest, void* src)
		{
			new(dest) U(std::move(*static_cast<U*>(src)));
		};

		data.functionTable.copy = [](void* dest, const void* src)
		{
			if constexpr (std::is_copy_constructible_v<U>) {
				new(dest) U(*static_cast<const U*>(src));
			}
			else {
				assert(false && "Attempt to copy a move-only type!");
			}
		};

		data.functionTable.destructor = [](void* src) {
			std::destroy_at(static_cast<U*>(src));
		};
	}

	template<typename... U>
	inline void initLayoutData(LayoutData* dataArray, const std::array<uint32_t, sizeof...(U)>& offsets) {
		uint16_t counter = 0;
		(initLayoutData<std::remove_const_t<std::remove_pointer_t<std::remove_reference_t<U>>>>(dataArray[counter], counter, offsets[counter]), ...);
	}

#define INITIALIZE_COMMON_META_DATA(...)																					\
		static constexpr auto count = types::OffsetArray<Dummy::sectorSize, __VA_ARGS__>::count;							\
		static constexpr auto totalSize = types::OffsetArray<Dummy::sectorSize, __VA_ARGS__>::totalSize;					\
		static constexpr auto offsets = types::OffsetArray<Dummy::sectorSize, __VA_ARGS__>::offsets;						\
		LayoutData layout[count];																							\
																															\
	public:																													\
		SectorLayoutMeta() { initLayoutData<__VA_ARGS__>(layout, offsets); }												\
		uint16_t getTotalSize() const override { return totalSize; }														\
		const LayoutData& getLayoutData(uint8_t index) const override { assert(index < count); return layout[index]; }		\
																															\
	private:																												\
		uint8_t getTypesCount() const override { return count; }															\
		const LayoutData* getLayouts() const override { return layout; }													\
	public:

	template<typename... Types>
	struct SectorLayoutMeta : ISectorLayoutMeta {
		INITIALIZE_COMMON_META_DATA(Types...)
	private:
		static inline size_t typeIds[count] = { TypeId<Types>()... };
		uint8_t getIndexByType(size_t hash) const override {
			for (uint8_t i = 0; i < count; ++i) {
				if (typeIds[i] == hash) {
					return i;
				}
			}
					
			assert(false);
			return count;
		}
	};

	template<typename T>
	struct SectorLayoutMeta<T> : ISectorLayoutMeta {
		INITIALIZE_COMMON_META_DATA(T)
	private:
		static inline size_t typeId1 = TypeId<T>();
		uint8_t getIndexByType(size_t hash) const override { assert(hash == typeId1); return 0; }
	};

	template<typename T1, typename T2>
	struct SectorLayoutMeta<T1, T2> : ISectorLayoutMeta {
		INITIALIZE_COMMON_META_DATA(T1, T2)
	private:
		static inline size_t typeId1 = TypeId<T1>();
		static inline size_t typeId2 = TypeId<T2>();
		uint8_t getIndexByType(size_t hash) const override {
			if (hash == typeId1) return 0;
			if (hash == typeId2) return 1;
			assert(false);
			return count;
		}
	};

	/*
	* sector stores data for any custom type in theory, offset to type stores in SectorMetadata struct
	* --------------------------------------------------------------------------------------------
	*                                       [SECTOR]
	* 0x 00                                                     { SectorId  }
	* 0x sizeof(Sector)                                         { SomeMember  }
	* 0x sizeof(Sector + SomeMember)                            { SomeMember1 }
	* 0x sizeof(Sector + SomeMember + SomeMember1)              { SomeMember2 }
	* ...
	* 0x sizeof(Sector... + ...SomeMemberN - 1 + SomeMemberN)   { SomeMemberN }
	*
	*--------------------------------------------------------------------------------------------
	*/
	struct Sector final {
		SectorId id;
		uint32_t isAliveData; //bits for alive flags

		constexpr void setAlive(size_t componentPosition, bool value) { value ? isAliveData |= (1u << componentPosition) : isAliveData &= ~(1u << componentPosition); }

		constexpr bool isAlive(size_t componentPosition) const { return isAliveData & (1u << componentPosition); }

		template<typename T>
		constexpr T* getMember(size_t offset, size_t componentPosition) { return isAlive(componentPosition) ? static_cast<T*>(getMemberPtr(this, offset)) : nullptr; }

		template<typename T>
		constexpr T* getMember(const LayoutData& layout) { return getMember<T>(layout.offset, layout.index); }

		static void* getMemberPtr(Sector* sectorAdr, size_t offset) { return reinterpret_cast<char*>(sectorAdr) + offset; }

		constexpr bool isSectorAlive() const { return isAliveData != 0; }

		template <typename T>
		static T* getComponentFromSector(Sector* sector, ISectorLayoutMeta* sectorLayout) {
			if (!sector) {
				return nullptr;
			}
			const auto& layout = sectorLayout->getLayoutData<T>();
			return sector->getMember<T>(layout.offset, layout.index);
		}

		void destroyMember(const LayoutData& layout) {
			if (isAlive(layout.index)) {
				setAlive(layout.index, false);
				layout.functionTable.destructor(getMemberPtr(this, layout.offset));
			}
		}

		template<typename T, class ...Args>
		inline static T* emplaceMember(Sector* sector, const LayoutData& layout, Args&&... args) {
			assert(sector);
			sector->destroyMember(layout);
			sector->setAlive(layout.index, true);
			return new(getMemberPtr(sector, layout.offset))T(std::forward<Args>(args)...);
		}

		template<typename T>
		inline static T* copyMember(const T& from, Sector* to, const LayoutData& layout) {
			assert(to);
			to->destroyMember(layout);
			to->setAlive(layout.index, true);
			return new(getMemberPtr(to, layout.offset))T(from);
		}

		template<typename T>
		inline static T* moveMember(T&& from, Sector* to, const LayoutData& layout) {
			assert(to);
			to->destroyMember(layout);
			to->setAlive(layout.index, true);
			return new(getMemberPtr(to, layout.offset))T(std::move(from));
		}

		inline static void* moveMember(void* from, Sector* to, const LayoutData& layout) {
			assert(to);
			to->destroyMember(layout);
			auto ptr = getMemberPtr(to, layout.offset);
			if (!from) {
				return ptr;
			}
			layout.functionTable.move(ptr, from);
			to->setAlive(layout.index, !!from);
			layout.functionTable.destructor(from);
			return ptr;
		}

		inline static void* copyMember(const void* from, Sector* to, const LayoutData& layout) {
			assert(to);
			to->destroyMember(layout);
			
			auto ptr = getMemberPtr(to, layout.offset);
			if (!from) {
				return ptr;
			}
			layout.functionTable.copy(ptr, const_cast<void*>(from));
			to->setAlive(layout.index, !!from);
			return ptr;
		}

		inline static Sector* moveSector(Sector* from, Sector* to, const ISectorLayoutMeta* layouts) {
			assert(from != to);
			assert(from && to);
			destroySector(to, layouts);

			new (to)Sector(std::move(*from));
			for (const auto& layout : *layouts) {
				if (!from->isAlive(layout.index)) {
					continue;
				}

				layout.functionTable.move(getMemberPtr(to, layout.offset), getMemberPtr(from, layout.offset));

				to->setAlive(layout.index, true);
			}
			destroySector(from, layouts);
			return to;
		}

		inline static Sector* copySector(Sector* from, Sector* to, const ISectorLayoutMeta* layouts) {
			assert(from != to);
			assert(from && to);
			destroySector(to, layouts);
			
			if constexpr (std::is_trivially_copyable_v<Sector>) {
				memcpy(to, from, sizeof(Sector));
			}
			else {
				new (to)Sector(*from);
			}
			
			for (const auto& layout : *layouts) {
				if (!from->isAlive(layout.index)) {
					continue;
				}

				layout.functionTable.copy(getMemberPtr(to, layout.offset), getMemberPtr(from, layout.offset));

				to->setAlive(layout.index, true);
			}

			return to;
		}

		inline static void destroySector(Sector* sector, const ISectorLayoutMeta* layouts) {
			if (sector) {
				destroyMembers(sector, layouts);
				sector->~Sector();
			}
		}

		inline static void destroyMembers(Sector* sector, const ISectorLayoutMeta* layouts) {
			if (sector) {
				if (sector->isSectorAlive()) {
					for (const auto& layout : (*layouts)) {
						sector->destroyMember(layout);
					}
				}
			}
		}
	};

	static_assert(sizeof(Dummy::Sector) == sizeof(Sector), "Dummy and real Sector differ!");
	static_assert(std::is_trivial_v<Sector>);
}
