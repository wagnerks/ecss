#pragma once

#include <shared_mutex>

namespace ecss::Threads {
    template<class L>
    class [[nodiscard]] MaybeLock {
    public:
        MaybeLock() noexcept = default;

        MaybeLock(std::shared_mutex& m, bool do_lock) noexcept : mLock(m, std::defer_lock) { if (do_lock) { mLock.lock(); } }

        MaybeLock(const MaybeLock&) = delete;
        MaybeLock& operator=(const MaybeLock&) = delete;
        MaybeLock(MaybeLock&&) noexcept = default;
        MaybeLock& operator=(MaybeLock&&) noexcept = default;

        explicit operator bool() const noexcept { return mLock.owns_lock(); }
        L& getLock() { return mLock; }
    private:
        L mLock{};
    };

    using SharedGuard = MaybeLock<std::shared_lock<std::shared_mutex>>;
    using UniqueGuard = MaybeLock<std::unique_lock<std::shared_mutex>>;

    [[nodiscard]] inline SharedGuard sharedLock(std::shared_mutex& mtx, bool sync = true) noexcept { return SharedGuard(mtx, sync); }

    [[nodiscard]] inline UniqueGuard uniqueLock(std::shared_mutex& mtx, bool sync = true) noexcept { return UniqueGuard(mtx, sync); }
}
