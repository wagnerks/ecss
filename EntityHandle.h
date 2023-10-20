#pragma once

#include "Types.h"

namespace ecss {
	class EntityHandle {
	public:
		operator bool() const { return mId != INVALID_ID; }
		EntityHandle(SectorId entityID = INVALID_ID) : mId(entityID) {}
		SectorId getID() const { return mId; }

	private:
		SectorId mId = INVALID_ID;
	};
}