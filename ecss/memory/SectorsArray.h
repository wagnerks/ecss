#pragma once

#include <algorithm>
#include <cassert>
#include <shared_mutex>
#include <bit>

#include "Sector.h"
#include "Reflection.h"

namespace ecss::Memory {

	/// <summary>
	/// data container with sectors of custom data in it
	///	
	///	the Sector is object in memory which includes Sector{ uint32_t(by default) sectorId; }
	///	
	///	members from sector can be pulled as void*, and then casted to desired type, as long as the information about offsets for every member stored according to their type ids
	///
	///	you can't get member without knowing it's type
	/// </summary>
	///
	class SectorsArray final {
	public:
		class Iterator {
		public:
			using iterator_category = std::forward_iterator_tag;
			using value_type = Sector*;
			using difference_type = std::ptrdiff_t;
			using pointer = value_type*;
			using reference = value_type&;

			Iterator() = default;

			Iterator(const SectorsArray* array, size_t idx)
			: mArray(array)
			, mIdx(idx)
			, mChunkSize(mArray->mChunksManager.mChunkSize)
			, sectorSize(mArray->mChunksManager.mSectorSize)
			{
				auto sectorsSize = mArray->size();
				if (mIdx < sectorsSize) {
					mSector = mArray->at(mIdx);
					mInChunkIndex = mIdx % mChunkSize;
				}
				else {
					mIdx = sectorsSize;
					mInChunkIndex = mIdx % mChunkSize;
				}
			}

			Iterator& operator++()
			{
				mIdx++;
				mInChunkIndex++;
				mSector = reinterpret_cast<Sector*>(reinterpret_cast<char*>(mSector) + sectorSize);
				updateSector = mInChunkIndex == mChunkSize;
				return *this;
			}
			Iterator operator++(int) { Iterator tmp = *this; ++(*this); return tmp; }

			bool operator==(const Iterator& other) const { return mIdx == other.mIdx; }
			bool operator!=(const Iterator& other) const { return !(*this == other); }

			Sector* operator*() const
			{
				if (updateSector) {
					mInChunkIndex = 0;
					updateSector = false;
					mSector = mArray->at(mIdx);
				}
				return mSector;
			}

			Sector* operator->() const
			{
				if (updateSector) {
					mInChunkIndex = 0;
					updateSector = false;
					mSector = mArray->at(mIdx);
				}
				return mSector;
			}

			size_t getIndex() { return mIdx; }
		private:
			mutable bool updateSector = false; 
			const SectorsArray* mArray = nullptr;
			size_t mIdx{};
			mutable size_t mInChunkIndex{};
			mutable Sector* mSector = nullptr;
			size_t mChunkSize{};
			uint16_t sectorSize{};
		};

		Iterator begin() const { return {this, 0}; }
		Iterator end() const { return {this, size()}; }

	public:
		~SectorsArray() { clear(); }

		SectorsArray(const SectorsArray& other) noexcept : mChunksManager(other.mChunksManager.mChunkSize) {
			mChunksManager.mChunkSize = other.mChunksManager.mChunkSize;
			mChunksManager.mSectorSize = other.mChunksManager.mSectorSize;
			mLayoutMap = other.mLayoutMap;
			mSectorLayout = other.mSectorLayout;
			mIsSectorTrivial = other.mIsSectorTrivial;

			auto lock = writeLock();
			auto otherLock = other.readLock();

			if (mIsSectorTrivial) {
				mChunksManager = other.mChunksManager;
			}
			else {
				reserve(other.mSize);
				for (auto i = 0u; i < other.mSize; i++) {
					Sector::copySector(other.at(i), at(i), mSectorLayout);
				}
			}
			mSize = other.mSize;
			mSectorsMap = other.mSectorsMap;
		}

		SectorsArray& operator=(const SectorsArray& other) noexcept {
			const bool isSame = other.mSectorLayout == mSectorLayout;
			if (!isSame || this == &other) {
				assert(isSame && "wrong source sectors array type");
				return *this;
			}

			mChunksManager.mChunkSize = other.mChunksManager.mChunkSize;
			mChunksManager.mSectorSize = other.mChunksManager.mSectorSize;
			mLayoutMap = other.mLayoutMap;
			mSectorLayout = other.mSectorLayout;
			mIsSectorTrivial = other.mIsSectorTrivial;

			auto lock = writeLock();
			auto otherLock = other.readLock();

			if (mIsSectorTrivial) {
				mChunksManager = other.mChunksManager;
			}
			else {
				erase(other.mSize, mSize - other.mSize);
				reserveSafe(other.mSize);
				for (auto i = 0u; i < other.mSize; i++) {
					Sector::copySector(other.at(i), at(i), mSectorLayout);
				}
			}
			mSize = other.mSize;
			mSectorsMap = other.mSectorsMap;

			return *this;
		}

		SectorsArray(SectorsArray&& other) noexcept : mChunksManager(other.mChunksManager.mChunkSize) {
			mLayoutMap = other.mLayoutMap;
			mSectorLayout = other.mSectorLayout;
			mIsSectorTrivial = other.mIsSectorTrivial;

			auto lock = writeLock();
			auto otherLock = other.writeLock();

			mChunksManager = std::move(other.mChunksManager);

			mSize = other.mSize;
			mSectorsMap = std::move(other.mSectorsMap);

			other.mSize = 0;
			other.mSectorsMap.clear();
		}

		SectorsArray& operator=(SectorsArray&& other) noexcept {
			const bool isSame = other.mSectorLayout == mSectorLayout;
			if (!isSame || this == &other) {
				assert(isSame && "wrong source sectors array type");
				return *this;
			}

			mChunksManager.mChunkSize = other.mChunksManager.mChunkSize;
			mChunksManager.mSectorSize = other.mChunksManager.mSectorSize;
			mLayoutMap = other.mLayoutMap;
			mSectorLayout = other.mSectorLayout;
			mIsSectorTrivial = other.mIsSectorTrivial;

			auto lock = writeLock();
			auto otherLock = other.writeLock();

			mChunksManager = std::move(other.mChunksManager);

			mSize = other.mSize;
			mSectorsMap = std::move(other.mSectorsMap);

			other.mSize = 0;
			other.mSectorsMap.clear();

			return *this;
		}

	private:
		SectorsArray(uint32_t chunkSize = 8192) : mChunksManager(chunkSize){}

		template <typename... Types>
		void initializeSector(ReflectionHelper& reflectionHelper, uint32_t capacity) {
			static_assert(types::areUnique<Types...>(), "Duplicates detected in types");
			static SectorLayoutMeta<Types...> layoutMeta;
			mSectorLayout = &layoutMeta;

			mChunksManager.mSectorSize = mSectorLayout->getTotalSize();
			mLayoutMap.reserve(sizeof...(Types));
			((mLayoutMap[reflectionHelper.getTypeId<Types>()] = static_cast<uint8_t>(mSectorLayout->getLayoutData<Types>().index)), ...);

			if (mIsSectorTrivial) {
				for (auto& layout : *mSectorLayout) {
					mIsSectorTrivial = mIsSectorTrivial && layout.isTrivial;
				}
			}

			reserveSafe(capacity);
		}

	public:
		// avoid chunk sizes not equal to power of two
		template <typename... Types>
		static inline SectorsArray* create(ReflectionHelper& reflectionHelper, uint32_t capacity = 0, uint32_t chunkSize = 8192) {
			const auto array = new SectorsArray(chunkSize);
			array->initializeSector<Types...>(reflectionHelper, capacity);

			return array;
		}

	public: // sector helpers
		ISectorLayoutMeta* getLayout() const { return mSectorLayout; }
		template<typename T>
		const LayoutData& getLayoutData() const { return mSectorLayout->getLayoutData<T>(); }
		const LayoutData& getLayoutData(ECSType typeId) const {	return mSectorLayout->getLayoutData(mLayoutMap.at(typeId)); }

		Sector* findSector(SectorId sectorId) const { return containsSector(sectorId) ? at(getSectorIndex(sectorId)) : nullptr;	}
		Sector* getSector(SectorId sectorId) const { return at(getSectorIndex(sectorId)); }
		Sector* acquireSector(SectorId sectorId) {
			if (auto sector = findSector(sectorId)) {
				return sector;
			}

			return emplaceSector(sectorId);
		}

		size_t sectorsCapacity() const { return mSectorsMap.size(); }
		SectorId getSectorIndex(SectorId sectorId) const { return mSectorsMap.at(sectorId); }
		void setSectorIndex(SectorId sectorId, SectorId newPos) { mSectorsMap[sectorId] = newPos; }
		bool containsSector(SectorId id) const { return id < sectorsCapacity() && getSectorIndex(id) < size(); }
		const std::vector<SectorId>& getSectorsMap() const { return mSectorsMap; }
		Sector* emplaceSectorSafe(SectorId sectorId) {
			auto lock = writeLock();
			return emplaceSector(sectorId);
		}

		// Inserts a sector at position 'pos' with a given sectorId.
		// If a sector with that id exists, destroys it first.
		// Shifts data right if needed. Ensures mapping is updated.
		// Returns pointer to the newly constructed sector.
		Sector* emplaceSector(SectorId sectorId) {
			if (auto found = findSector(sectorId)) {
				Sector::destroyMembers(found, mSectorLayout);
				return found;
			}

			if (sectorId >= sectorsCapacity()) {
				if (sectorId >= sectorsCapacity()) {
					mSectorsMap.resize(sectorId + 1, INVALID_ID);
				}
			}

			reserve(size() + 1);
			auto pos = findInsertPosition(sectorId);
			if (pos < size()) {
				++mSize;
				shiftDataRight(pos);
			}
			else {
				++mSize;
			}
			

			setSectorIndex(sectorId, static_cast<SectorId>(pos));

			return new (at(pos))Sector(sectorId, 0);
		}

	public: // container methods
		// WARNING! operator[] don't make any bounding checks
		Sector* operator[](size_t i) const { return at(i); }
		// WARNING! at() don't make any bounding checks
		Sector* at(size_t sectorIndex) const { return mChunksManager.getSector(sectorIndex); }

		uint32_t capacity() const { return mChunksManager.capacity();  }
		uint32_t size() const { return mSize; }
		bool empty() const { return !size(); }
		void clear() {
			if (mIsSectorTrivial) {
				auto lock = writeLock();
				mSize = 0;
				mChunksManager.clear();
				mSectorsMap.clear();
			}
			else {
				auto lock = writeLock();
				erase(0, size());
				mSectorsMap.clear();
			}
		}

		void reserveSafe(uint32_t newCapacity) {
			if (newCapacity > capacity()) {
				auto lock = writeLock();
				reserve(newCapacity);
			}
		}

		void reserve(uint32_t newCapacity) {
			if (newCapacity > capacity()) {
				auto newChunksCount = (newCapacity + mChunksManager.mChunkSize - 1) / mChunksManager.mChunkSize;
				mChunksManager.allocateChunks(newChunksCount);

				if (capacity() > sectorsCapacity()) {
					mSectorsMap.resize(capacity(), INVALID_ID);
				}
			}
		}

		void shrinkToFit() { mChunksManager.shrinkToFit(size()); }
		template<typename T>
		std::remove_reference_t<T>* insertSafe(SectorId sectorId, T&& data) {
			using U = std::remove_reference_t<T>;
			auto lock = writeLock();
			Sector* sector = acquireSector(sectorId);

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

		template<typename T>
		std::remove_reference_t<T>* insert(SectorId sectorId, T&& data) {
			using U = std::remove_reference_t<T>;\
			Sector* sector = acquireSector(sectorId);

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

		template<typename T, class ...Args>
		T* emplace(SectorId sectorId, Args&&... args) {
			return Memory::Sector::emplaceMember<T>(acquireSector(sectorId), getLayoutData<T>(), std::forward<Args>(args)...);
		}

		template<typename T, class ...Args>
		T* emplaceSafe(SectorId sectorId, Args&&... args) {
			auto lock = writeLock();
			return Memory::Sector::emplaceMember<T>(acquireSector(sectorId), getLayoutData<T>(), std::forward<Args>(args)...);
		}

		void eraseSector(SectorId sectorId, bool shift = true) {
			auto lock = writeLock();
			if (containsSector(sectorId)) {
				erase(getSectorIndex(sectorId), 1, shift);
			}
		}

		void eraseSafe(size_t begin, size_t count = 1, bool shift = true) {
			auto lock = writeLock();
			erase(begin, count, shift);
		}

		void erase(size_t begin, size_t count = 1, bool shift = true) {
			count = std::min(size() - begin, count);
			if (count == 0 || begin >= size()) {
				return;
			}
			
			for (auto i = begin; i < begin + count; i++) {
				auto sector = at(i);
				setSectorIndex(sector->id, INVALID_ID);
				Sector::destroySector(sector, mSectorLayout);
			}

			if (shift) {
				shiftDataLeft(begin, count);
				mSize -= static_cast<uint32_t>(count);
				shrinkToFit();
			}
		}

		void defragment() {
			if (empty()) {
				return;
			}

			auto lock = writeLock();
			if (empty()) {
				return;
			}
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

			uint32_t deleted = 0;
			size_t emptyPos = 0;
			for (auto i = 0u; i < size(); i++) {
				auto sector = at(i);
				if (!sector->isSectorAlive()) {
					setSectorIndex(sector->id, INVALID_ID);
					Sector::destroySector(sector, mSectorLayout);
					deleted++;
				}
				else {
					if (emptyPos == i) {
						emptyPos++;
						continue;
					}

					auto newSector = Sector::moveSector(sector, at(emptyPos), mSectorLayout);
					setSectorIndex(newSector->id, static_cast<SectorId>(emptyPos++));
				}
			}

			mSize -= deleted;
			
			shrinkToFit();
		}

		std::shared_lock<std::shared_mutex> readLock() const { return std::shared_lock(mMutex); }
		std::unique_lock<std::shared_mutex> writeLock() const { return std::unique_lock(mMutex); }
		std::shared_mutex& getMutex() const { return mMutex; }

	private:
		struct ChunksManager {
			ChunksManager(const ChunksManager& other) : mChunkSize(other.mChunkSize), mIsChunkSizePowerOfTwo(isPowerOfTwo(mChunkSize)), mSectorSize(other.mSectorSize) {
				allocateChunks(other.mChunks.size());
				for (auto i = 0u; i < other.mChunks.size(); i++) {
					memcpy(mChunks[i], other.mChunks[i], mChunkSize * static_cast<size_t>(mSectorSize));
				}
			}

			ChunksManager(ChunksManager&& other) noexcept : mChunkSize(other.mChunkSize), mIsChunkSizePowerOfTwo(isPowerOfTwo(mChunkSize)), mSectorSize(other.mSectorSize), mChunks(std::move(other.mChunks)) {}

			ChunksManager& operator=(const ChunksManager& other) {
				if (this == &other)
					return *this;

				mChunkSize = other.mChunkSize;
				mSectorSize = other.mSectorSize;
				mIsChunkSizePowerOfTwo = isPowerOfTwo(mChunkSize);
				clear();
				allocateChunks(other.mChunks.size());
				for (auto i = 0u; i < other.mChunks.size(); i++) {
					memcpy(mChunks[i], other.mChunks[i], mChunkSize * static_cast<size_t>(mSectorSize));
				}

				return *this;
			}

			ChunksManager& operator=(ChunksManager&& other) noexcept {
				if (this == &other)
					return *this;

				clear();
				mChunkSize = other.mChunkSize;
				mIsChunkSizePowerOfTwo = isPowerOfTwo(mChunkSize);
				mSectorSize = other.mSectorSize;
				mChunks = std::move(other.mChunks);

				return *this;
			}

			ChunksManager(uint32_t chunkSize) : mChunkSize(chunkSize), mIsChunkSizePowerOfTwo(isPowerOfTwo(mChunkSize)) {}

			void allocateChunks(size_t count = 1) {
				if (count == 0) {
					return;
				}
				std::unique_lock lock(mtx);

				mChunks.reserve(mChunks.size() + count);
				for (auto i = 0u; i < count; i++) {
					void* ptr = calloc(mChunkSize, mSectorSize);
					if (!ptr) throw std::bad_alloc();
					mChunks.emplace_back(ptr);
				}
			}

			void clear() {
				std::unique_lock lock(mtx);
				for (auto chunk : mChunks) {
					std::free(chunk);
				}
				mChunks.clear();
				mChunks.shrink_to_fit();
			}

			void shrinkToFit(size_t sectorsSize) {
				size_t last = calcChunkIndex(sectorsSize + mChunkSize - 1);
				std::unique_lock lock(mtx);
				const auto size = mChunks.size();
				for (auto i = last; i < size; i++) {
					std::free(mChunks.at(i));
				}

				mChunks.erase(mChunks.begin() + static_cast<int64_t>(last), mChunks.end());
				mChunks.shrink_to_fit();
			}

			uint32_t capacity() const { std::shared_lock lock(mtx); return mChunkSize * static_cast<uint32_t>(mChunks.size()); }
			size_t calcInChunkIndex(size_t sectorIdx) const { return mIsChunkSizePowerOfTwo ? (sectorIdx & (mChunkSize - 1)) * mSectorSize : (sectorIdx % mChunkSize) * mSectorSize; }
			size_t calcChunkIndex(size_t sectorIdx) const {	return mIsChunkSizePowerOfTwo ? sectorIdx >> std::countr_zero(mChunkSize) : sectorIdx / mChunkSize; }
			
			Sector* getSector(size_t sectorIndex) const {
				std::shared_lock containerLock(mtx);
				auto chunkIndex = calcChunkIndex(sectorIndex);
				auto inChunkIndex = calcInChunkIndex(sectorIndex);

				return reinterpret_cast<Sector*>(static_cast<char*>(getChunk(chunkIndex)) + inChunkIndex);
			}

			void* getChunk(size_t index) const { return mChunks[index]; }

			static constexpr bool isPowerOfTwo(uint64_t x) noexcept {
				return x != 0 && (x & (x - 1)) == 0;
			}

		public:
			std::vector<void*> mChunks;
			uint32_t mChunkSize = 0;
			bool mIsChunkSizePowerOfTwo = false;
			uint16_t mSectorSize = 0;

			mutable std::shared_mutex mtx;
		};

		//shifts chunk data right
		//[][][][from][][][]   -> [][][] [empty] [from][][][]
		void shiftDataRight(size_t from, size_t count = 1) {
			if (count == 0) {
				return;
			}
			for (size_t i = size(); i-- > from + count;) {
				auto sector = Sector::moveSector(at(i - count), at(i), mSectorLayout);
				setSectorIndex(sector->id, static_cast<SectorId>(i));
			}
		}

		//shifts chunk data left
		//[][][][from][][][]   -> [][][from][][][]
		void shiftDataLeft(size_t from, size_t count = 1) {
			if (count == 0) {
				return;
			}
			for (auto i = from; i < size() - count; i++) {
				auto sector = Sector::moveSector(at(i + count), at(i), mSectorLayout);
				setSectorIndex(sector->id, static_cast<SectorId>(i));
			}
		}

		//fast binary search to find sector insert position
		__forceinline size_t findInsertPosition(SectorId sectorId) const {
			size_t right = mSize;
			if (right == 0 || at(0)->id > sectorId) {
				return 0;
			}
			if (at(right - 1)->id < sectorId) {
				return right;
			}
			size_t left = 0;
			while (right - left > 1) {
				size_t mid = left + (right - left) / 2;
				auto mid_id = at(mid)->id;
				if (mid_id < sectorId) {
					left = mid;
				}
				else {
					right = mid;
				}
			}
			
			return at(left)->id == sectorId ? left : right;
		}

	private:
		std::vector<SectorId> mSectorsMap;
		ChunksManager mChunksManager; //split whole data to chunks to make it more memory fragmentation friendly ( but less memory friendly, whole chunk will be allocated)

		ContiguousMap<ECSType, uint8_t> mLayoutMap;
		ISectorLayoutMeta* mSectorLayout = nullptr;

		uint32_t mSize = 0;

		bool mIsSectorTrivial = std::is_trivial_v<Sector>;

		mutable std::shared_mutex mMutex;
	};

	namespace SectorArrayUtils
	{
		inline SectorId findRightNearestSectorIndex(const SectorsArray& array, SectorId sectorId) {
			while (sectorId < array.sectorsCapacity()) {
				SectorId index = array.getSectorIndex(sectorId++);
				if (index < array.size()) {
					return index;
				}
			}

			return array.size();
		}

		inline void destroyMember(const SectorsArray& array, ECSType componentTypeId, SectorId sectorId) {
			auto lock = array.writeLock();
			const auto sector = array.findSector(sectorId);
			if (!sector) {
				return;
			}

			sector->destroyMember(array.getLayoutData(componentTypeId));
		}

		template<typename T>
		inline void destroyMember(const SectorsArray& array, SectorId sectorId) {
			auto lock = array.writeLock();
			const auto sector = array.findSector(sectorId);
			if (!sector) {
				return;
			}

			sector->destroyMember(array.getLayoutData<T>());
		}

		template<typename T>
		inline void destroyMembers(const SectorsArray& array, std::vector<SectorId>& sectorIds, bool sort = true) {
			if (sectorIds.empty()) {
				return;
			}

			if (sort) {
				std::ranges::sort(sectorIds);
			}

			const auto& layout = array.getLayoutData<T>();
			auto lock = array.writeLock();

			const auto sectorsSize = array.sectorsCapacity();
			
			for (const auto sectorId : sectorIds) {
				if (sectorId >= sectorsSize) {
					break; //all valid entities destroyed
				}

				if (auto sector = array.findSector(sectorId)) {
					sector->destroyMember(layout);
				}

			}
		}

		inline void destroyMembers(const SectorsArray& array, ECSType componentTypeId, std::vector<SectorId>& sectorIds, bool sort = true) {
			if (sectorIds.empty()) {
				return;
			}

			if (sort) {
				std::ranges::sort(sectorIds);
			}

			const auto& layout = array.getLayoutData(componentTypeId);
			auto lock = array.writeLock();

			const auto sectorsSize = array.sectorsCapacity();

			for (const auto sectorId : sectorIds) {
				if (sectorId >= sectorsSize) {
					break; //all valid entities destroyed
				}

				if (auto sector = array.findSector(sectorId)) {
					sector->destroyMember(layout);
				}

			}
		}

		inline void destroyAllMembers(const SectorsArray& array, std::vector<SectorId>& sectorIds, bool sort = true) {
			if (sectorIds.empty()) {
				return;
			}

			if (sort) {
				std::ranges::sort(sectorIds);
			}

			const auto layout = array.getLayout();
			auto lock = array.writeLock();

			const auto sectorsSize = array.sectorsCapacity();

			for (const auto sectorId : sectorIds) {
				if (sectorId >= sectorsSize) {
					break; //all valid entities destroyed
				}

				Sector::destroyMembers(array.findSector(sectorId), layout);
			}
		}

		inline void destroySectors(SectorsArray& array, std::vector<SectorId>& sectorIds, bool sort = true) {
			if (sectorIds.empty()) {
				return;
			}

			if (sort) {
				std::ranges::sort(sectorIds);
			}

			const auto layout = array.getLayout();
			auto lock = array.writeLock();
			const auto sectorsSize = array.sectorsCapacity();
			
			for (const auto sectorId : sectorIds) {
				if (sectorId >= sectorsSize) {
					break; //all valid entities destroyed
				}
				Sector::destroySector(array.findSector(sectorId), layout);
				array.setSectorIndex(sectorId, INVALID_ID);
			}
		}
	}
}
