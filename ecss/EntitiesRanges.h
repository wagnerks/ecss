#pragma once
#include <deque>
#include <vector>

#include "Types.h"

namespace ecss {
	struct EntitiesRanges {
		using range = std::pair<EntityId, EntityId>;
		std::vector<range> ranges;

		EntitiesRanges() = default;
		
		EntitiesRanges(const std::vector<EntityId>& sortedEntities) {
			if (sortedEntities.empty()) {
				return;
			}

			EntityId previous = sortedEntities.front();
			EntityId begin = previous;

			for (auto i = 1u; i < sortedEntities.size(); i++) {
				const auto current = sortedEntities[i];
				if (current == previous) {
					continue;
				}
				if (current - previous > 1) {
					ranges.emplace_back(begin, previous + 1);
					begin = current;
				}

				previous = current;
			}
			ranges.emplace_back(begin, previous + 1);
		}

		EntitiesRanges(const std::vector<range>& range) {
			ranges = range;
			mergeIntersections();
		}

		inline void mergeIntersections() {
			if (ranges.empty()) {
				return;
			}

			for (auto it = ranges.begin() + 1; it != ranges.end();) {
				auto& prev = *(it - 1);
				auto& cur = *(it);
				if (prev.second >= cur.first) {
					prev.second = std::max(prev.second, cur.second);
					it = ranges.erase(it);
				}
				else {
					++it;
				}
			}
		}

		EntityId take() {
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

		void insert(EntityId id) {
			auto it = std::lower_bound(ranges.begin(), ranges.end(), id, [](const range& r, EntityId id) {
				return r.second < id;
			});


			if (it == ranges.end()) {
				// insert at end
				ranges.emplace_back(id, id + 1);
				return;
			}

			auto& [begin, end] = *it;

			if (id >= begin && id < end) return; // уже есть

			if (id == end) {
				end++;
				// check next range for merge
				auto next = it + 1;
				if (next != ranges.end() && next->first == end) {
					end = next->second;
					ranges.erase(next);
				}
				return;
			}

			if (id == begin - 1) {
				begin--;
				// check prev range for merge
				if (it != ranges.begin()) {
					auto prev = it - 1;
					if (prev->second == begin) {
						prev->second = end;
						ranges.erase(it);
					}
				}
				return;
			}

			// insert before current
			ranges.insert(it, { id, id + 1 });
		}

		void erase(EntityId id) {
			auto index = binarySearchInRanges(ranges, id);
			if (index == -1) {
				return;
			}
			auto entRangeIt = ranges.begin();
			std::advance(entRangeIt, index);

			if (id == entRangeIt->second - 1) {
				entRangeIt->second--;
			}
			else if (id == entRangeIt->first) {
				entRangeIt->first++;
			}
			else {
				entRangeIt = ranges.insert(entRangeIt, range{});
				entRangeIt->first = (entRangeIt + 1)->first;
				(entRangeIt + 1)->first = id + 1;
				entRangeIt->second = id;
			}

			if (entRangeIt->first == entRangeIt->second) {
				entRangeIt = ranges.erase(entRangeIt);
			}
		}

		static inline int binarySearchInRanges(const std::vector<range>& ranges, EntityId id) {
			if (ranges.empty()) {
				return -1;
			}

			if (id < ranges[0].first || id >= ranges.back().second) {
				return -1;
			}

			if (ranges[0].first <= id && ranges[0].second > id) {
				return 0;
			}
			int left = 0;
			int right = static_cast<int>(ranges.size()) - 1;
			if (ranges.back().first <= id && ranges.back().second > id) {
				return right;
			}

			while (left <= right) {
				int mid = (left + right) / 2;
				const auto& [begin, end] = ranges[mid];

				if (id < begin)
					right = mid - 1;
				else if (id >= end)
					left = mid + 1;
				else
					return mid; // id ∈ [begin, end)
			}

			return -1;
		}

		void clear() { ranges.clear(); }
		size_t size() const { return ranges.size(); }
		range& front() { return ranges.front(); }
		range& back() { return ranges.back(); }
		void pop_back() { ranges.pop_back(); }
		bool empty() const { return !size(); }
		bool contains(ecss::EntityId id) const { return binarySearchInRanges(ranges, id) != -1; }
		std::vector<EntityId> getAll() const {
			std::vector<EntityId> res;
			size_t size = 0;
			for (auto& rangesRange : ranges) {
				size += rangesRange.second - rangesRange.first;
			}
			res.reserve(size);
			for (auto& rangesRange : ranges) {
				for (auto i = rangesRange.first; i < rangesRange.second; i++) {
					res.emplace_back(i);
				}
			}

			return res;
		}
	};
}
