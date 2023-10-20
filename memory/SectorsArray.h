#pragma once

#include <cassert>

#include "Sector.h"
#include "Reflection.h"

namespace ecss::Memory {
	
	/**
	 * data container with sectors of custom data in it
	 *
	 * the Sector is object in memory which includes Sector {uint32_t(by default) sectorId;}
	 *
	 * sector stores data for any custom type in theory, offset to type stores in SectorMetadata struct
	 *--------------------------------------------------------------------------------------------
	 *                                          [SECTOR]
	 * 0x 00                                                       {SectorId}
	 * 0x sizeof(Sector)                                           {SomeObject}
	 * 0x sizeof(Sector + SomeObject)                              {SomeObject1}
	 * 0x sizeof(Sector + SomeObject + SomeObject1)                {SomeObject2}
	 * ...
	 * 0x sizeof(Sector... + ...SomeObjectN-1 + SomeObjectN)       {SomeObjectN}
	 *
	 *--------------------------------------------------------------------------------------------
	 *
	 * objects from sector can be pulled as void*, and then casted to desired type, as long as the information about offsets for every object stored according to their type ids
	 * you can't get object without knowing it's type
	 *
	 */
	class SectorsArray final {
	private:
		SectorsArray(const SectorsArray&) = delete;
		SectorsArray& operator=(SectorsArray&) = delete;
		SectorsArray(uint32_t chunkSize = 1024) : mChunkSize(chunkSize){}
	
	public:
		template <typename... Types>
		static inline constexpr SectorsArray* createSectorsArray(uint32_t capacity = 0, uint32_t chunkSize = 1024) {
			const auto array = new SectorsArray(chunkSize);
			array->fillSectorData<Types...>(capacity);

			return array;
		}

		~SectorsArray();
		
		inline Sector* operator[](size_t i) const {
			return mChunks.size() <= i / mChunkSize ? nullptr : static_cast<Sector*>(static_cast<void*>(static_cast<char*>(mChunks[i / mChunkSize]) + (i % mChunkSize) * mSectorMeta.sectorSize));
		}

		uint32_t size() const;
		bool empty() const;
		void clear();

		uint32_t capacity() const;
		void reserve(uint32_t newCapacity);
		void shrinkToFit();

		size_t entitiesCapacity() const;

		void* acquireSector(ECSType componentTypeId, SectorId sectorId);

		void destroyObject(ECSType componentTypeId, SectorId sectorId);
		void destroyObjects(ECSType componentTypeId, std::vector<SectorId> sectorIds);
		void destroySector(SectorId sectorId);

		inline Sector* tryGetSector(SectorId sectorId) const {
			return sectorId >= mSectorsMap.size() || mSectorsMap[sectorId] >= size() ? nullptr : getSector(sectorId);
		}

		inline Sector* getSector(SectorId sectorId) const {
			return (*this)[mSectorsMap[sectorId]];
		}

		template<typename T>
		inline T* getComponent(SectorId sectorId) {
			return getComponent<T>(sectorId, getTypeOffset(ReflectionHelper::getTypeId<T>()));
		}

		template<typename T>
		inline T* getComponent(SectorId sectorId, uint16_t offset) {
			auto info = tryGetSector(sectorId);
			return info ? info->getObject<T>(offset) : nullptr;
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

	private:
		void* initSectorMember(void* sectorPtr, ECSType componentTypeId) const;

		void setCapacity(uint32_t newCap);

		void* createSector(size_t pos, SectorId sectorId);
		void destroyObject(void* sectorPtr, ECSType typeId) const;
		void destroySector(void* sectorPtr) const;

		void erase(size_t pos);
		void erase(size_t from, size_t to);

		//shifts chunk data right
		//[][][][from][][][]   -> [][][] [empty] [from][][][]
		void shiftDataRight(size_t from, size_t offset = 1);

		//shifts chunk data left
		//[][][][from][][][]   -> [][][from][][][]
		//caution - shifting on alive data will produce memory leak
		void shiftDataLeft(size_t from, size_t offset = 1);

		template <typename... Types>
		void fillSectorData(uint32_t capacity) {
			static_assert(types::areUnique<Types...>(), "Duplicates detected in types");

			mSectorMeta.sectorSize = static_cast<uint16_t>(sizeof(Sector));
			((
				mSectorMeta.membersLayout[Memory::ReflectionHelper::getTypeId<Types>()] = mSectorMeta.sectorSize,
				mSectorMeta.sectorSize += static_cast<uint16_t>(sizeof(Types)) + 1 //+1 for is alive bool
			)
			, ...);

			mSectorMeta.membersLayout.shrinkToFit();

			reserve(capacity);
		}

	private:
		std::vector<SectorId> mSectorsMap;
		std::vector<void*> mChunks;//split whole data to chunks to make it more memory fragmentation friendly ( but less memory friendly, whole chunk will be allocated)

		SectorMetadata mSectorMeta;

		uint32_t mSize = 0;
		uint32_t mCapacity = 0;

		const uint32_t mChunkSize = 1024;
	};
}
