

# File PinCounters.h

[**File List**](files.md) **>** [**ecss**](dir_194708e763cf312315c6b23555bce86f.md) **>** [**threads**](dir_a9a674ced088cdcac6c51605566c5246.md) **>** [**PinCounters.h**](PinCounters_8h.md)

[Go to the documentation of this file](PinCounters_8h.md)


```C++
#pragma once

#include <shared_mutex>
#include <bit>
#include <atomic>

#include <cstddef>
#include <vector>
#include <mutex>
#include <type_traits>

namespace ecss::Threads {
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

        static constexpr size_t     wordIndexOf(size_t idx)  { return idx >> shift; }
        static constexpr size_t     bitOffsetOf(size_t idx)  { return idx & kBitsCount; }
        static constexpr BITS_TYPE  bitMask(size_t off)      { return static_cast<BITS_TYPE>(1) << off; }

        static constexpr size_t levelsFor(size_t idx)  {
            auto w0 = wordIndexOf(idx);
            return w0 ? 2 + (std::bit_width(w0) - 1) / shift : 1;
        }

        size_t ensurePath(size_t idx, std::array<size_t, maxlvl>& path)  {
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

    public:
        PinnedIndexesBitMask()  { bits[0].resize(1); }

        void set(SectorId index, bool state)  {
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
                    return; // other bits still set in this word -> ancestors stay
                }

                for (size_t lvl = 1; lvl < size; ++lvl) {
                    const auto childWord = path[lvl - 1];
                    const auto parentWord = path[lvl];
                    const auto bm = bitMask(bitOffsetOf(childWord));
                    auto old = std::atomic_ref{ bits[lvl][parentWord] }.fetch_and(~bm, std::memory_order_acq_rel);
                    if (old & ~bm) {
                        break; // sibling still present
                    }
                }
            }
        }

        bool test(SectorId index) const  {
            auto lock = std::shared_lock(mtx);

            const auto w = wordIndexOf(index);
            if (w >= bits[0].size()) {
                return false;
            }

            const auto m = bitMask(bitOffsetOf(index));
            auto v = std::atomic_ref{ bits[0][w] }.load(std::memory_order_acquire);

            return (v & m) != 0;
        }

        PinIndex highestSet() const  {
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
                if (w == 0) { return -1; } // defensive
                auto b = w != 0 ? kBitsCount - std::countl_zero(w) : w;
                wordIdx = static_cast<PinIndex>((wordIdx << shift) | b);
            }

            return wordIdx;
        }

    private:
        mutable std::shared_mutex mtx;
        mutable std::array<std::vector<BITS_TYPE>, maxlvl> bits;
    };

    struct PinCounters {
        void pin(SectorId id)  {
            assert(id != INVALID_ID);

            epoch.fetch_add(1, std::memory_order_release);
            auto prev = get(id).fetch_add(1, std::memory_order_release);
            if (prev == 0) {
                pinsBitMask.set(id, true);
                totalPinnedSectors.fetch_add(1, std::memory_order_acq_rel);
            }

            auto want = static_cast<PinIndex>(id);
            auto cur = maxPinnedSector.load(std::memory_order_relaxed);
            while (want > cur && !maxPinnedSector.compare_exchange_weak(cur, want, std::memory_order_release, std::memory_order_relaxed)) {}
        }

        void unpin(SectorId id)  {
            assert(id != INVALID_ID);

            epoch.fetch_add(1, std::memory_order_release);

            auto& var = get(id);
            auto prev = var.fetch_sub(1, std::memory_order_release);

            if (prev == 1) {
                pinsBitMask.set(id, false);
                totalPinnedSectors.fetch_sub(1, std::memory_order_acq_rel);

                updateMaxPinned();
                var.notify_all();
            }
        }

        bool canMoveSector(SectorId sectorId) const  {
            assert(sectorId != INVALID_ID);

            const auto max = maxPinnedSector.load(std::memory_order_acquire);
            return static_cast<PinIndex>(sectorId) > max && get(sectorId).load(std::memory_order_acquire) == 0;
        }

        void waitUntilChangeable(SectorId sid = 0) const {
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

        FORCE_INLINE bool isPinned(SectorId id) const {
            return get(id).load(std::memory_order_acquire) != 0;
        }

        FORCE_INLINE bool isArrayLocked() const {
            return hasAnyPins();
        }

        FORCE_INLINE bool hasAnyPins() const noexcept {
            return totalPinnedSectors.load(std::memory_order_acquire) != 0;
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

        void updateMaxPinned()  {
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

        mutable std::shared_mutex mtx;                       
        mutable std::vector<std::unique_ptr<std::atomic<uint16_t>[]>> blocks; 

        std::atomic<PinIndex>  maxPinnedSector{ -1 };         
        std::atomic<uint64_t>  epoch{ 0 };                    
        std::atomic<uint32_t>  totalPinnedSectors{ 0 };       
    };

}
```


