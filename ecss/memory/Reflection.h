#pragma once

#include <atomic>
#include <ecss/Types.h>

namespace ecss::Memory {

	/**
	 * @brief Ultra-fast compile-time type ID generation.
	 * 
	 * Uses address of static variable as unique type identifier.
	 * This is resolved at compile/link time - zero runtime overhead.
	 * IDs are stable across the program lifetime but NOT dense (not sequential).
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
		template<typename T>
		FORCE_INLINE static ECSType getId() noexcept {
			// Static local - initialized once per type, thread-safe in C++11+
			static const ECSType id = sNextId.fetch_add(1, std::memory_order_relaxed);
			return id;
		}
		
		static ECSType getCount() noexcept {
			return sNextId.load(std::memory_order_relaxed);
		}
	};

	/**
	 * @brief Helper for runtime type reflection.
	 * 
	 * Thin wrapper around DenseTypeIdGenerator for API compatibility.
	 * No per-instance state needed - all type IDs are global.
	 */
	class ReflectionHelper {
	public:
		ReflectionHelper() noexcept = default;
		~ReflectionHelper() noexcept = default;
		
		// Copyable and movable (no state)
		ReflectionHelper(const ReflectionHelper&) noexcept = default;
		ReflectionHelper& operator=(const ReflectionHelper&) noexcept = default;
		ReflectionHelper(ReflectionHelper&&) noexcept = default;
		ReflectionHelper& operator=(ReflectionHelper&&) noexcept = default;

		template<typename T>
		FORCE_INLINE ECSType getTypeId() const noexcept {
			return DenseTypeIdGenerator::getId<std::remove_const_t<std::remove_pointer_t<std::remove_reference_t<T>>>>();
		}

		FORCE_INLINE ECSType getTypesCount() const noexcept {
			return DenseTypeIdGenerator::getCount();
		}
	};

} // namespace ecss::Memory
