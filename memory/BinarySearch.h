#pragma once

#include "SectorsArray.h"
#include "../Types.h"

namespace ecss::Memory::Utils {
	inline void* binarySearch(SectorId sectorId, size_t& idx, SectorsArray* sectors) {
		auto right = sectors->size();

		if (right == 0 || (*sectors)[0]->id > sectorId) {
			idx = 0;
			return nullptr;
		}

		if ((*sectors)[right - 1]->id < sectorId) {
			idx = right;
			return nullptr;
		}

		uint32_t left = 0u;
		void* result = nullptr;

		while(true) {
			const auto dist = right - left;
			if (dist == 1) {
				idx = left + 1;
				break;
			}

			if ((*sectors)[left]->id == sectorId) {
				idx = left;
				result = (*sectors)[left];
				break;
			}

			const auto mid = left + dist / 2;

			if ((*sectors)[mid]->id > sectorId) {
				right = mid;
				continue;
			}

			if ((*sectors)[mid]->id == sectorId) {
				idx = mid;
				result = (*sectors)[mid];
				break;
			}

			left = mid;
		}

		return result;
	}
}
