#pragma once

#include <ecss/memory/SectorLayoutMeta.h>

namespace ecss::Memory {
	/** @brief
	 * Sector stores data for multiple component types; per-type offsets are described by SectorLayoutMeta.
	 * \code
	 * [SECTOR]
	 * 0x + 0                                       { id }
	 * 0x + sizeof(SectorId)                        { isAliveData }
	 * 0x + sizeof(Sector)                          { SomeMember }
	 * 0x + sizeof(Sector) + sizeof(SomeMember)     { SomeMember1 }
	 * 0x + sizeof(Sector) + ... + sizeof(MemberN)  { SomeMemberN }
	 * \endcode
	 * \param id           Sector identifier.
	 * \param isAliveData  Bitfield of component liveness; 32 bits => up to 32 components per sector.
	 */
	struct Sector final {
		SectorId id;
		uint32_t isAliveData;

		/** @brief Set or clear bits in `isAliveData`.
		 *  \param mask  Bitmask of bits to modify (1s mark the affected bits).
		 *  \param value If true, sets the bits in `mask`; if false, clears them.
		 *  \note expected that if value == false - mask is already ~mask
		 */
		inline constexpr void setAlive(size_t mask, bool value) { value ? isAliveData |= mask : isAliveData &= mask; }
		/** @brief Branch-free convenience for marking bits as alive (sets them to 1).
		 *  \param mask Bitmask of bits to set in `isAliveData`.
		 */
		inline constexpr void markAlive(size_t mask) { isAliveData |= mask; }
		/** @brief Branch-free convenience for marking bits as not alive (clears them to 0).
		 *  \param mask Bitmask of bits to clear in `isAliveData`.
		 */
		inline constexpr void markNotAlive(size_t mask) { isAliveData &= mask; }
		/** @brief Check whether any masked bit is marked alive.
		 *  \param mask Bitmask to test against `isAliveData`.
		 *  \return true if any of the masked bits are set; otherwise, false.
		 */
		inline constexpr bool isAlive(size_t mask) const { return isAliveData & mask; }
		/** @brief Check whether any bit is marked alive.
		 *  \return true if any of the bits are set; otherwise, false.
		 */
		inline constexpr bool isSectorAlive() const { return isAliveData != 0; }

	public:
		/** @brief Get a member pointer by offset if the corresponding liveness bit is set.
		*  \return Pointer to T or nullptr if not alive.
		*/
		template<typename T>
		inline constexpr T* getMember(size_t offset, size_t mask) const { return isAliveData & mask ? static_cast<T*>(getMemberPtr(this, offset)) : nullptr; }
		/** @overload */
		template<typename T>
		inline constexpr T* getMember(size_t offset, size_t mask) { return isAliveData & mask ? static_cast<T*>(getMemberPtr(this, offset)) : nullptr; }

		/** @brief Get a member pointer using layout metadata; returns nullptr if not alive. */
		template<typename T>
		inline constexpr T* getMember(const LayoutData& layout) const { return getMember<T>(layout.offset, layout.isAliveMask); }
		/** @overload */
		template<typename T>
		inline constexpr T* getMember(const LayoutData& layout) { return getMember<T>(layout.offset, layout.isAliveMask); }

		/** @brief Raw member address by byte offset from the sector base. */
		inline static void* getMemberPtr(const Sector* sectorAdr, size_t offset) { return const_cast<char*>(reinterpret_cast<const char*>(sectorAdr) + offset); }
		/** @overload */
		inline static void* getMemberPtr(Sector* sectorAdr, size_t offset) { return reinterpret_cast<char*>(sectorAdr) + offset; }

		/** @brief Fetch component pointer of type T from a sector using its layout; may be nullptr if not alive. */
		template <typename T>
		inline static const T* getComponentFromSector(const Sector* sector, SectorLayoutMeta* sectorLayout) {
			if (!sector) {
				return nullptr;
			}
			const auto& layout = sectorLayout->getLayoutData<T>();
			return sector->getMember<T>(layout.offset, layout.isAliveMask);
		}
		/** @overload */
		template <typename T>
		inline static T* getComponentFromSector(Sector* sector, SectorLayoutMeta* sectorLayout) {
			if (!sector) {
				return nullptr;
			}
			const auto& layout = sectorLayout->getLayoutData<T>();
			return sector->getMember<T>(layout.offset, layout.isAliveMask);
		}

		/** @brief (Re)construct member T in place and mark it alive. Destroys previous value if present. */
		template<typename T, class ...Args>
		inline T* emplaceMember(const LayoutData& layout, Args&&... args) {
			void* memberPtr = getMemberPtr(this, layout.offset);
			if constexpr (!std::is_trivially_destructible_v<T>) {
				if (isAlive(layout.isAliveMask)) {
					layout.functionTable.destructor(memberPtr);
				}
			}

			markAlive(layout.isAliveMask);

			return new(memberPtr)T(std::forward<Args>(args)... );
		}

		/** @brief Static helper: emplace member into a given sector. */
		template<typename T, class ...Args>
		inline static T* emplaceMember(Sector* sector, const LayoutData& layout, Args&&... args) {
			assert(sector);

			void* memberPtr = getMemberPtr(sector, layout.offset);
			if constexpr (!std::is_trivially_destructible_v<T>) {
				if (sector->isAlive(layout.isAliveMask)) {
					layout.functionTable.destructor(memberPtr);
				}
			}

			sector->markAlive(layout.isAliveMask);

			return new(memberPtr)T{ std::forward<Args>(args)... };
		}

		/** @brief Copy-assign member T into \p to (replaces existing value) and mark alive. */
		template<typename T>
		inline static T* copyMember(const T& from, Sector* to, const LayoutData& layout) {
			assert(to);
			to->destroyMember(layout);
			to->setAlive(layout.isAliveMask, true);
			return new(getMemberPtr(to, layout.offset))T(from);
		}
		/** @brief Move-assign member T into \p to (replaces existing value) and mark alive. */
		template<typename T>
		inline static T* moveMember(T&& from, Sector* to, const LayoutData& layout) {
			assert(to);
			to->destroyMember(layout);
			to->setAlive(layout.isAliveMask, true);
			return new(getMemberPtr(to, layout.offset))T(std::forward<T>(from));
		}

		/** @brief Copy-assign an opaque member using layout function table; marks alive if \p from is non-null. */
		inline static void* copyMember(const void* from, Sector* to, const LayoutData& layout) {
			assert(to);
			to->destroyMember(layout);
			
			auto ptr = getMemberPtr(to, layout.offset);
			if (!from) {
				return ptr;
			}
			layout.functionTable.copy(ptr, const_cast<void*>(from));
			to->setAlive(!!from ? layout.isAliveMask : layout.isNotAliveMask, !!from);
			return ptr;
		}

		/** @brief Move-assign an opaque member using layout function table; marks alive if \p from is non-null. */
		inline static void* moveMember(void* from, Sector* to, const LayoutData& layout) {
			assert(to);
			to->destroyMember(layout);
			auto ptr = getMemberPtr(to, layout.offset);
			if (!from) {
				return ptr;
			}
			layout.functionTable.move(ptr, from);
			to->setAlive(!!from ? layout.isAliveMask : layout.isNotAliveMask, !!from);
			layout.functionTable.destructor(from);
			return ptr;
		}

		/** @brief Copy-assign a sector using layout function table. */
		inline static Sector* copySector(Sector* from, Sector* to, const SectorLayoutMeta* layouts) {
			assert(from != to);
			assert(from && to);
			assert(to);
			destroySector(to, layouts);

			if constexpr (std::is_trivially_copyable_v<Sector>) {
				memcpy(to, from, sizeof(Sector));
			}
			else {
				new (to)Sector(*from);
			}

			for (const auto& layout : *layouts) {
				if (!from->isAlive(layout.isAliveMask)) {
					continue;
				}

				layout.functionTable.copy(getMemberPtr(to, layout.offset), getMemberPtr(from, layout.offset));
			}

			return to;
		}

		/** @brief Move-assign a sector using layout function table. */
		inline static Sector* moveSector(Sector* from, Sector* to, const SectorLayoutMeta* layouts) {
			assert(from != to);
			assert(from && to);
			assert(to);
			destroySector(to, layouts);

			new (to)Sector(std::move(*from));
			for (const auto& layout : *layouts) {
				if (!from->isAlive(layout.isAliveMask)) {
					continue;
				}

				layout.functionTable.move(getMemberPtr(to, layout.offset), getMemberPtr(from, layout.offset));

				to->setAlive(layout.isAliveMask, true);
			}
			destroySector(from, layouts);
			return to;
		}

		/** @brief Destroy all alive members in the sector using layout metadata and clear liveness bits. */
		inline static void destroySector(Sector* sector, const SectorLayoutMeta* layouts) {
			if (!sector || !sector->isSectorAlive()) {
				return;
			}

			if (!layouts->isTrivial()) {
				for (const auto& layout : (*layouts)) {
					if (sector->isAlive(layout.isAliveMask)) {
						layout.functionTable.destructor(ecss::Memory::Sector::getMemberPtr(sector, layout.offset));
					}
				}
			}
			sector->isAliveData = 0;
		}

		/** @brief Destroy a specific member if alive and clear its liveness bits. */
		inline void destroyMember(const LayoutData& layout) {
			if (!layout.isTrivial) {
				if (isAlive(layout.isAliveMask)) {
					layout.functionTable.destructor(getMemberPtr(this, layout.offset));
				}
			}

			setAlive(layout.isNotAliveMask, false);
		}
	};

	static_assert(sizeof(Dummy::Sector) == sizeof(Sector), "Dummy and real Sector differ!");
	static_assert(std::is_trivial_v<Sector>); //Sector should be always trivial - to enable trivially copy trivial types inside
}
