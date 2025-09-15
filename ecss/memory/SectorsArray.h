#pragma once

#include <algorithm>
#include <cassert>
#include <shared_mutex>

#include <utility>
#include <vector>

#include <ecss/memory/Sector.h>
#include <ecss/EntitiesRanges.h>
#include <ecss/threads/SyncManager.h>
#include <ecss/threads/PinCounters.h>
#include <ecss/memory/ChunksAllocator.h>

namespace ecss
{
	template <bool ThreadSafe, typename Allocator>
	class Registry;

	template <bool ThreadSafe, typename Allocator, bool Ranged, typename T, typename ...ComponentTypes>
	class ArraysView;
}

namespace ecss::Memory {
#define SHARED_LOCK() auto lock = ecss::Threads::sharedLock(mtx, true)
#define UNIQUE_LOCK() auto lock = ecss::Threads::uniqueLock(mtx, true)

/// \brief Thread-safety guard macro for single-expression sections.
/// \param TS_FLAG boolean template flag (usually ThreadSafe)
/// \param LOCK_MACRO SHARED or UNIQUE
/// \param EXPR expression to execute under the chosen lock when TS_FLAG==true
#define TS_GUARD(TS_FLAG, LOCK_MACRO, EXPR) \
	do {if constexpr (TS_FLAG) { LOCK_MACRO##_LOCK(); EXPR; } else { EXPR; }} while(0)

/// \brief Thread-safety guard with an additional pre-expression “sink”.
/// \param TS_FLAG boolean template flag (usually ThreadSafe)
/// \param LOCK_MACRO SHARED or UNIQUE
/// \param ADDITIONAL_SINK extra code executed under the lock before EXPR
/// \param EXPR expression to execute
#define TS_GUARD_S(TS_FLAG, LOCK_MACRO, ADDITIONAL_SINK, EXPR) \
	do {if constexpr (TS_FLAG) { LOCK_MACRO##_LOCK(); ADDITIONAL_SINK; EXPR; } else { EXPR; }} while(0)

	/**
	 * \brief RAII pin for a sector to prevent moves/erases while in use.
	 *
	 * Pins a sector in \ref Threads::PinCounters on construction and unpins on
	 * destruction or \ref release().
	 *
	 * \warning Never retain a PinnedSector beyond the lifetime of the owning
	 *          SectorsArray; doing so would call into a dead PinCounters.
	 */
	struct PinnedSector {
		PinnedSector() = default;

		/**
		 * \brief Construct and pin a sector id in the given PinCounters.
		 * \param o   Pin counters owner.
		 * \param s   Raw sector pointer (may be nullptr).
		 * \param sid Sector id; must not be INVALID_ID.
		 */
		PinnedSector(const Threads::PinCounters& o, Sector* s, SectorId sid) : sec(s), owner(&o), id(sid) {
			assert(id != INVALID_ID);
			const_cast<Threads::PinCounters*>(owner)->pin(id);
		}

		PinnedSector(const PinnedSector&) = delete;
		PinnedSector& operator=(const PinnedSector&) = delete;

		PinnedSector(PinnedSector&& other) noexcept { *this = std::move(other); }
		PinnedSector& operator=(PinnedSector&& other) noexcept {
			if (this == &other) { return *this; }

			release();
			owner = other.owner;
			sec = other.sec;
			id = other.id;

			other.owner = nullptr;
			other.sec = nullptr;
			other.id = static_cast<SectorId>(INVALID_ID);

			return *this;
		}

		~PinnedSector() { release(); }

		void release() {
			if (owner) {
				const_cast<Threads::PinCounters*>(owner)->unpin(id);
			}

			owner = nullptr;
			sec = nullptr;
			id = static_cast<SectorId>(INVALID_ID);
		}

		Sector* get() const { return sec; }
		Sector* operator->() const { return sec; }
		explicit operator bool() const { return sec != nullptr; }
		SectorId getId() const { return id; }
	private:
		Sector* sec = nullptr;
		const Threads::PinCounters* owner = nullptr;
		SectorId id = INVALID_ID;
	};

	/** @brief Data container with sectors of custom data in it and with optional thread-safety..
	 * Synchronization lives in the container (allocator is single-threaded under write lock).
	 * 
	 * @tparam ThreadSafe  If true, public APIs acquire shared/unique locks and use PinCounters
	 *                     so writers wait for readers. If false, calls are non-synchronized.
	 * @tparam Allocator   Storage policy (e.g., \ref ChunksAllocator<8192>).
	 *
	 * Design:
	 *  - Storage is provided by @p Allocator (chunked, contiguous in chunks).
	 *  - A direct map (SectorId -> Sector*) is kept by the allocator for O(1) lookup.
	 *  - This wrapper adds thread-safety (optional), pin/unpin semantics and
	 *    higher-level operations (erase queuing, defragmentation, ranged iteration).
	 *  - Fast O(1) lookup by SectorId via an internal id->pointer map.
	 *  - Supports erasure (immediate or deferred), defragmentation, and ranged iteration.
	 *
	 * Concurrency (when ThreadSafe = true):
	 *  - Readers use shared locks; writers use unique locks.
	 *  - PinCounters ensure a sector (or the whole array) is not moved/destroyed while pinned.
	 *  - Vector buffers inside the allocator use RetireAllocator to defer frees after reallocation;
	 *    old buffers are reclaimed in @ref processPendingErases() via emptyVectorsBin().
	 *
	 * Iteration:
	 *  - @ref Iterator — linear walk over all sectors (including dead ones).
	 *  - @ref IteratorAlive — linear walk over sectors whose isAliveData matches a mask.
	 *  - @ref RangedIterator — walks only specified [begin,end) index ranges.
	 *  - @ref RangedIteratorAlive — same as RangedIterator but filtered by alive mask.
	 *  
	 *  \note In ThreadSafe mode, writers wait for pins to clear before moving/erasing.
	 *  
	 */
	template<bool ThreadSafe = true, typename Allocator = ChunksAllocator<8192>>
	class SectorsArray final {
		template<bool, typename>
		friend class SectorsArray;

		template<bool, typename>
		friend class ecss::Registry;

		template <bool TS, typename Alloc, bool Ranged, typename T, typename ...ComponentTypes>
		friend class ecss::ArraysView;

	public:
		/// \brief Common iterator typedefs injected into iterator classes.
#define ITERATOR_COMMON_USING(IteratorName)															\
		using iterator_concept  = std::forward_iterator_tag;										\
		using iterator_category = std::forward_iterator_tag;										\
		using value_type = Sector*;																	\
		using difference_type = std::ptrdiff_t;														\
		using pointer = Sector*;																	\
		using reference = Sector*;																	\
		IteratorName() = default;																	\
		inline IteratorName operator++(int) { auto tmp = *this; ++(*this); return cursor; }			\
		inline bool operator!=(const IteratorName& other) const { return !(*this == other); }		\
		inline bool operator==(const IteratorName& other) const { return cursor == other.cursor; }	\
		inline value_type operator*() const { return *cursor; }										\
		inline value_type operator->() const { return *cursor; }									\
		inline size_t linearIndex() const { return cursor.linearIndex(); }							\
		inline std::byte* rawPtr()  const { return cursor.rawPtr(); }

		 /**
		 * \brief Linear iterator over sectors (includes dead sectors).
		 * \note Use \ref IteratorAlive to iterate only “alive” sectors for a type.
		 */
		class Iterator {
		public:
			ITERATOR_COMMON_USING(Iterator)

			Iterator(const SectorsArray* array, size_t idx) : cursor(array->mAllocator.getCursor(std::min(idx, array->sizeImpl()))) { }

			inline Iterator& operator++() noexcept { return ++cursor, *this; }
			Iterator& operator+=(difference_type n) noexcept { cursor = cursor + n; return *this; }
			Iterator operator+(difference_type n) const noexcept { Iterator t(*this); t += n; return t; }
			friend Iterator operator+(difference_type n, Iterator it) noexcept { it += n; return it; }

			reference operator[](difference_type n) const noexcept { return *(*this + n); }
		private:
			typename Allocator::Cursor cursor;
		};

		template<bool TS = ThreadSafe> Iterator begin() const { TS_GUARD(TS, SHARED, return Iterator(this, 0);); }
		template<bool TS = ThreadSafe> Iterator end()   const { TS_GUARD(TS, SHARED, return Iterator(this, sizeImpl());); }

		/**
		 * \brief Linear iterator that skips sectors where the given type is not alive.
		 * \details Filters by \p aliveMask, typically \ref getLayoutData<T>().isAliveMask.
		 */
		class IteratorAlive {
		public:
			ITERATOR_COMMON_USING(IteratorAlive)

			IteratorAlive(const SectorsArray* array, size_t idx, size_t sz, uint32_t aliveMask)
			: IteratorAlive(array, EntitiesRanges{{ EntitiesRanges::range{static_cast<uint32_t>(idx), static_cast<uint32_t>(sz)} } }, aliveMask) {}

			IteratorAlive(const SectorsArray* array, const EntitiesRanges& range, uint32_t aliveMask) : cursor(array->mAllocator.getRangesCursor(range, array->sizeImpl())), mTypeAliveMask(aliveMask) {
				while (cursor && !(cursor->isAliveData & mTypeAliveMask)) { cursor.step(); }
			}

			inline IteratorAlive& operator++() { do { cursor.step(); } while (cursor && !(cursor->isAliveData & mTypeAliveMask)); return *this; }

		private:
			typename Allocator::RangesCursor cursor;
			uint32_t mTypeAliveMask = 0;
		};

		template<class T, bool TS = ThreadSafe>
		IteratorAlive beginAlive()								const { TS_GUARD(TS, SHARED, return IteratorAlive(this, 0, sizeImpl(), getLayoutData<T>().isAliveMask);); }
		template<class T, bool TS = ThreadSafe>
		IteratorAlive beginAlive(const EntitiesRanges& ranges)  const { TS_GUARD(TS, SHARED, return IteratorAlive( this, ranges, getLayoutData<T>().isAliveMask );); }

		template<bool TS = ThreadSafe> IteratorAlive endAlive() const { TS_GUARD(TS, SHARED, return IteratorAlive();); }

		/**
		 * \brief Iterator that walks a set of index ranges.
		 * \details Use \ref beginRanged / \ref endRanged to iterate a precomputed \ref EntitiesRanges.
		 */
		class RangedIterator {
		public:
			ITERATOR_COMMON_USING(RangedIterator)

			RangedIterator(const SectorsArray* a, const EntitiesRanges& r) : cursor(a->mAllocator.getRangesCursor(r, a->sizeImpl())) {}

			void advanceToId(SectorId id) {	while (cursor && cursor->id < id) { cursor.step(); } }

			inline RangedIterator& operator++() noexcept { cursor.step(); return *this; }

		private:
			typename Allocator::RangesCursor cursor;
		};

		template<bool TS = ThreadSafe> RangedIterator beginRanged(const EntitiesRanges& ranges) const { TS_GUARD(TS, SHARED, return RangedIterator(this, ranges);); }
		template<bool TS = ThreadSafe> RangedIterator endRanged()   const { TS_GUARD(TS, SHARED, return RangedIterator();); }

	public:
		// copy
		// it allows to copy array with different allocator, if allocator supports it
		template<bool T, typename Alloc>
		SectorsArray(const SectorsArray<T, Alloc>& other) { *this = other; }
		SectorsArray(const SectorsArray& other)			  { *this = other; }

		template<bool T, typename Alloc>
		SectorsArray& operator=(const SectorsArray<T, Alloc>& other) { if (!isSameAdr(this, &other)) { copy(other); }  return *this; }
		SectorsArray& operator=(const SectorsArray& other) { if (this != &other) { copy(other); }  return *this; }

		//move
		template<bool T, typename Alloc>
		SectorsArray(SectorsArray<T, Alloc>&& other) noexcept { *this = std::move(other); }
		SectorsArray(SectorsArray&& other)			 noexcept { *this = std::move(other); }

		template<bool T, typename Alloc>
		SectorsArray& operator=(SectorsArray<T, Alloc>&& other) noexcept { if (!isSameAdr(this, &other)) { move(std::move(other)); }  return *this; }
		SectorsArray& operator=(SectorsArray&& other)			noexcept { if (this != &other) { move(std::move(other)); } return *this; }

	private:
		SectorsArray(SectorLayoutMeta* meta, Allocator&& allocator = {}) : mAllocator(std::move(allocator)) { mAllocator.init(meta);
			publishNew(mSectorsMap);
		}

	public:
		~SectorsArray() { clear(); shrinkToFit(); }

		/**
		 * \brief Create an array configured for a set of component types.
		 * \tparam Types... Component types stored in each sector.
		 * \param allocator Optional allocator instance to move into the array.
		 * \return Newly allocated SectorsArray*; caller owns.
		 */
		template <typename... Types>
		static SectorsArray* create(Allocator&& allocator = {}) { static_assert(types::areUnique<Types...>(), "Duplicates detected in SectorsArray types!");
			static SectorLayoutMeta* meta = SectorLayoutMeta::create<Types...>();
			return new SectorsArray(meta, std::move(allocator));
		}
	public: // sector helpers
		template<typename T>
		const LayoutData& getLayoutData() const { return getLayout()->template getLayoutData<T>(); }
		SectorLayoutMeta* getLayout()	  const { return mAllocator.getSectorLayout(); }

	public:
		/**
		 * \brief Pin a concrete sector pointer.
		 * \param sector Sector pointer (may be nullptr).
		 * \return RAII pin; empty if sector==nullptr.
		 */
		template<bool Lock = true>
		[[nodiscard]] PinnedSector pinSector(Sector* sector) const requires(ThreadSafe) { TS_GUARD(Lock, SHARED, return (sector ? PinnedSector{ mPinsCounter, sector, sector->id } : PinnedSector{});); }
		/**
		 * \brief Pin by sector id.
		 * \param id Sector id.
		 * \return RAII pin or empty if not found.
		 */
		template<bool Lock = true>
		[[nodiscard]] PinnedSector pinSector(SectorId id)	 const requires(ThreadSafe) { TS_GUARD(Lock, SHARED, return pinSectorImpl(findSectorImpl(id));); }
		/**
		 * \brief Pin by linear index in storage.
		 * \param idx Linear position (0..size()).
		 */
		template<bool Lock = true>
		[[nodiscard]] PinnedSector pinSectorAt(size_t idx)	 const requires(ThreadSafe) { TS_GUARD(Lock, SHARED, return pinSectorImpl(mAllocator.at(idx));); }

		/**
		 * \brief Pin the last (most recent) sector if any.
		 * \return Empty pin if the array is empty.
		 */
		template<bool Lock = true>
		[[nodiscard]] PinnedSector pinBackSector()			 const requires(ThreadSafe) {
			TS_GUARD(Lock, SHARED, auto contSize = sizeImpl(); return contSize == 0 ? PinnedSector{} : pinSectorImpl(mAllocator.at(contSize - 1)););
		}

	public:
		/**
		 * \brief Erase a range by linear index.
		 * \param beginIdx Starting index.
		 * \param count    Number of sectors to erase.
		 * \param defragment If true, compacts storage immediately.
		 * \note In ThreadSafe mode, writers wait for unpinned sectors.
		 */
		template<bool TS = ThreadSafe>
		void erase(size_t beginIdx, size_t count = 1, bool defragment = false) {
			if constexpr(TS) {
				UNIQUE_LOCK();
				auto begin = Iterator(this, beginIdx);
				if (!*begin) { return; }

				mPinsCounter.waitUntilChangeable(begin->id);
				if (count == 1) {
					erase<false>(begin, defragment);
				}
				else if (count > 1) {
					erase<false>(begin, Iterator(this, beginIdx + count), defragment);
				}
			}
			else {
				if (count == 1) {
					erase(Iterator(this, beginIdx), defragment);
				}
				else if (count > 1) {
					erase(Iterator(this, beginIdx), Iterator(this, beginIdx + count), defragment);
				}
			}
		}

		/// \brief Erase [first,last) via iterators; optionally defragment.
		template<bool TS = ThreadSafe>
		Iterator erase(Iterator it, bool defragment = false) noexcept {
			if (!(*it)) { return it; }
			TS_GUARD_S(TS, UNIQUE, mPinsCounter.waitUntilChangeable(it->id);, return eraseImpl(it, defragment););
		}

		/// \brief Erase [first,last) via iterators; optionally defragment.
		template<bool TS = ThreadSafe>
		Iterator erase(Iterator first, Iterator last, bool defragment = false) noexcept {
			if (first == last || !(*first)) { return first; }
			TS_GUARD_S(TS, UNIQUE, mPinsCounter.waitUntilChangeable(first->id);, return eraseImpl(first, last, defragment););
		}

		/**
		 * \brief Conditional erase over [first,last).
		 * \tparam Func predicate taking Sector*; if true, the sector is erased.
		 * \param defragment If true, compacts storage at the end.
		 */
		template<typename Func, bool TS = ThreadSafe>
		void erase_if(Iterator first, Iterator last, Func&& func, bool defragment = false) noexcept {
			if (first == last || !(*first)) { return; }
			TS_GUARD_S(TS, UNIQUE, mPinsCounter.waitUntilChangeable(first->id);, erase_ifImpl(first, last, std::forward<Func>(func), defragment););
		}

		/**
		 * \brief Queue asynchronous erases by id (deferred while pinned).
		 * \param id    First sector id.
		 * \param count Number of ids to mark for erasure.
		 * \note Actual destruction happens in \ref processPendingErases().
		 */
		void eraseAsync(SectorId id, size_t count = 1) requires(ThreadSafe) {
			for (auto i = id; i < id + count; ++i) {
				eraseAsyncImpl(i);
			}
		}

		/// \brief Find the first sector index at or to the right of \p sectorId.
		template<bool TS = ThreadSafe> size_t findRightNearestSectorIndex(SectorId sectorId) const { TS_GUARD(TS, SHARED, return findRightNearestSectorIndexImpl(sectorId)); }

		template<bool TS = ThreadSafe> bool containsSector(SectorId id)					     const { TS_GUARD(TS, SHARED, return containsSectorImpl(id)); }
		template<bool TS = ThreadSafe> Sector* at(size_t sectorIndex)						 const { TS_GUARD(TS, SHARED, return atImpl(sectorIndex)); }
		//todo make it faster (avoid atomic load twice)
		template<bool TS = ThreadSafe> Sector* findSector(SectorId id)						 const { TS_GUARD(TS, SHARED, return findSectorImpl(id)); }
		template<bool TS = ThreadSafe> Sector* getSector(SectorId id)						 const { TS_GUARD(TS, SHARED, return getSectorImpl(id)); }

		// return INVALID_ID if not found
		template<bool TS = ThreadSafe> size_t getSectorIndex(SectorId id)					 const { TS_GUARD(TS, SHARED, return getSectorIndexImpl(id)); }
		template<bool TS = ThreadSafe> size_t getSectorIndex(Sector* sector)			     const { TS_GUARD(TS, SHARED, return getSectorIndexImpl(sector)); }
		template<bool TS = ThreadSafe> size_t sectorsMapCapacity()							 const { TS_GUARD(TS, SHARED, return sectorsMapCapacityImpl()); }
		template<bool TS = ThreadSafe> size_t capacity()									 const { TS_GUARD(TS, SHARED, return capacityImpl()); }
		template<bool TS = ThreadSafe> size_t size()										 const { TS_GUARD(TS, SHARED, return sizeImpl()); }
		template<bool TS = ThreadSafe> bool empty()										     const { TS_GUARD(TS, SHARED, return emptyImpl()); }
		template<bool TS = ThreadSafe> void shrinkToFit()										   { TS_GUARD(TS, UNIQUE, shrinkToFitImpl()); }

		template<bool TS = ThreadSafe> void reserve(uint32_t newCapacity) { TS_GUARD(TS, UNIQUE, reserveImpl(newCapacity)); }
		template<bool TS = ThreadSafe> void clear()					      { TS_GUARD_S(TS, UNIQUE, mPinsCounter.waitUntilChangeable();, clearImpl(););}

		/**
		 * \brief Defragment to remove holes from erased sectors.
		 * \note In ThreadSafe mode, waits until no pins block compaction.
		 */
		template<bool TS = ThreadSafe>
		void defragment() { TS_GUARD_S(TS, UNIQUE, mPinsCounter.waitUntilChangeable(); , defragmentImpl();); }
		
		template<bool TS = ThreadSafe>
		void tryDefragment() { TS_GUARD_S(TS, UNIQUE, if (mPinsCounter.isArrayLocked()){ return;} , defragmentImpl();); }
		void incDefragmentSize(uint32_t count = 1) { mDefragmentSize += count; }

		template<bool TS = ThreadSafe> auto getDefragmentationSize()			const { TS_GUARD(TS, SHARED, return mDefragmentSize; ); }
		template<bool TS = ThreadSafe> auto getDefragmentationRatio()			const { TS_GUARD(TS, SHARED, return mSize ? (static_cast<float>(mDefragmentSize) / static_cast<float>(mSize)) : 0.f;); }
		template<bool TS = ThreadSafe> bool needDefragment()					const { TS_GUARD(TS, SHARED, return getDefragmentationRatio<false>() > mDefragThreshold; ); }
		/// \brief Set the threshold for \ref needDefragment() (default 0.2f). \note threshold [0.0 ... 1.0]
		template<bool TS = ThreadSafe> void setDefragmentThreshold(float threshold)	  { TS_GUARD(TS, UNIQUE, mDefragThreshold = std::max(0.f, std::min(threshold, 1.f)); ); }

		/**
		 * \brief Insert or overwrite a member T in the sector with id \p sectorId.
		 * \tparam T   Component type or \ref Sector for whole-sector copy/move.
		 * \param sectorId Target sector id (allocated if missing).
		 * \param data     Source object; moved or copied depending on value category.
		 * \return Pointer to constructed member (or sector pointer if T==Sector).
		 */
		template<typename T, bool TS = ThreadSafe>
		std::remove_cvref_t<T>* insert(SectorId sectorId, T&& data) {
			TS_GUARD_S(TS, UNIQUE, mPinsCounter.waitUntilChangeable(sectorId);, return insertImpl(sectorId, std::forward<T>(data)););
		}

		/**
		 * \brief In-place construct member T for sector \p sectorId.
		 * \tparam T Component type.
		 * \param sectorId Target sector id (allocated if missing).
		 * \param args     Constructor arguments for T.
		 * \return Pointer to constructed member.
		 */
		template<typename T, bool TS = ThreadSafe, class... Args>
		T* emplace(SectorId sectorId, Args&&... args)  {
			TS_GUARD_S(TS, UNIQUE, mPinsCounter.waitUntilChangeable(sectorId);, return emplaceImpl<T>(sectorId, std::forward<Args>(args)...););
		}

		/**
		 * \brief Convenience: if a single argument of type T is passed, calls insert();
		 * otherwise calls emplace<T>().
		 */
		template<typename T, bool TS = ThreadSafe, class... Args>
		T* push(SectorId sectorId, Args&&... args) {
			if constexpr (sizeof...(Args) == 1 && (std::is_same_v<std::remove_cvref_t<Args>, T> && ...)) {
				return insert<Args..., TS>(sectorId, std::forward<Args>(args)...);
			}
			else {
				return emplace<T, TS>(sectorId, std::forward<Args>(args)...);
			}
		}

		/**
		 * \brief Maintenance hook to process deferred erases and optionally defragment.
		 * \param withDefragment If true, perform defragmentation when threshold exceeded.
		 * \param sync           If true, take a unique lock inside; for nested callers you can pass false.
		 * \note Always drains the retire bin at the end.
		 */
		void processPendingErases(bool withDefragment = true, bool sync = ThreadSafe) requires(ThreadSafe) {
			auto lock = ecss::Threads::uniqueLock(mtx, sync);
			if (mPendingErase.empty()) {
				if (needDefragment<false>()) {
					if (withDefragment) {
						mPinsCounter.waitUntilChangeable();
						defragmentImpl();
					}
				}

				return;
			}

			auto tmp = std::move(mPendingErase);
			std::ranges::sort(tmp);
			tmp.erase(std::ranges::unique(tmp).begin(), tmp.end());
			for (auto id : tmp) {
				if (mPinsCounter.canMoveSector(id)) {
					Sector::destroySector(getSectorImpl(id), getLayout());
					incDefragmentSize();
					mSectorsMap[id] = nullptr;
				}
				else {
					mPendingErase.emplace_back(id);
				}
			}

			if (needDefragment<false>()) {
				if (withDefragment) {
					mPinsCounter.waitUntilChangeable();
					defragmentImpl();
				}
			}
		}

	private:
		template<bool T, typename Alloc, bool TS = ThreadSafe>
		void copy(const SectorsArray<T, Alloc>& other)  {
			if constexpr (TS) {
				auto lock = ecss::Threads::uniqueLock(mtx, true);
				auto otherLock = ecss::Threads::sharedLock(other.mtx, true);
				mPinsCounter.waitUntilChangeable();

				copyImpl(other);
			}
			else {
				copyImpl(other);
			}
		}

		template<bool T, typename Alloc, bool TS = ThreadSafe>
		void move(SectorsArray<T, Alloc>&& other) {
			if constexpr (TS || T) {
				auto lock = ecss::Threads::uniqueLock(mtx, true);
				auto otherLock = ecss::Threads::uniqueLock(other.mtx, true);
				mPinsCounter.waitUntilChangeable();
				other.mPinsCounter.waitUntilChangeable();

				moveImpl(std::move(other));
			}
			else {
				moveImpl(std::move(other));
			}
		}

	private: // impl functions
		Iterator eraseImpl(Iterator it, bool defragment = false) noexcept {
			if (!(*it) || mSectorsMapView.load().size <= it->id) {
				return it;
			}

			auto idx = it.linearIndex();

			mSectorsMap[it->id] = nullptr;
			Sector::destroySector(*it, getLayout());
			if (defragment) {
				shiftSectorsImpl<Left>(idx + 1);
				mSize--;
			}
			else {
				incDefragmentSize();
			}

			return Iterator(this, idx);
		}
		Iterator eraseImpl(Iterator first, Iterator last, bool defragment = false) noexcept {
			if (first == last || !(*first)) { return first; }

			auto idx = first.linearIndex();
			auto lastIdx = last.linearIndex();

			for (auto it = first; it != last; ++it) {
				if (mSectorsMap.size() > it->id) {
					mSectorsMap[it->id] = nullptr;
				}
				
				Sector::destroySector(*it, getLayout());
			}

			if (defragment) {
				shiftSectorsImpl<Left>(lastIdx, lastIdx - idx);
				mSize -= lastIdx - idx;
			}
			else {
				incDefragmentSize(static_cast<uint32_t>(lastIdx - idx));
			}

			return Iterator(this, idx);
		}

		template<typename Func>
		void erase_ifImpl(Iterator first, Iterator last, Func&& func, bool defragment = false) noexcept {
			if (first == last || !(*first)) { return; }
	
			auto idx = first.linearIndex();
			auto lastIdx = last.linearIndex();
			for (auto it = first; it != last; ++it) {
				if (std::forward<Func>(func)(*it)) {
					mSectorsMap[it->id] = nullptr;
					Sector::destroySector(*it, getLayout());
					incDefragmentSize();
				}
			}

			if (defragment) {
				defragmentImpl(idx);
			}
		}

		size_t findRightNearestSectorIndexImpl(SectorId sectorId) const {
			if (sectorId < mSectorsMapView.load().size) {
				for (auto it = mSectorsMap.begin() + sectorId, end = mSectorsMap.end(); it != end; ++it) {
					if (*it) {
						return mAllocator.find(*it);
					}
				}
			}

			return mSize;
		}

		bool containsSectorImpl(SectorId id)	const { return findSectorImpl(id) != nullptr; }

		Sector* atImpl(size_t sectorIndex)		const { assert(sectorIndex < mSize); return mAllocator.at(sectorIndex); }
		Sector* findSectorImpl(SectorId id)		const { auto map = mSectorsMapView.load(); return id < map.size ? map.vectorData[id] : nullptr; }
		Sector* getSectorImpl(SectorId id)		const { assert(id < mSectorsMapView.load().size); return mSectorsMapView.load().vectorData[id]; }
		// return INVALID_ID if not found
		size_t getSectorIndexImpl(SectorId id)	const { return mAllocator.find(findSectorImpl(id)); }
		size_t getSectorIndexImpl(Sector* sector)	const { return mAllocator.find(sector); }

		size_t sectorsMapCapacityImpl()			const { return mSectorsMapView.load().size; }
		size_t capacityImpl()					const { return mAllocator.capacity(); }
		size_t sizeImpl()						const { return mSize; }
		bool   emptyImpl()						const { return !mSize; }

		void shrinkToFitImpl() { mAllocator.deallocate(mSize, mAllocator.capacity()); }

		void clearImpl() {
			if (mSize) {
				if (!getLayout()->isTrivial()) { //call destructors for nontrivial types
					for (auto it = Iterator(this, 0), endIt = Iterator(this, sizeImpl()); it != endIt; ++it) {
						Sector::destroySector(*it, getLayout());
						incDefragmentSize();
					}
				}

				mSectorsMap.clear();
				publishNew(mSectorsMap);
				mPendingErase.clear();
				mSize = 0;
			}
		}

		void reserveImpl(uint32_t newCapacity) {
			if (mAllocator.capacity() < newCapacity) {
				mAllocator.allocate(newCapacity);
				if (newCapacity > mSectorsMapView.load().size) {
					mSectorsMap.resize(newCapacity, nullptr);
					publishNew(mSectorsMap);
				}
			}
		}

		template<typename T>
		std::remove_reference_t<T>* insertImpl(SectorId sectorId, T&& data) {
			using U = std::remove_cvref_t<T>;

			Sector* sector = acquireSectorImpl(sectorId);
			if constexpr (std::is_same_v<U, Sector>) {
				if constexpr (std::is_lvalue_reference_v<T>) {
					return Sector::copySector(std::addressof(data), sector, getLayout());
				}
				else {
					return Sector::moveSector(std::addressof(data), sector, getLayout());
				}
			}
			else {
				if constexpr (std::is_lvalue_reference_v<T>) {
					return Sector::copyMember<U>(std::forward<T>(data), sector, getLayoutData<U>());
				}
				else {
					return Sector::moveMember<U>(std::forward<T>(data), sector, getLayoutData<U>());
				}
			}
		}

		template<typename T, class... Args>
		T* emplaceImpl(SectorId sectorId, Args&&... args) {	return acquireSectorImpl(sectorId)->template emplaceMember<T>(getLayoutData<T>(), std::forward<Args>(args)...); }

		template<bool T, typename Alloc>
		void copyImpl(const SectorsArray<T, Alloc>& other) {
			clearImpl();
			shrinkToFitImpl();
			if constexpr (ThreadSafe || T) {
				processPendingErases(true, false);
			}
			mSize = other.mSize;
			mAllocator = other.mAllocator;

			mSectorsMap.resize(other.mSectorsMap.size(), nullptr);
			publishNew(mSectorsMap);
			for (auto it = Iterator(this, 0), endIt = Iterator(this, sizeImpl()); it != endIt; ++it) {
				auto sector = *it;
				mSectorsMap[sector->id] = sector;
			}

			defragmentImpl();
		}

		template<bool T, typename Alloc>
		void moveImpl(SectorsArray<T, Alloc>&& other) {
			clearImpl();
			shrinkToFitImpl();
			if constexpr (ThreadSafe || T) {
				processPendingErases(true, false);
				other.processPendingErases(true, false);
			}
			
			mSize = other.mSize;
			mAllocator = std::move(other.mAllocator);

			mSectorsMap.assign(other.mSectorsMap.size(), nullptr);
			publishNew(mSectorsMap);
			size_t i = 0;

			auto cursor = mAllocator.getCursor(0);
			while (cursor && i++ < sizeImpl()) {
				mSectorsMap[cursor->id] = *cursor;
				++cursor;
			}

			mDefragmentSize = other.mDefragmentSize;
			mDefragThreshold = other.mDefragThreshold;

			other.mSize = 0;
			other.shrinkToFitImpl();
		}

		enum ShiftDirection : uint8_t { Left = 0, Right = 1 };
		// RIGHT - [][][][from]->...[to][][] -> [][][]...[from][][][]
		// LEFT -  [][][to]...<-[from][][][] -> [][][from][][][]
		template<ShiftDirection ShiftDir = Left>
		void shiftSectorsImpl(size_t from, size_t count = 1)  {
			if (!count) return;

			if constexpr (ShiftDir == Left) {
				if (from < count) {
					return;
				}

				if (auto tail = from > mSize ? 0 : mSize - from) {
					mAllocator.moveSectors(from - count, from, tail);
					for (auto it = Iterator(this, from - count), end = Iterator(this, from + tail); it != end; ++it) {
						mSectorsMap[it->id] = *it;
					}
				}
			}
			else {
				const size_t oldSize = mSize - count;
				if (const size_t tail = oldSize > from ? (oldSize - from) : 0) {
					mAllocator.moveSectors(from + count, from, tail);
					for (auto it = Iterator(this, from + count), end = Iterator(this, from + count + tail); it != end; ++it) {
						mSectorsMap[it->id] = *it;
					}
				}
			}
		}

		size_t findInsertPositionImpl(SectorId sectorId, size_t size) const  {
			if (size == 0) {
				return 0;
			}

			if (mAllocator.at(size - 1)->id < sectorId) {
				return size;
			}

			if (mAllocator.at(0)->id > sectorId) {
				return 0;
			}

			size_t right = size;
			size_t left = 0;
			while (right - left > 1) {
				size_t mid = left + (right - left) / 2;
				auto mid_id = mAllocator.at(mid)->id;
				if (mid_id < sectorId) {
					left = mid;
				}
				else {
					right = mid;
				}
			}

			return right;
		}

		Sector* acquireSectorImpl(SectorId sectorId) {
			if (sectorId >= mSectorsMapView.load().size) [[unlikely]] {
				mSectorsMap.resize(static_cast<size_t>(sectorId) + 1, nullptr);
				publishNew(mSectorsMap);
			}
			mAllocator.allocate(mSize + 1);

			auto& sector = mSectorsMap[sectorId];
			if (sector) [[unlikely]] {
				return sector;
			}

			size_t pos = mSize++;
			if (!((pos == 0) || (sectorId > mAllocator.at(pos - 1)->id))) [[unlikely]] {
				pos = findInsertPositionImpl(sectorId, pos);

				if (pos != mSize - 1) [[unlikely]] {
					shiftSectorsImpl<Right>(pos);
				}
			}

			sector = mAllocator.at(pos);
			sector->id = sectorId;
			return sector;
		}

		void defragmentImpl(size_t from = 0)  {
			//algorithm which will not shift all sectors left every time, but shift only alive sectors to left border till not found empty place
			//OOOOxOxxxOOxxxxOOxOOOO   0 - start
			//OOOOx<-OxxxOOxxxxOOxOOOO 0
			//OOOOOxxxxOOxxxxOOxOOOO   1
			//OOOOOxxxx<-OOxxxxOOxOOOO 1
			//OOOOOOOxxxxxxxxOOxOOOO   2
			//OOOOOOOxxxxxxxx<-OOxOOOO 2
			//OOOOOOOOOxxxxxxxxxOOOO   3
			//OOOOOOOOOxxxxxxxxx<-OOOO 3
			//OOOOOOOOOOOOOxxxxxxxxx   4 - end

			size_t read = from;
			size_t write = from;
			size_t deleted = 0;
			const size_t n = mSize;

			while (read < n) {
				while (read < n) {
					auto s = mAllocator.at(read);
					if (s->isSectorAlive()) break;
					mSectorsMap[s->id] = nullptr;
					++read; ++deleted;
				}
				if (read >= n) break;

				size_t runBeg = read;
				while (read < n) {
					auto s = mAllocator.at(read);
					if (!s->isSectorAlive()) break;
					++read;
				}
				const size_t runLen = read - runBeg;
				if (write != runBeg) {
					mAllocator.moveSectors(write, runBeg, runLen);
					for (size_t i = 0; i < runLen; ++i) {
						auto ns = mAllocator.at(write + i);
						mSectorsMap[ns->id] = ns;
					}
				}
				else {
					for (size_t i = 0; i < runLen; ++i) {
						auto ns = mAllocator.at(write + i);
						mSectorsMap[ns->id] = ns;
					}
				}

				write += runLen;
			}

			mSize -= deleted;
			mDefragmentSize = from != 0 && mDefragmentSize < static_cast<uint32_t>(deleted) ? mDefragmentSize - static_cast<uint32_t>(deleted) : 0;
			
			shrinkToFitImpl();
		}

		void eraseAsyncImpl(SectorId id) requires(ThreadSafe) {
			if (!findSector(id)) {
				return;
			}

			if (!mPinsCounter.isPinned(id)) {
				UNIQUE_LOCK();
				if (mPinsCounter.canMoveSector(id)){
					if (auto sector = findSector<false>(id)) {
						Sector::destroySector(sector, getLayout());
						incDefragmentSize();
						mSectorsMap[id] = nullptr;
					}
				}
			}
			else {
				UNIQUE_LOCK();
				mPendingErase.push_back(id);
			}
		}

		[[nodiscard]] PinnedSector pinSectorImpl(Sector* sector) const requires(ThreadSafe) { return sector ? PinnedSector{ mPinsCounter, sector, sector->id } : PinnedSector{}; }

		template<typename T>
		void publishNew(const T& v) const {
			mSectorsMapView.store(SectorsView{v.data(), v.size()}, std::memory_order_release);
			if constexpr (ThreadSafe) {
				mBin.drainAll();
			}
		}

	private:
		auto readLock()  const { return ecss::Threads::sharedLock(mtx, ThreadSafe); }
		auto writeLock() const { return ecss::Threads::uniqueLock(mtx, ThreadSafe); }

	private:
		Allocator mAllocator;

		mutable std::shared_mutex mtx;

		mutable Threads::PinCounters mPinsCounter;

		using SectorsMapAlloc =	std::conditional_t<ThreadSafe, Memory::RetireAllocator<Sector*>, std::allocator<Sector*>>;
		struct EmptyBin {};
		using RetireBinT = std::conditional_t<ThreadSafe, Memory::RetireBin, EmptyBin>;
		static SectorsMapAlloc makeSectorsMapAlloc(RetireBinT* bin) { if constexpr (ThreadSafe) { return SectorsMapAlloc{ bin }; } else { return SectorsMapAlloc{}; } }
		
		mutable RetireBinT mBin;
		std::vector<Sector*, SectorsMapAlloc> mSectorsMap{ makeSectorsMapAlloc(&mBin) };

		struct SectorsView {
			Sector* const * vectorData;
			size_t  size;
		};
		mutable std::atomic<SectorsView> mSectorsMapView{ SectorsView{nullptr, 0} };

		std::vector<SectorId> mPendingErase;

		size_t mSize = 0;
		float mDefragThreshold = 0.2f;
		uint32_t mDefragmentSize = 0;

	};
}
