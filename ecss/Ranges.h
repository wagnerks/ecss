#pragma once
#include <deque>
#include <vector>
#include <numeric>

#include <ecss/Types.h>

namespace ecss {
	template<typename Type = uint32_t>
	struct Ranges {
		static_assert(std::is_arithmetic_v<Type>, "Not an arithmetic type");
		using Range = std::pair<Type, Type>;

		std::vector<Range> ranges;

		Ranges() = default;
		
		Ranges(const std::vector<Type>& sortedRanges) {
			if (sortedRanges.empty()) {
				return;
			}

			EntityId previous = sortedRanges.front();
			EntityId begin = previous;

			for (auto i = 1u; i < sortedRanges.size(); i++) {
				const auto current = sortedRanges[i];
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

		Ranges(const std::vector<Range>& range) {
			ranges = range;
			mergeIntersections();
		}

		Ranges(const Range& range) : Ranges(std::vector<Range>{range}) {}

		FORCE_INLINE void mergeIntersections() {
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

		FORCE_INLINE Type take() {
			if (ranges.empty()) {
				ranges.push_back({ 0,0 });
			}
			auto id = ranges.front().second;
			++ranges.front().second;

			if (ranges.size() > 1) {
				if (ranges[0].second == ranges[1].first) {
					ranges[0].second = ranges[1].second;
					ranges.erase(ranges.begin() + 1);
				}
			}

			return id;
		}

		void insert(EntityId id) {
			auto it = std::lower_bound(ranges.begin(), ranges.end(), id, [](const Range& r, EntityId id) {
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
				entRangeIt = ranges.insert(entRangeIt, Range{});
				entRangeIt->first = (entRangeIt + 1)->first;
				(entRangeIt + 1)->first = id + 1;
				entRangeIt->second = id;
			}

			if (entRangeIt->first == entRangeIt->second) {
				entRangeIt = ranges.erase(entRangeIt);
			}
		}

		static int binarySearchInRanges(const std::vector<Range>& ranges, EntityId id) {
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

		FORCE_INLINE void clear() { ranges.clear(); }
		FORCE_INLINE size_t size() const { return ranges.size(); }
		FORCE_INLINE const Range& front() const { return ranges.front(); }
		FORCE_INLINE const Range& back() const { return ranges.back(); }
		FORCE_INLINE void pop_back() { ranges.pop_back(); }
		FORCE_INLINE bool empty() const { return !size(); }
		FORCE_INLINE bool contains(Type value) const { return binarySearchInRanges(ranges, value) != -1; }

		std::vector<Type> getAll() const {
			size_t total = 0;
			for (const auto& r : ranges) { total += static_cast<size_t>(r.second - r.first); }

			std::vector<Type> res;
			res.resize(total);

			Type* out = res.data();
			for (const auto& r : ranges) {
				const size_t len = static_cast<size_t>(r.second - r.first);
				std::iota(out, out + len, r.first);
				out += len;
			}

			return res;
		}
	};
}
