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
#include <ecss/contiguousMap.h>

namespace ecss::Memory {
#define SHARED_LOCK(sync) auto lock = ecss::Threads::sharedLock(mtx, sync);
#define UNIQUE_LOCK(sync) auto lock = ecss::Threads::uniqueLock(mtx, sync);

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
	public:
		// ===== Iterators over contiguous storage (allocator) =====
		class Iterator {
		public:
			using iterator_category = std::forward_iterator_tag;
			using value_type = Sector*;
			using difference_type = std::ptrdiff_t;
			using pointer = value_type*;
			using reference = value_type&;

			Iterator() noexcept = default;
			Iterator(const SectorsArray* array, size_t idx) noexcept : chunksIt(idx, &array->mAllocator) { }

			inline Iterator& operator++()	noexcept { return ++chunksIt, * this; }
			inline Iterator operator++(int) noexcept { Iterator tmp = *this; ++(*this); return tmp; }

			inline bool operator==(const Iterator& other) const noexcept { return chunksIt == other.chunksIt; }
			inline bool operator!=(const Iterator& other) const noexcept { return !(*this == other); }

			inline Sector* operator*()  const noexcept { return *chunksIt; }
			inline Sector* operator->() const noexcept { return *chunksIt; }

		private:
			typename Allocator::Iterator chunksIt;
		};

		Iterator begin(bool sync = ThreadSafe)  const noexcept { SHARED_LOCK(sync) return { this, 0 }; }
		Iterator end(bool sync = ThreadSafe)	const noexcept { SHARED_LOCK(sync) return { this, size(false) }; }

		class IteratorAlive {
		public:
			using iterator_category = std::forward_iterator_tag;
			using value_type = Sector*;
			using difference_type = std::ptrdiff_t;
			using pointer = value_type*;
			using reference = value_type&;

			IteratorAlive() noexcept = default;
			IteratorAlive(const SectorsArray* array, size_t idx, size_t sz, uint32_t aliveMask) noexcept
				: chunksIt(idx, &array->mAllocator), chunksLastIt(sz, &array->mAllocator), typeAliveMask(aliveMask) {
				while (chunksIt != chunksLastIt && !((*chunksIt)->isAliveData & typeAliveMask)) [[unlikely]] { ++chunksIt; }
			}

			IteratorAlive(const SectorsArray* array, size_t idx) noexcept : chunksIt(idx, &array->mAllocator) {}

			inline IteratorAlive& operator++() noexcept {
				do { ++chunksIt; } while (chunksIt != chunksLastIt && !((*chunksIt)->isAliveData & typeAliveMask));
				return *this;
			}

			inline IteratorAlive operator++(int) noexcept { IteratorAlive tmp = *this; ++(*this); return tmp; }

			inline bool operator==(const IteratorAlive& other) const noexcept { return chunksIt == other.chunksIt; }
			inline bool operator!=(const IteratorAlive& other) const noexcept { return !(*this == other); }

			inline Sector* operator*() const noexcept { return *chunksIt; }
			inline Sector* operator->() const noexcept { return *chunksIt; }

		private:
			typename Allocator::Iterator chunksIt;
			typename Allocator::Iterator chunksLastIt;
			uint32_t typeAliveMask = 0;
		};

		template<typename T>
		IteratorAlive beginAlive(bool sync = ThreadSafe)	const noexcept { SHARED_LOCK(sync) return { this, 0 , size(false), getLayoutData<T>().isAliveMask }; }
		IteratorAlive endAlive(bool sync = ThreadSafe)	const noexcept { SHARED_LOCK(sync) return { this, size(false) }; }

		class RangedIterator {
		public:
			using iterator_category = std::forward_iterator_tag;
			using value_type = Sector*;
			using difference_type = std::ptrdiff_t;
			using pointer = value_type*;
			using reference = value_type&;

			RangedIterator() noexcept = default;

			RangedIterator(const SectorsArray* array, const EntitiesRanges& ranges) noexcept {
				if (ranges.ranges.empty()) {
					chunksIt = { 0, &array->mAllocator };
					return;
				}

				mIterators.reserve(ranges.ranges.size() * 2);
				for (auto it = ranges.ranges.rbegin(), endIt = ranges.ranges.rend(); it != endIt; ++it) {
					mIterators.emplace_back(it->second, &array->mAllocator);
					mIterators.emplace_back(it->first, &array->mAllocator);
				}
				chunksIt = std::move(mIterators.back());
				mIterators.pop_back();
			}

			RangedIterator(const SectorsArray* array, size_t idx) noexcept : chunksIt(idx, &array->mAllocator) { }

			inline RangedIterator& operator++() noexcept {
				++chunksIt;
				if (chunksIt == mIterators.back()) [[unlikely]] {
					mIterators.pop_back();
					if (mIterators.empty()) [[unlikely]] {
						return *this;
					}
					chunksIt = std::move(mIterators.back());
					mIterators.pop_back();
				}
				return *this;
			}

			inline RangedIterator operator++(int) noexcept { RangedIterator tmp = *this; ++(*this); return tmp; }

			inline bool operator==(const RangedIterator& other) const noexcept { return chunksIt == other.chunksIt; }
			inline bool operator!=(const RangedIterator& other) const noexcept { return !(*this == other); }

			inline Sector* operator*() const noexcept { return *chunksIt; }
			inline Sector* operator->() const noexcept { return *chunksIt; }

		private:
			std::vector<typename Allocator::Iterator> mIterators;
			typename Allocator::Iterator chunksIt;
		};

		RangedIterator beginRanged(const EntitiesRanges& ranges, bool sync = ThreadSafe)	const noexcept { SHARED_LOCK(sync) return { this, ranges }; }
		RangedIterator endRanged(const EntitiesRanges& ranges, bool sync = ThreadSafe)	const noexcept { SHARED_LOCK(sync) return { this, ranges.empty() ? 0 : ranges.back().second }; }

		class RangedIteratorAlive {
		public:
			using iterator_category = std::forward_iterator_tag;
			using value_type = Sector*;
			using difference_type = std::ptrdiff_t;
			using pointer = value_type*;
			using reference = value_type&;

			RangedIteratorAlive() noexcept = default;

			RangedIteratorAlive(const SectorsArray* array, const EntitiesRanges& ranges, uint32_t aliveMask) noexcept : typeAliveMask(aliveMask) {
				if (ranges.ranges.empty()) {
					chunksIt = { 0, &array->mAllocator };
					return;
				}

				mIterators.reserve(ranges.ranges.size() * 2);
				for (auto it = ranges.ranges.rbegin(), endIt = ranges.ranges.rend(); it != endIt; ++it) {
					mIterators.emplace_back(it->second, &array->mAllocator);
					mIterators.emplace_back(it->first, &array->mAllocator);
				}
				chunksIt = std::move(mIterators.back());
				mIterators.pop_back();

				while (true) {
					if (chunksIt == mIterators.back() || !*chunksIt) [[unlikely]] {
						mIterators.pop_back();
						if (mIterators.empty()) [[unlikely]] {
							break;
						}
						chunksIt = std::move(mIterators.back());
						mIterators.pop_back();
						continue;
					}

						if ((*chunksIt)->isAliveData & typeAliveMask) [[likely]] {
							break;
						}

					++chunksIt;
				}
			}

			RangedIteratorAlive(const SectorsArray* array, size_t idx) noexcept : chunksIt(idx, &array->mAllocator) { }

			inline RangedIteratorAlive& operator++() noexcept {
				++chunksIt;

				while (true) {
					if (chunksIt == mIterators.back()) [[unlikely]] {
						mIterators.pop_back();
						if (mIterators.empty()) [[unlikely]] {
							break;
						}
						chunksIt = std::move(mIterators.back());
						mIterators.pop_back();
						continue;
					}

						if ((*chunksIt)->isAliveData & typeAliveMask) [[likely]] {
							break;
						}

					++chunksIt;
				}

				return *this;
			}

			inline RangedIteratorAlive operator++(int) noexcept { RangedIteratorAlive tmp = *this; ++(*this); return tmp; }

			inline bool operator==(const RangedIteratorAlive& other) const noexcept { return chunksIt == other.chunksIt; }
			inline bool operator!=(const RangedIteratorAlive& other) const noexcept { return !(*this == other); }

			inline Sector* operator*() const noexcept { return *chunksIt; }
			inline Sector* operator->() const noexcept { return *chunksIt; }

		private:
			std::vector<typename Allocator::Iterator> mIterators;
			typename Allocator::Iterator chunksIt;

			uint32_t typeAliveMask = 0;
		};

		template<typename T>
		RangedIteratorAlive beginRangedAlive(const EntitiesRanges& ranges, bool sync = ThreadSafe) const noexcept { SHARED_LOCK(sync) return { this, ranges, getLayoutData<T>().isAliveMask }; }
		RangedIteratorAlive endRangedAlive(const EntitiesRanges& ranges, bool sync = ThreadSafe)	 const noexcept { SHARED_LOCK(sync) return { this, ranges.empty() ? 0 : ranges.back().second }; }

	public:
		struct PinnedSector {
			PinnedSector() noexcept = default;
			PinnedSector(const SectorsArray* o, Sector* s, SectorId sid) noexcept : sec(s), owner(o), id(sid) {
				assert(id != INVALID_ID);
				if constexpr (ThreadSafe) {
					owner->mPinsCounter.pin(id);
				}
				
			}

			PinnedSector(const PinnedSector&) = delete;
			PinnedSector& operator=(const PinnedSector&) = delete;

			PinnedSector(PinnedSector&& other) noexcept { *this = std::move(other); }
			PinnedSector& operator=(PinnedSector&& other) noexcept {
				if (this == &other) { return *this;	}

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

			void release() noexcept {
				if (owner) {
					if constexpr (ThreadSafe) {
						owner->mPinsCounter.unpin(id);
					}
				}

				owner = nullptr;
				sec = nullptr;
				id = static_cast<SectorId>(INVALID_ID);
			}

			Sector* get() const noexcept { return sec; }
			Sector* operator->() const noexcept { return sec; }
			explicit operator bool() const noexcept { return sec != nullptr; }
			SectorId getId() const noexcept { return id; }
		private:
			Sector* sec = nullptr;
			const SectorsArray* owner = nullptr;
			SectorId id = INVALID_ID;
		};

	public:
		// it allows to copy array with different allocator, if allocator supports it
		template<bool T, typename Alloc>
		SectorsArray(const SectorsArray<T, Alloc>& other)			 noexcept { *this = other; }
		template<bool T, typename Alloc>
		SectorsArray& operator=(const SectorsArray<T, Alloc>& other) noexcept { if (reinterpret_cast<void*>(this) == reinterpret_cast<void*>(const_cast<SectorsArray<T, Alloc>*>(&other))) { return *this; } copy(other); return *this; }
		template<bool T, typename Alloc>
		SectorsArray(SectorsArray<T, Alloc>&& other)				 noexcept { *this = std::move(other); }
		template<bool T, typename Alloc>
		SectorsArray& operator=(SectorsArray<T, Alloc>&& other)		 noexcept { if (reinterpret_cast<void*>(this) == reinterpret_cast<void*>(const_cast<SectorsArray<T, Alloc>*>(&other))) { return *this; } move(std::move(other)); return *this; }

	public:
		SectorsArray(const SectorsArray& other)				noexcept { *this = other; }
		SectorsArray& operator=(const SectorsArray& other)	noexcept { if (this == &other) { return *this; } copy(other); return *this; }

		SectorsArray(SectorsArray&& other)					noexcept { *this = std::move(other); }
		SectorsArray& operator=(SectorsArray&& other)		noexcept { if (this == &other) { return *this; } move(std::move(other)); return *this; }

	public:
		~SectorsArray() = default;

		template <typename... Types>
		static SectorsArray* create(uint32_t capacity = 0, Allocator allocator = {}) noexcept {
			const auto array = new SectorsArray(std::move(allocator)); array->template initializeSector<Types...>(capacity);
			return array;
		}

	private:
		SectorsArray(Allocator allocator = {}) : mAllocator(std::move(allocator)) {}

		template <typename... Types>
		void initializeSector(uint32_t capacity) noexcept {
			static_assert(types::areUnique<Types...>(), "Duplicates detected in types");
			static SectorLayoutMeta* meta = SectorLayoutMeta::create<Types...>();

			mAllocator.init(meta);

			reserve(capacity, false);
		}

	public: // sector helpers
		template<typename T>
		const LayoutData& getLayoutData() const noexcept { return mAllocator.getSectorLayout()->template getLayoutData<T>(); }
		SectorLayoutMeta* getLayout()	  const noexcept { return mAllocator.getSectorLayout(); }

		[[nodiscard]] PinnedSector pinSector(Sector* sector, bool sync = ThreadSafe) const noexcept { SHARED_LOCK(sync) return sector ? PinnedSector{ this, sector, sector->id } : PinnedSector{}; }
		[[nodiscard]] PinnedSector pinSector(SectorId id, bool sync = ThreadSafe)	 const noexcept { SHARED_LOCK(sync) return pinSector(mAllocator.tryGetSector(id), false); }
		[[nodiscard]] PinnedSector pinSectorAt(size_t idx, bool sync = ThreadSafe)	 const noexcept { SHARED_LOCK(sync) return pinSector(mAllocator.at(idx), false); }
		[[nodiscard]] PinnedSector pinBackSector(bool sync = ThreadSafe) const noexcept {
			SHARED_LOCK(sync)
			auto contSize = size(false);
			if (contSize == 0) {
				return PinnedSector{};
			}
			return pinSector(mAllocator.at(contSize - 1), false);
		}

		void freeSector(SectorId id, bool shift = false, bool sync = ThreadSafe)		    noexcept { eraseSectorSafe(id, shift, sync); }
		bool containsSector(SectorId id, bool sync = ThreadSafe)					  const noexcept { return findSector(id, sync); }

		// unsafe getter, use pinned version in multithread
		Sector* findSector(SectorId id, bool sync = ThreadSafe)						  const noexcept { SHARED_LOCK(sync) return mAllocator.tryGetSector(id); }
		// unsafe getter, use pinned version in multithread
		Sector* getSector(SectorId id, bool sync = ThreadSafe)						  const noexcept { SHARED_LOCK(sync) return mAllocator.getSector(id); }

		size_t sectorsCapacity(bool sync = ThreadSafe)								  const noexcept { SHARED_LOCK(sync) return mAllocator.sectorsCapacity(); }
		size_t getSectorIndex(SectorId id, bool sync = ThreadSafe)					  const noexcept { SHARED_LOCK(sync) return mAllocator.calcSectorIndex(id); }

		size_t findRightNearestSectorIndex(SectorId sectorId, bool sync = ThreadSafe) const noexcept {
			SHARED_LOCK(sync)
			while (sectorId < sectorsCapacity(false)) {
				if (containsSector(sectorId, false)) {
					return getSectorIndex(sectorId, false);
				}
				sectorId++;
			}

			return size(false);
		}

		template<typename T>
		void destroyMember(SectorId sectorId, bool sync = ThreadSafe) const noexcept {
			UNIQUE_LOCK(sync)
			if constexpr (ThreadSafe) {
				mPinsCounter.waitUntilSectorChangeable(sectorId);
			}
			if (auto sector = findSector(sectorId, false)) {
				sector->destroyMember(getLayoutData<T>());
			}
		}

		template<typename T>
		void destroyMembers(std::vector<SectorId>& sectorIds, bool sort = true, bool sync = ThreadSafe) const noexcept {
			if (sectorIds.empty()) return;
			if (sort) std::ranges::sort(sectorIds);


			const auto& layout = getLayoutData<T>();
			UNIQUE_LOCK(sync)

			const auto sectorsSize = sectorsCapacity(false);
			for (const auto sectorId : sectorIds) {
				if (sectorId >= sectorsSize) break;
				if constexpr (ThreadSafe) {
					mPinsCounter.waitUntilSectorChangeable(sectorId);
				}
				if (auto sector = pinSector(sectorId, false)) {
					sector->destroyMember(layout);
				}
			}
		}

		void clearSectors(std::vector<SectorId>& sectorIds, bool sort = true, bool sync = ThreadSafe) const noexcept {
			if (sectorIds.empty()) return;
			if (sort) std::ranges::sort(sectorIds);

			const auto layout = getLayout();
			UNIQUE_LOCK(sync)

			const auto sectorsSize = sectorsCapacity(false);
			for (const auto sectorId : sectorIds) {
				if (sectorId >= sectorsSize) break;
				if constexpr (ThreadSafe) {
					mPinsCounter.waitUntilSectorChangeable(sectorId);
				}
				Sector::destroyMembers(findSector(sectorId, false), layout);
			}
		}

	public: // container methods
		// WARNING! at() doesn't do bounds checks
		Sector* at(size_t sectorIndex, bool sync = ThreadSafe) const noexcept { SHARED_LOCK(sync) return mAllocator.at(sectorIndex); }

		size_t capacity(bool sync = ThreadSafe)	const noexcept { SHARED_LOCK(sync) return mAllocator.capacity(); }
		size_t size(bool sync = ThreadSafe)		const noexcept { SHARED_LOCK(sync) return mAllocator.size(); }
		bool   empty(bool sync = ThreadSafe)		const noexcept { return !size(sync); }

		void shrinkToFit(bool sync = ThreadSafe) noexcept { UNIQUE_LOCK(sync) mAllocator.shrinkToFit(); }
		void clear(bool sync = ThreadSafe) noexcept {
			UNIQUE_LOCK(sync)
			if constexpr (ThreadSafe) {
				mPinsCounter.waitUntilChangeable();
			}

			mAllocator.free();
			mPendingErase.clear();
		}

		void reserve(uint32_t newCapacity, bool sync = ThreadSafe) noexcept {	UNIQUE_LOCK(sync) mAllocator.reserve(newCapacity); }

		template<typename T>
		std::remove_reference_t<T>* insert(SectorId sectorId, T&& data, bool sync = ThreadSafe) noexcept {
			using U = std::remove_reference_t<T>;
			UNIQUE_LOCK(sync)
			if (sync) {
				if constexpr (ThreadSafe) {
					mPinsCounter.waitUntilChangeable(sectorId);
				}
			}
		
			Sector* sector = mAllocator.acquireSector(sectorId);

			if constexpr (std::is_same_v<std::remove_cvref_t<T>, Sector>) {
				if constexpr (std::is_lvalue_reference_v<T>) {
					return Sector::copySector(std::addressof(data), sector, mAllocator.getSectorLayout());
				}
				else {
					return Sector::moveSector(std::addressof(data), sector, mAllocator.getSectorLayout());
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
		T* emplace(SectorId sectorId, bool sync = ThreadSafe, Args&&... args) noexcept {
			UNIQUE_LOCK(sync)
			if constexpr (ThreadSafe) {
				if (sync) {
					mPinsCounter.waitUntilChangeable(sectorId);
				}
			}

			return Memory::Sector::emplaceMember<T>(mAllocator.acquireSector(sectorId), getLayoutData<T>(), std::forward<Args>(args)...);
		}

		// Safe erase: only if not pinned and id >= last active pinned id; otherwise queued.
		void eraseSectorSafe(SectorId id, bool shift = false, bool sync = ThreadSafe) noexcept {
			Sector* s = findSector(id, sync);
			if (!s) {
				return;
			}

			UNIQUE_LOCK(sync)
			if constexpr (ThreadSafe) {
				if (shift) {
					if (mPinsCounter.canMoveSector(id)) {
						mAllocator.freeSector(id, shift);
					}
					else {
						mPendingErase.push_back(id);
					}
				}
				else {
					if (!mPinsCounter.isPinned(id)) {
						mAllocator.freeSector(id, shift);
					}
					else {
						mPendingErase.push_back(id);
					}
				}
			}
			else {
				mAllocator.freeSector(id, shift);
			}
		}

		// Raw erase by contiguous index (compat). Applies same safety rule per SectorId.
		void erase(size_t beginIdx, size_t count = 1, bool shift = false, bool sync = ThreadSafe) noexcept {
			if (!count) return;
			UNIQUE_LOCK(sync)
			const size_t endIdx = std::min(beginIdx + count, mAllocator.size());
			for (size_t i = beginIdx; i < endIdx; ++i) {
				auto s = mAllocator.at(i);
				if (!s) continue;
				eraseSectorSafe(s->id, shift, false);
			}
		}

		void defragment(bool sync = ThreadSafe) noexcept {
			if (empty(sync)) {
				return;
			}

			UNIQUE_LOCK(sync)
			if constexpr (ThreadSafe) {
				if (sync) {
					mPinsCounter.waitUntilChangeable();
				}
			}

			mAllocator.defragment();
		}

		/** @brief Maintenance hook (e.g., end of frame).
		 *  - Attempts queued erases that were deferred due to pins.
		 *  - If array is not "locked" by readers, drains deferred vector buffers
		 *    (allocator.emptyVectorsBin()) and optionally defragments.
		 *
		 * Call this periodically to keep memory and fragmentation under control.
		 */
		void processPendingErases(bool sync = ThreadSafe) noexcept {
			
			if constexpr (ThreadSafe) {
				if (mPendingErase.empty()) {
					mAllocator.emptyVectorsBin();
					defragment(false); // todo defragment only if fragmentation koeff too high
					return;
				}
				UNIQUE_LOCK(sync)

				auto tmp = std::move(mPendingErase);
				std::ranges::sort(tmp);
				for (auto id : tmp) {
					if (!mPinsCounter.isPinned(id)) {
						mAllocator.freeSector(id, false);
					}
					else {
						mPendingErase.emplace_back(id);
					}
				}

				if (!mPinsCounter.isArrayLocked()) {
					mAllocator.emptyVectorsBin();
					defragment(false); // todo defragment only if fragmentation koeff too high
				}
			}
			else {
				mAllocator.emptyVectorsBin();
				defragment(false); // todo defragment only if fragmentation koeff too high
			}
		}

	private:
		template<bool T, typename Alloc>
		void copy(const SectorsArray<T, Alloc>& other) noexcept {
			if constexpr (ThreadSafe) {
				auto lock = ecss::Threads::uniqueLock(mtx, true);
				auto otherLock = ecss::Threads::sharedLock(other.mtx, true);
				mPinsCounter.waitUntilChangeable();

				processPendingErases(false);
				mAllocator = other.mAllocator;
			}
			else {
				processPendingErases(false);
				mAllocator = other.mAllocator;
			}
		}

		void move(SectorsArray&& other) noexcept {
			if constexpr (ThreadSafe) {
				auto lock = ecss::Threads::uniqueLock(mtx, true);
				auto otherLock = ecss::Threads::uniqueLock(other.mtx, true);
				mPinsCounter.waitUntilChangeable();
				other.mPinsCounter.waitUntilChangeable();

				processPendingErases(false);
				other.processPendingErases(false);

				mAllocator = std::move(other.mAllocator);
			}
			else {
				processPendingErases(false);
				other.processPendingErases(false);

				mAllocator = std::move(other.mAllocator);
			}
			
		}

	private:
		Allocator mAllocator;

		mutable std::shared_mutex mtx;

		mutable Threads::PinCounters mPinsCounter;

		std::vector<SectorId> mPendingErase;
	};
}
