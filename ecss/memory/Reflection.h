#pragma once

#include <functional>

#include "../Types.h"
#include <mutex>

namespace ecss::Memory {
	class ReflectionHelper {
	public:
		ReflectionHelper() : mCurrentInstance(mHelperInstances++) {}

		template<typename T>
		inline ECSType getTypeId() {
			return getTypeIdImpl<std::remove_const_t<std::remove_pointer_t<std::remove_reference_t<T>>>>();
		}

		inline ECSType getTypesCount() const {
			return mTypes[mCurrentInstance];
		}

	private:
		static inline uint16_t mHelperInstances = 0;
		static inline std::array<ECSType, std::numeric_limits<decltype(mHelperInstances)>::max()> mTypes;
		decltype(mHelperInstances) mCurrentInstance = 0;

		static constexpr ECSType INVALID_TYPE = std::numeric_limits<ECSType>::max();

		template<typename T>
		inline ECSType getTypeIdImpl() {
			static constexpr size_t maxInstancesCount = std::numeric_limits<decltype(mHelperInstances)>::max();
			static ECSType types[maxInstancesCount];
			static std::once_flag flags[maxInstancesCount];

			std::call_once(flags[mCurrentInstance], [&] { types[mCurrentInstance] = mTypes[mCurrentInstance]++; });

			return types[mCurrentInstance];
		}
	};

}
