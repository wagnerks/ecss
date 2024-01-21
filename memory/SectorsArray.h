#pragma once

#include <cassert>
#include <map>
#include <shared_mutex>

#include "Sector.h"
#include "Reflection.h"

namespace ecss::Memory {

	/// <summary>
	/// data container with sectors of custom data in it
	///	
	///	the Sector is object in memory which includes Sector{ uint32_t(by default) sectorId; }
	///	
	///	members from sector can be pulled as void*, and then casted to desired type, as long as the information about offsets for every member stored according to their type ids
	///
	///	you can't get member without knowing it's type
	/// </summary>
	///
	class SectorsArray final {
	private:
		SectorsArray(const SectorsArray&) = delete;
		SectorsArray& operator=(SectorsArray&) = delete;
		SectorsArray(uint32_t chunkSize = 10240) : mChunkSize(chunkSize){}
	
	public:
		template <typename... Types>
		static inline constexpr SectorsArray* createSectorsArray(uint32_t capacity = 0, uint32_t chunkSize = 10240) {
			const auto array = new SectorsArray(chunkSize);
			array->fillSectorData<Types...>(capacity);

			return array;
		}

		~SectorsArray();
		
		inline Sector* operator[](size_t i) const {
			return getSectorByIdx(i);
		}

		uint32_t size() const;
		bool empty() const;

		//clear will delete all sectors with members and destroy chunks
		void clear();

		uint32_t capacity() const;
		void reserve(uint32_t newCapacity);
		void shrinkToFit();

		size_t entitiesCapacity() const;

		void* acquireSector(ECSType componentTypeId, SectorId sectorId);

		void destroyMember(ECSType componentTypeId, SectorId sectorId);
		void destroyMembers(ECSType componentTypeId, std::vector<SectorId>& sectorIds, bool sort = true);
		void destroySector(SectorId sectorId);

		inline Sector* tryGetSector(SectorId sectorId) const {
			return sectorId >= mSectorsMap.size() || mSectorsMap[sectorId] == INVALID_ID ? nullptr : getSector(sectorId);
		}

		inline Sector* getSector(SectorId sectorId) const {
			return getSectorByIdx(getSectorIdx(sectorId));
		}

		inline Sector* getSectorByIdx(size_t idx) const {
			return idx / mChunkSize < mChunks.size() ? static_cast<Sector*>(static_cast<void*>(static_cast<char*>(mChunks.at(idx / mChunkSize)) + (idx % mChunkSize) * mSectorMeta.sectorSize)) : nullptr;
		}

		inline SectorId getSectorIdx(SectorId sectorId) const {
			return mSectorsMap[sectorId];
		}

		template<typename T>
		inline T* getComponent(SectorId sectorId) {
			return getComponent<T>(sectorId, getTypeOffset(ReflectionHelper::getTypeId<T>()));
		}

		template<typename T>
		inline T* getComponent(SectorId sectorId, uint16_t offset) {
			auto info = tryGetSector(sectorId);
			return info ? info->getMember<T>(offset) : nullptr;
		}

		template<typename T>
		void insert(SectorId sectorId, T* data) {
			if (!data || !hasType<T>()) {
				assert(false);
				return;
			}

			auto sector = acquireSector(Memory::ReflectionHelper::getTypeId<T>(), sectorId);
			if (!sector) {
				assert(false);
				return;
			}

			new (sector) T(*data);
		}

		template<typename T>
		void move(SectorId sectorId, T* data) {
			if (!data || !hasType<T>()) {
				assert(false);
				return;
			}

			auto sector = acquireSector(Memory::ReflectionHelper::getTypeId<T>(), sectorId);
			if (!sector) {
				assert(false);
				return;
			}

			new (sector) T(std::move(*data));
		}

		template<typename T>
		inline bool hasType() const {
			return hasType(ReflectionHelper::getTypeId<T>());
		}

		inline bool hasType(ECSType typeId) const {
			return mSectorMeta.membersLayout.contains(typeId);
		}

		template<typename T>
		inline uint16_t getTypeOffset() const {
			return getTypeOffset(ReflectionHelper::getTypeId<T>());
		}

		inline uint16_t getTypeOffset(ECSType typeId) const {
			return mSectorMeta.membersLayout.at(typeId);
		}

		inline const SectorMetadata& getSectorData() { return mSectorMeta; }

		void removeEmptySectors();

	private:
		void* initSectorMember(Sector* sector, ECSType componentTypeId) const;

		void incrementCapacity();

		Sector* emplaceSector(size_t pos, SectorId sectorId);
		void destroyMember(Sector* sector, ECSType typeId) const;
		void destroySector(Sector* sector);
		void destroySectors(size_t begin, size_t count = 1);

		void erase(size_t begin, size_t count = 1);

		//shifts chunk data right
		//[][][][from][][][]   -> [][][] [empty] [from][][][]
		void shiftDataRight(size_t from, size_t count = 1);

		//shifts chunk data left
		//[][][][from][][][]   -> [][][from][][][]
		//caution - shifting on alive data will produce memory leak
		void shiftDataLeft(size_t from, size_t count = 1);

		template <typename... Types>
		void fillSectorData(uint32_t capacity) {
			static_assert(types::areUnique<Types...>(), "Duplicates detected in types");

			mSectorMeta.sectorSize = static_cast<uint16_t>((sizeof(Sector) + 8 - 1) / 8 * 8);
			((
				mSectorMeta.membersLayout[Memory::ReflectionHelper::getTypeId<Types>()] = mSectorMeta.sectorSize,
				mSectorMeta.sectorSize += static_cast<uint16_t>(8 + (sizeof(Types) + alignof(Types) - 1) / alignof(Types) * alignof(Types)) //+8 for is alive bool
			)
			, ...);

			mSectorMeta.sectorSize = (mSectorMeta.sectorSize + alignof(Sector) - 1) / alignof(Sector) * alignof(Sector);
			mSectorMeta.membersLayout.shrinkToFit();

			reserve(capacity);
		}

	private:
		std::vector<SectorId> mSectorsMap;
		std::vector<void*> mChunks;//split whole data to chunks to make it more memory fragmentation friendly ( but less memory friendly, whole chunk will be allocated)

		SectorMetadata mSectorMeta;
		uint32_t mSize = 0;
		
		const uint32_t mChunkSize;
	};
}
