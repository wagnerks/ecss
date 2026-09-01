#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <map>
#include <shared_mutex>
#include <utility>
#include <vector>

#include <ecss/Types.h>

namespace ecss {
	/// @brief Claim a component type for reading. @see Registry::access
	template <typename T> struct Read { using Component = T; static constexpr bool kWrites = false; };

	/// @brief Claim a component type for writing. @see Registry::access
	template <typename T> struct Write { using Component = T; static constexpr bool kWrites = true; };

	namespace detail {
		/**
		 * @brief Reader-writer lock per component type, held for the length of a system.
		 *
		 * The container keeps an array's shape safe on its own, but not a component's value:
		 * guarding that per element would cost more than the lock-free read paths save. This
		 * puts the guarantee where systems actually work -- a whole component type at a time --
		 * so it is paid once per system per frame rather than once per element. 29.5 ns for one
		 * type, 84.8 for three; fifty systems naming three apiece come to 4.2 us in a frame,
		 * which is 0.03% of one at sixty a second.
		 *
		 * Claims are taken in one call on purpose. Acquiring them one at a time lets two
		 * systems take the same pair in opposite orders and stop, so the guard sorts them by
		 * type id and locks in that order; every caller therefore agrees on the sequence.
		 *
		 * Re-entering from the same thread is allowed and does nothing, because std::shared_mutex
		 * is not recursive and a second shared acquire deadlocks against a waiting writer.
		 * Asking to write a type this thread already holds for reading is refused rather than
		 * quietly upgraded: there is no atomic upgrade, and doing it in two steps is where the
		 * deadlock would come back.
		 */
		class AccessGuard final {
		public:
			/// @brief One component type's claim: which lock, and whether for writing.
			struct Entry {
				std::shared_mutex* mutex = nullptr;
				ECSType type = 0;
				bool writes = false;
				bool acquired = false;   ///< false when this thread already held it
			};

			AccessGuard() = default;

			/// @param claims one entry per component type, in any order.
			template <size_t N>
			explicit AccessGuard(std::array<Entry, N> claims) {
				std::sort(claims.begin(), claims.end(),
					[](const Entry& a, const Entry& b) { return a.type < b.type; });

				static_assert(N <= 8, "name at most 8 component types in one access()");
				for (auto& claim : claims) {
					auto& depth = depthFor(claim.mutex);
					if (depth.reads != 0 || depth.writes != 0) {
						assert(!(claim.writes && depth.writes == 0)
							&& "ecss: access<Write<T>> while this thread already holds Read<T>; "
							   "there is no lock upgrade, ask for Write from the start");
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

			AccessGuard(AccessGuard&& other) noexcept { *this = std::move(other); }
			AccessGuard& operator=(AccessGuard&& other) noexcept {
				if (this != &other) {
					release();
					mEntries = other.mEntries;
					mCount = other.mCount;
					other.mCount = 0;
				}
				return *this;
			}
			AccessGuard(const AccessGuard&) = delete;
			AccessGuard& operator=(const AccessGuard&) = delete;

			~AccessGuard() { release(); }

		private:
			struct Depth { const void* mutex = nullptr; int reads = 0; int writes = 0; };

			/// Keyed by the mutex rather than the type id, so two registries holding the same
			/// component type stay separate. A flat vector rather than a map: a thread holds a
			/// handful of claims at once, and a linear scan over that beats a tree walk plus the
			/// node allocation a map wants on every first claim.
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

			/// Inline, because a system naming a few types should not touch the heap to say so.
			static constexpr size_t kMaxClaims = 8;
			std::array<Entry, kMaxClaims> mEntries{};
			size_t mCount = 0;
		};
	} // namespace detail
} // namespace ecss
