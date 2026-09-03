

# File AccessTracker.h

[**File List**](files.md) **>** [**ecss**](dir_194708e763cf312315c6b23555bce86f.md) **>** [**AccessTracker.h**](AccessTracker_8h.md)

[Go to the documentation of this file](AccessTracker_8h.md)


```C++
#pragma once

#include <ecss/Fwd.h>

#ifndef NDEBUG
#include <cassert>
#include <cstdio>
#include <map>
#include <atomic>
#include <mutex>
#include <thread>
#include <typeinfo>
#include <utility>
#endif

namespace ecss::detail {
    class AccessTracker {
    public:
#ifndef NDEBUG
        static bool enabled() noexcept { return flag().load(std::memory_order_relaxed); }

        static void setEnabled(bool on) noexcept { flag().store(on, std::memory_order_relaxed); }

        static void beginRead(ECSType type, const char* name) {
            if (!enabled()) { return; }
            auto& st = stateFor(type);
            auto lock = std::lock_guard(st.mutex);
            if (st.writerDepth != 0 && st.writer != std::this_thread::get_id()) {
                report(name, "read", "written", st.writer);
            }
            ++st.readers[std::this_thread::get_id()];
        }

        static void endRead(ECSType type) {
            if (!enabled()) { return; }
            auto& st = stateFor(type);
            auto lock = std::lock_guard(st.mutex);
            const auto it = st.readers.find(std::this_thread::get_id());
            if (it != st.readers.end() && --it->second == 0) { st.readers.erase(it); }
        }

        static void beginWrite(ECSType type, const char* name) {
            if (!enabled()) { return; }
            auto& st = stateFor(type);
            auto lock = std::lock_guard(st.mutex);
            const auto self = std::this_thread::get_id();
            if (st.writerDepth != 0 && st.writer != self) {
                report(name, "written", "written", st.writer);
            }
            for (const auto& [thread, depth] : st.readers) {
                if (thread != self && depth != 0) { report(name, "written", "read", thread); break; }
            }
            st.writer = self;
            ++st.writerDepth;
        }

        static void endWrite(ECSType type) {
            if (!enabled()) { return; }
            auto& st = stateFor(type);
            auto lock = std::lock_guard(st.mutex);
            if (st.writerDepth != 0 && --st.writerDepth == 0) { st.writer = std::thread::id{}; }
        }
#else
        static void beginRead(ECSType, const char*) noexcept {}
        static void endRead(ECSType) noexcept {}
        static void beginWrite(ECSType, const char*) noexcept {}
        static void endWrite(ECSType) noexcept {}
#endif

    private:
#ifndef NDEBUG
        static std::atomic<bool>& flag() noexcept {
            static std::atomic<bool> on{ false };
            return on;
        }

        struct State {
            std::mutex mutex;
            std::map<std::thread::id, int> readers;   
            std::thread::id writer{};
            int writerDepth = 0;
        };

        static State& stateFor(ECSType type) {
            static std::mutex tableMutex;
            static std::map<ECSType, State> table;
            auto lock = std::lock_guard(tableMutex);
            return table[type];
        }

        [[noreturn]] static void report(const char* name, const char* mine, const char* theirs,
                                        std::thread::id other) {
            std::fprintf(stderr,
                "ecss: component '%s' is being %s by this thread while thread %llu has it %s.\n"
                "      The container keeps the array's shape safe, not a component's value. Two\n"
                "      threads on one component type is also a scheduling bug: the frame's result\n"
                "      depends on which of them won.\n"
                "      Take reg.access<Read<T>, Write<U>>() around the work, or arrange the\n"
                "      systems not to overlap on this type.\n",
                name, mine,
                static_cast<unsigned long long>(std::hash<std::thread::id>{}(other)), theirs);
            std::fflush(stderr);
            assert(false && "ecss: concurrent access to one component type (see stderr)");
            std::abort();
        }
#endif
    };

    class AccessScope {
    public:
        AccessScope() = default;

        AccessScope(ECSType type, const char* name, bool forWriting)
            : mType(type), mWriting(forWriting), mHeld(true) {
            if (mWriting) { AccessTracker::beginWrite(mType, name); }
            else          { AccessTracker::beginRead(mType, name); }
        }

        AccessScope(AccessScope&& other) noexcept { *this = std::move(other); }
        AccessScope& operator=(AccessScope&& other) noexcept {
            if (this != &other) {
                release();
                mType = other.mType;
                mWriting = other.mWriting;
                mHeld = other.mHeld;
                other.mHeld = false;
            }
            return *this;
        }
        AccessScope(const AccessScope&) = delete;
        AccessScope& operator=(const AccessScope&) = delete;

        ~AccessScope() { release(); }

    private:
        void release() {
            if (!mHeld) { return; }
            mHeld = false;
            if (mWriting) { AccessTracker::endWrite(mType); }
            else          { AccessTracker::endRead(mType); }
        }

        ECSType mType = 0;
        bool mWriting = false;
        bool mHeld = false;
    };

    template <typename T>
    inline const char* accessTypeName() noexcept {
#ifndef NDEBUG
        return typeid(T).name();
#else
        return "";
#endif
    }

    template <typename T>
    [[nodiscard]] inline AccessScope readScope(ECSType type) { return { type, accessTypeName<T>(), false }; }

    template <typename T>
    [[nodiscard]] inline AccessScope writeScope(ECSType type) { return { type, accessTypeName<T>(), true }; }

} // namespace ecss::detail
```


