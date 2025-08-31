#pragma once

#include <cstdint>

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
			for (size_t i = 0; i < types::OffsetArray<Dummy::sectorSize, Types...>::count; i++) {
				mIsTrivial = mIsTrivial && layout[i].isTrivial;
				if (!mIsTrivial) {
					break;
				}
			}
		}

		~SectorLayoutMeta() {
			delete[] layout;
			delete[] typeIds;
		}

		uint16_t getTotalSize() const {	return totalSize; }
		bool isTrivial() const { return mIsTrivial; }

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
		bool mIsTrivial = std::is_trivial_v<Dummy::Sector>;
	};
}
