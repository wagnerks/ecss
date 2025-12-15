#pragma once

#include <atomic>
#include <vector>
#include <mutex>
#include <ecss/Types.h>

namespace ecss::Memory {
	/**
	 * @brief Helper class for assigning dense, sequential type IDs to component types.
	 * 
	 * Each Registry instance gets its own type ID namespace starting from 0.
	 * Instance slots are recycled when Registry is destroyed.
	 * Epoch counter detects stale entries from previous slot occupants.
	 * 
	 * Type IDs are dense and sequential per-instance, allowing efficient
	 * vector-based lookup in Registry::mComponentsArraysMap.
	 */
	class ReflectionHelper {
		static constexpr size_t kMaxInstances = 4096;
		
		struct InstancePool {
			std::mutex mtx;
			std::vector<uint16_t> freeList;
			uint16_t nextId;
			uint16_t epochs[kMaxInstances];
			
			InstancePool() : nextId(0) {
				std::fill_n(epochs, kMaxInstances, uint16_t(0));
			}
			
			std::pair<uint16_t, uint16_t> acquire() { // returns {id, epoch}
				std::lock_guard lock(mtx);
				if (!freeList.empty()) {
					uint16_t id = freeList.back();
					freeList.pop_back();
					return {id, ++epochs[id]};
				}
				uint16_t id = nextId++;
				return {id, epochs[id]};
			}
			
			void release(uint16_t id) {
				std::lock_guard lock(mtx);
				freeList.push_back(id);
			}
		};
		
		static inline InstancePool sInstancePool{};
		
		// Per-type storage: [instanceId] -> {epoch, typeId}
		// If stored epoch != current epoch, entry is stale
		// Initial epoch is UINT16_MAX so first real epoch (0) triggers assignment
		// Uses atomics to avoid data races in multi-threaded access
		struct PerTypeData {
			struct Entry {
				std::atomic<uint16_t> epoch{UINT16_MAX};
				std::atomic<ECSType> typeId{0};
			};
			Entry entries[kMaxInstances];
			std::mutex mtx; // Protects slow-path updates
		};

	public:
		ReflectionHelper() {
			auto [id, epoch] = sInstancePool.acquire();
			mInstanceId = id;
			mEpoch = epoch;
		}
		
		~ReflectionHelper() {
			sInstancePool.release(mInstanceId);
		}
		
		// Non-copyable, non-movable (instance ID is unique)
		ReflectionHelper(const ReflectionHelper&) = delete;
		ReflectionHelper& operator=(const ReflectionHelper&) = delete;
		ReflectionHelper(ReflectionHelper&&) = delete;
		ReflectionHelper& operator=(ReflectionHelper&&) = delete;

		template<typename T>
		FORCE_INLINE ECSType getTypeId() {
			return getTypeIdImpl<std::remove_const_t<std::remove_pointer_t<std::remove_reference_t<T>>>>();
		}

		FORCE_INLINE ECSType getTypesCount() const {
			return mTypesCount;
		}

	private:
		uint16_t mInstanceId;
		uint16_t mEpoch;
		ECSType mTypesCount = 0;

		template<typename T>
		FORCE_INLINE ECSType getTypeIdImpl() {
			static PerTypeData data{};
			
			auto& entry = data.entries[mInstanceId];
			
			// Fast path - check epoch atomically (common case: already initialized)
			uint16_t storedEpoch = entry.epoch.load(std::memory_order_acquire);
			if (storedEpoch == mEpoch) [[likely]] {
				return entry.typeId.load(std::memory_order_relaxed);
			}
			
			// Slow path - need to initialize under lock
			std::lock_guard lock(data.mtx);
			
			// Double-check after acquiring lock (another thread may have updated)
			storedEpoch = entry.epoch.load(std::memory_order_relaxed);
			if (storedEpoch == mEpoch) {
				return entry.typeId.load(std::memory_order_relaxed);
			}
			
			// Update entry - store typeId first, then epoch (epoch acts as "ready" flag)
			ECSType newTypeId = mTypesCount++;
			entry.typeId.store(newTypeId, std::memory_order_relaxed);
			entry.epoch.store(mEpoch, std::memory_order_release);
			
			return newTypeId;
		}
	};

}
