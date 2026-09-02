#pragma once

#include <atomic>
#include <ecss/Fwd.h>

namespace ecss::Memory {

	/**
	 * @brief Ultra-fast compile-time type ID generation.
	 * 
	 * Uses address of static variable as unique type identifier.
	 * This is resolved at compile/link time - zero runtime overhead.
	 * IDs are stable across the program lifetime but NOT dense (not sequential).
	 * @thread_safety Internally synchronized. The address of a static, resolved at link time.
	 *                Nothing is written at runtime.
	 */
	template<typename T>
	FORCE_INLINE size_t GlobalTypeId() noexcept {
		static constexpr char tag = 0;
		return reinterpret_cast<size_t>(&tag);
	}

	/**
	 * @brief Dense sequential type ID generator for efficient array indexing.
	 * 
	 * Assigns sequential IDs (0, 1, 2, ...) to types on first use.
	 * Thread-safe initialization, then lock-free reads.
	 * IDs are global - same type gets same ID across all Registry instances.
	 */
	class DenseTypeIdGenerator {
		static inline std::atomic<ECSType> sNextId{0};
		
	public:
		/// @thread_safety Internally synchronized. A function-local static, so the first caller for a
		///                type assigns the id and every other thread waits for that initialization --
		///                the language guarantees it. Afterwards it is a plain read of a constant.
		template<typename T>
		FORCE_INLINE static ECSType getId() noexcept {
			// Static local - initialized once per type, thread-safe in C++11+
			static const ECSType id = sNextId.fetch_add(1, std::memory_order_relaxed);
			return id;
		}

		/// @thread_safety Internally synchronized. Forwards to getId() after stripping cv and
		///                reference, so const T, T& and T share one id.
		template<typename T>
		FORCE_INLINE static ECSType getTypeId() noexcept {
			return DenseTypeIdGenerator::getId<std::remove_const_t<std::remove_pointer_t<std::remove_reference_t<T>>>>();
		}

		/// @thread_safety Internally synchronized. One relaxed load. A moving target: another thread
		///                naming a new type raises it.
		static ECSType getCount() noexcept {
			return sNextId.load(std::memory_order_relaxed);
		}
	};
} // namespace ecss::Memory
