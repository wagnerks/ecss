#pragma once

#include <functional>

#include "../Types.h"
#include <mutex>

namespace ecss::Memory {
	class ReflectionHelper {
	public:
		ReflectionHelper() : mCurrentInstance(mHelperInstances++) { mTypes.resize(mHelperInstances); }

		struct FunctionTable {
			std::function<void(void* dest, void* src)> move;
			std::function<void(void* dest, void* src)> copy;
			std::function<void(void* src)> destructor;
		};

		template<typename T>
		inline ECSType getTypeId() {
			return getTypeIdImpl<std::remove_const_t<std::remove_pointer_t<std::remove_reference_t<T>>>>();
		}

		inline ECSType getTypesCount() const {
			return mTypes[mCurrentInstance];
		}

	private:
		static inline uint8_t mHelperInstances = 0;
		static inline std::vector<ECSType> mTypes;
		uint8_t mCurrentInstance = 0;

		static constexpr ECSType INVALID_TYPE = std::numeric_limits<ECSType>::max();

		template<typename T>
		inline ECSType getTypeIdImpl() {
			static constexpr size_t maxInstancesCount = 256;
			static ECSType types[maxInstancesCount];
			static std::once_flag flags[maxInstancesCount];

			std::call_once(flags[mCurrentInstance], [&] { types[mCurrentInstance] = mTypes[mCurrentInstance]++; });

			return types[mCurrentInstance];
		}
	};

}
