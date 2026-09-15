#pragma once

#include <atomic>
#include <cassert>
#include <concepts>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <type_traits>
#include <vector>

#include <ecss/Fwd.h>

/**
 * @file Relations.h
 * @brief Typed directed relations between entities.
 *
 * A component describes one entity. A relation is a directed edge between two entities,
 * like "player holds sword". Each relation type has its own storage: a pool of edges that
 * never move, each linked into its source's outgoing list and its target's incoming list.
 */
namespace ecss {

	/// @brief What every relation type must satisfy.
	///
	/// The API is `noexcept` like the rest of the library, so a payload that throws from its
	/// move or destructor would just call terminate. Better to reject it at compile time.
	/// Move-assignment is not required: without it, overwrite destroys and rebuilds in place.
	template<class R>
	concept RelationPayload =
		std::is_nothrow_move_constructible_v<R> &&
		std::is_nothrow_destructible_v<R>;

	/// @brief What the tag form of relate() additionally requires.
	///
	/// Empty alone is not enough: `struct Tag { Tag() = delete; };` is empty but can't be
	/// built, and a default constructor can still throw.
	template<class R>
	concept RelationTag =
		RelationPayload<R> &&
		std::is_empty_v<R> &&
		std::default_initializable<R> &&
		std::is_nothrow_default_constructible_v<R>;

	namespace Relations {

		/// @brief Index into a relation storage's edge pool.
		///
		/// A separate type, not a bare uint32_t. An Edge holds two EntityId and four EdgeIndex
		/// of the same width, and mixing them up would compile without a word.
		enum class EdgeIndex : uint32_t {};

		inline constexpr EdgeIndex kNoEdge = static_cast<EdgeIndex>(~uint32_t{ 0 });

		[[nodiscard]] FORCE_INLINE constexpr uint32_t raw(EdgeIndex e) noexcept { return static_cast<uint32_t>(e); }
		[[nodiscard]] FORCE_INLINE constexpr EdgeIndex edgeIndex(uint32_t v) noexcept { return static_cast<EdgeIndex>(v); }

		/// @brief Dense sequential ids for relation types.
		///
		/// Separate from component ids, so `struct Parent {}` can be both a component and a
		/// relation without the two getting mixed up.
		class RelationTypeIdGenerator {
			static inline std::atomic<uint32_t> sNextId{ 0 };

		public:
			template<class R>
			[[nodiscard]] static uint32_t getId() noexcept {
				static const uint32_t id = sNextId.fetch_add(1, std::memory_order_relaxed);
				return id;
			}

			[[nodiscard]] static uint32_t count() noexcept { return sNextId.load(std::memory_order_relaxed); }
		};

		/// @brief Per-storage structural lock, compiled away in the non-thread-safe build.
		///
		/// Reads take a shared lock instead of going lock-free. Edges don't move, but that is
		/// not enough: a freed slot can be given to another edge while a reader still holds
		/// its index. With the lock held, nobody can remove an edge while someone is reading.
		template<bool ThreadSafe>
		struct StructuralMutex {
			[[nodiscard]] std::shared_lock<std::shared_mutex> shared() const { return std::shared_lock{ mMutex }; }
			[[nodiscard]] std::unique_lock<std::shared_mutex> unique() const { return std::unique_lock{ mMutex }; }

		private:
			mutable std::shared_mutex mMutex;
		};

		template<>
		struct StructuralMutex<false> {
			struct Guard {};
			[[nodiscard]] Guard shared() const noexcept { return {}; }
			[[nodiscard]] Guard unique() const noexcept { return {}; }
		};

		template<bool ThreadSafe>
		using SharedGuard = decltype(std::declval<const StructuralMutex<ThreadSafe>&>().shared());

		/// @brief Growable array of trivially-constructible T indexed by a dense id.
		///
		/// Blocks are allocated once and never moved, so element addresses stay valid. Only
		/// blocks for ids actually in use get allocated, so a relation between fifty entities
		/// doesn't cost an array as big as the highest entity id.
		template<class T, uint32_t BlockShift = 9>
		class BlockArray {
			static constexpr uint32_t kBlockSize = 1u << BlockShift;
			static constexpr uint32_t kBlockMask = kBlockSize - 1;

		public:
			[[nodiscard]] T* find(uint32_t index) noexcept {
				const uint32_t b = index >> BlockShift;
				return b < mBlocks.size() && mBlocks[b] ? mBlocks[b].get() + (index & kBlockMask) : nullptr;
			}

			[[nodiscard]] const T* find(uint32_t index) const noexcept {
				const uint32_t b = index >> BlockShift;
				return b < mBlocks.size() && mBlocks[b] ? mBlocks[b].get() + (index & kBlockMask) : nullptr;
			}

			/// @brief Reach @p index, committing its block if absent.
			[[nodiscard]] T& ensure(uint32_t index) {
				const uint32_t b = index >> BlockShift;
				if (b >= mBlocks.size()) { mBlocks.resize(b + 1); }
				if (!mBlocks[b]) { mBlocks[b] = std::make_unique<T[]>(kBlockSize); }
				return mBlocks[b][index & kBlockMask];
			}

			/// @brief One past the highest index any committed block can hold.
			[[nodiscard]] uint32_t committedEnd() const noexcept {
				return static_cast<uint32_t>(mBlocks.size()) << BlockShift;
			}

			void clear() noexcept { mBlocks.clear(); }

		private:
			std::vector<std::unique_ptr<T[]>> mBlocks;
		};

		/// @brief Adjacency list head for one entity in one direction.
		struct Adjacency {
			EdgeIndex head = kNoEdge;
			uint32_t  degree = 0;   ///< List length. Lets a lookup walk the shorter of the two lists.
		};

		/// @brief Base without R, so the registry can sweep and validate every storage.
		struct IRelationStorage {
			virtual ~IRelationStorage() = default;

			/// @brief Remove every edge that touches any id in [first, last).
			virtual void removeIncident(const EntityId* first, const EntityId* last) noexcept = 0;
			/// @brief Drop every edge, keeping the storage registered.
			virtual void clearAll() noexcept = 0;
			/// @brief Check the lists are consistent. For tests and debug builds.
			virtual bool validate() const noexcept = 0;
			virtual size_t edgeCount() const noexcept = 0;
		};

		/**
		 * @brief Storage for one relation type.
		 *
		 * Edges sit in a pool and never move. Freed slots go to a free list. Each edge is in
		 * two doubly linked lists (source's outgoing, target's incoming), so removing a found
		 * edge is O(1).
		 *
		 * @thread_safety Internally synchronized in the thread-safe build. Reads take the shared
		 *                lock (a query for the call, a view for as long as it lives), writes take
		 *                the unique lock. Writing from a thread that holds a view deadlocks,
		 *                because std::shared_mutex is not recursive.
		 */
		template<class R, bool ThreadSafe>
		class RelationStorage final : public IRelationStorage {
		public:
			static constexpr bool kThreadSafe = ThreadSafe;

			struct Edge {
				EntityId  source = 0;
				EntityId  target = 0;
				EdgeIndex prevOut = kNoEdge;
				EdgeIndex nextOut = kNoEdge;
				EdgeIndex prevIn = kNoEdge;
				EdgeIndex nextIn = kNoEdge;
				ECSS_NO_UNIQUE_ADDRESS R payload;
			};

			// A tag relation should add no bytes per edge. The attribute allows that but doesn't
			// guarantee it, so check.
			static_assert(!std::is_empty_v<R> || sizeof(Edge) == 2 * sizeof(EntityId) + 4 * sizeof(EdgeIndex),
			              "an empty relation payload must add no per-edge storage on this layout");

			~RelationStorage() override {
				for (uint32_t i = 0; i < mPoolSize; ++i) {
					if (mAlive[i]) { std::destroy_at(edgeAt(i)); }
				}
			}

			// -------------------------------------------------------------- structural writes

			/// @brief Insert (source -> target), or overwrite the payload of the existing edge.
			template<class V>
			void relate(EntityId source, EntityId target, V&& payload) noexcept {
				[[maybe_unused]] auto guard = mMutex.unique();
				const EdgeIndex existing = findEdgeLocked(source, target);
				if (existing != kNoEdge) {
					overwrite(edgeAt(raw(existing))->payload, std::forward<V>(payload));
					return;   // one edge per (source, target), just replace the payload
				}
				insertLocked(source, target, std::forward<V>(payload));
			}

			void unrelate(EntityId source, EntityId target) noexcept {
				[[maybe_unused]] auto guard = mMutex.unique();
				const EdgeIndex e = findEdgeLocked(source, target);
				if (e != kNoEdge) { unlinkLocked(e); }
			}

			void removeOutgoing(EntityId source) noexcept {
				[[maybe_unused]] auto guard = mMutex.unique();
				drainLocked(mOut.find(source));
			}

			void removeIncoming(EntityId target) noexcept {
				[[maybe_unused]] auto guard = mMutex.unique();
				drainLocked(mIn.find(target));
			}

			/// @brief Remove all edges of a batch of dying entities, under one lock.
			///
			/// For each id we empty the outgoing list, then the incoming one. Unlinking takes an
			/// edge out of both lists at once, so an edge is never seen twice. That covers a
			/// self-edge A -> A and both ends dying in the same batch, with no extra bookkeeping.
			void removeIncident(const EntityId* first, const EntityId* last) noexcept override {
				[[maybe_unused]] auto guard = mMutex.unique();
				for (auto it = first; it != last; ++it) {
					drainLocked(mOut.find(*it));
					drainLocked(mIn.find(*it));
				}
			}

			void clearAll() noexcept override {
				[[maybe_unused]] auto guard = mMutex.unique();
				for (uint32_t i = 0; i < mPoolSize; ++i) {
					if (mAlive[i]) { std::destroy_at(edgeAt(i)); mAlive[i] = 0; }
				}
				mFree.clear();
				mPoolSize = 0;
				mLiveCount = 0;
				mAlive.clear();
				mPool.clear();
				mOut.clear();
				mIn.clear();
			}

			// -------------------------------------------------------------- structural reads

			[[nodiscard]] bool hasRelation(EntityId source, EntityId target) const noexcept {
				[[maybe_unused]] auto guard = mMutex.shared();
				return findEdgeLocked(source, target) != kNoEdge;
			}

			[[nodiscard]] EdgeIndex outHead(EntityId source) const noexcept {
				const Adjacency* a = mOut.find(source);
				return a ? a->head : kNoEdge;
			}

			[[nodiscard]] EdgeIndex inHead(EntityId target) const noexcept {
				const Adjacency* a = mIn.find(target);
				return a ? a->head : kNoEdge;
			}

			[[nodiscard]] SharedGuard<ThreadSafe> lockShared() const { return mMutex.shared(); }

			[[nodiscard]] Edge* edgeAt(uint32_t index) noexcept {
				return reinterpret_cast<Edge*>(mPool.find(index));
			}
			[[nodiscard]] const Edge* edgeAt(uint32_t index) const noexcept {
				return reinterpret_cast<const Edge*>(mPool.find(index));
			}

			[[nodiscard]] size_t edgeCount() const noexcept override {
				[[maybe_unused]] auto guard = mMutex.shared();
				return mLiveCount;
			}

			// -------------------------------------------------------------- validation

			/// @brief Every live edge is in both lists, degrees match list lengths, and no list
			///        points at a freed slot.
			[[nodiscard]] bool validate() const noexcept override {
				[[maybe_unused]] auto guard = mMutex.shared();

				std::vector<uint8_t> seenOut(mPoolSize, 0);
				std::vector<uint8_t> seenIn(mPoolSize, 0);
				size_t fromOut = 0, fromIn = 0;

				for (uint32_t id = 0; id < mOut.committedEnd(); ++id) {
					const Adjacency* a = mOut.find(id);
					if (!a) { continue; }
					uint32_t walked = 0;
					for (EdgeIndex e = a->head; e != kNoEdge; e = edgeAt(raw(e))->nextOut) {
						const uint32_t i = raw(e);
						if (i >= mPoolSize || !mAlive[i]) { return false; }   // points at a freed slot
						if (seenOut[i]) { return false; }                      // cycle in the list
						seenOut[i] = 1;
						if (edgeAt(i)->source != id) { return false; }
						++walked;
						++fromOut;
					}
					if (walked != a->degree) { return false; }                 // degree is off
				}

				for (uint32_t id = 0; id < mIn.committedEnd(); ++id) {
					const Adjacency* a = mIn.find(id);
					if (!a) { continue; }
					uint32_t walked = 0;
					for (EdgeIndex e = a->head; e != kNoEdge; e = edgeAt(raw(e))->nextIn) {
						const uint32_t i = raw(e);
						if (i >= mPoolSize || !mAlive[i]) { return false; }
						if (seenIn[i]) { return false; }
						seenIn[i] = 1;
						if (edgeAt(i)->target != id) { return false; }
						++walked;
						++fromIn;
					}
					if (walked != a->degree) { return false; }
				}

				// Both directions must see the same edges, and exactly the ones marked alive.
				if (fromOut != fromIn || fromOut != mLiveCount) { return false; }
				for (uint32_t i = 0; i < mPoolSize; ++i) {
					if (mAlive[i] != (seenOut[i] && seenIn[i])) { return false; }
				}
				return true;
			}

		private:
			// -------------------------------------------------------------- pool

			[[nodiscard]] uint32_t allocateSlot() {
				if (!mFree.empty()) {
					const uint32_t i = mFree.back();
					mFree.pop_back();
					return i;
				}
				const uint32_t i = mPoolSize++;
				(void)mPool.ensure(i);   // commit the block; the reference is taken via edgeAt()
				mAlive.resize(mPoolSize, 0);
				return i;
			}

			// -------------------------------------------------------------- linking

			template<class V>
			void insertLocked(EntityId source, EntityId target, V&& payload) {
				const uint32_t i = allocateSlot();

				// Build the whole edge, payload included, before linking it anywhere, so nobody
				// can reach a half-built one. Constructed in one go because R doesn't have to be
				// default-constructible or assignable.
				Edge* e = edgeAt(i);
				std::construct_at(e, Edge{ source, target, kNoEdge, kNoEdge, kNoEdge, kNoEdge,
				                           R(std::forward<V>(payload)) });
				mAlive[i] = 1;
				++mLiveCount;

				Adjacency& out = mOut.ensure(source);
				e->prevOut = kNoEdge;
				e->nextOut = out.head;
				if (out.head != kNoEdge) { edgeAt(raw(out.head))->prevOut = edgeIndex(i); }
				out.head = edgeIndex(i);
				++out.degree;

				Adjacency& in = mIn.ensure(target);
				e->prevIn = kNoEdge;
				e->nextIn = in.head;
				if (in.head != kNoEdge) { edgeAt(raw(in.head))->prevIn = edgeIndex(i); }
				in.head = edgeIndex(i);
				++in.degree;
			}

			void unlinkLocked(EdgeIndex which) noexcept {
				const uint32_t i = raw(which);
				Edge* e = edgeAt(i);

				// find(), not ensure(): a linked edge always has its entries, and ensure() may
				// allocate, which we don't want in noexcept code.
				Adjacency* out = mOut.find(e->source);
				assert(out && "unlinking an edge whose source has no adjacency entry");
				if (e->prevOut != kNoEdge) { edgeAt(raw(e->prevOut))->nextOut = e->nextOut; }
				else                       { out->head = e->nextOut; }
				if (e->nextOut != kNoEdge) { edgeAt(raw(e->nextOut))->prevOut = e->prevOut; }
				--out->degree;

				Adjacency* in = mIn.find(e->target);
				assert(in && "unlinking an edge whose target has no adjacency entry");
				if (e->prevIn != kNoEdge) { edgeAt(raw(e->prevIn))->nextIn = e->nextIn; }
				else                      { in->head = e->nextIn; }
				if (e->nextIn != kNoEdge) { edgeAt(raw(e->nextIn))->prevIn = e->prevIn; }
				--in->degree;

				std::destroy_at(e);
				mAlive[i] = 0;
				--mLiveCount;
				mFree.push_back(i);
			}

			void drainLocked(Adjacency* a) noexcept {
				if (!a) { return; }
				while (a->head != kNoEdge) { unlinkLocked(a->head); }
			}

			/// @brief Walk the shorter of the two adjacency lists. O(min(outDeg, inDeg)).
			[[nodiscard]] EdgeIndex findEdgeLocked(EntityId source, EntityId target) const noexcept {
				const Adjacency* out = mOut.find(source);
				const Adjacency* in = mIn.find(target);
				if (!out || !in || out->degree == 0 || in->degree == 0) { return kNoEdge; }

				if (out->degree <= in->degree) {
					for (EdgeIndex e = out->head; e != kNoEdge; e = edgeAt(raw(e))->nextOut) {
						if (edgeAt(raw(e))->target == target) { return e; }
					}
				}
				else {
					for (EdgeIndex e = in->head; e != kNoEdge; e = edgeAt(raw(e))->nextIn) {
						if (edgeAt(raw(e))->source == source) { return e; }
					}
				}
				return kNoEdge;
			}

			/// Assign if R can be assigned, otherwise destroy and rebuild in place. That is safe
			/// because RelationPayload already requires a nothrow move and destructor.
			template<class V>
			static void overwrite(R& slot, V&& value) noexcept {
				if constexpr (std::is_nothrow_move_assignable_v<R>) {
					slot = R(std::forward<V>(value));
				}
				else {
					std::destroy_at(&slot);
					std::construct_at(&slot, std::forward<V>(value));
				}
			}

			/// Raw storage, so a slot holds no R until an edge occupies it.
			struct alignas(Edge) Slot { std::byte bytes[sizeof(Edge)]; };

			StructuralMutex<ThreadSafe> mMutex;

			BlockArray<Slot>      mPool;        ///< Edges. Never moved.
			std::vector<uint8_t>  mAlive;       ///< Whether slot i currently holds a constructed Edge.
			std::vector<uint32_t> mFree;        ///< Erased slots, reusable.
			uint32_t              mPoolSize = 0;
			size_t                mLiveCount = 0;

			BlockArray<Adjacency> mOut;         ///< source -> outgoing list head + degree
			BlockArray<Adjacency> mIn;          ///< target -> incoming list head + degree
		};

		// ------------------------------------------------------------------ views

		/// @brief Iterable range over one adjacency list, holding the storage's shared guard.
		///
		/// The lock lives as long as the view. While it is held no edge can be removed and no
		/// slot reused, so the indices the view walks stay valid. Changing the same relation
		/// while holding a view deadlocks.
		template<class Storage, bool Outgoing, bool WithPayload, class Payload>
		class RelationView {
			using StorageRef = std::conditional_t<std::is_const_v<Payload>, const Storage*, Storage*>;
			using Edge = typename Storage::Edge;

		public:
			RelationView() = default;
			RelationView(StorageRef storage, EdgeIndex head, SharedGuard<Storage::kThreadSafe> guard) noexcept
				: mStorage(storage), mHead(head), mGuard(std::move(guard)) {}

			using EdgePtr = std::conditional_t<std::is_const_v<Payload>, const Edge*, Edge*>;

			/// @brief What a payload-carrying view yields: both endpoints plus the edge's data.
			///
			/// Constness comes from the pointer: a const Registry gives const R&, a non-const one
			/// gives R&, and no cast is needed.
			struct EdgeRef {
				EdgePtr edge;

				[[nodiscard]] EntityId source() const noexcept { return edge->source; }
				[[nodiscard]] EntityId target() const noexcept { return edge->target; }
				[[nodiscard]] Payload& data() const noexcept { return edge->payload; }
			};

			class iterator {
			public:
				iterator() = default;
				iterator(StorageRef storage, EdgeIndex at) noexcept : mStorage(storage), mAt(at) {}

				[[nodiscard]] auto operator*() const noexcept {
					auto* e = mStorage->edgeAt(raw(mAt));
					if constexpr (WithPayload) { return EdgeRef{ e }; }
					else if constexpr (Outgoing) { return e->target; }
					else { return e->source; }
				}

				iterator& operator++() noexcept {
					const auto* e = mStorage->edgeAt(raw(mAt));
					mAt = Outgoing ? e->nextOut : e->nextIn;
					return *this;
				}

				[[nodiscard]] bool operator==(const iterator& other) const noexcept { return mAt == other.mAt; }

			private:
				StorageRef mStorage = nullptr;
				EdgeIndex  mAt = kNoEdge;
			};

			[[nodiscard]] iterator begin() const noexcept { return { mStorage, mHead }; }
			[[nodiscard]] iterator end() const noexcept { return { mStorage, kNoEdge }; }
			[[nodiscard]] bool empty() const noexcept { return mHead == kNoEdge; }

		private:
			StorageRef mStorage = nullptr;
			EdgeIndex  mHead = kNoEdge;
			ECSS_NO_UNIQUE_ADDRESS SharedGuard<Storage::kThreadSafe> mGuard{};
		};

	} // namespace Relations
} // namespace ecss
