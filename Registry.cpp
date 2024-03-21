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
			
			auto lock = containerWriteLock(static_cast<ECSType>(i));
			compContainer->clear();
		}

		std::unique_lock lock(mEntitiesMutex);
		mEntities.clear();
	}

	void Registry::destroyComponents(EntityId entity) const {
		for (size_t i = 0; i < mComponentsArraysMap.size(); i++) {
			const auto compContainer = mComponentsArraysMap[i];
			if (!compContainer) {
				continue;
			}

			auto lock = containerWriteLock(static_cast<ECSType>(i));
			compContainer->destroySector(entity);
		}
	}

	EntityId Registry::takeEntity() {
		std::unique_lock lock(mEntitiesMutex);
		return { mEntities.take() };
	}

	bool Registry::contains(EntityId entityId) const {
		return mEntities.contains(entityId);
	}

	void Registry::destroyEntity(EntityId entity) {
		if (!entity) {
			return;
		}

		std::unique_lock lock(mEntitiesMutex);
		mEntities.erase(entity);
		destroyComponents(entity);
	}

	void Registry::destroyEntities(std::vector<EntityId>& entities) {
		if (entities.empty()) {
			return;
		}
		std::sort(entities.begin(), entities.end());

		for (size_t i = 0; i < mComponentsArraysMap.size(); i++) {
			const auto compContainer = mComponentsArraysMap[i];
			if (!compContainer) {
				continue;
			}
			//todo thread for every con
			
			auto lock2 = containerWriteLock(static_cast<ECSType>(i));
			compContainer->destroyMembers(static_cast<ECSType>(i), entities, false);
		}

		std::unique_lock lock(mEntitiesMutex);
		for (auto id : entities) {
			mEntities.erase(id);
		}
	}

	void Registry::removeEmptySectors() {
		for (size_t i = 0; i < mComponentsArraysMap.size(); i++) {
			const auto compContainer = mComponentsArraysMap[i];
			if (!compContainer) {
				continue;
			}

			auto lock = containerWriteLock(static_cast<ECSType>(i));
			compContainer->removeEmptySectors();
		}
	}

	const std::vector<EntityId> Registry::getAllEntities() {
		std::shared_lock lock(mEntitiesMutex);
		return mEntities.getAll();
	}

	EntityId EntitiesRanges::take() {
		if (ranges.empty()) {
			ranges.push_back({ 0,0 });
		}
		auto id = ranges.front().second;
		ranges.front().second++;
		if (ranges.size() > 1) {
			if (ranges[0].second == ranges[1].first) {
				ranges[0].second = ranges[1].second;
				ranges.erase(ranges.begin() + 1);
			}
		}

		return id;
	}

	void EntitiesRanges::insert(EntityId id) {
		for (auto i = 0u; i < ranges.size(); i++) {
			auto& range = ranges[i];
			if (id < range.second && id >= range.first) {
				return;
			}

			if (id == range.second) {
				range.second++;
				if (i != ranges.size() - 1) {
					if (ranges[i + 1].first == range.second) {
						ranges[i + 1].first = range.first;
						ranges.erase(ranges.begin() + i);
					}
				}
				return;
			}

			if (id < range.first) {
				if (id == range.first - 1) {
					range.first--;
					if (i != 0) {
						if (ranges[i - 1].second == range.first) {
							ranges[i - 1].second = range.second;
							ranges.erase(ranges.begin() + i);
						}
					}
				}
				else {
					ranges.insert(ranges.begin() + i, {id, id + 1});
				}

				return;
			}
		}

		ranges.push_back({ id, id + 1});
	}

	void EntitiesRanges::erase(EntityId id) {
		for (auto entRangeIt = ranges.begin(); entRangeIt != ranges.end(); ++entRangeIt) {
			if (id >= entRangeIt->first && id < entRangeIt->second) {
				if (id == entRangeIt->second - 1) {
					entRangeIt->second--;
				}
				else {
					auto it = ranges.insert(entRangeIt, range{});
					(it + 1)->first = id + 1;
					it->second = id;
				}
				break;
			}
		}
	}

	bool EntitiesRanges::contains(EntityId id) const {
		if (id >= ranges.back().second) {
			return false;
		}

		for (const auto& entRange : ranges) {
			if (id >= entRange.first && id < entRange.second) {
				return true;
			}
		}
		return false;
	}

	std::vector<EntityId> EntitiesRanges::getAll() const {
		std::vector<EntityId> res;
		for (auto& rangesRange : ranges) {
			for (auto i = rangesRange.first; i < rangesRange.second; i++) {
				res.emplace_back(i);
			}
		}

		return res;
	}
}
