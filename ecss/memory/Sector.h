#pragma once

#include <cstdint>
#include <typeindex>
#include "stdint.h"

#include <ecss/memory/Reflection.h>
#include <ecss/Types.h>

namespace ecss::Memory {
	namespace Dummy
	{
		struct Sector { SectorId id; uint32_t isAliveData; };
		inline constexpr size_t sectorSize = (sizeof(Dummy::Sector) + alignof(Dummy::Sector) - 1) / alignof(Dummy::Sector) * alignof(Dummy::Sector);
	}

	struct LayoutData {
		struct FunctionTable {
			std::function<void(void* dest, void* src)> move;
			std::function<void(void* dest, void* src)> copy;
			std::function<void(void* src)> destructor;
		};

		FunctionTable functionTable;
		size_t typeHash = 0;
		uint32_t offset = 0;
		uint16_t index = 0;
		uint16_t isAliveMask = 0;
		uint16_t isNotAliveMask = 0;
		bool isTrivial = false;
	};

	struct SectorLayoutMeta {
		template<typename U>
		inline void initLayoutData(LayoutData& data, uint8_t& index, uint32_t offset) const noexcept {
			static_assert(std::is_move_constructible_v<U>, "Type must be move-constructible for use in SectorsArray");

			data.typeHash = SectorLayoutMeta::TypeId<U>();
			data.offset = offset;
			data.index = index++;
			data.isAliveMask = static_cast<uint16_t>(1u << data.index);
			data.isNotAliveMask = ~(data.isAliveMask);
			data.isTrivial = std::is_trivial_v<U>;

			data.functionTable.move = [](void* dest, void* src) { new(dest) U(std::move(*static_cast<U*>(src))); };

			data.functionTable.copy = [](void* dest, const void* src)
			{
				if constexpr (std::is_copy_constructible_v<U>) {
					new(dest) U(*static_cast<const U*>(src));
				}
				else { assert(false && "Attempt to copy a move-only type!"); }
			};

			data.functionTable.destructor = [](void* src) { std::destroy_at(static_cast<U*>(src)); };
		}

		template<typename... U>
		inline void initLayoutData(LayoutData* dataArray) {
			uint8_t counter = 0;
			(initLayoutData<std::remove_const_t<std::remove_pointer_t<std::remove_reference_t<U>>>>(dataArray[counter], counter, types::OffsetArray<Dummy::sectorSize, U...>::offsets[counter]), ...);
		}

		class Iterator {
		public:
			using iterator_category = std::forward_iterator_tag;
			using value_type = const LayoutData;
			using difference_type = std::ptrdiff_t;
			using pointer = value_type*;
			using reference = value_type&;

			Iterator(const SectorLayoutMeta* layoutMeta, uint8_t idx) : layoutsArray(const_cast<LayoutData*>(layoutMeta->getLayouts()) + idx) {}

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
		template<typename... Types>
		static inline SectorLayoutMeta* create()
		{
			auto meta = new SectorLayoutMeta();
			meta->initData<Types...>();
			return meta;
		}

		template<typename... Types>
		void initData()	{
			count = types::OffsetArray<Dummy::sectorSize, Types...>::count;
			totalSize = types::OffsetArray<Dummy::sectorSize, Types...>::totalSize;
			typeIds = new size_t[types::OffsetArray<Dummy::sectorSize, Types...>::count]{ TypeId<Types>()... };

			layout = new LayoutData[types::OffsetArray<Dummy::sectorSize, Types...>::count];
			initLayoutData<Types...>(layout);
		}

		~SectorLayoutMeta() {
			delete[] layout;
			delete[] typeIds;
		}

		uint16_t getTotalSize() const {	return totalSize; }

		template<typename T>
		inline const LayoutData& getLayoutData() const { return layout[getIndex<T>()]; }
		const LayoutData& getLayoutData(uint8_t idx) const { return layout[idx]; }

	public:
		template<typename T>
		inline static size_t TypeId() { return TypeIdImpl<std::remove_const_t<std::remove_pointer_t<std::remove_reference_t<T>>>>(); }

	private:
		template<typename T>
		inline static size_t TypeIdImpl() { static char tag; return reinterpret_cast<size_t>(&tag); }

		template<typename T>
		inline uint8_t getIndex() const { return getIndexByType(TypeId<T>()); }

		const LayoutData* getLayouts() const { return layout; }
		
		uint8_t getIndexByType(size_t hash) const {
			for (uint8_t i = 0; i < count; ++i) {
				if (typeIds[i] == hash) {
					return i;
				}
			}

			assert(false);
			return count;
		}

		uint8_t getTypesCount() const { return count; }

	private:
		LayoutData* layout = nullptr;
		size_t* typeIds = nullptr;
		uint16_t totalSize = 0;
		uint8_t count = 0;
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
	struct alignas(8) Sector final {
		SectorId id;
		uint32_t isAliveData; //bits for alive flags

		inline constexpr void setAlive(size_t mask, bool value) { value ? isAliveData |= mask : isAliveData &= mask; }
		inline constexpr void markAlive(size_t mask) { isAliveData |= mask; }
		inline constexpr void markNotAlive(size_t mask) { isAliveData &= mask; }

		inline constexpr bool isAlive(size_t mask) const { return isAliveData & mask; }

		template<typename T>
		inline constexpr T* getMember(size_t offset, size_t mask) const { return isAliveData & mask ? static_cast<T*>(getMemberPtr(this, offset)) : nullptr; }

		template<typename T>
		inline constexpr T* getMember(size_t offset, size_t mask) { return isAliveData & mask ? static_cast<T*>(getMemberPtr(this, offset)) : nullptr; }

		template<typename T>
		inline constexpr T* getMember(const LayoutData& layout) const { return getMember<T>(layout.offset, layout.isAliveMask); }

		template<typename T>
		inline constexpr T* getMember(const LayoutData& layout) { return getMember<T>(layout.offset, layout.isAliveMask); }

		inline static void* getMemberPtr(const Sector* sectorAdr, size_t offset) { return const_cast<char*>(reinterpret_cast<const char*>(sectorAdr) + offset); }
		inline static void* getMemberPtr(Sector* sectorAdr, size_t offset) { return reinterpret_cast<char*>(sectorAdr) + offset; }

		inline constexpr bool isSectorAlive() const { return isAliveData != 0; }

		template <typename T>
		inline static const T* getComponentFromSector(const Sector* sector, SectorLayoutMeta* sectorLayout) {
			if (!sector) {
				return nullptr;
			}
			const auto& layout = sectorLayout->getLayoutData<T>();
			return sector->getMember<T>(layout.offset, layout.isAliveMask);
		}

		template <typename T>
		inline static T* getComponentFromSector(Sector* sector, SectorLayoutMeta* sectorLayout) {
			if (!sector) {
				return nullptr;
			}
			const auto& layout = sectorLayout->getLayoutData<T>();
			return sector->getMember<T>(layout.offset, layout.isAliveMask);
		}

		inline void destroyMember(const LayoutData& layout) {
			if (isAlive(layout.isAliveMask)) {
				setAlive(layout.isNotAliveMask, false);
				layout.functionTable.destructor(getMemberPtr(this, layout.offset));
			}
		}

		template<typename T, class ...Args>
		inline static T* emplaceMember(Sector* sector, const LayoutData& layout, Args&&... args) {
			assert(sector);

			void* memberPtr = getMemberPtr(sector, layout.offset);
			if constexpr (!std::is_trivially_destructible_v<T>) {
				if (sector->isAlive(layout.isAliveMask)) {
					layout.functionTable.destructor(memberPtr);
				}
			}

			sector->markAlive(layout.isAliveMask);

			return new(memberPtr)T{ std::forward<Args>(args)... };
		}

		template<typename T>
		inline static T* copyMember(const T& from, Sector* to, const LayoutData& layout) {
			assert(to);
			to->destroyMember(layout);
			to->setAlive(layout.isAliveMask, true);
			return new(getMemberPtr(to, layout.offset))T(from);
		}

		template<typename T>
		inline static T* moveMember(T&& from, Sector* to, const LayoutData& layout) {
			assert(to);
			to->destroyMember(layout);
			to->setAlive(layout.isAliveMask, true);
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
			to->setAlive(!!from ? layout.isAliveMask : layout.isNotAliveMask, !!from);
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
			to->setAlive(!!from ? layout.isAliveMask : layout.isNotAliveMask, !!from);
			return ptr;
		}

		inline static Sector* moveSector(Sector* from, Sector* to, const SectorLayoutMeta* layouts) {
			assert(from != to);
			assert(from && to);
			destroySector(to, layouts);

			new (to)Sector(std::move(*from));
			for (const auto& layout : *layouts) {
				if (!from->isAlive(layout.isAliveMask)) {
					continue;
				}

				layout.functionTable.move(getMemberPtr(to, layout.offset), getMemberPtr(from, layout.offset));

				to->setAlive(layout.isAliveMask, true);
			}
			destroySector(from, layouts);
			return to;
		}

		inline static Sector* copySector(Sector* from, Sector* to, const SectorLayoutMeta* layouts) {
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
				if (!from->isAlive(layout.isAliveMask)) {
					continue;
				}

				layout.functionTable.copy(getMemberPtr(to, layout.offset), getMemberPtr(from, layout.offset));
			}

			return to;
		}

		inline static void destroySector(Sector* sector, const SectorLayoutMeta* layouts) {
			if (sector) {
				destroyMembers(sector, layouts);
			}
		}

		inline static void destroyMembers(Sector* sector, const SectorLayoutMeta* layouts) {
			if (sector) {
				if (sector->isSectorAlive()) {
					for (const auto& layout : (*layouts)) {
						if (sector->isAlive(layout.isAliveMask)) {
							layout.functionTable.destructor(sector->getMemberPtr(sector, layout.offset));
						}
					}
					sector->isAliveData = 0;
				}
			}
		}
	};

	static_assert(sizeof(Dummy::Sector) == sizeof(Sector), "Dummy and real Sector differ!");
	static_assert(std::is_trivial_v<Sector>);
}
