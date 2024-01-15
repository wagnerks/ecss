#include "Registry.h"

#include <algorithm>
#include <map>

namespace ecss {
	Registry::~Registry() {
		clear();

		std::map<void*, bool> deleted;
		for (auto i = 0; i < mComponentsArraysMap.size(); i++){
			auto container = mComponentsArraysMap[i];
			if (!container || deleted[container]) {//skip not created and containers of multiple components
				continue;
			}

			delete mComponentsArraysMutexes[i];
			delete container;
			deleted[container] = true;
		}
	}

	void Registry::clear() {
		for (size_t i = 0; i < mComponentsArraysMap.size(); i++) {
			const auto compContainer = mComponentsArraysMap[i];
			if (!compContainer) {
				continue;
			}
			
			auto lock = containerWriteLock(i);
			compContainer->clear();
		}

		std::unique_lock lock(mEntitiesMutex);
		mEntities.clear();
		mFreeEntities.clear();
	}

	void Registry::destroyComponents(const EntityHandle& entity) const {
		for (size_t i = 0; i < mComponentsArraysMap.size(); i++) {
			const auto compContainer = mComponentsArraysMap[i];
			if (!compContainer) {
				continue;
			}

			auto lock = containerWriteLock(i);
			compContainer->destroySector(entity.getID());
		}
	}

	EntityHandle Registry::takeEntity() {
		std::unique_lock lock(mEntitiesMutex);
		auto id = getNewId();
		mEntities.insert(mEntities.begin() + id, id);

		return { id };
	}

	EntityHandle Registry::getEntity(SectorId entityId) const {
		std::shared_lock lock(mEntitiesMutex);
		if (mEntities.empty() || mEntities.back() < entityId || mFreeEntities.contains(entityId)) {
			return { INVALID_ID };
		}

		return { entityId };
	}

	void Registry::destroyEntity(const EntityHandle& entity) {
		if (!entity) {
			return;
		}
		std::unique_lock lock(mEntitiesMutex);
		const auto id = entity.getID();
		if (std::find(mEntities.begin(), mEntities.end(), id) == mEntities.end()) {
			return;
		}

		if (id != *mEntities.rbegin()) {//if entity was removed not from end - add its id to the list
			mFreeEntities.insert(id);
		}

		mEntities.erase(std::find(mEntities.begin(), mEntities.end(),id));
		destroyComponents(id);
	}

	void Registry::destroyEntities(std::vector<SectorId>& entities) {
		std::sort(entities.begin(), entities.end());

		for (size_t i = 0; i < mComponentsArraysMap.size(); i++) {
			const auto compContainer = mComponentsArraysMap[i];
			if (!compContainer) {
				continue;
			}
			//todo thread for every con
			
			auto lock2 = containerWriteLock(i);
			compContainer->destroyMembers(i, entities, false);
		}

		std::unique_lock lock(mEntitiesMutex);
		{

			for (auto entity : entities) {
				auto entIt = std::find(mEntities.begin(), mEntities.end(), entity);
				if (entIt == mEntities.end()) {
					continue;
				}

				mFreeEntities.insert(entity);
				mEntities.erase(entIt);
			}

			if (!mEntities.empty()) {
				const auto last = mEntities.back();
				for (auto it = mFreeEntities.begin(); it != mFreeEntities.end();) {
					if (*it > last) {
						it = mFreeEntities.erase(it);
						continue;
					}
					++it;
				}
			}
			else {
				mFreeEntities.clear();
			}
		}
	}

	void Registry::removeEmptySectors() {
		for (size_t i = 0; i < mComponentsArraysMap.size(); i++) {
			const auto compContainer = mComponentsArraysMap[i];
			if (!compContainer) {
				continue;
			}

			auto lock = containerWriteLock(i);
			compContainer->removeEmptySectors();
		}
	}

	const std::vector<SectorId>& Registry::getAllEntities() {
		std::shared_lock lock(mEntitiesMutex);
		return mEntities;
	}

	SectorId Registry::getNewId() {
		if (!mFreeEntities.empty()) {
			const auto id = *mFreeEntities.begin();
			mFreeEntities.erase(id);

			return id;
		}

		return static_cast<SectorId>(mEntities.size());
	}
}