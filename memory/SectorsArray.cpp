#include "SectorsArray.h"

#include "BinarySearch.h"

#include <algorithm>
#include <stdio.h>
#include <stdlib.h>

namespace ecss::Memory {
	SectorsArray::~SectorsArray() {
		if (mChunks.empty()) {
			return;
		}

		clear();

		for (auto data : mChunks) {
			std::free(data);
		}
		mChunks.clear();
	}

	uint32_t SectorsArray::size() const {
		return mSize;
	}

	bool SectorsArray::empty() const {
		return !size();
	}

	void SectorsArray::clear() {
		if (!mSize) {
			return;
		}

		destroySectors(0, size());

		mSize = 0;
		mSectorsMap.clear();
		mSectorsMap.resize(mCapacity, INVALID_ID);
	}

	uint32_t SectorsArray::capacity() const {
		return mCapacity;
	}

	size_t SectorsArray::entitiesCapacity() const {
		return mSectorsMap.size();
	}

	void SectorsArray::reserve(uint32_t newCapacity) {
		if (newCapacity <= mCapacity) {
			return;
		}

		const auto diff = newCapacity - mCapacity;
		for (auto i = 0u; i <= diff / mChunkSize; i++) {
			setCapacity(static_cast<uint32_t>(mCapacity + mChunkSize));
		}
	}

	void SectorsArray::shrinkToFit() {
		auto last = static_cast<uint32_t>(std::ceil(size() / static_cast<float>(mChunkSize)));
		for (auto i = last; i < mChunks.size(); i++) {
			std::free(mChunks[i]);
		}
		mChunks.resize(last);
		mChunks.shrink_to_fit();
		mCapacity = static_cast<uint32_t>(mChunks.size() * mChunkSize);
	}

	void SectorsArray::setCapacity(uint32_t newCap) {
		if (mCapacity == newCap) {
			return;
		}

		mCapacity = newCap;

		mChunks.push_back(calloc(mChunkSize, mSectorMeta.sectorSize));

		if (mCapacity > mSectorsMap.size()) {
			mSectorsMap.resize(mCapacity, INVALID_ID);
		}
	}

	void SectorsArray::erase(size_t begin, size_t count) {
		if (count <= 0) {
			return;
		}

		for (auto i = begin; i < begin + count; i++) {
			const auto sectorInfo = (*this)[i];
			mSectorsMap[sectorInfo->id] = INVALID_ID;
		}

		shiftDataLeft(begin, count);
		mSize -= static_cast<uint32_t>(count);

		shrinkToFit();
	}

	void* SectorsArray::initSectorMember(Sector* sector, const ECSType componentTypeId) const {
		destroyObject(sector, componentTypeId);
		
		sector->setAlive(getTypeOffset(componentTypeId), true);
		return sector->getObjectPtr(getTypeOffset(componentTypeId));
	}

	Sector* SectorsArray::emplaceSector(size_t pos, const SectorId sectorId) {
		if (pos < size()) {
			shiftDataRight(pos);
		}

		++mSize;
		
		const auto sector = new ((*this)[pos])Sector(sectorId, mSectorMeta.membersLayout);
		mSectorsMap[sectorId] = static_cast<SectorId>(pos);

		return sector;
	}

	void* SectorsArray::acquireSector(const ECSType componentTypeId, const SectorId sectorId) {
		if (size() >= mCapacity) {
			setCapacity(static_cast<uint32_t>(mCapacity + mChunkSize));
		}

		if (mSectorsMap.size() <= sectorId) {
			mSectorsMap.resize(sectorId + 1, INVALID_ID);
		}
		else {
			if (mSectorsMap[sectorId] < size()) {
				return initSectorMember(getSector(sectorId), componentTypeId);
			}
		}

		size_t idx = 0;
		Utils::binarySearch(sectorId, idx, this); //find the place where to insert sector

		return initSectorMember(emplaceSector(idx, sectorId), componentTypeId);
	}

	void SectorsArray::destroyObject(const ECSType componentTypeId, const SectorId sectorId) {
		if (mSectorsMap[sectorId] >= size()) {
			return;
		}

		const auto sector = getSector(sectorId);
		
		destroyObject(sector, componentTypeId);

		if (!sector->isSectorAlive(mSectorMeta.membersLayout)) {
			destroySector(sector);
		}
	}

	void SectorsArray::destroyObject(Sector* sector, ECSType typeId) const {
		if (!sector->isAlive(getTypeOffset(typeId))) {
			return;
		}

		sector->setAlive(getTypeOffset(typeId), false);

		ReflectionHelper::functionTables[typeId].destructor(sector->getObjectPtr(getTypeOffset(typeId)));
	}

	void SectorsArray::destroyObjects(ECSType componentTypeId, std::vector<SectorId> sectorIds) {
		if (sectorIds.empty()) {
			return;
		}

		std::sort(sectorIds.begin(), sectorIds.end());
		if (sectorIds.front() >= size()) {
			return;
		}

		auto lastPos = mSectorsMap[sectorIds.front()];
		auto curPos = mSectorsMap[sectorIds.front()];
		auto destroy = [&]() {
			destroySectors(lastPos, curPos - lastPos);
			lastPos = curPos;
		};

		for (auto i = 0u; i < sectorIds.size(); i++) {
			const auto sectorId = sectorIds[i];
			if (sectorId == INVALID_ID) {
				break; //all valid entities destroyed
			}

			if (mSectorsMap[sectorId] >= size()) {
				continue;//there is no such entity in container
			}

			const auto sector = getSector(sectorId);
			destroyObject(sector, componentTypeId);
			
			if (!sector->isSectorAlive(mSectorMeta.membersLayout)) {
				curPos = mSectorsMap[sectorId];

				const bool isLast = i == sectorIds.size() - 1;
				if (isLast || ((sectorIds[i + 1] - sectorId) > 1)) {
					destroy();
				}
				//continue iterations till not found some nod dead sector
				continue;
			}

			destroy();
		}
	}

	void SectorsArray::destroySectors(size_t begin, size_t count) {
		if (begin >= size() || count <= 0) {
			return;
		}

		count = std::min(size() - begin, count);

		for (auto i = begin; i < begin + count; i++) {
			const auto sector = (*this)[i];
			for (auto& [typeId, typeIdx] : mSectorMeta.membersLayout) {
				destroyObject(sector, typeId);
			}
			sector->~Sector();
		}

		erase(begin, count);
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
			destroyObject(sector, typeId);
		}

		sector->~Sector();
		erase(mSectorsMap[sector->id]);
	}

	void SectorsArray::shiftDataRight(size_t from, size_t count) {
		for (auto i = size() - count; i >= from; i--) {
			auto prevAdr = (*this)[i];
			auto newAdr = (*this)[i + count];

			for (auto& [typeId, offset] : mSectorMeta.membersLayout) {
				if (!prevAdr->isAlive(offset)) {
					newAdr->setAlive(offset, false);
					continue;
				}
				
				const auto oldPlace = prevAdr->getObjectPtr(offset);
				const auto newPlace = newAdr->getObjectPtr(offset);
				ReflectionHelper::functionTables[typeId].move(newPlace, oldPlace);

				newAdr->setAlive(offset, true);
			}

			new (newAdr)Sector(std::move(*prevAdr));
			mSectorsMap[newAdr->id] = static_cast<SectorId>(i + count);

			if (i == 0) {
				break;
			}
		}
	}

	void SectorsArray::shiftDataLeft(size_t from, size_t count) {
		for (auto i = from; i < size() - count; i++) {
			auto newAdr = (*this)[i];
			auto prevAdr = (*this)[i + count];

			for (auto& [typeId, offset] : mSectorMeta.membersLayout) {
				if (!prevAdr->isAlive(offset)) {
					newAdr->setAlive(offset, false);
					continue;
				}

				const auto oldPlace = prevAdr->getObjectPtr(offset);
				const auto newPlace = newAdr->getObjectPtr(offset);
				ReflectionHelper::functionTables[typeId].move(newPlace, oldPlace);
				
				newAdr->setAlive(offset, true);
			}

			new (newAdr)Sector(std::move(*prevAdr));
			mSectorsMap[newAdr->id] = static_cast<SectorId>(i);
		}
	}

}
