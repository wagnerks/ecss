#pragma once

#include <functional>

#include "../Types.h"
#include "../contiguousMap.h"
#include "shared_mutex"

namespace ecss::Memory {
	class ReflectionHelper {
	public:
		ReflectionHelper() : mCurrentInstance(mHelperInstances++) {	}

		struct FunctionTable {
			std::function<void(void* dest, void* src)> move;
			std::function<void(void* dest, void* src)> copy;
			std::function<void(void* src)> destructor;
		};

		ContiguousMap<ECSType, FunctionTable> functionsTable;

		template<typename T>
		__forceinline ECSType getTypeId() {
			return getTypeIdImpl<std::remove_const_t<std::remove_pointer_t<T>>>();
		}

		ECSType getTypesCount() const {
			return mTypes;
		}

	private:
		static inline uint8_t mHelperInstances = 0;
		uint8_t mCurrentInstance = 0;

		ECSType mTypes = 0;

		std::shared_mutex mtx;

		template<typename T>
		__forceinline ECSType initType() {
			mtx.lock();
			const ECSType id = mTypes++;

			functionsTable[id].move = [](void* dest, void* src) { new(dest)T(std::move(*static_cast<T*>(src))); };
			functionsTable[id].copy = [](void* dest, void* src) { new(dest)T(*static_cast<T*>(src)); };
			functionsTable[id].destructor = [](void* src) { static_cast<T*>(src)->~T(); };
			mtx.unlock();

			return id;
		}

		static constexpr inline ECSType INVALID_TYPE = std::numeric_limits<ECSType>::max();
		template<typename T>
		__forceinline ECSType getTypeIdImpl() {
			static std::array<ECSType, 64> types {
				INVALID_TYPE, INVALID_TYPE, INVALID_TYPE, INVALID_TYPE, INVALID_TYPE, INVALID_TYPE, INVALID_TYPE, INVALID_TYPE,
				INVALID_TYPE, INVALID_TYPE, INVALID_TYPE, INVALID_TYPE, INVALID_TYPE, INVALID_TYPE, INVALID_TYPE, INVALID_TYPE,
				INVALID_TYPE, INVALID_TYPE, INVALID_TYPE, INVALID_TYPE, INVALID_TYPE, INVALID_TYPE, INVALID_TYPE, INVALID_TYPE,
				INVALID_TYPE, INVALID_TYPE, INVALID_TYPE, INVALID_TYPE, INVALID_TYPE, INVALID_TYPE, INVALID_TYPE, INVALID_TYPE,
				INVALID_TYPE, INVALID_TYPE, INVALID_TYPE, INVALID_TYPE, INVALID_TYPE, INVALID_TYPE, INVALID_TYPE, INVALID_TYPE,
				INVALID_TYPE, INVALID_TYPE, INVALID_TYPE, INVALID_TYPE, INVALID_TYPE, INVALID_TYPE, INVALID_TYPE, INVALID_TYPE,
				INVALID_TYPE, INVALID_TYPE, INVALID_TYPE, INVALID_TYPE, INVALID_TYPE, INVALID_TYPE, INVALID_TYPE, INVALID_TYPE,
				INVALID_TYPE, INVALID_TYPE, INVALID_TYPE, INVALID_TYPE, INVALID_TYPE, INVALID_TYPE, INVALID_TYPE, INVALID_TYPE,
			};

			auto& type = types[mCurrentInstance];
			return type == INVALID_TYPE ? type = initType<T>(), type : type;
		}
	};

}
