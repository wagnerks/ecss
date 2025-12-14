#pragma once

#include <mutex> // for std::once_flag
#include <cassert>
#include <ecss/Types.h>

namespace ecss::Memory {
	/**
	 * @brief Maximum number of Registry instances supported.
	 * 
	 * This limits memory usage for per-type static arrays in getTypeIdImpl().
	 * Each component type creates arrays of this size for type ID tracking.
	 * 256 instances should be more than enough for any practical use case.
	 * 
	 * Previous value (65535) caused ~640KB static allocation per component type,
	 * which triggered STATUS_STACK_BUFFER_OVERRUN on MSVC due to /GS checks
	 * during thread-safe static initialization.
	 */
	inline constexpr size_t kMaxRegistryInstances = 256;

	class ReflectionHelper {
	public:
		ReflectionHelper() : mCurrentInstance(mHelperInstances++) {
			assert(mCurrentInstance < kMaxRegistryInstances && "Too many Registry instances created");
		}

		template<typename T>
		FORCE_INLINE ECSType getTypeId() {
			return getTypeIdImpl<std::remove_const_t<std::remove_pointer_t<std::remove_reference_t<T>>>>();
		}

		FORCE_INLINE ECSType getTypesCount() const {
			return mTypes[mCurrentInstance];
		}

	private:
		static inline uint16_t mHelperInstances = 0;
		static inline std::array<ECSType, kMaxRegistryInstances> mTypes{};
		uint16_t mCurrentInstance = 0;

		static constexpr ECSType INVALID_TYPE = std::numeric_limits<ECSType>::max();

		template<typename T>
		FORCE_INLINE ECSType getTypeIdImpl() {
			static ECSType types[kMaxRegistryInstances]{};
			static std::once_flag flags[kMaxRegistryInstances];

			std::call_once(flags[mCurrentInstance], [&] { types[mCurrentInstance] = mTypes[mCurrentInstance]++; });

			return types[mCurrentInstance];
		}
	};

}
