

# File Access.h

[**File List**](files.md) **>** [**ecss**](dir_194708e763cf312315c6b23555bce86f.md) **>** [**Access.h**](Access_8h.md)

[Go to the documentation of this file](Access_8h.md)


```C++
#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <shared_mutex>
#include <stdexcept>
#include <utility>
#include <vector>

#include <ecss/Fwd.h>

namespace ecss {
    template <typename T> struct Read { using Component = T; static constexpr bool kWrites = false; };

    template <typename T> struct Write { using Component = T; static constexpr bool kWrites = true; };

    namespace detail {
        class AccessGuard final {
        public:
            struct Entry {
                std::shared_mutex* mutex = nullptr;
                ECSType type = 0;
                bool writes = false;
                bool acquired = false;   
            };

            AccessGuard() = default;

            template <size_t N>
            explicit AccessGuard(std::array<Entry, N> claims) {
                std::sort(claims.begin(), claims.end(),
                    [](const Entry& a, const Entry& b) { return a.type < b.type; });

                static_assert(N <= 8, "name at most 8 component types in one access()");

                // Checked before a single lock is taken, and that ordering is the point: throwing
                // from the middle of the acquisition loop would abandon the guard half-built, with
                // no destructor to run and the locks it had already taken held for good.
                //
                // Asking to write what this thread already holds for reading has no answer: there is
                // no upgrade, and taking the lock again deadlocks against a waiting writer. It used
                // to assert and carry on, so a release build handed out a Write claim backed by
                // nothing but a shared lock -- other readers still inside it, mutation under way,
                // no diagnostic. This fails the same way in every configuration.
                for (const auto& claim : claims) {
                    const auto& depth = depthFor(claim.mutex);
                    if (claim.writes && depth.reads != 0 && depth.writes == 0) {
                        throw std::logic_error(
                            "ecss: access<Write<T>> while this thread already holds Read<T>; "
                            "there is no lock upgrade, ask for Write from the start");
                    }
                }

                for (auto& claim : claims) {
                    auto& depth = depthFor(claim.mutex);
                    if (depth.reads != 0 || depth.writes != 0) {
                        claim.acquired = false;
                    }
                    else {
                        if (claim.writes) { claim.mutex->lock(); }
                        else              { claim.mutex->lock_shared(); }
                        claim.acquired = true;
                    }
                    if (claim.writes) { ++depth.writes; } else { ++depth.reads; }
                    mEntries[mCount++] = claim;
                }
            }

            // Neither copyable nor movable, and the move is deleted on purpose rather than for
            // tidiness. A guard holds std::shared_mutex locks and keeps its reentrancy bookkeeping
            // in thread_local storage, so releasing it from a thread other than the one that took
            // it would unlock a mutex that thread never locked -- undefined behaviour -- and
            // decrement a depth list belonging to somebody else. Moving is how it would get
            // there, so it cannot.
            //
            // Nothing is lost: Registry::access() returns a prvalue, which C++17 constructs in
            // place, so `auto g = reg.access<...>()` needs no move at all.
            //
            // Note this is the opposite of PinnedSector and StructuralHold, which are movable and
            // may be released by a different thread than took them -- their bookkeeping travels
            // inside the object rather than living in thread_local storage.
            AccessGuard(AccessGuard&&) = delete;
            AccessGuard& operator=(AccessGuard&&) = delete;
            AccessGuard(const AccessGuard&) = delete;
            AccessGuard& operator=(const AccessGuard&) = delete;

            ~AccessGuard() { release(); }

        private:
            struct Depth { const void* mutex = nullptr; int reads = 0; int writes = 0; };

            static std::vector<Depth>& heldByThisThread() {
                static thread_local std::vector<Depth> held;
                return held;
            }

            static Depth& depthFor(const void* mutex) {
                auto& held = heldByThisThread();
                for (auto& d : held) { if (d.mutex == mutex) { return d; } }
                held.push_back(Depth{ mutex, 0, 0 });
                return held.back();
            }

            void release() {
                // Reverse order, so a nested guard never releases what an outer one still needs.
                for (size_t i = mCount; i > 0; --i) {
                    auto& entry = mEntries[i - 1];
                    auto& depth = depthFor(entry.mutex);
                    if (entry.writes) { --depth.writes; } else { --depth.reads; }
                    if (!entry.acquired) { continue; }
                    if (entry.writes) { entry.mutex->unlock(); }
                    else              { entry.mutex->unlock_shared(); }
                }
                mCount = 0;
            }

            static constexpr size_t kMaxClaims = 8;
            std::array<Entry, kMaxClaims> mEntries{};
            size_t mCount = 0;
        };
    } // namespace detail
} // namespace ecss
```


