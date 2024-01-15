#pragma once

#include "Types.h"

namespace ecss {
	class EntityHandle {
	public:
		operator bool() const { return mId != INVALID_ID; }

		operator size_t() const { return mId; }
		operator int() const { return mId; }
		operator SectorId() const { return mId; }

		EntityHandle(SectorId entityID = INVALID_ID) : mId(entityID) {}
		inline SectorId getID() const { return mId; }

		inline bool operator==(const EntityHandle& handle) const {
			return mId == handle.mId;
		}
		inline bool operator==(const SectorId& id) const {
			return mId == id;
		}

		inline bool operator!=(const EntityHandle& handle) const {
			return !operator==(handle);
		}

		inline bool operator!=(const SectorId& id) const {
			return !operator==(id);
		}

	private:
		SectorId mId = INVALID_ID;
	};
}