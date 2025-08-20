#pragma once

#include <algorithm>
#include <cassert>
#include <shared_mutex>
#include <bit>
#include <atomic>
#include <vector>

#include <ecss/memory/Sector.h>
#include <ecss/EntitiesRanges.h>
#include <ecss/threads/SyncManager.h>
#include <ecss/memory/ChunksAllocator.h>
#include <ecss/contiguousMap.h>

namespace ecss::Memory {
	using PinIndex = int_fast64_t;

	struct PinnedIndexesBitMask {
	private:
		using BITS_TYPE = uint64_t;
		static_assert(std::is_unsigned_v<BITS_TYPE>, "BITS_TYPE must be unsigned");
		static_assert((std::numeric_limits<BITS_TYPE>::digits& (std::numeric_limits<BITS_TYPE>::digits - 1)) == 0, "digits must be power of two (8/16/32/64)");

		static constexpr auto kFanout = std::numeric_limits<BITS_TYPE>::digits;
		static constexpr auto kBitsCount = kFanout - 1;
		static constexpr auto shift = std::countr_zero(static_cast<unsigned>(kFanout));
		static constexpr auto maxlvl = 1 + (std::numeric_limits<size_t>::digits + shift - 1) / shift;

		static constexpr size_t		wordIndexOf(size_t idx) noexcept { return idx >> shift; }						//		/kFanout
		static constexpr size_t		bitOffsetOf(size_t idx) noexcept { return idx & kBitsCount; }					//		%kFanout 
		static constexpr BITS_TYPE	bitMask(size_t off)		noexcept { return static_cast<BITS_TYPE>(1) << off; }

		static constexpr size_t levelsFor(size_t idx) noexcept {
			auto w0 = wordIndexOf(idx);
			return w0 ? 2 + (std::bit_width(w0) - 1) / shift : 1;
		}

		size_t ensurePath(size_t idx, std::array<size_t, maxlvl>& path) noexcept {
			auto lock = std::unique_lock(mtx);

			auto w = idx;
			size_t level = 0;
			do {
				w = wordIndexOf(w);
				if (bits[level].size() <= w) { bits[level].resize(w + 1, 0); }
				path[level++] = w;

			} while (w != 0);

			return level;
		}

	private:

		mutable std::shared_mutex mtx;
		std::vector<std::vector<BITS_TYPE>> bits;

	public:
		PinnedIndexesBitMask() noexcept { bits.resize(maxlvl); bits[0].resize(1); }

		void set(SectorId index, bool state) noexcept {
			thread_local std::array<size_t, maxlvl> path;
			auto size = ensurePath(index, path);

			const auto m0 = bitMask(bitOffsetOf(index));

			auto lock = std::shared_lock(mtx);

			if (state) {
				auto before = std::atomic_ref{ bits[0][path[0]] }.fetch_or(m0, std::memory_order_acq_rel);
				if (before & m0) { return; }

				for (size_t lvl = 1; lvl < size; ++lvl) {
					const auto childWord = path[lvl - 1];
					const auto parentWord = path[lvl];
					const auto bm = bitMask(bitOffsetOf(childWord));
					auto old = std::atomic_ref{ bits[lvl][parentWord] }.fetch_or(bm, std::memory_order_acq_rel);
					if (old & bm) { break; }
				}
			}
			else {
				auto before = std::atomic_ref{ bits[0][path[0]] }.fetch_and(~m0, std::memory_order_acq_rel);
				if (before & ~m0) {
					return;
				}

				for (size_t lvl = 1; lvl < size; ++lvl) {
					const auto childWord = path[lvl - 1];
					const auto parentWord = path[lvl];
					const auto bm = bitMask(bitOffsetOf(childWord));
					auto old = std::atomic_ref{ bits[lvl][parentWord] }.fetch_and(~bm, std::memory_order_acq_rel);
					if (old & ~bm) {
						break;
					}
				}
			}
		}

		bool test(SectorId index) const noexcept {
			auto lock = std::shared_lock(mtx);

			const auto w = wordIndexOf(index);
			if (w >= bits[0].size()) {
				return false;
			}

			const auto m = bitMask(bitOffsetOf(index));
			const auto v = std::atomic_ref{ bits[0][w] }.load(std::memory_order_acquire);

			return (v & m) != 0;
		}

		PinIndex highestSet() const noexcept {
			auto lock = std::shared_lock(mtx);

			int top = static_cast<int>(bits.size() - 1);
			while (top >= 0 && (bits[top].empty() || std::atomic_ref{ bits[top][0] }.load(std::memory_order_acquire) == 0)) {
				--top;
			}

			if (top < 0) {
				return -1;
			}

			PinIndex wordIdx = kBitsCount - std::countl_zero(std::atomic_ref(bits[top][0]).load(std::memory_order_acquire));
			if (wordIdx == -1) {
				return wordIdx;
			}
			for (int lvl = top - 1; lvl >= 0; --lvl) {
				auto w = std::atomic_ref(bits[lvl][wordIdx]).load(std::memory_order_acquire);
				if (w == 0) { return -1; } // data-race fail-safe
				auto b = w != 0 ? kBitsCount - std::countl_zero(w) : w;
				wordIdx = static_cast<PinIndex>((wordIdx << shift) | b);
			}

			return wordIdx;
		}
	};

	struct PinCounters {
		void pin(SectorId id) noexcept {
			assert(id != INVALID_ID);

			epoch.fetch_add(1, std::memory_order_release);
			auto prev = get(id).fetch_add(1, std::memory_order_release);
			if (prev == 0) {
				pinsBitMask.set(id, true);
			}

			auto want = static_cast<PinIndex>(id);
			auto cur = maxPinnedSector.load(std::memory_order_relaxed);
			while (want > cur && !maxPinnedSector.compare_exchange_weak(cur, want, std::memory_order_release, std::memory_order_relaxed)) {}
		}

		void unpin(SectorId id) noexcept {
			assert(id != INVALID_ID);

			epoch.fetch_add(1, std::memory_order_release);

			auto& var = get(id);
			auto prev = var.fetch_sub(1, std::memory_order_release);

			if (prev == 1) {
				pinsBitMask.set(id, false);

				// failsafe
				if (var.load(std::memory_order_acquire) != 0) {
					pinsBitMask.set(id, true);
					return;
				}

				updateMaxPinned();
				var.notify_all();
			}
		}

		bool canMoveSector(SectorId sectorId) const noexcept {
			assert(sectorId != INVALID_ID);

			const auto max = maxPinnedSector.load(std::memory_order_acquire);
			return static_cast<PinIndex>(sectorId) > max && get(sectorId).load(std::memory_order_acquire) == 0;
		}

		void waitUntilChangeable(SectorId sid = 0) const noexcept {
			assert(sid != INVALID_ID);

			const auto id = static_cast<PinIndex>(sid);
			for (;;) {
				auto max = maxPinnedSector.load(std::memory_order_acquire);
				if (id <= max) {
					maxPinnedSector.wait(max, std::memory_order_acquire);
					continue;
				}
				auto& var = get(sid);
				auto c = var.load(std::memory_order_acquire);
				if (c != 0) {
					var.wait(c, std::memory_order_acquire);
					continue;
				}
				return;
			}
		}

		void waitUntilSectorChangeable(SectorId sid) const noexcept {
			assert(sid != INVALID_ID);

			for (;;) {
				auto& var = get(sid);
				auto c = var.load(std::memory_order_acquire);
				if (c != 0) {
					var.wait(c, std::memory_order_acquire);
					continue;
				}
				return;
			}
		}

		bool isPinned(SectorId id) const {
			return get(id).load(std::memory_order_acquire) != 0;
		}

		bool isArrayLocked() const {
			return maxPinnedSector.load(std::memory_order_acquire) > -1;
		}

	private:
		std::atomic<uint16_t>& get(SectorId id) const {
			const size_t bi = id / BLOCK, off = id % BLOCK;

			{   // fast-path
				std::shared_lock r(mtx);
				if (bi < blocks.size() && blocks[bi]) return blocks[bi][off];
			}

			{   // slow-path
				std::unique_lock w(mtx);
				if (bi >= blocks.size()) blocks.resize(bi + 1);
				if (!blocks[bi]) {
					blocks[bi] = std::make_unique<std::atomic<uint16_t>[]>(BLOCK);
					for (size_t j = 0; j < BLOCK; ++j) blocks[bi][j].store(0, std::memory_order_relaxed);
				}

				return blocks[bi][off];
			}
		}

		void updateMaxPinned() noexcept {
			const auto e1 = epoch.load(std::memory_order_acquire);
			auto cur = maxPinnedSector.load(std::memory_order_relaxed);
			if (cur != -1) {
				if ((epoch.load(std::memory_order_acquire) == e1) && maxPinnedSector.compare_exchange_strong(cur, pinsBitMask.highestSet(), std::memory_order_release, std::memory_order_relaxed)) {
					maxPinnedSector.notify_all();
				}
			}
		}

	private:
		static constexpr size_t BLOCK = 4096;

		PinnedIndexesBitMask pinsBitMask;
		std::shared_mutex mtx;

		mutable std::vector<std::unique_ptr<std::atomic<uint16_t>[]>> blocks;

		std::atomic<PinIndex> maxPinnedSector{ -1 };
		std::atomic<uint64_t> epoch{ 0 };
	};

#define SHARED_LOCK(sync) auto lock = ecss::Threads::sharedLock(mtx, sync);
#define UNIQUE_LOCK(sync) auto lock = ecss::Threads::uniqueLock(mtx, sync);

	/// Data container with sectors of custom data in it.
	/// Synchronization lives in the container (allocator is single-threaded under write lock).
	template<typename Allocator = ChunksAllocator>
	class SectorsArray final {
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

		Iterator begin()	const noexcept { return { this, 0 }; }
		Iterator end()		const noexcept { return { this, size(false) }; }

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
		IteratorAlive beginAlive()	const noexcept { return { this, 0 , size(), getLayoutData<T>().isAliveMask }; }
		IteratorAlive endAlive()	const noexcept { return { this, size() }; }

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

		RangedIterator beginRanged(const EntitiesRanges& ranges)	const noexcept { return { this, ranges }; }
		RangedIterator endRanged(const EntitiesRanges& ranges)		const noexcept { return { this, ranges.back().second }; }

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
		RangedIteratorAlive beginRangedAlive(const EntitiesRanges& ranges)	const noexcept { return { this, ranges, getLayoutData<T>().isAliveMask }; }
		RangedIteratorAlive endRangedAlive(const EntitiesRanges& ranges)	const noexcept { return { this, ranges.back().second }; }

	public:
		struct PinnedSector {
			Sector* sec = nullptr;

			PinnedSector() noexcept = default;
			PinnedSector(const SectorsArray* o, Sector* s, SectorId sid) noexcept	: sec(s), owner(o), id(sid) {
				assert(id != INVALID_ID);
				owner->mPinsCounter.pin(id);
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
					owner->mPinsCounter.unpin(id);
				}

				owner = nullptr;
				sec = nullptr;
				id = static_cast<SectorId>(INVALID_ID);
			}

			Sector* get() const noexcept { return sec; }
			Sector* operator->() const noexcept { return sec; }
			explicit operator bool() const noexcept { return sec != nullptr; }

		private:
			const SectorsArray* owner = nullptr;
			SectorId id = INVALID_ID;
		};

	public:
		SectorsArray(const SectorsArray& other)				noexcept { *this = other; }
		SectorsArray& operator=(const SectorsArray& other)	noexcept { if (this == &other) { return *this; } copy(other); return *this; }

		SectorsArray(SectorsArray&& other)					noexcept { *this = std::move(other); }
		SectorsArray& operator=(SectorsArray&& other)		noexcept { if (this == &other) { return *this; } move(std::move(other)); return *this; }

		~SectorsArray() = default;

		template <typename... Types>
		static SectorsArray* create(uint32_t capacity = 0, uint32_t chunkSize = 8192) noexcept {
			const auto array = new SectorsArray(chunkSize); array->template initializeSector<Types...>(capacity);
			return array;
		}

	private:
		SectorsArray(uint32_t chunkSize = 8192) noexcept : mAllocator(chunkSize) {}

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


		[[nodiscard]] PinnedSector pinSector(Sector* sector, bool sync = true)	const noexcept { SHARED_LOCK(sync) return sector ? PinnedSector{ this, sector, sector->id } : PinnedSector{}; }
		[[nodiscard]] PinnedSector pinSector(SectorId id, bool sync = true)		const noexcept { SHARED_LOCK(sync) return pinSector(mAllocator.tryGetSector(id), false); }

		void freeSector(SectorId id, bool shift = false, bool sync = true)			  noexcept { eraseSectorSafe(id, shift, sync); }
		bool containsSector(SectorId id, bool sync = true)						const noexcept { return findSector(id, sync); }
		// unsafe getter, use pinned version in multithread
		Sector* findSector(SectorId id, bool sync = true)						const noexcept { SHARED_LOCK(sync) return mAllocator.tryGetSector(id); }
		// unsafe getter, use pinned version in multithread
		Sector* getSector(SectorId id, bool sync = true)						const noexcept { SHARED_LOCK(sync) return mAllocator.getSector(id); }

		size_t sectorsCapacity(bool sync = true)								const noexcept { SHARED_LOCK(sync) return mAllocator.sectorsCapacity(); }
		size_t getSectorIndex(SectorId id, bool sync = true)					const noexcept { SHARED_LOCK(sync) return mAllocator.calcSectorIndex(id); }

		size_t findRightNearestSectorIndex(SectorId sectorId, bool sync = true) const noexcept {
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
		void destroyMember(SectorId sectorId, bool sync = true) const noexcept {
			UNIQUE_LOCK(sync)
			mPinsCounter.waitUntilSectorChangeable(sectorId);
			if (auto sector = findSector(sectorId, false)) {
				sector->destroyMember(getLayoutData<T>());
			}
		}

		template<typename T>
		void destroyMembers(std::vector<SectorId>& sectorIds, bool sort = true, bool sync = true) const noexcept {
			if (sectorIds.empty()) return;
			if (sort) std::ranges::sort(sectorIds);


			const auto& layout = getLayoutData<T>();
			UNIQUE_LOCK(sync)

			const auto sectorsSize = sectorsCapacity(false);
			for (const auto sectorId : sectorIds) {
				if (sectorId >= sectorsSize) break;
				mPinsCounter.waitUntilSectorChangeable(sectorId);
				if (auto sector = pinSector(sectorId, false)) {
					sector->destroyMember(layout);
				}
			}
		}

		void clearSectors(std::vector<SectorId>& sectorIds, bool sort = true, bool sync = true) const noexcept {
			if (sectorIds.empty()) return;
			if (sort) std::ranges::sort(sectorIds);

			const auto layout = getLayout();
			UNIQUE_LOCK(sync)

			const auto sectorsSize = sectorsCapacity(false);
			for (const auto sectorId : sectorIds) {
				if (sectorId >= sectorsSize) break;
				mPinsCounter.waitUntilSectorChangeable(sectorId);
				Sector::destroyMembers(findSector(sectorId, false), layout);
			}
		}

	public: // container methods
		// WARNING! at() doesn't do bounds checks
		Sector* at(size_t sectorIndex, bool sync = true) const noexcept { SHARED_LOCK(sync) return mAllocator.at(sectorIndex); }

		size_t capacity(bool sync = true) const noexcept { SHARED_LOCK(sync) return mAllocator.capacity(); }
		size_t size(bool sync = true) const noexcept		{ SHARED_LOCK(sync) return mAllocator.size(); }
		bool	  empty(bool sync = true) const noexcept	{ return !size(sync); }

		void shrinkToFit(bool sync = true) noexcept { UNIQUE_LOCK(sync) mAllocator.shrinkToFit(); }
		void clear(bool sync = true) noexcept {
			UNIQUE_LOCK(sync)
			mPinsCounter.waitUntilChangeable();

			mAllocator.free();
			mPendingErase.clear();
		}

		void reserve(uint32_t newCapacity, bool sync = true) noexcept {	UNIQUE_LOCK(sync) mAllocator.reserve(newCapacity); }

		template<typename T>
		std::remove_reference_t<T>* insert(SectorId sectorId, T&& data, bool sync = true) noexcept {
			using U = std::remove_reference_t<T>;
			UNIQUE_LOCK(sync)
			if (sync) {
				mPinsCounter.waitUntilChangeable(sectorId);
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
		T* emplace(SectorId sectorId, bool sync = true, Args&&... args) noexcept {
			UNIQUE_LOCK(sync)

			if (sync) {
				mPinsCounter.waitUntilChangeable(sectorId);
			}

			return Memory::Sector::emplaceMember<T>(mAllocator.acquireSector(sectorId), getLayoutData<T>(), std::forward<Args>(args)...);
		}

		// Safe erase: only if not pinned and id >= last active pinned id; otherwise queued.
		void eraseSectorSafe(SectorId id, bool shift = false, bool sync = true) noexcept {
			UNIQUE_LOCK(sync)
			Sector* s = mAllocator.tryGetSector(id);
			if (!s) {
				return;
			}

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

		// Raw erase by contiguous index (compat). Applies same safety rule per SectorId.
		void erase(size_t beginIdx, size_t count = 1, bool shift = false, bool sync = true) noexcept {
			if (!count) return;
			UNIQUE_LOCK(sync)
			const size_t endIdx = std::min(beginIdx + count, mAllocator.size());
			for (size_t i = beginIdx; i < endIdx; ++i) {
				Sector* s = mAllocator.at(i);
				if (!s) continue;
				eraseSectorSafe(s->id, shift, false);
			}
		}

		void defragment(bool sync = true) noexcept {
			if (empty(sync)) {
				return;
			}

			UNIQUE_LOCK(sync)
			if (sync) {
				mPinsCounter.waitUntilChangeable();
			}

			mAllocator.defragment();
		}

		// (call at frame end / maintenance points)
		void processPendingErases(bool sync = true) noexcept {
			if (mPendingErase.empty()) { return; }

			UNIQUE_LOCK(sync)

			auto tmp = std::move(mPendingErase);
			for (auto id : tmp) {
				if (!mPinsCounter.isPinned(id)) {
					mAllocator.freeSector(id, false);
				}
				else {
					mPendingErase.emplace_back(id);
				}
			}

			if (!mPinsCounter.isArrayLocked()) {
				defragment(false); // todo defragment only if fragmentation koeff too high
			}
		}

	private:
		void copy(const SectorsArray& other) noexcept {
			auto lock = ecss::Threads::uniqueLock(mtx, true);
			auto otherLock = ecss::Threads::sharedLock(other.mtx, true);
			mPinsCounter.waitUntilChangeable();

			processPendingErases(false);
			mAllocator = other.mAllocator;
		}

		void move(SectorsArray&& other) noexcept {
			auto lock = ecss::Threads::uniqueLock(mtx, true);
			auto otherLock = ecss::Threads::uniqueLock(other.mtx, true);
			mPinsCounter.waitUntilChangeable();
			other.mPinsCounter.waitUntilChangeable();

			processPendingErases(false);
			other.processPendingErases(false);

			mAllocator = std::move(other.mAllocator);
		}

	public:
		std::shared_lock<std::shared_mutex> readLock()  const noexcept { return std::shared_lock(mtx); }
		std::shared_mutex& getMutex() const noexcept { return mtx; }

	private:
		Allocator mAllocator;

		mutable std::shared_mutex mtx;

		PinCounters	mPinsCounter;

		std::vector<SectorId> mPendingErase;
	};
}
