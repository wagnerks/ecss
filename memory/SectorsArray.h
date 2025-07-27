#pragma once

#include <algorithm>
#include <cassert>

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
	public:
		class Iterator {
		public:
			using iterator_category = std::forward_iterator_tag;
			using value_type = Sector*;
			using difference_type = std::ptrdiff_t;
			using pointer = value_type*;
			using reference = value_type&;

			Iterator() = default;

			Iterator(const SectorsArray* array, size_t idx)
			: mArray(array)
			, mIdx(idx)
			, mSectorsSize(mArray->size())
			, mChunkSize(mArray->mChunksManager.mChunkSize)
			, sectorSize(mArray->mSectorSize)
			{
				if (mIdx < mSectorsSize) {
					mSector = mArray->at(mIdx);
				}
				else {
					mIdx = mSectorsSize;
				}
			}

			Iterator& operator++() { mIdx++; advance(); return *this; }
			Iterator operator++(int) { Iterator tmp = *this; ++(*this); return tmp; }

			bool operator==(const Iterator& other) const { return mSector == other.mSector; }
			bool operator!=(const Iterator& other) const { return !(*this == other); }

			Sector* operator*() const { return mSector; }
			Sector* operator->() const { return mSector; }

			bool isValid() const { return mSector; }

			void advanceToAlive() {
				while (mIdx < mSectorsSize) {
					if (mSector->isSectorAlive()) {
						return;
					}
					++mIdx;
					advance();
				}
				mSector = nullptr;
			}

		private:

			void advance(){
				mSector = mIdx % mChunkSize && mSector ? reinterpret_cast<Sector*>(reinterpret_cast<char*>(mSector) + sectorSize) : mArray->at(mIdx);
			}

			const SectorsArray* mArray;
			size_t mIdx;
			size_t mSectorsSize;
			Sector* mSector = nullptr;
			size_t mChunkSize = 0;
			uint16_t sectorSize = 0;
		};

		Iterator begin() const { return {this, 0}; }
		Iterator end() const { return {this, size()}; }

	public:
		SectorsArray(const SectorsArray& other) noexcept : mChunksManager(other.mChunksManager.mChunkSize) {
			mSectorSize = other.mSectorSize;
			reserve(other.mSize);

			mSectorsMap = other.mSectorsMap;
			mSize = other.mSize;
			mLayoutMap = other.mLayoutMap;
			mSectorLayout = other.mSectorLayout;
			

			for (auto i = 0u; i < other.mSize; i++) {
				Sector::copySector(other.at(i), at(i), mSectorLayout);
			}
		}

		SectorsArray(SectorsArray&& other) noexcept : mChunksManager(other.mChunksManager.mChunkSize) {
			mSectorSize = other.mSectorSize;
			reserve(other.mSize);

			mSectorsMap = std::move(other.mSectorsMap);
			mSize = other.mSize;
			mLayoutMap = other.mLayoutMap;
			mSectorLayout = other.mSectorLayout;
			

			for (auto i = 0u; i < other.mSize; i++) {
				Sector::moveSector(other.at(i), at(i), mSectorLayout);
			}
		}

		~SectorsArray() { clear(); }

		SectorsArray& operator=(const SectorsArray& other) noexcept {
			const bool isSame = other.mSectorLayout == mSectorLayout;
			if (!isSame || this == &other) {
				assert(isSame && "wrong source sectors array type");
				return *this;
			}

			erase(other.mSize, mSize - other.mSize);
			mSectorSize = other.mSectorSize;
			reserve(other.mSize);

			mSectorsMap = other.mSectorsMap;
			mSize = other.mSize;
			mLayoutMap = other.mLayoutMap;
			mSectorLayout = other.mSectorLayout;
			mChunksManager.mChunkSize = other.mChunksManager.mChunkSize;

			for (auto i = 0u; i < other.mSize; i++) {
				Sector::copySector(other.at(i), at(i), mSectorLayout);
			}

			return *this;
		}

		SectorsArray& operator=(SectorsArray&& other) noexcept {
			const bool isSame = other.mSectorLayout == mSectorLayout;
			if (!isSame || this == &other) {
				assert(isSame && "wrong source sectors array type");
				return *this;
			}

			erase(other.mSize, mSize - other.mSize);
			mSectorSize = other.mSectorSize;
			reserve(other.mSize);

			mSectorsMap = std::move(other.mSectorsMap);
			mSize = other.mSize;
			mLayoutMap = other.mLayoutMap;
			mSectorLayout = other.mSectorLayout;
			mChunksManager.mChunkSize = other.mChunksManager.mChunkSize;

			for (auto i = 0u; i < other.mSize; i++) {
				Sector::moveSector(other.at(i), at(i), mSectorLayout);
			}

			return *this;
		}

	private:
		SectorsArray(uint32_t chunkSize = 10240) : mChunksManager(chunkSize){}

		template <typename... Types>
		void fillSectorData(ReflectionHelper& reflectionHelper, uint32_t capacity) {
			static_assert(types::areUnique<Types...>(), "Duplicates detected in types");
			static SectorLayoutMeta<Types...> layoutMeta;
			mSectorLayout = &layoutMeta;

			mSectorSize = mSectorLayout->getTotalSize();
			mLayoutMap.reserve(sizeof...(Types));
			((mLayoutMap[reflectionHelper.getTypeId<Types>()] = mSectorLayout->getLayoutData<Types>().index), ...);

			reserve(capacity);
		}

	public:
		template <typename... Types>
		static inline constexpr SectorsArray* createSectorsArray(ReflectionHelper& reflectionHelper, uint32_t capacity = 0, uint32_t chunkSize = 10240) {
			const auto array = new SectorsArray(chunkSize);
			array->fillSectorData<Types...>(reflectionHelper, capacity);

			return array;
		}

	public: // sector helpers
		ISectorLayoutMeta* getSectorLayout() const { return mSectorLayout; }
		template<typename T>
		const LayoutData& getLayoutData() const { return mSectorLayout->getLayoutData<T>(); }
		const LayoutData& getLayoutData(ECSType typeId) const {	return mSectorLayout->getLayoutData(mLayoutMap.at(typeId)); }

		Sector* findSector(SectorId sectorId) const { return containsSector(sectorId) ? at(getSectorIndex(sectorId)) : nullptr;	}
		Sector* getSector(SectorId sectorId) const { return at(getSectorIndex(sectorId)); }
		Sector* acquireSector(SectorId sectorId) {
			auto sector = findSector(sectorId);
			return sector ? sector : emplaceSector(sectorId);
		}

		size_t sectorsCapacity() const { return mSectorsMap.size(); }
		SectorId getSectorIndex(SectorId sectorId) const { return mSectorsMap.at(sectorId); }
		void setSectorIndex(SectorId sectorId, SectorId newPos) { mSectorsMap[sectorId] = newPos; }
		bool containsSector(SectorId id) const { return id < sectorsCapacity() && getSectorIndex(id) < size(); }
		const std::vector<SectorId>& getSectorsMap() const { return mSectorsMap; }

	public: // container methods
		// operator[] and at() don't make any bounding checks, if you want 
		Sector* operator[](size_t i) const { return at(i); }
		Sector* at(size_t sectorIndex) const { return mChunksManager.getSector(sectorIndex, mSectorSize); }

		uint32_t capacity() const { return mChunksManager.capacity();  }
		uint32_t size() const { return mSize; }
		bool empty() const { return !size(); }
		//todo clear whole data in O(1)	
		void clear() { erase(0, size()); mSectorsMap.clear(); }

		void reserve(uint32_t newCapacity) {
			if (newCapacity > capacity()) {
				auto newChunksCount = (newCapacity + mChunksManager.mChunkSize - 1) / mChunksManager.mChunkSize;
				mChunksManager.allocateChunks(mSectorSize, newChunksCount);
				if (capacity() > sectorsCapacity()) {
					mSectorsMap.resize(capacity(), INVALID_ID);
				}
			}
		}

		void shrinkToFit() { mChunksManager.shrinkToFit(size()); }

		template<typename T>
		T* insert(SectorId sectorId, const T& data) {
			return Sector::copyMember(static_cast<const void*>(&data), acquireSector(sectorId), getLayoutData<T>());
		}

		template<typename T>
		T* insert(SectorId sectorId, T&& data) {
			return Sector::moveMember(static_cast<void*>(&std::forward<T>(data)), acquireSector(sectorId), getLayoutData<T>());
		}

		Sector* insert(Sector* sector) {
			if (!sector) {
				return sector;
			}

			auto sectorId = sector->id;
			if (auto found = findSector(sectorId)) {
				return Sector::moveSector(sector, found, mSectorLayout);
			}

			if (sectorId >= sectorsCapacity()) {
				mSectorsMap.resize(sectorId + 1, INVALID_ID);
			}

			reserve(size() + 1);

			auto pos = findInsertPosition(sectorId);
			if (pos < size()) {
				++mSize;
				shiftDataRight(pos);
			}
			else {
				++mSize;
			}

			setSectorIndex(sectorId, static_cast<SectorId>(pos));
			return Sector::moveSector(sector, at(pos), mSectorLayout);
		}

		// Inserts a sector at position 'pos' with a given sectorId.
		// If a sector with that id exists, destroys it first.
		// Shifts data right if needed. Ensures mapping is updated.
		// Returns pointer to the newly constructed sector.
		Sector* emplaceSector(SectorId sectorId) {
			if (auto found = findSector(sectorId)) {
				Sector::destroySector(found, mSectorLayout);
				return new (found)Sector(sectorId);
			}

			if (sectorId >= sectorsCapacity()) {
				mSectorsMap.resize(sectorId + 1, INVALID_ID);
			}

			reserve(size() + 1);

			auto pos = findInsertPosition(sectorId);
			if (pos < size()) {
				++mSize;
				shiftDataRight(pos);
			}
			else {
				++mSize;
			}

			setSectorIndex(sectorId, static_cast<SectorId>(pos));
			return new (at(pos))Sector(sectorId);
		}

		void erase(size_t begin, size_t count = 1) {
			count = std::min(size() - begin, count);
			if (count == 0 || begin >= size()) {
				return;
			}

			for (auto i = begin; i < begin + count; i++) {
				auto sector = at(i);
				setSectorIndex(sector->id, INVALID_ID);
				Sector::destroySector(sector, mSectorLayout);
			}

			shiftDataLeft(begin, count);
			mSize -= static_cast<uint32_t>(count);

			shrinkToFit();
		}

		void defragmentSectors() {
			if (empty()) {
				return;
			}
			//algorithm which will not shift all sectors left every time, but shift only alive sectors to left border till not found empty place
			//OOOOxOxxxOOxxxxOOxOOOO   0 - start
			//OOOOx<-OxxxOOxxxxOOxOOOO 0
			//OOOOOxxxxOOxxxxOOxOOOO   1
			//OOOOOxxxx<-OOxxxxOOxOOOO 1
			//OOOOOOOxxxxxxxxOOxOOOO   2
			//OOOOOOOxxxxxxxx<-OOxOOOO 2
			//OOOOOOOOOxxxxxxxxxOOOO   3
			//OOOOOOOOOxxxxxxxxx<-OOOO 3
			//OOOOOOOOOOOOOxxxxxxxxx   4 - end

			uint32_t deleted = 0;
			size_t emptyPos = 0;
			for (auto i = 0u; i < size(); i++) {
				auto sector = at(i);
				if (!sector->isSectorAlive()) {
					setSectorIndex(sector->id, INVALID_ID);
					Sector::destroySector(sector, mSectorLayout);
					deleted++;
				}
				else {
					if (emptyPos == i) {
						emptyPos++;
						continue;
					}

					auto newSector = Sector::moveSector(sector, at(emptyPos), mSectorLayout);
					setSectorIndex(newSector->id, static_cast<SectorId>(emptyPos++));
				}
			}

			mSize -= deleted;
			shrinkToFit();
		}

	private:
		struct ChunksManager {
			ChunksManager(uint32_t chunkSize) : mChunkSize(chunkSize){}

			void allocateChunks(uint16_t sectorSize, size_t count = 1) {
				if (count == 0) {
					return;
				}

				mChunks.reserve(mChunks.size() + count);
				for (auto i = 0u; i < count; i++) {
					mChunks.emplace_back(calloc(mChunkSize, sectorSize));
				}
			}

			void shrinkToFit(size_t sectorsSize) {
				size_t last = (sectorsSize + mChunkSize - 1) / mChunkSize;
				const auto size = mChunks.size();
				for (auto i = last; i < size; i++) {
					std::free(mChunks.at(i));
				}
				mChunks.erase(mChunks.begin() + static_cast<int64_t>(last), mChunks.end());
				mChunks.shrink_to_fit();
			}

			uint32_t capacity() const { return mChunkSize * static_cast<uint32_t>(mChunks.size()); }
			size_t calcInChunkIndex(size_t sectorIdx, uint16_t sectorSize) const { return (sectorIdx % mChunkSize) * sectorSize; }
			size_t calcChunkIndex(size_t sectorIdx) const { return sectorIdx / mChunkSize; }
			
			Sector* getSector(size_t sectorIndex, uint16_t sectorSize) const {
				return reinterpret_cast<Sector*>(static_cast<char*>(getChunk(calcChunkIndex(sectorIndex))) + calcInChunkIndex(sectorIndex, sectorSize));
			}

			void* getChunk(size_t index) const { return mChunks[index]; }

		public:
			std::vector<void*> mChunks;
			uint32_t mChunkSize = 0;
		};

		//shifts chunk data right
		//[][][][from][][][]   -> [][][] [empty] [from][][][]
		void shiftDataRight(size_t from, size_t count = 1) {
			if (count == 0) {
				return;
			}
			for (size_t i = size(); i-- > from + count;) {
				auto sector = Sector::moveSector(at(i - count), at(i), mSectorLayout);
				setSectorIndex(sector->id, static_cast<SectorId>(i));
			}
		}

		//shifts chunk data left
		//[][][][from][][][]   -> [][][from][][][]
		void shiftDataLeft(size_t from, size_t count = 1) {
			if (count == 0) {
				return;
			}
			for (auto i = from; i < size() - count; i++) {
				auto sector = Sector::moveSector(at(i + count), at(i), mSectorLayout);
				setSectorIndex(sector->id, static_cast<SectorId>(i));
			}
		}

		//fast binary search to find sector insert position
		__forceinline size_t findInsertPosition(SectorId sectorId) const {
			size_t right = mSize;
			if (right == 0 || at(0)->id > sectorId) {
				return 0;
			}
			if (at(right - 1)->id < sectorId) {
				return right;
			}
			size_t left = 0;
			while (right - left > 1) {
				size_t mid = left + (right - left) / 2;
				auto mid_id = at(mid)->id;
				if (mid_id < sectorId) {
					left = mid;
				}
				else {
					right = mid;
				}
			}
			
			return at(left)->id == sectorId ? left : right;
		}

	private:
		std::vector<SectorId> mSectorsMap;
		ChunksManager mChunksManager; //split whole data to chunks to make it more memory fragmentation friendly ( but less memory friendly, whole chunk will be allocated)
		
		
		ContiguousMap<ECSType, uint8_t> mLayoutMap;
		ISectorLayoutMeta* mSectorLayout = nullptr;

		uint32_t mSize = 0;
		uint16_t mSectorSize = 0;
	};


	namespace SectorArrayUtils
	{
		inline SectorId findRightNearestSectorIndex(const SectorsArray& array, SectorId sectorId) {
			while (sectorId < array.sectorsCapacity()) {
				SectorId index = array.getSectorIndex(sectorId++);
				if (index < array.size()) {
					return index;
				}
			}

			return array.size();
		}

		inline void destroyMember(const SectorsArray& array, ECSType componentTypeId, SectorId sectorId) {
			const auto sector = array.findSector(sectorId);
			if (!sector) {
				return;
			}

			sector->destroyMember(array.getLayoutData(componentTypeId));
		}

		template<typename T>
		inline void destroyMember(const SectorsArray& array, SectorId sectorId) {
			const auto sector = array.findSector(sectorId);
			if (!sector) {
				return;
			}

			sector->destroyMember(array.getLayoutData<T>());
		}

		template<typename T>
		inline void destroyMembers(const SectorsArray& array, std::vector<SectorId>& sectorIds, bool sort = true) {
			if (sectorIds.empty()) {
				return;
			}

			if (sort) {
				std::ranges::sort(sectorIds);
			}

			const auto& layout = array.getLayoutData<T>();
			const auto sectorsSize = array.sectorsCapacity();
			for (const auto sectorId : sectorIds) {
				if (sectorId >= sectorsSize) {
					break; //all valid entities destroyed
				}

				if (auto sector = array.findSector(sectorId)) {
					sector->destroyMember(layout);
				}

			}
		}

		inline void destroyMembers(const SectorsArray& array, ECSType componentTypeId, std::vector<SectorId>& sectorIds, bool sort = true) {
			if (sectorIds.empty()) {
				return;
			}

			if (sort) {
				std::ranges::sort(sectorIds);
			}

			const auto& layout = array.getLayoutData(componentTypeId);
			const auto sectorsSize = array.sectorsCapacity();
			for (const auto sectorId : sectorIds) {
				if (sectorId >= sectorsSize) {
					break; //all valid entities destroyed
				}

				if (auto sector = array.findSector(sectorId)) {
					sector->destroyMember(layout);
				}

			}
		}
	}
}
