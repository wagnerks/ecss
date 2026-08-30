#pragma once

#include <atomic>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <type_traits>
#include <vector>

#include <ecss/Types.h>

namespace ecss {
	namespace detail {
		/// @brief Bit arithmetic shared by both IdSet flavours.
		struct IdSetLayout {
			using Word = uint64_t;
			static constexpr size_t kWordBits = std::numeric_limits<Word>::digits;
			static constexpr Word   kFull = ~Word{ 0 };

			static FORCE_INLINE size_t wordOf(size_t id) { return id / kWordBits; }
			static FORCE_INLINE Word   bitOf(size_t id) { return Word{ 1 } << (id % kWordBits); }
		};
	}

	/**
	 * @brief Dense set of allocated ids, one bit per id.
	 *
	 * Replaces a sorted interval list for the "which ids are live" question. An interval
	 * list is compact only while the set is contiguous: every erase in the middle of a run
	 * splits it, which is a vector insert over the interval array, and the interval count
	 * grows towards N/2 as entities die in arbitrary order. That made random-order
	 * destruction quadratic -- 171 ms for 200k ids, against 1.5 ms here.
	 *
	 * A bitmap does not care about fragmentation: erase clears a bit and nothing moves.
	 *
	 * Memory bound: take() always hands out the lowest free id and ids only ever enter
	 * through take(), so the high watermark is bounded by the *peak* number of
	 * simultaneously live ids -- it does not grow with churn. One million live ids costs
	 * 125 KB flat.
	 *
	 * @note Intervals remain the right shape for range *filters* (see Ranges and
	 *       Registry::view(ranges)); this type is only for the live-id set.
	 */
	template<typename Type = EntityId, bool ThreadSafe = true>
	struct IdSet;

	// ===================================================================== single-threaded
	template<typename Type>
	struct IdSet<Type, false> : private detail::IdSetLayout {
		static_assert(std::is_unsigned_v<Type>, "IdSet indexes bits by id, so ids must be unsigned");

		/// @brief Allocate and return the lowest free id.
		Type take() {
			for (size_t word = mHint; ; ++word) {
				if (word >= mWords.size()) {
					mWords.push_back(Word{ 0 });
				}
				if (mWords[word] != kFull) {
					const auto bit = static_cast<size_t>(std::countr_zero(~mWords[word]));
					mWords[word] |= (Word{ 1 } << bit);
					// Step past this word once full so the next take() does not rescan it.
					mHint = mWords[word] == kFull ? word + 1 : word;

					const size_t id = word * kWordBits + bit;
					assert(id < static_cast<size_t>(INVALID_ID) && "id space exhausted");
					return static_cast<Type>(id);
				}
			}
		}

		/// @brief Mark @p id allocated. No-op if it already is.
		void insert(Type id) {
			const size_t word = wordOf(id);
			if (word >= mWords.size()) { mWords.resize(word + 1, Word{ 0 }); }
			mWords[word] |= bitOf(id);
		}

		/// @brief Release @p id. No-op if it is not allocated.
		void erase(Type id) {
			const size_t word = wordOf(id);
			if (word >= mWords.size()) { return; }
			mWords[word] &= ~bitOf(id);
			if (word < mHint) { mHint = word; }
		}

		FORCE_INLINE bool contains(Type id) const {
			const size_t word = wordOf(id);
			return word < mWords.size() && (mWords[word] & bitOf(id)) != 0;
		}

		/// @brief Every allocated id, ascending.
		std::vector<Type> getAll() const {
			std::vector<Type> out;
			out.reserve(size());
			for (size_t word = 0; word < mWords.size(); ++word) {
				Word bits = mWords[word];
				while (bits) {
					const auto bit = static_cast<size_t>(std::countr_zero(bits));
					bits &= bits - 1; // clear the lowest set bit
					out.push_back(static_cast<Type>(word * kWordBits + bit));
				}
			}
			return out;
		}

		void clear() {
			mWords.clear(); // keeps capacity; the next take() starts from 0 again
			mHint = 0;
		}

		void reserve(Type maxId) {
			const size_t word = wordOf(maxId);
			if (word >= mWords.size()) { mWords.resize(word + 1, Word{ 0 }); }
		}

		/// @return Number of allocated ids. Counted, not cached: keeping a counter would
		/// mean one more contended write per take/erase in the thread-safe flavour, and
		/// this is never on a hot path.
		size_t size() const {
			size_t n = 0;
			for (auto w : mWords) { n += static_cast<size_t>(std::popcount(w)); }
			return n;
		}
		bool empty() const {
			for (auto w : mWords) { if (w) { return false; } }
			return true;
		}
		size_t byteSize() const { return mWords.capacity() * sizeof(Word); }

	private:
		std::vector<Word> mWords;
		size_t mHint = 0; ///< First word that may still hold a free bit.
	};

	// ======================================================================== thread-safe
	/**
	 * @brief Lock-free live-id set.
	 *
	 * take() claims a bit with a CAS, erase() clears one with fetch_and, contains() is a
	 * single load. No mutex on any of them: the previous version serialised every entity
	 * creation on one registry-global mutex (measured ~560x per-op latency at 32 threads),
	 * and the structure underneath was already O(1).
	 *
	 * Growth never moves a block. The published table holds *pointers* to blocks that are
	 * allocated once and never relocated, so adding a block cannot lose a bit that another
	 * thread is CASing at that moment -- which copying a flat array would.
	 *
	 * @warning clear() is not safe against concurrent take/erase; the owner serialises it
	 *          (Registry does so with mEntitiesMutex). Everything else is.
	 */
	template<typename Type>
	struct IdSet<Type, true> : private detail::IdSetLayout {
		static_assert(std::is_unsigned_v<Type>, "IdSet indexes bits by id, so ids must be unsigned");
		static_assert(types::isLockFreeAtomic<uint64_t>, "id bitmap words must be lock-free");

		IdSet() = default;
		IdSet(const IdSet&) = delete;
		IdSet& operator=(const IdSet&) = delete;

		~IdSet() {
			for (auto* table : mTables) { delete[] table->blocks; delete table; }
			for (auto* block : mBlocks) { delete[] block; }
		}

		Type take() {
			for (;;) {
				const Table* table = mTable.load(std::memory_order_acquire);
				const size_t words = table ? table->count * kWordsPerBlock : 0;
				const size_t hint = mHint.load(std::memory_order_relaxed);

				// Striping is self-tuning: while nothing has ever lost a CAS on this set, every
				// thread scans from the hint and gets strict lowest-free allocation -- which is
				// what a single-threaded caller wants, and it keeps its id sequence deterministic.
				// The first lost CAS flips mContended, after which threads start their scan at a
				// per-thread offset so they stop fighting over one cache line. Ids then stay within
				// kStripes*kWordBits of the true minimum, so the watermark stays bounded by peak use.
				const bool contended = mContended.load(std::memory_order_relaxed);
				const size_t stripe = contended ? threadStripe() % kStripes : 0;
				const size_t start = hint + stripe < words ? hint + stripe : hint;

				bool lostCas = false;
				if (const auto id = claimIn(table, start, words, lostCas); id != kNoId) { return id; }
				if (start != hint) {
					if (const auto id = claimIn(table, hint, start, lostCas); id != kNoId) { return id; }
				}

				if (lostCas) {
					// Someone beat us to a bit: the set is not full, it is busy. Mark it so the
					// next pass spreads out, and retry rather than growing.
					// Guarded: an unconditional store here would dirty a shared cache line on every
					// lost CAS -- the exact traffic the striping exists to remove. The load is on a
					// read-mostly line, and the store happens once in the lifetime of the set.
					if (!mContended.load(std::memory_order_relaxed)) {
						mContended.store(true, std::memory_order_relaxed);
					}
					continue;
				}
				grow(words); // every word full: add a block and rescan
			}
		}

		void insert(Type id) {
			ensure(id);
			const Table* table = mTable.load(std::memory_order_acquire);
			wordAt(table, wordOf(id)).fetch_or(bitOf(id), std::memory_order_acq_rel);
		}

		void erase(Type id) {
			const Table* table = mTable.load(std::memory_order_acquire);
			const size_t word = wordOf(id);
			if (!table || word >= table->count * kWordsPerBlock) { return; }

			if (wordAt(table, word).fetch_and(~bitOf(id), std::memory_order_acq_rel) & bitOf(id)) {
				lowerHint(word); // this word has room again
			}
		}

		FORCE_INLINE bool contains(Type id) const {
			const Table* table = mTable.load(std::memory_order_acquire);
			const size_t word = wordOf(id);
			if (!table || word >= table->count * kWordsPerBlock) { return false; }
			return (wordAt(table, word).load(std::memory_order_acquire) & bitOf(id)) != 0;
		}

		std::vector<Type> getAll() const {
			std::vector<Type> out;
			const Table* table = mTable.load(std::memory_order_acquire);
			if (!table) { return out; }

			const size_t words = table->count * kWordsPerBlock;
			out.reserve(size());
			for (size_t word = 0; word < words; ++word) {
				Word bits = wordAt(table, word).load(std::memory_order_acquire);
				while (bits) {
					const auto bit = static_cast<size_t>(std::countr_zero(bits));
					bits &= bits - 1;
					out.push_back(static_cast<Type>(word * kWordBits + bit));
				}
			}
			return out;
		}

		/// @warning Caller must exclude concurrent take/erase.
		void clear() {
			const Table* table = mTable.load(std::memory_order_acquire);
			if (table) {
				const size_t words = table->count * kWordsPerBlock;
				for (size_t word = 0; word < words; ++word) {
					wordAt(table, word).store(Word{ 0 }, std::memory_order_relaxed);
				}
			}
			mHint.store(0, std::memory_order_release);
		}

		void reserve(Type maxId) { ensure(maxId); }

		size_t size() const {
			const Table* table = mTable.load(std::memory_order_acquire);
			if (!table) { return 0; }
			size_t n = 0;
			const size_t words = table->count * kWordsPerBlock;
			for (size_t word = 0; word < words; ++word) {
				n += static_cast<size_t>(std::popcount(wordAt(table, word).load(std::memory_order_relaxed)));
			}
			return n;
		}
		bool empty() const { return size() == 0; }

		size_t byteSize() const {
			const Table* table = mTable.load(std::memory_order_acquire);
			return table ? table->count * kWordsPerBlock * sizeof(Word) : 0;
		}

	private:
		using Cell = std::atomic<Word>;
		static constexpr size_t kWordsPerBlock = 1024;                     // 8 KB, 65536 ids
		static constexpr size_t kIdsPerBlock = kWordsPerBlock * kWordBits;
		static constexpr size_t kStripes = 16;                             // 1024 ids of spread
		static constexpr Type   kNoId = std::numeric_limits<Type>::max();

		/// @brief Stable per-thread index, used only to spread the take() scan start.
		static size_t threadStripe() noexcept {
			static std::atomic<size_t> counter{ 0 };
			static thread_local const size_t stripe = counter.fetch_add(1, std::memory_order_relaxed);
			return stripe;
		}


		/// @brief Immutable published index over blocks. Blocks themselves never move.
		struct Table {
			size_t count;
			Cell** blocks;
		};

		static FORCE_INLINE Cell& wordAt(const Table* table, size_t word) {
			return table->blocks[word / kWordsPerBlock][word % kWordsPerBlock];
		}
		/// @brief Claim the first free bit in [from, to), or kNoId.
		/// @param lostCas set when a CAS was beaten by another thread (busy, not full).
		Type claimIn(const Table* table, size_t from, size_t to, bool& lostCas) {
			for (size_t word = from; word < to; ++word) {
				auto& cell = wordAt(table, word);
				Word value = cell.load(std::memory_order_acquire);
				while (value != kFull) {
					const auto bit = static_cast<size_t>(std::countr_zero(~value));
					const Word want = value | (Word{ 1 } << bit);
					// A failed CAS refreshes `value`, so we retry with the next free bit of
					// this word rather than restarting the scan.
					if (cell.compare_exchange_weak(value, want, std::memory_order_acq_rel, std::memory_order_acquire)) {
						if (want == kFull) { raiseHint(word + 1); }
						const size_t id = word * kWordBits + bit;
						assert(id < static_cast<size_t>(INVALID_ID) && "id space exhausted");
						return static_cast<Type>(id);
					}
					// Latch here, not at the call site: claimIn usually goes on to succeed in a
					// later word and returns, so a flag only inspected afterwards would never be
					// stored and the striping would never switch on.
					lostCas = true;
					mContended.store(true, std::memory_order_relaxed);
				}
			}
			return kNoId;
		}

		/// @brief Push the hint forward past a word that just became full.
		void raiseHint(size_t word) {
			auto cur = mHint.load(std::memory_order_relaxed);
			while (cur < word && !mHint.compare_exchange_weak(cur, word, std::memory_order_relaxed)) {}
		}

		/// @brief Pull the hint back to a word that just gained a free bit.
		void lowerHint(size_t word) {
			auto cur = mHint.load(std::memory_order_relaxed);
			while (word < cur && !mHint.compare_exchange_weak(cur, word, std::memory_order_relaxed)) {}
		}

		void ensure(Type id) {
			const Table* table = mTable.load(std::memory_order_acquire);
			const size_t word = wordOf(id);
			if (table && word < table->count * kWordsPerBlock) { return; }
			grow(word + 1);
		}

		/// @brief Publish a table covering at least @p minWords words.
		void grow(size_t minWords) {
			auto guard = std::lock_guard(mGrowMtx);

			const Table* cur = mTable.load(std::memory_order_relaxed);
			const size_t oldCount = cur ? cur->count : 0;
			if (oldCount * kWordsPerBlock > minWords) { return; } // someone else grew it

			const size_t wanted = (minWords + kWordsPerBlock) / kWordsPerBlock;
			const size_t newCount = std::max(wanted, oldCount ? oldCount * 2 : size_t{ 1 });

			auto** blocks = new Cell*[newCount];
			for (size_t i = 0; i < oldCount; ++i) { blocks[i] = cur->blocks[i]; }
			for (size_t i = oldCount; i < newCount; ++i) {
				auto* block = new Cell[kWordsPerBlock];
				for (size_t j = 0; j < kWordsPerBlock; ++j) { block[j].store(Word{ 0 }, std::memory_order_relaxed); }
				blocks[i] = block;
				mBlocks.push_back(block);
			}

			auto* table = new Table{ newCount, blocks };
			// Superseded tables stay alive until destruction: a lock-free reader may still
			// hold one, and there are only O(log maxId) of them.
			mTables.push_back(table);
			mTable.store(table, std::memory_order_release);
		}

		std::atomic<Table*>       mTable{ nullptr };
		// Own line: written once, but sharing with mTable would invalidate the pointer
		// every take() reads.
		alignas(64) std::atomic<bool> mContended{ false }; ///< Latches on the first lost CAS.
		alignas(64) std::atomic<size_t> mHint{ 0 };  ///< First word that may hold a free bit.

		std::mutex                mGrowMtx;          ///< Serialises growth only.
		std::vector<Table*>       mTables;           ///< Every table ever published (freed in dtor).
		std::vector<Cell*>        mBlocks;           ///< Every block ever allocated (freed in dtor).
	};
}
