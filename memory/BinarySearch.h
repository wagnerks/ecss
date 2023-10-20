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
		
		if ((*sectors)[0]->id == sectorId) {
			idx = 0;
			return (*sectors)[0];
		}

		uint32_t left = 0u;
		void* result = nullptr;

		while (true) {
			const auto dist = right - left;
			if (dist == 1) {
				idx = left + 1;
				break;
			}

			left += dist / 2;

			if ((*sectors)[left]->id > sectorId) {
				right = left;
			}
			else if ((*sectors)[left]->id == sectorId) {
				idx = left;
				result = (*sectors)[left];
				break;
			}
		}

		return result;
	}
}
