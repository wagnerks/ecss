#pragma once

#include <algorithm>
#include <utility>
#include <vector>

#include <ecss/Fwd.h>

namespace ecss {
	/// @thread_safety Thread-confined -- and that applies to every member
	///                below, without exception. This is a plain sorted container of id ranges with
	///                no synchronization anywhere in it: take(), takeBlock(), insert() and erase()
	///                mutate, the rest read, and none of them is safe against a concurrent
	///                mutation. Build one, then hand it to view(); do not touch it again while a
	///                view built from it is still walking.
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

			Type previous = sortedRanges.front();
			Type begin = previous;

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

		/// @brief Collapse overlapping and touching ranges into one each.
		FORCE_INLINE void mergeIntersections() {
			if (ranges.size() < 2) {
				return;
			}

			// The merge below only ever compares neighbours, so it is correct only on a sorted
			// list -- and the constructor takes ranges straight from the caller. An unsorted one
			// used to come back quietly unmerged rather than merged, which every later lookup
			// then trusted. Checking is one linear pass; the sort is paid only when it is real.

			if (!std::is_sorted(ranges.begin(), ranges.end())) {
				std::sort(ranges.begin(), ranges.end());
			}

			// One pass with a write cursor. Erasing from the middle instead shifted the whole
			// tail once per merged range, so a list that collapses to a single range -- which is
			// what a contiguous block of ids does -- cost O(n^2) to fold.
			size_t out = 0;
			for (size_t in = 1; in < ranges.size(); ++in) {
				if (ranges[out].second >= ranges[in].first) {
					ranges[out].second = std::max(ranges[out].second, ranges[in].second);
				}
				else {
					ranges[++out] = ranges[in];
				}
			}
			ranges.resize(out + 1);
		}

		FORCE_INLINE Type take() { return takeBlock(1).first; }

		/**
		 * @brief Allocate up to @p maxCount contiguous values in one step.
		 * @return {first, count}: the first allocated value and how many were actually taken.
		 *         @p count is at least 1, and may be less than @p maxCount when the free gap
		 *         that got used was smaller.
		 *
		 * Batching exists so a caller can amortise whatever lock guards this container over
		 * many allocations instead of paying it per value.
		 */
		std::pair<Type, Type> takeBlock(Type maxCount) {
			if (maxCount < Type{ 1 }) {
				maxCount = Type{ 1 };
			}

			if (ranges.empty()) {
				ranges.push_back({ Type{}, maxCount });
				return { Type{}, maxCount };
			}

			// Prefer the gap *below* the first block. Values freed there used to be stranded
			// forever: take() only ever extended the first block upward, so after erase(0) on
			// {0..N} the next take() returned N+1 and 0 was never handed out again.
			auto& front = ranges.front();
			if (front.first > Type{}) {
				const Type count = maxCount < front.first ? maxCount : front.first;
				front.first -= count;
				return { front.first, count };
			}

			// Otherwise extend the first block upward, clamped to the next allocated block so
			// the returned span can never overlap values that are already handed out.
			const Type first = front.second;
			Type count = maxCount;
			if (ranges.size() > 1) {
				const Type gap = ranges[1].first - front.second;
				if (gap < count) {
					count = gap;
				}
			}
			front.second += count;

			// Closing the gap exactly makes the two blocks adjacent; merge them.
			while (ranges.size() > 1 && ranges[0].second >= ranges[1].first) {
				ranges[0].second = std::max(ranges[0].second, ranges[1].second);
				ranges.erase(ranges.begin() + 1);
			}

			return { first, count };
		}

		void insert(Type id) {
			auto it = std::lower_bound(ranges.begin(), ranges.end(), id, [](const Range& r, Type id) {
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
				++end;
				// check next range for merge
				auto next = it + 1;
				if (next != ranges.end() && next->first == end) {
					end = next->second;
					ranges.erase(next);
				}
				return;
			}

			if (begin > Type{} && id == begin - 1) {
				--begin;
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

		void erase(Type id) {
			auto index = binarySearchInRanges(ranges, id);
			if (index == -1) {
				return;
			}
			auto entRangeIt = ranges.begin();
			std::advance(entRangeIt, index);

			if (id == entRangeIt->second - 1) {
				--entRangeIt->second;
			}
			else if (id == entRangeIt->first) {
				++entRangeIt->first;
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

		static int binarySearchInRanges(const std::vector<Range>& ranges, Type id) {
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

		/// @brief Start of the first range with first > @p id, if any.
		FORCE_INLINE bool nextStartAfter(Type id, Type& out) const {
			int left = 0;
			int right = static_cast<int>(ranges.size()) - 1;
			int ans = -1;
			while (left <= right) {
				const int mid = (left + right) / 2;
				if (ranges[mid].first > id) {
					ans = mid;
					right = mid - 1;
				}
				else {
					left = mid + 1;
				}
			}
			if (ans < 0) {
				return false;
			}
			out = ranges[ans].first;
			return true;
		}

		std::vector<Type> getAll() const {
			size_t total = 0;
			for (const auto& r : ranges) { total += static_cast<size_t>(r.second - r.first); }

			std::vector<Type> res;
			res.resize(total);

			Type* out = res.data();
			for (const auto& r : ranges) {
				for (Type value = r.first; value != r.second; ++value) { *out++ = value; }
			}

			return res;
		}
	};
}
