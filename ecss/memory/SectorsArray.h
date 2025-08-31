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

#define TS_GUARD(TS_FLAG, LOCK_MACRO, EXPR) do {if constexpr (TS_FLAG) { LOCK_MACRO##_LOCK(); EXPR; } else { EXPR; }} while(0)
#define TS_GUARD_S(TS_FLAG, LOCK_MACRO, ADDITIONAL_SINK, EXPR) do {if constexpr (TS_FLAG) { LOCK_MACRO##_LOCK(); ADDITIONAL_SINK; EXPR; } else { EXPR; }} while(0)

	struct PinnedSector {
		PinnedSector() = default;
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

	/// Data container with sectors of custom data in it.
	/// Synchronization lives in the container (allocator is single-threaded under write lock).
	/**
	 * @tparam ThreadSafe  If true, public APIs acquire shared/unique locks and use PinCounters
	 *                     so writers wait for readers. If false, calls are non-synchronized.
	 * @tparam Allocator   Chunked sector storage (default: ChunksAllocator<8192>).
	 *
	 * Design:
	 *  - Storage is provided by @p Allocator (chunked, contiguous in chunks).
	 *  - A direct map (SectorId -> Sector*) is kept by the allocator for O(1) lookup.
	 *  - This wrapper adds thread-safety (optional), pin/unpin semantics and
	 *    higher-level operations (erase queuing, defragmentation, ranged iteration).
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
		// iterators
#define ITERATOR_COMMON_USING(IteratorName)														\
		using iterator_concept  = std::forward_iterator_tag;									\
		using iterator_category = std::forward_iterator_tag;									\
		using value_type = Sector*;																\
		using difference_type = std::ptrdiff_t;													\
		using pointer = Sector*;																\
		using reference = Sector*;																\
		IteratorName() = default;																\
		inline IteratorName operator++(int) { auto tmp = *this; ++(*this); return tmp; }		\
		inline bool operator!=(const IteratorName& other) const { return !(*this == other); }	\
		inline bool operator==(const IteratorName& other) const { return mIt == other.mIt; }	\
		inline value_type operator*() const { return *mIt; }									\
		inline value_type operator->() const { return *mIt; }

		class Iterator {
		public:
			ITERATOR_COMMON_USING(Iterator)

			Iterator(const SectorsArray* array, size_t idx)  : mIt(array->mAllocator.getCursor(std::min(idx, array->sizeImpl()))) { }

			Iterator& operator++() noexcept { ++mIt; return *this; }
			Iterator& operator+=(difference_type n) noexcept { mIt = mIt + n; return *this; }
			Iterator operator+(difference_type n) const noexcept { Iterator t(*this); t += n; return t; }
			friend Iterator operator+(difference_type n, Iterator it) noexcept { it += n; return it; }

			reference operator[](difference_type n) const noexcept { return *(*this + n); }

			size_t linearIndex() const { return mIt.linearIndex(); }
		private:
			typename Allocator::Cursor mIt;
		};

		template<bool TS = ThreadSafe> Iterator begin() const { TS_GUARD(TS, SHARED, return Iterator(this, 0);); }
		template<bool TS = ThreadSafe> Iterator end()   const { TS_GUARD(TS, SHARED, return Iterator(this, sizeImpl());); }

		class IteratorAlive {
		public:
			ITERATOR_COMMON_USING(IteratorAlive)

			IteratorAlive(const SectorsArray* array, size_t idx, size_t sz, uint32_t aliveMask)
				: mIt(array, idx), mLastIt(array, sz), typeAliveMask(aliveMask)
			{
				while (mIt != mLastIt && !(mIt->isAliveData & typeAliveMask)) { ++mIt; }
			}

			IteratorAlive(const SectorsArray* array, size_t idx)  : mIt(array, idx) {}

			inline IteratorAlive& operator++() { do { ++mIt; } while (mIt != mLastIt && !(mIt->isAliveData & typeAliveMask)); return *this; }

		private:
			Iterator mIt;
			Iterator mLastIt{};

			uint32_t typeAliveMask = 0;
		};

		template<typename T, bool TS = ThreadSafe>
		IteratorAlive beginAlive()								const { TS_GUARD(TS, SHARED, return IteratorAlive(this, 0, sizeImpl(), getLayoutData<T>().isAliveMask);); }
		template<bool TS = ThreadSafe> IteratorAlive endAlive() const { TS_GUARD(TS, SHARED, return IteratorAlive(this, sizeImpl());); }

		class RangedIterator {
		public:
			ITERATOR_COMMON_USING(RangedIterator)

			RangedIterator(const SectorsArray* array, const EntitiesRanges& ranges) : mIt(array, 0) {
				if (ranges.ranges.empty()) { return; }

				mIterators.reserve(ranges.ranges.size() * 2);
				for (auto it = ranges.ranges.rbegin(), endIt = ranges.ranges.rend(); it != endIt; ++it) {
					mIterators.emplace_back(array, it->second);
					mIterators.emplace_back(array, it->first);
				}
				mIt = std::move(mIterators.back());
				mIterators.pop_back();
			}

			RangedIterator(const SectorsArray* array, size_t idx) : mIt(array, idx) {}

			inline RangedIterator& operator++() {
				++mIt;
				if (mIt == mIterators.back()) [[unlikely]] {
					mIterators.pop_back();
					if (mIterators.empty()) [[unlikely]] {
						return *this;
					}
					mIt = std::move(mIterators.back());
					mIterators.pop_back();
				}
				return *this;
			}

		private:
			Iterator mIt;
			std::vector<Iterator> mIterators;
		};

		template<bool TS = ThreadSafe> RangedIterator beginRanged(const EntitiesRanges& ranges) const { TS_GUARD(TS, SHARED, return RangedIterator(this, ranges);); }
		template<bool TS = ThreadSafe> RangedIterator endRanged(const EntitiesRanges& ranges)   const { TS_GUARD(TS, SHARED, return RangedIterator(this, ranges.empty() ? 0 : ranges.back().second);); }

		class RangedIteratorAlive {
		public:
			ITERATOR_COMMON_USING(RangedIteratorAlive)

			RangedIteratorAlive(const SectorsArray* array, const EntitiesRanges& ranges, uint32_t aliveMask)
			: mIt{ array , ranges }
			, mLast{ array, ranges.empty() ? 0 : ranges.back().second }
			, mAliveMask(aliveMask)
			{
				while (mIt != mLast && !(mIt->isAliveData & mAliveMask)) { ++mIt; }
			}

			RangedIteratorAlive(const SectorsArray* array, size_t idx)  : mIt(array, idx) {}

			inline RangedIteratorAlive& operator++() { do { ++mIt; } while (mIt != mLast && !(mIt->isAliveData & mAliveMask)); return *this; }

		private:
			RangedIterator mIt;
			RangedIterator mLast = {};
			uint32_t mAliveMask = 0;
		};

		template<typename T, bool TS = ThreadSafe>
		RangedIteratorAlive beginRangedAlive(const EntitiesRanges& ranges)								const { TS_GUARD(TS, SHARED, return RangedIteratorAlive( this, ranges, getLayoutData<T>().isAliveMask );); }
		template<bool TS = ThreadSafe> RangedIteratorAlive endRangedAlive(const EntitiesRanges& ranges) const { TS_GUARD(TS, SHARED, return RangedIteratorAlive( this, ranges.empty() ? 0 : ranges.back().second );); }

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
		SectorsArray(SectorLayoutMeta* meta, Allocator&& allocator = {}) : mAllocator(std::move(allocator)) { mAllocator.init(meta); }

	public:
		~SectorsArray() { clear(); shrinkToFit(); }

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
		[[nodiscard]] PinnedSector pinSector(Sector* sector) const requires(ThreadSafe) { SHARED_LOCK(); return sector ? PinnedSector{ mPinsCounter, sector, sector->id } : PinnedSector{}; }
		[[nodiscard]] PinnedSector pinSector(SectorId id)	 const requires(ThreadSafe) { SHARED_LOCK(); return pinSectorImpl(findSectorImpl(id)); }
		[[nodiscard]] PinnedSector pinSectorAt(size_t idx)	 const requires(ThreadSafe) { SHARED_LOCK(); return pinSectorImpl(mAllocator.at(idx)); }
		[[nodiscard]] PinnedSector pinBackSector()			 const requires(ThreadSafe) { SHARED_LOCK();
			auto contSize = sizeImpl();
			return contSize == 0 ? PinnedSector{} : pinSectorImpl(mAllocator.at(contSize - 1));
		}

	public:
		void erase(size_t beginIdx, size_t count = 1, bool defragment = false) {
			if (count == 1) {
				erase(Iterator(this, beginIdx), defragment);
			}
			else if (count > 1) {
				erase(Iterator(this, beginIdx), Iterator(this, beginIdx + count), defragment);
			}
		}

		template<bool TS = ThreadSafe>
		Iterator erase(Iterator it, bool defragment = false) noexcept {
			if (!(*it)) { return it; }
			TS_GUARD_S(TS, UNIQUE, mPinsCounter.waitUntilChangeable(it->id);, return eraseImpl(it, defragment););
		}

		template<bool TS = ThreadSafe>
		Iterator erase(Iterator first, Iterator last, bool defragment = false) noexcept {
			if (first == last || !(*first)) { return first; }
			TS_GUARD_S(TS, UNIQUE, mPinsCounter.waitUntilChangeable(first->id);, return eraseImpl(first, last, defragment););
		}

		template<typename Func, bool TS = ThreadSafe>
		void erase_if(Iterator first, Iterator last, Func&& func, bool defragment = false) noexcept {
			if (first == last || !(*first)) { return; }
			TS_GUARD_S(TS, UNIQUE, mPinsCounter.waitUntilChangeable(first->id);, erase_ifImpl(first, last, std::forward<Func>(func), defragment););
		}

		void eraseAsync(SectorId id, size_t count = 1, bool defragment = false) requires(ThreadSafe) {
			UNIQUE_LOCK();
			for (auto i = id; i < id + count; ++i) {
				eraseAsyncImpl(i, defragment);
			}
		}

		template<bool TS = ThreadSafe> size_t findRightNearestSectorIndex(SectorId sectorId) const { TS_GUARD(TS, SHARED, return findRightNearestSectorIndexImpl(sectorId)); }

		template<bool TS = ThreadSafe> bool containsSector(SectorId id)					     const { TS_GUARD(TS, SHARED, return containsSectorImpl(id)); }
		template<bool TS = ThreadSafe> Sector* at(size_t sectorIndex)						 const { TS_GUARD(TS, SHARED, return atImpl(sectorIndex)); }
		template<bool TS = ThreadSafe> Sector* findSector(SectorId id)						 const { TS_GUARD(TS, SHARED, return findSectorImpl(id)); }
		template<bool TS = ThreadSafe> Sector* getSector(SectorId id)						 const { TS_GUARD(TS, SHARED, return getSectorImpl(id)); }

		// return INVALID_ID if not found
		template<bool TS = ThreadSafe> size_t getSectorIndex(SectorId id)					 const { TS_GUARD(TS, SHARED, return getSectorIndexImpl(id)); }
		template<bool TS = ThreadSafe> size_t sectorsMapCapacity()							 const { TS_GUARD(TS, SHARED, return sectorsMapCapacityImpl()); }
		template<bool TS = ThreadSafe> size_t capacity()									 const { TS_GUARD(TS, SHARED, return capacityImpl()); }
		template<bool TS = ThreadSafe> size_t size()										 const { TS_GUARD(TS, SHARED, return sizeImpl()); }
		template<bool TS = ThreadSafe> bool empty()										     const { TS_GUARD(TS, SHARED, return emptyImpl()); }
		template<bool TS = ThreadSafe> void shrinkToFit()										   { TS_GUARD(TS, UNIQUE, shrinkToFitImpl()); }

		template<bool TS = ThreadSafe> void reserve(uint32_t newCapacity) { TS_GUARD(TS, UNIQUE, reserveImpl(newCapacity)); }
		template<bool TS = ThreadSafe> void clear()					      { TS_GUARD_S(TS, UNIQUE, mPinsCounter.waitUntilChangeable();, clearImpl(););}

		template<bool TS = ThreadSafe>
		void defragment() { TS_GUARD_S(TS, UNIQUE, if (emptyImpl()) { return; } mPinsCounter.waitUntilChangeable(); , defragmentImpl();); }

		template<typename T, bool TS = ThreadSafe>
		std::remove_reference_t<T>* insert(SectorId sectorId, T&& data) {
			TS_GUARD_S(TS, UNIQUE, mPinsCounter.waitUntilChangeable(sectorId); , return insertImpl(sectorId, std::forward<T>(data)););
		}

		template<typename T, bool TS = ThreadSafe, class... Args>
		T* emplace(SectorId sectorId, Args&&... args)  {
			TS_GUARD_S(TS, UNIQUE, mPinsCounter.waitUntilChangeable(sectorId);, return emplaceImpl<T>(sectorId, std::forward<Args>(args)...););
		}

		template<typename T, bool TS = ThreadSafe, class... Args>
		T* push(SectorId sectorId, Args&&... args) requires(ThreadSafe) {
			if constexpr (sizeof...(Args) == 1 && (std::is_same_v<std::remove_cvref_t<Args>, T> && ...)) {
				return insert<Args..., TS>(sectorId, std::forward<Args>(args)...);
			}
			else {
				return emplace<T, TS>(sectorId, std::forward<Args>(args)...);
			}
		}

		/** @brief Maintenance hook (e.g., end of frame).
		 *  - Attempts queued erases that were deferred due to pins.
		 *  - If array is not "locked" by readers, drains deferred vector buffers
		 *    (allocator.emptyVectorsBin()) and optionally defragments.
		 *
		 * Call this periodically to keep memory and fragmentation under control.
		 */
		void processPendingErases(bool withDefragment = true, bool sync = ThreadSafe) requires(ThreadSafe) {
			auto lock = ecss::Threads::uniqueLock(mtx, sync);
			if (mPendingErase.empty()) {
				if (withDefragment && !mPinsCounter.isArrayLocked()) {
					defragmentImpl();
				}
				mBin.drainAll();
				return;
			}

			auto tmp = std::move(mPendingErase);
			std::ranges::sort(tmp);
			tmp.erase(std::ranges::unique(tmp).begin(), tmp.end());
			for (auto id : tmp) {
				if (!mPinsCounter.isPinned(id)) {
					Sector::destroySector(getSectorImpl(id), getLayout());
					mSectorsMap[id] = nullptr;
				}
				else {
					mPendingErase.emplace_back(id);
				}
			}

			if (withDefragment && !mPinsCounter.isArrayLocked()) {
				defragmentImpl();
			}

			mBin.drainAll();
		}

	private:
		template<typename T, class... Args>
		T* pushComponentNoLock(SectorId sectorId, Args&&... args) requires(ThreadSafe) {
			if constexpr (sizeof...(Args) == 1 && (std::is_same_v<std::remove_cvref_t<Args>, T> && ...)) {
				return insertImpl(sectorId, std::forward<Args>(args)...);
			}
			else {
				return emplaceImpl<T>(sectorId, std::forward<Args>(args)...);
			}
		}

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
			if (!(*it)) {
				return it;
			}

			auto idx = it.linearIndex();

			mSectorsMap[it->id] = nullptr;
			Sector::destroySector(*it, getLayout());
			if (defragment) {
				shiftSectorsImpl<Left>(idx + 1);
				mSize--;
			}

			return Iterator(this, idx);
		}
		Iterator eraseImpl(Iterator first, Iterator last, bool defragment = false) noexcept {
			if (first == last || !(*first)) { return first; }

			auto idx = first.linearIndex();
			auto lastIdx = last.linearIndex();

			for (auto it = first; it != last; ++it) {
				mSectorsMap[it->id] = nullptr;
				Sector::destroySector(*it, getLayout());
			}

			if (defragment) {
				shiftSectorsImpl<Left>(lastIdx, lastIdx - idx);
				mSize -= lastIdx - idx;
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
				}
			}

			if (defragment) {
				defragmentImpl(idx);
			}
		}

		size_t findRightNearestSectorIndexImpl(SectorId sectorId) const {
			if (sectorId < mSectorsMap.size()) {
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
		Sector* findSectorImpl(SectorId id)		const { return id < mSectorsMap.size() ? mSectorsMap[id] : nullptr; }
		Sector* getSectorImpl(SectorId id)		const { assert(id < mSectorsMap.size()); return mSectorsMap[id]; }
		// return INVALID_ID if not found
		size_t getSectorIndexImpl(SectorId id)	const { return mAllocator.find(findSectorImpl(id)); }

		size_t sectorsMapCapacityImpl()			const { return mSectorsMap.size(); }
		size_t capacityImpl()					const { return mAllocator.capacity(); }
		size_t sizeImpl()						const { return mSize; }
		bool   emptyImpl()						const { return !mSize; }

		void shrinkToFitImpl() { mAllocator.deallocate(mSize, mAllocator.capacity()); }

		void clearImpl() {
			if (mSize) {
				if (!getLayout()->isTrivial()) { //call destructors for nontrivial types
					for (auto it = Iterator(this, 0), endIt = Iterator(this, sizeImpl()); it != endIt; ++it) {
						Sector::destroySector(*it, getLayout());
					}
				}

				mSectorsMap.clear();
				mPendingErase.clear();
				mSize = 0;
			}
		}

		void reserveImpl(uint32_t newCapacity) {
			if (mAllocator.capacity() < newCapacity) {
				mAllocator.allocate(newCapacity);
				if (newCapacity > mSectorsMap.size()) {
					mSectorsMap.resize(newCapacity, nullptr);
				}
			}
		}

		template<typename T>
		std::remove_reference_t<T>* insertImpl(SectorId sectorId, T&& data) {
			using U = std::remove_reference_t<T>;

			Sector* sector = acquireSectorImpl(sectorId);
			if constexpr (std::is_same_v<std::remove_cvref_t<T>, Sector>) {
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

			mSectorsMap.resize(other.mSectorsMap.size(), nullptr);
			for (auto it = Iterator(this, 0), endIt = Iterator(this, sizeImpl()); it != endIt; ++it) {
				auto sector = *it;
				mSectorsMap[sector->id] = sector;
			}

			defragmentImpl();

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
			if (sectorId >= mSectorsMap.size()) [[unlikely]] {
				mSectorsMap.resize(static_cast<size_t>(sectorId) + 1, nullptr);
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
			const size_t n = mSize;
			if (!n) return;

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
			while (read < n) {
				auto s = mAllocator.at(read);
				if (s->isSectorAlive()) break;
				mSectorsMap[s->id] = nullptr;
				if (!mAllocator.mIsSectorTrivial) {
					Sector::destroySector(s, mAllocator.mSectorLayout);
				}
				++read; ++deleted;
			}

			if (read == n) {
				shrinkToFitImpl();
				return;
			}

			while (read < n) {
				while (read < n) {
					auto s = mAllocator.at(read);
					if (s->isSectorAlive()) break;
					mSectorsMap[s->id] = nullptr;
					if (!mAllocator.mIsSectorTrivial) {
						Sector::destroySector(s, mAllocator.mSectorLayout);
					}
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

			shrinkToFitImpl();
		}

		void eraseAsyncImpl(SectorId id, bool defragment = false) requires(ThreadSafe) {
			if (auto sector = findSectorImpl(id)) {
				if (defragment) {
					if (mPinsCounter.canMoveSector(id)) {
						Sector::destroySector(sector, getLayout());
						mSectorsMap[id] = nullptr;
						shiftSectorsImpl<Left>(mAllocator.find(sector) + 1);
						mSize--;
					}
					else {
						mPendingErase.push_back(id);
					}
				}
				else {
					if (!mPinsCounter.isPinned(id)) {
						Sector::destroySector(sector, getLayout());
						mSectorsMap[id] = nullptr;
					}
					else {
						mPendingErase.push_back(id);
					}
				}
			}
		}

		[[nodiscard]] PinnedSector pinSectorImpl(Sector* sector) const requires(ThreadSafe) { return sector ? PinnedSector{ mPinsCounter, sector, sector->id } : PinnedSector{}; }

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

		std::vector<SectorId> mPendingErase;

		size_t mSize = 0;
	};
}
