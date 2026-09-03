

# File IdSet.h

[**File List**](files.md) **>** [**ecss**](dir_194708e763cf312315c6b23555bce86f.md) **>** [**IdSet.h**](IdSet_8h.md)

[Go to the documentation of this file](IdSet_8h.md)


```C++
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
        struct IdSetLayout {
            using Word = uint64_t;
            static constexpr size_t kWordBits = std::numeric_limits<Word>::digits;
            static constexpr Word   kFull = ~Word{ 0 };

            static FORCE_INLINE size_t wordOf(size_t id) { return id / kWordBits; }
            static FORCE_INLINE Word   bitOf(size_t id) { return Word{ 1 } << (id % kWordBits); }
        };
    }

    template<typename Type = EntityId, bool ThreadSafe = true>
    struct IdSet;

    // ===================================================================== single-threaded
    template<typename Type>
    struct IdSet<Type, false> : private detail::IdSetLayout {
        static_assert(std::is_unsigned_v<Type>, "IdSet indexes bits by id, so ids must be unsigned");

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

        void take(size_t count, std::vector<Type>& out) {
            if (!count) { return; }
            out.reserve(out.size() + count);

            for (size_t word = mHint; count; ++word) {
                if (word >= mWords.size()) { mWords.push_back(Word{ 0 }); }

                Word& cell = mWords[word];

                // A whole empty word in one step, when the batch is big enough to want it.
                if (cell == 0 && count >= kWordBits) {
                    const size_t base = word * kWordBits;
                    assert(base + kWordBits - 1 < static_cast<size_t>(INVALID_ID) && "id space exhausted");
                    cell = kFull;
                    for (size_t b = 0; b < kWordBits; ++b) { out.push_back(static_cast<Type>(base + b)); }
                    count -= kWordBits;
                    mHint = word + 1;
                    continue;
                }

                Word free = ~cell;
                while (free && count) {
                    const auto bit = static_cast<size_t>(std::countr_zero(free));
                    free &= free - 1;
                    cell |= (Word{ 1 } << bit);

                    const size_t id = word * kWordBits + bit;
                    assert(id < static_cast<size_t>(INVALID_ID) && "id space exhausted");
                    out.push_back(static_cast<Type>(id));
                    --count;
                }
                mHint = (cell == kFull) ? word + 1 : word;
            }
        }

        void insert(Type id) {
            const size_t word = wordOf(id);
            if (word >= mWords.size()) { mWords.resize(word + 1, Word{ 0 }); }
            mWords[word] |= bitOf(id);
        }

        void erase(Type id) {
            const size_t word = wordOf(id);
            if (word >= mWords.size()) { return; }
            mWords[word] &= ~bitOf(id);
            if (word < mHint) { mHint = word; }
        }

        void erase(const Type* begin, const Type* end) {
            while (begin != end) {
                const size_t word = wordOf(*begin);
                Word mask = 0;
                do {
                    mask |= bitOf(*begin);
                    ++begin;
                } while (begin != end && wordOf(*begin) == word);

                if (word >= mWords.size()) { continue; }
                mWords[word] &= ~mask;
                if (word < mHint) { mHint = word; }
            }
        }

        FORCE_INLINE bool contains(Type id) const {
            const size_t word = wordOf(id);
            return word < mWords.size() && (mWords[word] & bitOf(id)) != 0;
        }

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
        size_t mHint = 0; 
    };

    // ======================================================================== thread-safe
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

        void take(size_t count, std::vector<Type>& out) {
            if (!count) { return; }
            out.reserve(out.size() + count);

            while (count) {
                const Table* table = mTable.load(std::memory_order_acquire);
                const size_t words = table ? table->count * kWordsPerBlock : 0;
                const size_t remaining = count;

                for (size_t word = mHint.load(std::memory_order_relaxed); word < words && count; ++word) {
                    claimWord(table, word, count, out);
                }

                // Nothing anywhere: the set is full rather than busy, so add a block.
                if (count == remaining) { grow(words); }
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

        void erase(const Type* begin, const Type* end) {
            const Table* table = mTable.load(std::memory_order_acquire);
            if (!table) { return; }
            const size_t words = table->count * kWordsPerBlock;

            while (begin != end) {
                const size_t word = wordOf(*begin);
                Word mask = 0;
                do {
                    mask |= bitOf(*begin);
                    ++begin;
                } while (begin != end && wordOf(*begin) == word);

                if (word >= words) { continue; }
                if (wordAt(table, word).fetch_and(~mask, std::memory_order_acq_rel) & mask) {
                    lowerHint(word);
                }
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

        static size_t threadStripe() noexcept {
            static std::atomic<size_t> counter{ 0 };
            static thread_local const size_t stripe = counter.fetch_add(1, std::memory_order_relaxed);
            return stripe;
        }


        struct Table {
            size_t count;
            Cell** blocks;
        };

        static FORCE_INLINE Cell& wordAt(const Table* table, size_t word) {
            return table->blocks[word / kWordsPerBlock][word % kWordsPerBlock];
        }
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

        void claimWord(const Table* table, size_t word, size_t& count, std::vector<Type>& out) {
            auto& cell = wordAt(table, word);
            Word value = cell.load(std::memory_order_acquire);

            // The whole point of the batch: 64 ids for one atomic, when there is room for them.
            if (value == 0 && count >= kWordBits) {
                if (cell.compare_exchange_strong(value, kFull, std::memory_order_acq_rel, std::memory_order_acquire)) {
                    const size_t base = word * kWordBits;
                    assert(base + kWordBits - 1 < static_cast<size_t>(INVALID_ID) && "id space exhausted");
                    for (size_t b = 0; b < kWordBits; ++b) { out.push_back(static_cast<Type>(base + b)); }
                    count -= kWordBits;
                    raiseHint(word + 1);
                    return;
                }
                // Lost it: another thread took the word. `value` now holds what they left.
            }

            while (value != kFull && count) {
                const auto bit = static_cast<size_t>(std::countr_zero(~value));
                const Word want = value | (Word{ 1 } << bit);
                // A failed exchange refreshes `value`, so the retry picks the next free bit.
                if (cell.compare_exchange_weak(value, want, std::memory_order_acq_rel, std::memory_order_acquire)) {
                    const size_t id = word * kWordBits + bit;
                    assert(id < static_cast<size_t>(INVALID_ID) && "id space exhausted");
                    out.push_back(static_cast<Type>(id));
                    --count;
                    value = want;
                    if (want == kFull) { raiseHint(word + 1); }
                }
            }
        }

        void raiseHint(size_t word) {
            auto cur = mHint.load(std::memory_order_relaxed);
            while (cur < word && !mHint.compare_exchange_weak(cur, word, std::memory_order_relaxed)) {}
        }

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
        alignas(64) std::atomic<bool> mContended{ false }; 
        alignas(64) std::atomic<size_t> mHint{ 0 };  

        std::mutex                mGrowMtx;          
        std::vector<Table*>       mTables;           
        std::vector<Cell*>        mBlocks;           
    };
}
```


