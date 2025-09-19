#pragma once

#include <mutex> // for std::once_flag
#include <ecss/Types.h>

namespace ecss::Memory {
	class ReflectionHelper {
	public:
		ReflectionHelper() : mCurrentInstance(mHelperInstances++) {}

		template<typename T>
		FORCE_INLINE ECSType getTypeId() {
			return getTypeIdImpl<std::remove_const_t<std::remove_pointer_t<std::remove_reference_t<T>>>>();
		}

		FORCE_INLINE ECSType getTypesCount() const {
			return mTypes[mCurrentInstance];
		}

	private:
		static inline uint16_t mHelperInstances = 0;
		static inline std::array<ECSType, std::numeric_limits<decltype(mHelperInstances)>::max()> mTypes;
		decltype(mHelperInstances) mCurrentInstance = 0;

		static constexpr ECSType INVALID_TYPE = std::numeric_limits<ECSType>::max();

		template<typename T>
		FORCE_INLINE ECSType getTypeIdImpl() {
			static constexpr size_t maxInstancesCount = std::numeric_limits<decltype(mHelperInstances)>::max();
			static ECSType types[maxInstancesCount];
			static std::once_flag flags[maxInstancesCount];

			std::call_once(flags[mCurrentInstance], [&] { types[mCurrentInstance] = mTypes[mCurrentInstance]++; });

			return types[mCurrentInstance];
		}
	};

}
