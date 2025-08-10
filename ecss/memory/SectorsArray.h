#pragma once

#include <algorithm>
#include <cassert>
#include <shared_mutex>
#include <bit>
#include <variant>

#include <ecss/memory/Sector.h>
#include <ecss/memory/Reflection.h>
#include <ecss/EntitiesRanges.h>
#include <ecss/threads/SyncManager.h>
#include <ecss/memory/ChunksAllocator.h>
#include <ecss/contiguousMap.h>

namespace ecss::Memory {
	/// <summary>
	/// Data container with sectors of custom data in it
	///	
	///	the Sector is object in memory which includes Sector{ uint32_t(by default) sectorId; }
	/// 
	///	you can't get member without knowing it's type
	/// </summary>
	template<typename Allocator = ChunksAllocator>
	class SectorsArray final {
	public:
		class Iterator {
		public:
			using iterator_category = std::forward_iterator_tag;
			using value_type = Sector*;
			using difference_type = std::ptrdiff_t;
			using pointer = value_type*;
			using reference = value_type&;

			Iterator() noexcept = default;
			Iterator(const SectorsArray* array, size_t idx) noexcept : chunksIt(idx, &array->mAllocator) { }

			inline Iterator& operator++()	noexcept { return ++chunksIt, *this; }
			inline Iterator operator++(int) noexcept { Iterator tmp = *this; ++(*this); return tmp; }

			inline bool operator==(const Iterator& other) const noexcept { return chunksIt == other.chunksIt; }
			inline bool operator!=(const Iterator& other) const noexcept { return !(*this == other); }

			inline Sector* operator*()  const noexcept { return *chunksIt; }
			inline Sector* operator->() const noexcept { return *chunksIt; }

		private:
			typename Allocator::Iterator chunksIt;
		};

		Iterator begin()	const noexcept { return { this, 0 }; }
		Iterator end()		const noexcept { return { this, size(SyncType::NONE) }; }

		class IteratorAlive {
		public:
			using iterator_category = std::forward_iterator_tag;
			using value_type = Sector*;
			using difference_type = std::ptrdiff_t;
			using pointer = value_type*;
			using reference = value_type&;

			IteratorAlive() noexcept = default;
			IteratorAlive(const SectorsArray* array, size_t idx, size_t size, uint32_t aliveMask) noexcept : chunksIt(idx, &array->mAllocator), chunksLastIt(size, &array->mAllocator), typeAliveMask(aliveMask) {
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
		IteratorAlive beginAlive()	const { return { this, 0 , size(), getLayoutData<T>().isAliveMask }; }
		template<typename T>
		IteratorAlive endAlive()	const { return { this, size() }; }

		class RangedIterator {
		public:
			using iterator_category = std::forward_iterator_tag;
			using value_type = Sector*;
			using difference_type = std::ptrdiff_t;
			using pointer = value_type*;
			using reference = value_type&;

			RangedIterator() noexcept = default;

			RangedIterator(const SectorsArray* array, const EntitiesRanges& ranges) noexcept {
				mIterators.reserve(ranges.ranges.size() * 2);
				for (auto it = ranges.ranges.rbegin(), endIt = ranges.ranges.rend(); it != endIt; ++it) {
					mIterators.emplace_back((*it).second, &array->mAllocator);
					mIterators.emplace_back((*it).first, &array->mAllocator);
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
				assert(!ranges.ranges.empty());

				mIterators.reserve(ranges.ranges.size() * 2);
				for (auto it = ranges.ranges.rbegin(), endIt = ranges.ranges.rend(); it != endIt; ++it) {
					mIterators.emplace_back((*it).second, &array->mAllocator);
					mIterators.emplace_back((*it).first, &array->mAllocator);
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
		template<typename T>
		RangedIteratorAlive endRangedAlive(const EntitiesRanges& ranges)	const noexcept { return { this, ranges.back().second }; }

	public:
		SectorsArray(const SectorsArray& other)				noexcept { copy(other); }
		SectorsArray& operator=(const SectorsArray& other)	noexcept { if (this == &other) { return *this; } return copy(other), *this; }

		SectorsArray(SectorsArray&& other)					noexcept { move(std::move(other)); }
		SectorsArray& operator=(SectorsArray&& other)		noexcept { if (this == &other) { return *this; } return move(std::move(other)), *this; }

		~SectorsArray() = default;

		template <typename... Types>
		static inline SectorsArray* create(ReflectionHelper& reflectionHelper, uint32_t capacity = 0, uint32_t chunkSize = 8192) noexcept {
			const auto array = new SectorsArray(chunkSize); array->initializeSector<Types...>(reflectionHelper, capacity); return array;
		}

	private:
		SectorsArray(uint32_t chunkSize = 8192) noexcept : mAllocator(chunkSize){}

		template <typename... Types>
		void initializeSector(ReflectionHelper& reflectionHelper, uint32_t capacity) noexcept {
			static_assert(types::areUnique<Types...>(), "Duplicates detected in types");
			static SectorLayoutMeta* meta = SectorLayoutMeta::create<Types...>();
			mSectorLayout = meta;
			mAllocator.setLayoutMeta(mSectorLayout);

			reserve(capacity);
		}

	public: // sector helpers
		template<typename T>
		inline const LayoutData& getLayoutData()																const noexcept { return mSectorLayout->getLayoutData<T>(); }
		SectorLayoutMeta*		 getLayout()																	const noexcept { return mSectorLayout; }

		inline void		freeSector(SectorId id, bool shift = false, SyncType syncType = SyncType::UNIQUE)			  noexcept { auto lock = SM::Lock(mtx, syncType); mAllocator.freeSector(id, shift); }
		inline bool		containsSector(SectorId id,					SyncType syncType = SyncType::SHARED)		const noexcept { return findSector(id, syncType); }
		inline Sector*	findSector(SectorId id,						SyncType syncType = SyncType::SHARED)		const noexcept { auto lock = SM::Lock(mtx, syncType); return mAllocator.tryGetSector(id);	}
		inline Sector*	getSector(SectorId id,						SyncType syncType = SyncType::SHARED)		const noexcept { auto lock = SM::Lock(mtx, syncType); return mAllocator.getSector(id); }

		inline size_t	sectorsCapacity(							SyncType syncType = SyncType::SHARED)		const noexcept { auto lock = SM::Lock(mtx, syncType); return mAllocator.sectorsCapacity(); }
		inline size_t	getSectorIndex(SectorId id,					SyncType syncType = SyncType::SHARED)		const noexcept { auto lock = SM::Lock(mtx, syncType); return mAllocator.calcSectorIndex(id); }

	public: // container methods
		// WARNING! at() don't make any bounding checks
		Sector*	at(size_t sectorIndex,								SyncType syncType = SyncType::SHARED)		const noexcept { auto lock = SM::Lock(mtx, syncType); return mAllocator.at(sectorIndex); }

		inline size_t capacity(										SyncType syncType = SyncType::SHARED)		const noexcept { auto lock = SM::Lock(mtx, syncType); return mAllocator.capacity();  }
		inline size_t size(											SyncType syncType = SyncType::SHARED)		const noexcept { auto lock = SM::Lock(mtx, syncType); return mAllocator.size(); }
		inline bool	  empty(										SyncType syncType = SyncType::SHARED)		const noexcept { return !size(syncType); }

		inline void shrinkToFit(									SyncType syncType = SyncType::UNIQUE)			  noexcept { auto lock = SM::Lock(mtx, syncType); mAllocator.shrinkToFit(); }
		inline void clear(											SyncType syncType = SyncType::UNIQUE)			  noexcept { auto lock = SM::Lock(mtx, syncType); mAllocator.free(); }
		inline void reserve(uint32_t newCapacity,					SyncType syncType = SyncType::UNIQUE)			  noexcept { auto lock = SM::Lock(mtx, syncType); mAllocator.reserve(newCapacity); }

		template<typename T>
		std::remove_reference_t<T>* insert(SectorId sectorId, T&& data, SyncType syncType = SyncType::UNIQUE) noexcept {
			using U = std::remove_reference_t<T>;
			auto lock = SM::Lock(mtx, syncType);
			Sector* sector = mAllocator.acquireSector(sectorId);

			if constexpr (std::is_same_v<std::remove_cvref_t<T>, Sector>) {
				if constexpr (std::is_lvalue_reference_v<T>) {
					return Sector::copySector(std::addressof(data), sector, mSectorLayout);
				}
				else {
					return Sector::moveSector(std::addressof(data), sector, mSectorLayout);
				}
			}
			else {
				if constexpr (std::is_lvalue_reference_v<T>) {
					return Sector::copyMember<U>(std::forward<T>(data), sector, getLayoutData<T>());
				}
				else {
					return Sector::moveMember<U>(std::forward<T>(data), sector, getLayoutData<T>());
				}
			}
		}

		template<typename T, class... Args>
		T* emplace(SectorId id, SyncType syncType = SyncType::UNIQUE, Args&&... args) noexcept {
			auto lock = SM::Lock(mtx, syncType);
			return Memory::Sector::emplaceMember<T>(mAllocator.acquireSector(id), getLayoutData<T>(), std::forward<Args>(args)...);
		}

		void erase(size_t begin, size_t count = 1, bool shift = false, SyncType syncType = SyncType::UNIQUE) noexcept {
			if (empty()) { return; }

			auto lock = SM::Lock(mtx, syncType);
			mAllocator.erase(begin, count, shift);
		}

		void defragment(SyncType syncType = SyncType::UNIQUE) noexcept {
			if (empty()) { return; }

			auto lock = SM::Lock(mtx, syncType);
			mAllocator.defragment();
		}

	private:
		void copyCommonData(const SectorsArray& other) noexcept {
			mSectorLayout = other.mSectorLayout;
		}

		void copy(const SectorsArray& other) noexcept {
			copyCommonData(other);
			auto lock = SM::Lock(mtx, SyncType::UNIQUE);
			auto otherLock = SM::Lock(other.mtx, SyncType::SHARED);

			mAllocator = other.mAllocator;
		}

		void move(SectorsArray&& other) noexcept {
			copyCommonData(other);

			auto lock = SM::Lock(mtx, SyncType::UNIQUE);
			auto otherLock = SM::Lock(other.mtx, SyncType::UNIQUE);

			mAllocator = std::move(other.mAllocator);
		}

	private:
		Allocator mAllocator;
		SectorLayoutMeta* mSectorLayout = nullptr;

	public:
		// thread safe helpers
		std::shared_lock<std::shared_mutex> readLock() const noexcept { return std::shared_lock(mtx); }
		std::unique_lock<std::shared_mutex> writeLock() const noexcept { return std::unique_lock(mtx); }
		std::shared_mutex& getMutex() const noexcept { return mtx; }

	private:
		mutable std::shared_mutex mtx;
		std::atomic_uint64_t pinned = 0;
	};

	namespace SectorArrayUtils {
		inline size_t findRightNearestSectorIndex(SectorsArray<>* array, SectorId sectorId) noexcept {
			while (sectorId < array->sectorsCapacity(SyncType::NONE)) {
				if (array->findSector(sectorId, SyncType::NONE)) {
					return array->getSectorIndex(sectorId, SyncType::NONE);
				}
				sectorId++;
			}

			return array->size(SyncType::NONE);
		}

		template<typename T>
		inline void destroyMember(SectorsArray<>* array, SectorId sectorId) noexcept {
			auto lock = array->writeLock();
			const auto sector = array->findSector(sectorId, SyncType::NONE);
			if (!sector) {
				return;
			}

			sector->destroyMember(array->getLayoutData<T>());
		}

		template<typename T>
		inline void destroyMembers(SectorsArray<>* array, std::vector<SectorId>& sectorIds, bool sort = true) noexcept {
			if (sectorIds.empty()) {
				return;
			}

			if (sort) {
				std::ranges::sort(sectorIds);
			}

			const auto& layout = array->getLayoutData<T>();
			auto lock = array->writeLock();

			const auto sectorsSize = array->sectorsCapacity(SyncType::NONE);
			
			for (const auto sectorId : sectorIds) {
				if (sectorId >= sectorsSize) {
					break; //all valid entities destroyed
				}

				if (auto sector = array->findSector(sectorId, SyncType::NONE)) {
					sector->destroyMember(layout);
				}

			}
		}

		inline void destroyAllMembers(SectorsArray<>* array, std::vector<SectorId>& sectorIds, bool sort = true) noexcept {
			if (sectorIds.empty()) {
				return;
			}

			if (sort) {
				std::ranges::sort(sectorIds);
			}

			const auto layout = array->getLayout();
			auto lock = array->writeLock();

			const auto sectorsSize = array->sectorsCapacity(SyncType::NONE);

			for (const auto sectorId : sectorIds) {
				if (sectorId >= sectorsSize) {
					break; //all valid entities destroyed
				}

				Sector::destroyMembers(array->findSector(sectorId, SyncType::NONE), layout);
			}
		}

		inline void destroySectors(SectorsArray<>* array, std::vector<SectorId>& sectorIds, bool sort = true) noexcept {
			if (sectorIds.empty()) {
				return;
			}

			if (sort) {
				std::ranges::sort(sectorIds);
			}

			const auto layout = array->getLayout();
			auto lock = array->writeLock();
			const auto sectorsSize = array->sectorsCapacity(SyncType::NONE);
			
			for (const auto sectorId : sectorIds) {
				if (sectorId >= sectorsSize) {
					break; //all valid entities destroyed
				}
				array->freeSector(sectorId, false, SyncType::NONE);
			}
		}
	}
}
