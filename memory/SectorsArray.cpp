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

		for (SectorId i = 0; i < size(); i++) {
			destroySector((*this)[i]);
		}

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
		auto last = size() / mChunkSize;
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

	void SectorsArray::erase(size_t pos) {
		const auto sectorInfo = (*this)[pos];
		mSectorsMap[sectorInfo->id] = INVALID_ID;

		shiftDataLeft(pos);
		--mSize;

		shrinkToFit();
	}

	void SectorsArray::erase(size_t from, size_t to) {
		if (from == to) {
			return;
		}
		shiftDataLeft(from, to - from);
		mSize -= static_cast<uint32_t>(to - from);

		shrinkToFit();
	}

	void* SectorsArray::initSectorMember(void* sectorPtr, const ECSType componentTypeId) const {
		const auto sectorInfo = static_cast<Sector*>(sectorPtr);
		destroyObject(sectorPtr, componentTypeId);
		
		sectorInfo->setAlive(getTypeOffset(componentTypeId), true);
		return sectorInfo->getObjectPtr(getTypeOffset(componentTypeId));
	}

	void* SectorsArray::createSector(size_t pos, const SectorId sectorId) {
		const auto sectorAdr = (*this)[pos];

		if (pos < size()) {
			shiftDataRight(pos);
		}

		++mSize;
		sectorAdr->id = sectorId;
		for (auto& [typeId, offset] : mSectorMeta.membersLayout) {
			sectorAdr->setAlive(offset, false);
		}

		mSectorsMap[sectorAdr->id] = static_cast<SectorId>(pos);

		return sectorAdr;
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

		return initSectorMember(createSector(idx, sectorId), componentTypeId);
	}

	void SectorsArray::destroyObject(const ECSType componentTypeId, const SectorId sectorId) {
		if (mSectorsMap[sectorId] >= size()) {
			return;
		}

		const auto sectorInfo = getSector(sectorId);
		
		destroyObject(sectorInfo, componentTypeId);

		if (!sectorInfo->isSectorAlive(mSectorMeta.membersLayout)) {
			size_t pos = 0;
			Utils::binarySearch(sectorId, pos, this);
			erase(pos);
		}
	}

	void SectorsArray::destroyObjects(ECSType componentTypeId, std::vector<SectorId> sectorIds) {
		if (sectorIds.empty()) {
			return;
		}

		std::sort(sectorIds.begin(), sectorIds.end());
		if (sectorIds.front() >= size()) {
			return;
		}

		auto prevPos = mSectorsMap[sectorIds.front()];
		auto lastPos = mSectorsMap[sectorIds.front()];

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
				lastPos = mSectorsMap[sectorId];
				mSectorsMap[sectorId] = INVALID_ID;

				const bool isLast = i == sectorIds.size() - 1;
				if (isLast || ((sectorIds[i + 1] - sectorId) > 1)) {
					erase(prevPos, lastPos);
					prevPos = lastPos;
				}
				//continue iterations till not found some nod dead sector
				continue;
			}

			erase(prevPos, lastPos);//todo not sure this part works correctly
			prevPos = lastPos;
		}
	}

	void SectorsArray::destroyObject(void* sectorPtr, ECSType typeId) const {
		const auto sectorInfo = static_cast<Sector*>(sectorPtr);
		if (!sectorInfo->isAlive(getTypeOffset(typeId))) {
			return;
		}

		sectorInfo->setAlive(getTypeOffset(typeId),false);
		
		ReflectionHelper::functionsTable[typeId].destructor(sectorInfo->getObjectPtr(getTypeOffset(typeId)));
	}

	void SectorsArray::destroySector(void* sectorPtr) const {
		for (auto& [typeId, typeIdx] : mSectorMeta.membersLayout) {
			destroyObject(sectorPtr, typeId);
		}
	}

	void SectorsArray::destroySector(const SectorId sectorId) {
		auto sector = tryGetSector(sectorId);
		if (!sector) {
			return;
		}

		destroySector(sector);

		size_t pos = 0;
		Utils::binarySearch(sectorId, pos, this);
		erase(pos);
	}
	
	void SectorsArray::shiftDataRight(size_t from, size_t offset) {
		for (auto i = size() - offset; i >= from; i--) {
			auto prevAdr = (*this)[i];
			auto newAdr = (*this)[i + 1];

			for (auto& [typeId, offset] : mSectorMeta.membersLayout) {
				if (!prevAdr->isAlive(offset)) {
					newAdr->setAlive(offset, false);
					continue;
				}
				
				const auto oldPlace = prevAdr->getObjectPtr(offset);
				const auto newPlace = newAdr->getObjectPtr(offset);
				ReflectionHelper::functionsTable[typeId].move(newPlace, oldPlace);//call move constructor

				newAdr->setAlive(offset, true);
			}

			//move data one sector right to empty place for new sector
			new (newAdr)Sector(std::move(*prevAdr));//call move constructor for sector info
			mSectorsMap[newAdr->id] = static_cast<SectorId>(i + 1);

			if (i == 0) {
				break;
			}
		}
	}

	void SectorsArray::shiftDataLeft(size_t from, size_t offset) {
		for (auto i = from; i < size() - offset; i++) {
			auto newAdr = (*this)[i];
			auto prevAdr = (*this)[i + offset];

			for (auto& [typeId, offset] : mSectorMeta.membersLayout) {
				if (!prevAdr->isAlive(offset)) {
					newAdr->setAlive(offset, false);
					continue;
				}

				const auto oldPlace = prevAdr->getObjectPtr(offset);
				const auto newPlace = newAdr->getObjectPtr(offset);
				ReflectionHelper::functionsTable[typeId].move(newPlace, oldPlace);//call move constructor
				
				newAdr->setAlive(offset, true);
			}

			new (newAdr)Sector(std::move(*prevAdr));//move sector info
			mSectorsMap[newAdr->id] = static_cast<SectorId>(i);
		}
	}

}
