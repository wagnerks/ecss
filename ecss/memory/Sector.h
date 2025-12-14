#pragma once

#include <ecss/memory/SectorLayoutMeta.h>

namespace ecss::Memory {
	/**
	 * @brief Namespace containing static functions for component operations within sector data.
	 * 
	 * Sector is now a logical concept - a contiguous memory region storing multiple components.
	 * The metadata (id, isAliveData) is stored externally in SectorsArray for better cache locality.
	 * 
	 * Memory layout per sector slot:
	 * \code
	 * [SECTOR DATA at linearIdx]
	 * 0x + 0                          { Component0 }
	 * 0x + sizeof(Component0)         { Component1 }
	 * 0x + ... + sizeof(ComponentN)   { ComponentN }
	 * \endcode
	 * 
	 * External arrays in SectorsArray:
	 * - mIds[linearIdx]        -> SectorId
	 * - mIsAliveData[linearIdx] -> uint32_t bitfield of alive components
	 */
	namespace Sector {

		/** @brief Get a member pointer by offset if the corresponding liveness bit is set.
		 *  @param data Raw pointer to sector data
		 *  @param offset Byte offset of the component
		 *  @param mask Liveness bitmask for this component type
		 *  @param isAliveData Current alive state from external array
		 *  @return Pointer to T or nullptr if not alive.
		 */
		template<typename T>
		FORCE_INLINE T* getMember(std::byte* data, uint16_t offset, uint32_t mask, uint32_t isAliveData) noexcept {
			return (isAliveData & mask) ? reinterpret_cast<T*>(data + offset) : nullptr;
		}

		/** @overload const version */
		template<typename T>
		FORCE_INLINE const T* getMember(const std::byte* data, uint16_t offset, uint32_t mask, uint32_t isAliveData) noexcept {
			return (isAliveData & mask) ? reinterpret_cast<const T*>(data + offset) : nullptr;
		}

		/** @brief Get a member pointer using layout metadata; returns nullptr if not alive. */
		template<typename T>
		FORCE_INLINE T* getMember(std::byte* data, const LayoutData& layout, uint32_t isAliveData) noexcept {
			return getMember<T>(data, layout.offset, layout.isAliveMask, isAliveData);
		}

		/** @overload const version */
		template<typename T>
		FORCE_INLINE const T* getMember(const std::byte* data, const LayoutData& layout, uint32_t isAliveData) noexcept {
			return getMember<T>(data, layout.offset, layout.isAliveMask, isAliveData);
		}

		/** @brief Raw member address by byte offset from the sector data base. */
		FORCE_INLINE void* getMemberPtr(std::byte* data, uint16_t offset) noexcept {
			return data + offset;
		}

		/** @overload const version */
		FORCE_INLINE const void* getMemberPtr(const std::byte* data, uint16_t offset) noexcept {
			return data + offset;
		}

		/** @brief Check whether any masked bit is marked alive. */
		FORCE_INLINE bool isAlive(uint32_t isAliveData, uint32_t mask) noexcept {
			return isAliveData & mask;
		}

		/** @brief Check whether any bit is marked alive (sector has any live component). */
		FORCE_INLINE bool isSectorAlive(uint32_t isAliveData) noexcept {
			return isAliveData != 0;
		}

		/** @brief Set or clear bits in isAliveData.
		 *  @param isAliveData Reference to alive data to modify
		 *  @param mask Bitmask of bits to modify
		 *  @param value If true, sets the bits; if false, clears them
		 *  @note When value == false, mask should already be ~mask
		 */
		FORCE_INLINE void setAlive(uint32_t& isAliveData, uint32_t mask, bool value) noexcept {
			value ? isAliveData |= mask : isAliveData &= mask;
		}

		/** @brief Mark bits as alive (sets them to 1). */
		FORCE_INLINE void markAlive(uint32_t& isAliveData, uint32_t mask) noexcept {
			isAliveData |= mask;
		}

		/** @brief Mark bits as not alive (clears them to 0). */
		FORCE_INLINE void markNotAlive(uint32_t& isAliveData, uint32_t mask) noexcept {
			isAliveData &= mask;
		}

		/** @brief Fetch component pointer of type T using layout; may be nullptr if not alive. */
		template <typename T>
		const T* getComponent(const std::byte* data, uint32_t isAliveData, SectorLayoutMeta* sectorLayout) {
			if (!data) {
				return nullptr;
			}
			const auto& layout = sectorLayout->getLayoutData<T>();
			return getMember<T>(data, layout.offset, layout.isAliveMask, isAliveData);
		}

		/** @overload non-const version */
		template <typename T>
		T* getComponent(std::byte* data, uint32_t isAliveData, SectorLayoutMeta* sectorLayout) {
			if (!data) {
				return nullptr;
			}
			const auto& layout = sectorLayout->getLayoutData<T>();
			return getMember<T>(data, layout.offset, layout.isAliveMask, isAliveData);
		}

		/** @brief (Re)construct member T in place and mark it alive. Destroys previous value if present.
		 *  @param data Raw pointer to sector data
		 *  @param isAliveData Reference to alive data (will be modified)
		 *  @param layout Layout data for this component type
		 *  @param args Constructor arguments
		 *  @return Pointer to constructed component
		 */
		template<typename T, class ...Args>
		T* emplaceMember(std::byte* data, uint32_t& isAliveData, const LayoutData& layout, Args&&... args) {
			void* memberPtr = getMemberPtr(data, layout.offset);
			if constexpr (!std::is_trivially_destructible_v<T>) {
				if (isAlive(isAliveData, layout.isAliveMask)) {
					layout.functionTable.destructor(memberPtr);
				}
			}

			markAlive(isAliveData, layout.isAliveMask);

			return new(memberPtr) T(std::forward<Args>(args)...);
		}

		/** @brief Destroy a specific member if alive and clear its liveness bits. */
		inline void destroyMember(std::byte* data, uint32_t& isAliveData, const LayoutData& layout) {
			if (!layout.isTrivial) {
				if (isAlive(isAliveData, layout.isAliveMask)) {
					layout.functionTable.destructor(getMemberPtr(data, layout.offset));
				}
			}
			setAlive(isAliveData, layout.isNotAliveMask, false);
		}

		/** @brief Destroy all alive members in sector data and clear liveness bits. */
		inline void destroySectorData(std::byte* data, uint32_t& isAliveData, const SectorLayoutMeta* layouts) {
			if (!data || !isSectorAlive(isAliveData)) {
				return;
			}

			if (!layouts->isTrivial()) {
				for (const auto& layout : *layouts) {
					if (isAlive(isAliveData, layout.isAliveMask)) {
						layout.functionTable.destructor(getMemberPtr(data, layout.offset));
					}
				}
			}
			isAliveData = 0;
		}

		/** @brief Copy-assign member T into destination and mark alive. */
		template<typename T>
		T* copyMember(const T& from, std::byte* toData, uint32_t& toIsAlive, const LayoutData& layout) {
			assert(toData);
			Sector::destroyMember(toData, toIsAlive, layout);
			setAlive(toIsAlive, layout.isAliveMask, true);
			return new(getMemberPtr(toData, layout.offset)) T(from);
		}

		/** @brief Move-assign member T into destination and mark alive. */
		template<typename T>
		T* moveMember(T&& from, std::byte* toData, uint32_t& toIsAlive, const LayoutData& layout) {
			assert(toData);
			Sector::destroyMember(toData, toIsAlive, layout);
			setAlive(toIsAlive, layout.isAliveMask, true);
			return new(getMemberPtr(toData, layout.offset)) T(std::forward<T>(from));
		}

		/** @brief Copy-assign an opaque member using layout function table. */
		inline void* copyMember(const void* from, std::byte* toData, uint32_t& toIsAlive, const LayoutData& layout) {
			assert(toData);
			Sector::destroyMember(toData, toIsAlive, layout);

			auto ptr = getMemberPtr(toData, layout.offset);
			if (!from) {
				return ptr;
			}
			layout.functionTable.copy(ptr, from);
			setAlive(toIsAlive, from ? layout.isAliveMask : layout.isNotAliveMask, from != nullptr);
			return ptr;
		}

		/** @brief Move-assign an opaque member using layout function table. */
		inline void* moveMember(void* from, std::byte* toData, uint32_t& toIsAlive, const LayoutData& layout) {
			assert(toData);
			Sector::destroyMember(toData, toIsAlive, layout);
			auto ptr = getMemberPtr(toData, layout.offset);
			if (!from) {
				return ptr;
			}
			layout.functionTable.move(ptr, from);
			setAlive(toIsAlive, from ? layout.isAliveMask : layout.isNotAliveMask, from != nullptr);
			layout.functionTable.destructor(from);
			return ptr;
		}

		/** @brief Copy sector data from one location to another.
		 *  @param fromData Source sector data
		 *  @param fromIsAlive Source alive state
		 *  @param toData Destination sector data
		 *  @param toIsAlive Reference to destination alive state (will be modified)
		 *  @param layouts Sector layout metadata
		 */
		inline void copySectorData(const std::byte* fromData, uint32_t fromIsAlive,
		                           std::byte* toData, uint32_t& toIsAlive,
		                           const SectorLayoutMeta* layouts) {
			assert(fromData != toData);
			assert(fromData && toData);
			Sector::destroySectorData(toData, toIsAlive, layouts);

			toIsAlive = fromIsAlive;

			for (const auto& layout : *layouts) {
				if (!isAlive(fromIsAlive, layout.isAliveMask)) {
					continue;
				}
				layout.functionTable.copy(getMemberPtr(toData, layout.offset),
				                          getMemberPtr(const_cast<std::byte*>(fromData), layout.offset));
			}
		}

		/** @brief Move sector data from one location to another.
		 *  @param fromData Source sector data
		 *  @param fromIsAlive Reference to source alive state (will be cleared)
		 *  @param toData Destination sector data
		 *  @param toIsAlive Reference to destination alive state (will be modified)
		 *  @param layouts Sector layout metadata
		 */
		inline void moveSectorData(std::byte* fromData, uint32_t& fromIsAlive,
		                           std::byte* toData, uint32_t& toIsAlive,
		                           const SectorLayoutMeta* layouts) {
			assert(fromData != toData);
			assert(fromData && toData);
			Sector::destroySectorData(toData, toIsAlive, layouts);

			toIsAlive = fromIsAlive;

			for (const auto& layout : *layouts) {
				if (!isAlive(fromIsAlive, layout.isAliveMask)) {
					continue;
				}
				layout.functionTable.move(getMemberPtr(toData, layout.offset),
				                          getMemberPtr(fromData, layout.offset));
			}

			Sector::destroySectorData(fromData, fromIsAlive, layouts);
		}

	} // namespace Sector

} // namespace ecss::Memory
