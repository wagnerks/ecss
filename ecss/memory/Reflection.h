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
			uint16_t nextId = 0;
			uint16_t epochs[kMaxInstances]{}; // Incremented each time slot is reused
			
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
		
		static inline InstancePool sInstancePool;
		
		// Per-type storage: [instanceId] -> {epoch, typeId}
		// If stored epoch != current epoch, entry is stale
		struct PerTypeData {
			struct Entry {
				uint16_t epoch = 0;
				ECSType typeId = 0;
			};
			Entry entries[kMaxInstances]{};
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
			
			// Check if entry is from current epoch (this instance)
			if (entry.epoch != mEpoch) [[unlikely]] {
				entry.epoch = mEpoch;
				entry.typeId = mTypesCount++;
			}
			
			return entry.typeId;
		}
	};

}
