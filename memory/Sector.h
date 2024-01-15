#pragma once

#include <cstdint>

#include "../Types.h"
#include "../contiguousMap.h"

namespace ecss::Memory {
	struct SectorMetadata {
		uint16_t sectorSize = 0;

		ContiguousMap<ECSType, uint16_t> membersLayout;//type and offset from start (can not be 0)
	};
	
	/*
	* sector stores data for any custom type in theory, offset to type stores in SectorMetadata struct
	* --------------------------------------------------------------------------------------------
	*                                       [SECTOR]
	* 0x 00                                                         { SectorId  }
	* 0x sizeof(Sector)                                         { SomeMember  }
	* 0x sizeof(Sector + SomeMember)                            { SomeMember1 }
	* 0x sizeof(Sector + SomeMember + SomeMember1)              { SomeMember2 }
	* ...
	* 0x sizeof(Sector... + ...SomeMemberN - 1 + SomeMemberN)   { SomeMemberN }
	*
	*--------------------------------------------------------------------------------------------
	*/
	struct Sector {
		Sector(SectorId id, const ContiguousMap<ECSType, uint16_t>& membersLayout) : id(id) {
			for (auto& [typeId, offset] : membersLayout) {
				setAlive(offset, false);
			}
		}

		SectorId id;

		inline constexpr void setAlive(size_t offset, bool value) {
			*static_cast<bool*>(static_cast<void*>(static_cast<char*>(static_cast<void*>(this)) + offset)) = value;//use first byte which is also reserved for align - to store if object alive
		}

		inline constexpr bool isAlive(size_t offset) {
			return *static_cast<bool*>(static_cast<void*>(static_cast<char*>(static_cast<void*>(this)) + offset));
		}

		template<typename T>
		inline constexpr T* getMember(size_t offset) {
			const auto alive = static_cast<bool*>(static_cast<void*>(static_cast<char*>(static_cast<void*>(this)) + offset));
			return *alive ? static_cast<T*>(static_cast<void*>(static_cast<char*>(static_cast<void*>(alive)) + 1)) : nullptr;
		}

		inline constexpr void* getMemberPtr(size_t offset) {
			return static_cast<bool*>(static_cast<void*>(static_cast<char*>(static_cast<void*>(this)) + offset + 1));
		}

		inline bool isSectorAlive(const ContiguousMap<ECSType, uint16_t>& membersLayout) {
			for (const auto& [type, offset] : membersLayout) {
				if (isAlive(offset)) {
					return true;
				}
			}

			return false;
		}
	};

}
