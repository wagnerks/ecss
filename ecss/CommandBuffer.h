#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include <ecss/Registry.h>

namespace ecss {
	/**
	 * @brief Records structural changes and applies them together at a chosen point.
	 *
	 * Adding or removing a component, or destroying an entity, changes the shape of an array
	 * rather than the value of a component. Those are the only operations the thread-safe
	 * build charges for -- component values are written straight through the iterator and cost
	 * the same either way -- and they are the only ones that are illegal while iterating the
	 * array they touch. Both problems come from the same place, and both go away if the change
	 * is recorded now and applied later:
	 *
	 *   - one lock and one pass per component type instead of per call, and one sorted merge
	 *     instead of a shift per insert;
	 *   - nothing is applied while a view is open, so the structural change cannot invalidate
	 *     an iterator, and a thread cannot end up waiting for a hold only it could release.
	 *
	 * It is not free, and not always a win. Adding a component to 200000 entities, ns per add:
	 *
	 *                        immediate    recorded + applied
	 *   ascending ids, plain       7.3                  18.8
	 *   ascending ids, TS         37.1                  21.2
	 *   shuffled ids, plain    62563.7                  73.9
	 *   shuffled ids, TS      101611.1                  89.7
	 *
	 * Against an append in the non-thread-safe build the buffer loses: that path is already a
	 * push and recording costs more than doing it. It wins wherever the immediate call would
	 * pay for either per-call synchronisation or a middle insert, which is every other row --
	 * and ids arriving in ascending order is the exception in gameplay code, not the rule.
	 *
	 * Deferral is explicit: the buffer is a separate object with its own verbs, so it is never
	 * a surprise that a recorded change is not visible until apply(). Registry's own
	 * addComponent/destroyEntity still take effect immediately.
	 *
	 * @note Ids come from the registry, not the buffer, so an entity created for a recorded
	 *       add is usable as an id right away -- see Registry::takeEntities.
	 *
	 * @thread_safety One buffer belongs to one thread. Give each recording thread its own and
	 *                apply them one after another; the buffer itself takes no locks.
	 */
	template<bool ThreadSafe = true, typename Allocator = Memory::ChunksAllocator<8192>>
	class CommandBuffer final {
		using Reg = Registry<ThreadSafe, Allocator>;

	public:
		/// @thread_safety Thread-confined. One buffer belongs to one thread; nothing in it
		///                takes a lock. Give each recording thread its own.
		CommandBuffer() = default;
		/// @brief Copying is forbidden; recorded operations have one owning buffer.
		CommandBuffer(const CommandBuffer&) = delete;
		/// @brief Copy assignment is forbidden; recorded operations have one owning buffer.
		CommandBuffer& operator=(const CommandBuffer&) = delete;
		/// @brief Transfer all recorded operations to another buffer.
		/// @post The destination has the source's former observable state.
		/// @note The moved-from buffer remains valid but its state is unspecified.
		CommandBuffer(CommandBuffer&&) noexcept = default;
		/// @brief Replace this buffer with the recorded operations owned by @p other.
		/// @post The destination has @p other's former observable state.
		/// @note The moved-from buffer remains valid but its state is unspecified.
		CommandBuffer& operator=(CommandBuffer&&) noexcept = default;

		/// @brief Record "give @p entity a T", to be applied by apply().
		/// Recording the same entity twice for one type keeps the later value, matching what a
		/// pair of immediate addComponent calls would have left behind.
		/// @thread_safety Thread-confined. One buffer belongs to one thread; nothing in it
		///                takes a lock. Give each recording thread its own.
		template<typename T, typename... Args>
		void addComponent(EntityId entity, Args&&... args) {
			auto& st = store<T>();
			st.adds.emplace_back(entity, T{ std::forward<Args>(args)... });
			st.addSeq.push_back(mSeq++);
		}

		/// @brief Record "take T away from @p entity".
		/// @thread_safety Thread-confined. One buffer belongs to one thread; nothing in it
		///                takes a lock. Give each recording thread its own.
		template<typename T>
		void destroyComponent(EntityId entity) {
			auto& st = store<T>();
			st.removals.push_back(entity);
			st.removeSeq.push_back(mSeq++);
		}

		/// @brief Record "destroy @p entity", across every component type it has.
		/// @thread_safety Thread-confined. One buffer belongs to one thread; nothing in it
		///                takes a lock. Give each recording thread its own.
		void destroyEntity(EntityId entity) { mDestroyed.push_back(entity); }

		/// @brief Apply everything recorded, then empty the buffer.
		///
		/// For any one entity and component type the last thing recorded wins, so removing a
		/// component and adding it back in the same frame leaves it present, and the reverse
		/// leaves it absent -- the same answer the immediate calls would have given in that
		/// order. Batching is kept: the surviving adds and removals still go out as one call
		/// per type.
		///
		/// Entity destruction is applied after all of it and is terminal, so an entity both
		/// written to and destroyed in the same frame ends up destroyed regardless of the order
		/// the two were recorded in. Recording anything against an entity already destroyed in
		/// this buffer is a caller error -- its id may already belong to something else.
		/// @thread_safety Thread-confined; blocks. This is the call that
		///                touches the registry: it inserts and destroys, so it takes on the contract of
		///                those operations. Apply at a point where nothing is iterating the arrays it
		///                writes to -- that is the whole reason to record into a buffer first.
		void apply(Reg& registry) {
			for (auto& slot : mStores) {
				if (slot) { slot->apply(registry); }
			}
			if (!mDestroyed.empty()) {
				registry.destroyEntities(mDestroyed);
				mDestroyed.clear();
			}
			mSeq = 0;
		}

		/// @brief Drop everything recorded without applying it.
		/// @thread_safety Thread-confined. One buffer belongs to one thread; nothing in it
		///                takes a lock. Give each recording thread its own.
		void clear() {
			for (auto& slot : mStores) {
				if (slot) { slot->clear(); }
			}
			mDestroyed.clear();
		}

		/// @return True when nothing is waiting to be applied.
		/// @thread_safety Thread-confined. One buffer belongs to one thread; nothing in it
		///                takes a lock. Give each recording thread its own.
		[[nodiscard]] bool empty() const {
			if (!mDestroyed.empty()) { return false; }
			for (const auto& slot : mStores) {
				if (slot && !slot->empty()) { return false; }
			}
			return true;
		}

		/// @return How many changes are recorded, for sizing a flush or for diagnostics.
		/// @thread_safety Thread-confined. One buffer belongs to one thread; nothing in it
		///                takes a lock. Give each recording thread its own.
		[[nodiscard]] size_t size() const {
			size_t n = mDestroyed.size();
			for (const auto& slot : mStores) {
				if (slot) { n += slot->size(); }
			}
			return n;
		}

	private:
		/// @brief Type-erased handle so the buffer can hold one bucket per component type.
		struct IStore {
			virtual ~IStore() = default;
			virtual void apply(Reg&) = 0;
			virtual void clear() = 0;
			virtual bool empty() const = 0;
			virtual size_t size() const = 0;
		};

		template<typename T>
		struct Store final : IStore {
			/// Adds are held in the shape insertBulk already takes, so the common case can hand
			/// them straight over. The sequence numbers sit alongside and are only consulted
			/// when the same type was both added and removed in one buffer.
			std::vector<std::pair<EntityId, T>> adds;
			std::vector<uint64_t> addSeq;
			std::vector<EntityId> removals;
			std::vector<uint64_t> removeSeq;

			void apply(Reg& registry) override {
				if (adds.empty() && removals.empty()) { return; }

				// The usual frame touches a type one way or the other, never both, and then
				// there is nothing to reconcile: insertBulk already sorts the batch and keeps
				// the last value for a repeated id, and destroyComponent already sorts. Doing
				// either here as well was the whole cost of the buffer -- 90 ns per element
				// against 13 for the insert it was preparing.
				if (removals.empty()) {
					registry.template insertBulk<T>(adds.begin(), adds.end());
					clear();
					return;
				}
				if (adds.empty()) {
					registry.template destroyComponent<T>(removals);
					clear();
					return;
				}

				resolveByLastRecorded();
				if (!mLiveAdds.empty()) {
					registry.template insertBulk<T>(mLiveAdds.begin(), mLiveAdds.end());
				}
				if (!mLiveRemovals.empty()) {
					registry.template destroyComponent<T>(mLiveRemovals);
				}
				clear();
			}

			void clear() override {
				adds.clear();
				addSeq.clear();
				removals.clear();
				removeSeq.clear();
				mLiveAdds.clear();
				mLiveRemovals.clear();
			}
			bool empty() const override { return adds.empty() && removals.empty(); }
			size_t size() const override { return adds.size() + removals.size(); }

		private:
			/// @brief Both an add and a removal were recorded for this type: keep, per entity,
			/// whichever was recorded last.
			void resolveByLastRecorded() {
				// When an entity appears more than once on a side, only its latest record can
				// win, so index by entity and keep the newest seen.
				mLatest.clear();
				for (size_t i = 0; i < adds.size(); ++i) {
					auto& slot = mLatest[adds[i].first];
					if (!slot.seen || addSeq[i] > slot.seq) { slot = { true, addSeq[i], true, i }; }
				}
				for (size_t i = 0; i < removals.size(); ++i) {
					auto& slot = mLatest[removals[i]];
					if (!slot.seen || removeSeq[i] > slot.seq) { slot = { true, removeSeq[i], false, i }; }
				}

				mLiveAdds.clear();
				mLiveRemovals.clear();
				for (const auto& [entity, latest] : mLatest) {
					if (latest.isAdd) { mLiveAdds.emplace_back(entity, adds[latest.index].second); }
					else { mLiveRemovals.push_back(entity); }
				}
			}

			struct Latest { bool seen = false; uint64_t seq = 0; bool isAdd = false; size_t index = 0; };
			std::unordered_map<EntityId, Latest> mLatest;
			std::vector<std::pair<EntityId, T>> mLiveAdds;
			std::vector<EntityId> mLiveRemovals;
		};

		template<typename T>
		Store<T>& store() {
			const auto type = Reg::template componentTypeId<T>();
			if (type >= mStores.size()) { mStores.resize(static_cast<size_t>(type) + 1); }
			if (!mStores[type]) { mStores[type] = std::make_unique<Store<T>>(); }
			return static_cast<Store<T>&>(*mStores[type]);
		}

		std::vector<std::unique_ptr<IStore>> mStores;  ///< One bucket per component type id.
		std::vector<EntityId> mDestroyed;              ///< Entities to destroy outright.
		uint64_t mSeq = 0;                             ///< Records the order operations arrived in.
	};
} // namespace ecss
