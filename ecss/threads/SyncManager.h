#pragma once

namespace ecss {
	struct SyncManager {
		enum class SyncType { NONE, SHARED, UNIQUE };

		struct AnyLock {
			std::shared_mutex* m = nullptr;
			SyncType t = SyncType::NONE;
			bool owns = false;

			AnyLock() = default;
			explicit AnyLock(std::shared_mutex& mtx, SyncType type) noexcept : m(&mtx), t(type) {
				switch (t) {
				case SyncType::SHARED: m->lock_shared(); owns = true; break;
				case SyncType::UNIQUE: m->lock();        owns = true; break;
				default: break;
				}
			}
			AnyLock(AnyLock&& o) noexcept { *this = std::move(o); }
			AnyLock& operator=(AnyLock&& o) noexcept {
				if (this != &o) { unlock(); m = o.m; t = o.t; owns = o.owns; o.m = nullptr; o.owns = false; }
				return *this;
			}
			~AnyLock() { unlock(); }

			void unlock() noexcept {
				if (!owns || !m) return;
				if (t == SyncType::SHARED) m->unlock_shared(); else m->unlock();
				owns = false;
			}
		};

		static AnyLock Lock(std::shared_mutex& mtx, SyncType syncType) {
			return AnyLock(mtx, syncType);
		}
	};

	using SM = SyncManager;
	using SyncType = SyncManager::SyncType;
}
