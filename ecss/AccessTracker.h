#pragma once

#include <ecss/Types.h>

#ifndef NDEBUG
#include <cassert>
#include <cstdio>
#include <map>
#include <mutex>
#include <thread>
#include <typeinfo>
#include <utility>
#endif

namespace ecss::detail {
	/**
	 * @brief Debug-only detector for two threads touching one component type at once.
	 *
	 * The container guarantees *structure*: an array will not be relocated under an iterator,
	 * and a pinned sector will not move or die. It does not guarantee that a component's
	 * *value* is stable while another thread writes it -- doing so would mean locking or
	 * pinning per element, which costs more than everything the lock-free read paths save.
	 *
	 * That leaves a gap the compiler cannot see: a system reading Position while another
	 * writes it is a race, and worse, it is a scheduling bug -- the frame's result depends on
	 * which thread won, so making the memory safe would not make the answer right. This finds
	 * it and names it, at the point it happens, instead of leaving it to a sanitizer run or to
	 * a bug report about numbers that flicker.
	 *
	 * A view is a read scope for as long as it lives; a mutator is a write scope for the
	 * duration of its call. Re-entering from the same thread is fine and expected -- a system
	 * routinely reads what it just wrote.
	 *
	 * Compiled out entirely when NDEBUG is set: the whole point is to be free in the build
	 * that ships. @see Registry::setAccessTracking
	 */
	class AccessTracker {
	public:
#ifndef NDEBUG
		/// @brief Whether conflicts are watched for. Off until a Registry turns it on.
		static bool& enabled() noexcept { static bool on = false; return on; }

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
		struct State {
			std::mutex mutex;
			std::map<std::thread::id, int> readers;   ///< reentrant depth per thread
			std::thread::id writer{};
			int writerDepth = 0;
		};

		/// One record per component type, for the life of the process. Types are numbered from
		/// a single counter, so a plain map keyed by that number is enough and never rehashes
		/// under a reader.
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

	/**
	 * @brief RAII claim on one component type, for the tracker to see.
	 *
	 * Movable and default-constructible so a view can keep an array of them, one per component
	 * type it names, without knowing the count until instantiation. A moved-from or
	 * default-constructed scope releases nothing.
	 */
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

	/// @brief Name a component type for a tracker message. Debug only; empty otherwise.
	template <typename T>
	inline const char* accessTypeName() noexcept {
#ifndef NDEBUG
		return typeid(T).name();
#else
		return "";
#endif
	}

	/// @brief Claim @p type for reading until the returned scope dies. @see AccessTracker
	template <typename T>
	[[nodiscard]] inline AccessScope readScope(ECSType type) { return { type, accessTypeName<T>(), false }; }

	/// @brief Claim @p type for writing until the returned scope dies. @see AccessTracker
	template <typename T>
	[[nodiscard]] inline AccessScope writeScope(ECSType type) { return { type, accessTypeName<T>(), true }; }

} // namespace ecss::detail
