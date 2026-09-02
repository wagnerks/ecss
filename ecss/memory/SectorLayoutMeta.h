#pragma once
#include <limits>

#include <cassert>
#include <cstdint>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

#include <ecss/Types.h>

namespace ecss::Memory {

	/** @brief Metadata describing how a component type is laid out within sector data.
	*
	* Each component stored in a sector has a corresponding LayoutData that
	* specifies its byte offset, liveness bit masks, and the functions required
	* to move/copy/destroy the value. This enables type-erased operations while
	* keeping the sector data trivially copyable for trivial types.
	*
	* Notes:
	* - For trivial types, \p isTrivial should be true and the \p functionTable
	*   may hold empty/no-op functors.
	* - \p isAliveMask marks the bit(s) that indicate the component is present.
	* - \p isNotAliveMask is typically the bitwise complement used when clearing.
	* - Offsets now start from 0 (no embedded id/isAlive header in sector data).
	*/
	struct LayoutData {
		struct FunctionTable {
			void (*move)(void* dest, void* src);
			void (*copy)(void* dest, const void* src);
			void (*destructor)(void* src);
		};

		FunctionTable functionTable; // Type-erased operations for this component.
		size_t typeHash = 0;         // Optional: stable hash/ID of the component type.
		uint32_t isAliveMask = 0;    // Bit(s) set when the component is alive/present.
		uint32_t isNotAliveMask = 0; // Bit mask used to clear liveness (often ~isAliveMask & mask_width).
		uint16_t offset = 0;	     // Byte offset of the component within the sector data (starts from 0).
		uint16_t index = 0;          // Index of this component within the sector layout.
		bool isTrivial = false;      // True if the component is trivially destructible/copiable/movable.
	};

	/**
	 * @brief Immutable description of one sector's component layout.
	 *
	 * Built once by create() and never changed again: SectorsArray::create() keeps one
	 * instance per distinct type pack in a function-local static, so every array sharing a
	 * pack shares this object for the lifetime of the process, and the LayoutData records
	 * live at fixed addresses. Everything that reads a layout therefore holds it by
	 * const pointer -- the only mutation in the type is create() initialising what it just
	 * allocated, which is why initData/initLayoutData are private.
	 * @thread_safety Internally synchronized, because it never changes -- and that applies to
	 *                every member below. The layout is built once, by create(), and is const from
	 *                then on, so any number of threads may read it without a lock and none of it
	 *                can go stale. Only create() and the init* helpers it calls are exclusive,
	 *                and they run before anything can name the object.
	 */
	struct SectorLayoutMeta {
		// Non-copyable / non-movable: exactly one instance per type pack, shared by every
		// array built from it, so copying one would silently fork a shared invariant.
		SectorLayoutMeta(const SectorLayoutMeta& other) = delete;
		SectorLayoutMeta(SectorLayoutMeta&& other) noexcept = delete;
		SectorLayoutMeta& operator=(const SectorLayoutMeta& other) = delete;
		SectorLayoutMeta& operator=(SectorLayoutMeta&& other) noexcept = delete;
		~SectorLayoutMeta() = default;

	private:
		/**
		 * @brief Initialize LayoutData for a single component type U.
		 *
		 * Populates per-type metadata: byte offset in the sector data, index,
		 * liveness bit masks, triviality flag, and the type-erased function table
		 * (move/copy/destroy).
		 *
		 * @tparam U    Component type.
		 * @param data  LayoutData record to fill.
		 * @param index Running index (incremented after use). The index determines
		 *              which liveness bit is used; the mask is (1 << index).
		 * @param offset Byte offset from the beginning of sector data at which U is stored.
		 *
		 * @note U must be move-constructible.
		 * @warning The function table stores operations using type-erased lambdas;
		 *          these must match the object lifetime semantics you expect.
		 */
		template<typename U>
		inline void initLayoutData(LayoutData& data, uint8_t& index, uint16_t offset) const noexcept {
			static_assert(std::is_move_constructible_v<U>, "Type must be move-constructible for use in SectorsArray");

			data.typeHash = SectorLayoutMeta::TypeId<U>();
			data.offset = offset;
			data.index = index++;
			data.isAliveMask = static_cast<uint32_t>(1u << data.index);
			data.isNotAliveMask = ~(data.isAliveMask);
			data.isTrivial = std::is_trivially_copyable_v<U>;
			ecss::detail::checkTriviality<U>();

			data.functionTable.move = [](void* dest, void* src) { new(dest) U(std::move(*static_cast<U*>(src))); };

			data.functionTable.copy = [](void* dest, const void* src)
			{
				if constexpr (std::is_copy_constructible_v<U>) {
					new(dest) U(*static_cast<const U*>(src));
				}
				else { assert(false && "Attempt to copy a move-only type!"); }
			};

			data.functionTable.destructor = [](void* src) { std::destroy_at(static_cast<U*>(src)); };
		}

		/**
		* @brief Initialize an array of LayoutData for a parameter pack of types.
		*
		* Uses the compile-time OffsetArray to compute per-type offsets and calls
		* the single-type initializer for each entry. Offsets now start from 0.
		*
		* @tparam U ... Component types to lay out in the sector.
		*/
		template<typename... U>
		inline void initLayoutData() {
			static_assert(sizeof...(U) <= maxComponentsPerSector, "Too many component types per sector (max 32)");
			uint8_t counter = 0;
			// Use EmptyBase for offset calculation - offsets start from 0
			(initLayoutData<std::remove_const_t<std::remove_pointer_t<std::remove_reference_t<U>>>>(
				layout[counter], counter, 
				static_cast<uint16_t>(types::OffsetArray<types::EmptyBase, U...>::offsets[counter])
			), ...);
		}

	public:
		/**
		 * @brief Forward iterator over the contiguous LayoutData array.
		 *
		 * Provides read-only iteration over all component layout records.
		 */
		class Iterator {
		public:
			using iterator_category = std::forward_iterator_tag;
			using value_type = const LayoutData;
			using difference_type = std::ptrdiff_t;
			using pointer = value_type*;
			using reference = value_type&;

			Iterator(const SectorLayoutMeta* layoutMeta, uint8_t idx) : layoutsArray(layoutMeta->getLayouts() + idx) {}

			Iterator& operator++() { ++layoutsArray; return *this; }
			Iterator operator++(int) { Iterator tmp = *this; ++(*this); return tmp; }

			bool operator==(const Iterator& other) const { return layoutsArray == other.layoutsArray; }
			bool operator!=(const Iterator& other) const { return !(*this == other); }

			reference& operator*() const { return *layoutsArray; }
			reference& operator->() const { return *layoutsArray; }

		private:
			const LayoutData* layoutsArray;
		};

		/// @brief Begin/end iterators over layout records.
		/// @thread_safety Internally synchronized. Walks layout records that are immutable once built.
		Iterator begin() const { return { this, 0 }; }
		/// @thread_safety Internally synchronized. Walks layout records that are immutable once built.
		Iterator end() const { return { this, getTypesCount() }; }

	public:
		/**
		 * @brief Factory: allocate and initialize metadata for a set of component types.
		 *
		 * @tparam Types ... Component types stored in a sector.
		 * @return Newly allocated SectorLayoutMeta*; caller owns and must delete.
		 * @thread_safety Caller must ensure exclusive access. Builds the layout. Nothing may read it until this
		 *                returns; afterwards it never changes again.
		 */
		template<typename... Types>
		static inline SectorLayoutMeta* create()
		{
			auto meta = new SectorLayoutMeta();
			meta->initData<Types...>();
			return meta;
		}

	private:
		/**
		* @brief Compute counts, total size, allocate storage, and populate per-type metadata.
		*
		* Initializes:
		* - `\p count` (number of component types)
		* - `\p totalSize` (bytes for component payloads, starting from offset 0)
		* - `\p typeIds` (stable per-process type tokens)
		* - `\p layout` (array of LayoutData entries)
		* Also computes `mIsTrivial` (true if all components are trivial).
		*/
		template<typename... Types>
		void initData()	{
			// T, T*, T& and const T all resolve to the same type id (DenseTypeIdGenerator::
			// getTypeId strips all three), and one id means one layout record. For const that
			// is exactly right -- view<const T> asks for read-only access to the same object,
			// same size, same offsets. A pointer or a reference is a different size, so it
			// would file sizeof(void*) under the record of the type it names and overwrite it.
			static_assert((!std::is_pointer_v<std::remove_cv_t<Types>> && ...)
				&& (!std::is_reference_v<Types> && ...),
				"a component is stored by value: name A or const A, not A* or A& -- a pointer "
				"shares the type id of what it points to but not its size, and would overwrite "
				"that type's layout record");
			
			// Chunks come from calloc, which promises alignof(std::max_align_t) and no more,
			// and sectors are placed at a stride inside them. An alignas(32) or alignas(64)
			// member would be handed an address that merely looks right in testing.
			static_assert(((alignof(Types) <= alignof(std::max_align_t)) && ...),
				"over-aligned component types are not supported: chunk memory comes from "
				"calloc, which guarantees only max_align_t");
			
			// Offsets and the stride are stored as uint16_t. Past 64 KB the cast below wraps
			// and every sector address after the first is wrong, with nothing to say so.
			static_assert(types::OffsetArray<types::EmptyBase, Types...>::totalSize
				<= std::numeric_limits<uint16_t>::max(),
				"sector larger than 64 KB: component offsets and the stride are uint16_t");
			
			count = types::OffsetArray<types::EmptyBase, Types...>::count;
			totalSize = static_cast<uint16_t>(types::OffsetArray<types::EmptyBase, Types...>::totalSize);
			size_t idx = 0;
			((typeIds[idx++] = TypeId<Types>()), ...);

			initLayoutData<Types...>();
			for (size_t i = 0; i < count; i++) {
				mIsTrivial = mIsTrivial && layout[i].isTrivial;
				if (!mIsTrivial) {
					break;
				}
			}
		}

	public:
		/**
		 * @brief Do these two describe the same sector shape?
		 *
		 * Instances are per type pack *per template instantiation*, so the same components
		 * laid out for SectorsArray<true> and for SectorsArray<false> are two distinct
		 * objects with identical contents. Pointer equality would reject copying between
		 * them, which is a supported operation, so the contents are compared.
		 *
		 * Order is part of the shape: [A, B] and [B, A] give each component a different
		 * offset and a different liveness bit, so they are not compatible.
		 */
		bool isCompatibleWith(const SectorLayoutMeta& other) const noexcept {
			if (count != other.count || totalSize != other.totalSize) {
				return false;
			}
			for (uint8_t i = 0; i < count; ++i) {
				if (typeIds[i] != other.typeIds[i]) { return false; }
				if (layout[i].offset != other.layout[i].offset) { return false; }
				if (layout[i].isAliveMask != other.layout[i].isAliveMask) { return false; }
			}
			return true;
		}

		/// @return Total bytes consumed by sector data (component payloads only, no header).
		uint16_t getTotalSize() const {	return totalSize; }

		/// @return True if all component types are trivial (copy/move/destroy are trivial).
		bool isTrivial() const { return mIsTrivial; }

		/// @brief Access LayoutData for a given component type T (throws in debug if not present).
		template<typename T>
		inline const LayoutData& getLayoutData() const { return layout[getIndex<T>()]; }

		/// @brief Access LayoutData by index (0..count-1).
		const LayoutData& getLayoutData(uint8_t idx) const { return layout[idx]; }

	public:
		/**
		 * @brief Get a process-stable (but not ABI/serialization-stable) type token for T.
		 *
		 * Implementation uses the address of an internal static tag, which is:
		 * - Unique per (type, process)
		 * - NOT stable across processes/builds/DSOs
		 *
		 * @tparam T Component type.
		 * @return Opaque size_t token; suitable for in-process lookup only.
		 * @warning Do not persist/serialize this value; it is not stable across runs.
		 */
		template<typename T>
		inline static size_t TypeId() { return TypeIdImpl<std::remove_const_t<std::remove_pointer_t<std::remove_reference_t<T>>>>(); }

	private:
		SectorLayoutMeta() = default;

		template<typename T>
		inline static size_t TypeIdImpl() { static char tag; return reinterpret_cast<size_t>(&tag); }

		template<typename T>
		inline uint8_t getIndex() const { return getIndexByType(TypeId<T>()); }

		const LayoutData* getLayouts() const { return layout; }

		/**
		* @brief Find component index by type token.
		* @param hash Type token returned by TypeId<T>().
		* @return Index in [0, count), or `count` after an assertion failure in debug builds.
		*
		* @note Linear scan is fine for small component counts typical per sector.
		*/
		uint8_t getIndexByType(size_t hash) const {
			for (uint8_t i = 0; i < count; ++i) {
				if (typeIds[i] == hash) {
					return i;
				}
			}

			assert(false);
			return count;
		}

	public:
		// Iteration needs to know whether a sector holds exactly one component, which is
		// what makes a chunk a plain array of it. Reads data fixed at construction.
		uint8_t getTypesCount() const { return count; }
	private:

	private:
		inline static constexpr size_t maxComponentsPerSector = 32; // Arbitrary limit for sanity checks.

		LayoutData	layout[maxComponentsPerSector];
		size_t		typeIds[maxComponentsPerSector];

		// Overall layout properties.
		uint16_t totalSize = 0; ///< Total bytes required for sector data (component payloads only).
		uint8_t  count     = 0; ///< Number of component types in this layout.
		bool mIsTrivial = true; ///< True if all components are trivial.
	};
}
