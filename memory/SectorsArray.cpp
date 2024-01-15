#include "SectorsArray.h"

#include "BinarySearch.h"

#include <algorithm>
#include <stdio.h>
#include <stdlib.h>

namespace ecss::Memory {
	SectorsArray::~SectorsArray() {
		clear();
	}

	uint32_t SectorsArray::size() const {
		return mSize;
	}

	bool SectorsArray::empty() const {
		return !size();
	}

	void SectorsArray::clear() {
		destroySectors(0, size());

		mSectorsMap.clear();
	}

	uint32_t SectorsArray::capacity() const {
		return mChunkSize * mChunks.size();
	}

	size_t SectorsArray::entitiesCapacity() const {
		return mSectorsMap.size();
	}

	void SectorsArray::reserve(uint32_t newCapacity) {
		if (newCapacity <= capacity()) {
			return;
		}
		const auto diff = newCapacity - capacity();
		for (auto i = 0u; i <= diff / mChunkSize; i++) {
			incrementCapacity();
		}
	}

	void SectorsArray::shrinkToFit() {
		auto last = static_cast<uint32_t>(std::ceil(size() / static_cast<float>(mChunkSize)));
		const auto size = mChunks.size();
		for (auto i = last; i < size; i++) {
			std::free(mChunks.at(i));
		}
		mChunks.erase(mChunks.begin() + last, mChunks.end());
		mChunks.shrink_to_fit();
	}

	void SectorsArray::incrementCapacity() {
		mChunks.emplace_back(calloc(mChunkSize, mSectorMeta.sectorSize));
		mChunks.shrink_to_fit();
		if (capacity() > entitiesCapacity()) {
			mSectorsMap.resize(capacity(), INVALID_ID);
		}
	}

	void SectorsArray::erase(size_t begin, size_t count) {
		if (count <= 0) {
			return;
		}

		for (auto i = begin; i < begin + count; i++) {
			const auto sectorInfo = getSectorByIdx(i);
			mSectorsMap[sectorInfo->id] = INVALID_ID;
		}

		shiftDataLeft(begin, count);
		mSize -= static_cast<uint32_t>(count);

		shrinkToFit();
	}

	void* SectorsArray::initSectorMember(Sector* sector, const ECSType componentTypeId) const {
		destroyMember(sector, componentTypeId);
		
		sector->setAlive(getTypeOffset(componentTypeId), true);
		return sector->getMemberPtr(getTypeOffset(componentTypeId));
	}

	Sector* SectorsArray::emplaceSector(size_t pos, const SectorId sectorId) {
		if (pos < size()) {
			++mSize;
			shiftDataRight(pos);
		}
		else {
			++mSize;
		}
		

		const auto sector = new (getSectorByIdx(pos))Sector(sectorId, mSectorMeta.membersLayout);
		mSectorsMap[sectorId] = static_cast<SectorId>(pos);

		return sector;
	}

	void* SectorsArray::acquireSector(const ECSType componentTypeId, const SectorId sectorId) {
		if (size() >= capacity()) {
			incrementCapacity();
		}

		if (entitiesCapacity() <= sectorId) {
			mSectorsMap.resize(sectorId + 1, INVALID_ID);
		}
		else {
			if (getSectorIdx(sectorId) < size()) {
				return initSectorMember(getSector(sectorId), componentTypeId);
			}
		}

		
		size_t idx = 0;
		Utils::binarySearch(sectorId, idx, this); //find the place where to insert sector

		return initSectorMember(emplaceSector(idx, sectorId), componentTypeId);
	}

	void SectorsArray::destroyMember(const ECSType componentTypeId, const SectorId sectorId) {
		if (getSectorIdx(sectorId) >= size()) {
			return;
		}

		const auto sector = getSector(sectorId);
		
		destroyMember(sector, componentTypeId);

		if (!sector->isSectorAlive(mSectorMeta.membersLayout)) {
			destroySector(sector);
		}
	}

	void SectorsArray::destroyMember(Sector* sector, ECSType typeId) const {
		if (!sector->isAlive(getTypeOffset(typeId))) {
			return;
		}

		sector->setAlive(getTypeOffset(typeId), false);

		ReflectionHelper::functionsTable[typeId].destructor(sector->getMemberPtr(getTypeOffset(typeId)));
	}

	void SectorsArray::destroyMembers(ECSType componentTypeId, std::vector<SectorId>& sectorIds, bool sort) {
		if (sectorIds.empty()) {
			return;
		}

		if (sort) {
			std::sort(sectorIds.begin(), sectorIds.end());
		}

		const auto sectorsSize = entitiesCapacity();
		for (const auto sectorId : sectorIds) {
			if (sectorId >= sectorsSize) {
				break; //all valid entities destroyed
			}

			const auto idx = getSectorIdx(sectorId);
			if (idx >= size()) {
				continue;//there is no such entity in container
			}
			
			destroyMember(getSectorByIdx(idx), componentTypeId);
		}
	}

	void SectorsArray::destroySectors(size_t begin, size_t count) {
		if (count <= 0 || begin >= size()) {
			return;
		}

		count = std::min(size() - begin, count);

		for (auto i = begin; i < begin + count; i++) {
			const auto sector = getSectorByIdx(i);
			for (auto& [typeId, typeIdx] : mSectorMeta.membersLayout) {
				destroyMember(sector, typeId);
			}
			sector->~Sector();
		}

		erase(begin, count);
	}

	void SectorsArray::removeEmptySectors() {
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
			auto sector = getSectorByIdx(i);
			if (!sector->isSectorAlive(mSectorMeta.membersLayout)) {
				mSectorsMap[sector->id] = INVALID_ID;
				sector->~Sector();
				deleted++;
			}
			else {
				if (emptyPos == i) {
					emptyPos++;
					continue;
				}
				//move
				auto emptyPlace = getSectorByIdx(emptyPos);
				for (auto& [typeId, offset] : mSectorMeta.membersLayout) {
					if (!sector->isAlive(offset)) {
						emptyPlace->setAlive(offset, false);
						continue;
					}

					ReflectionHelper::functionsTable[typeId].move(emptyPlace->getMemberPtr(offset), sector->getMemberPtr(offset));

					emptyPlace->setAlive(offset, true);
				}

				new (emptyPlace)Sector(std::move(*sector));
				mSectorsMap[emptyPlace->id] = static_cast<SectorId>(emptyPos++);
			}
		}

		mSize -= deleted;
		shrinkToFit();
	}

	void SectorsArray::destroySector(const SectorId sectorId) {
		const auto sector = tryGetSector(sectorId);
		if (!sector) {
			return;
		}

		destroySector(sector);
	}

	void SectorsArray::destroySector(Sector* sector) {
		for (auto& [typeId, offset] : mSectorMeta.membersLayout) {
			destroyMember(sector, typeId);
		}

		sector->~Sector();
		erase(mSectorsMap[sector->id]);
	}

	void SectorsArray::shiftDataRight(size_t from, size_t count) {
		for (auto i = size() - 1; i >= from + count; i--) {
			auto prevAdr = getSectorByIdx(i - count);
			auto newAdr = getSectorByIdx(i);


			for (auto& [typeId, offset] : mSectorMeta.membersLayout) {
				if (!prevAdr->isAlive(offset)) {
					newAdr->setAlive(offset, false);
					continue;
				}
				
				const auto oldPlace = prevAdr->getMemberPtr(offset);
				const auto newPlace = newAdr->getMemberPtr(offset);
				ReflectionHelper::functionsTable[typeId].move(newPlace, oldPlace);

				newAdr->setAlive(offset, true);
			}

			new (newAdr)Sector(std::move(*prevAdr));
			mSectorsMap[newAdr->id] = static_cast<SectorId>(i);
		}
	}

	void SectorsArray::shiftDataLeft(size_t from, size_t count) {
		for (auto i = from; i < size() - count; i++) {
			auto newAdr = getSectorByIdx(i);
			auto prevAdr = getSectorByIdx(i + count);

			for (auto& [typeId, offset] : mSectorMeta.membersLayout) {
				if (!prevAdr->isAlive(offset)) {
					newAdr->setAlive(offset, false);
					continue;
				}

				const auto oldPlace = prevAdr->getMemberPtr(offset);
				const auto newPlace = newAdr->getMemberPtr(offset);
				ReflectionHelper::functionsTable[typeId].move(newPlace, oldPlace);
				
				newAdr->setAlive(offset, true);
			}

			new (newAdr)Sector(std::move(*prevAdr));
			mSectorsMap[newAdr->id] = static_cast<SectorId>(i);
		}
	}

}
