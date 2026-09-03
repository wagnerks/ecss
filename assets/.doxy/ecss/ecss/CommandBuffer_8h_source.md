

# File CommandBuffer.h

[**File List**](files.md) **>** [**ecss**](dir_194708e763cf312315c6b23555bce86f.md) **>** [**CommandBuffer.h**](CommandBuffer_8h.md)

[Go to the documentation of this file](CommandBuffer_8h.md)


```C++
#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include <ecss/Registry.h>

namespace ecss {
    template<bool ThreadSafe = true, typename Allocator = Memory::ChunksAllocator<8192>>
    class CommandBuffer final {
        using Reg = Registry<ThreadSafe, Allocator>;

    public:
        CommandBuffer() = default;
        CommandBuffer(const CommandBuffer&) = delete;
        CommandBuffer& operator=(const CommandBuffer&) = delete;
        CommandBuffer(CommandBuffer&&) noexcept = default;
        CommandBuffer& operator=(CommandBuffer&&) noexcept = default;

        template<typename T, typename... Args>
        void addComponent(EntityId entity, Args&&... args) {
            auto& st = store<T>();
            st.adds.emplace_back(entity, T{ std::forward<Args>(args)... });
            st.addSeq.push_back(mSeq++);
        }

        template<typename T>
        void destroyComponent(EntityId entity) {
            auto& st = store<T>();
            st.removals.push_back(entity);
            st.removeSeq.push_back(mSeq++);
        }

        void destroyEntity(EntityId entity) { mDestroyed.push_back(entity); }

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

        void clear() {
            for (auto& slot : mStores) {
                if (slot) { slot->clear(); }
            }
            mDestroyed.clear();
        }

        [[nodiscard]] bool empty() const {
            if (!mDestroyed.empty()) { return false; }
            for (const auto& slot : mStores) {
                if (slot && !slot->empty()) { return false; }
            }
            return true;
        }

        [[nodiscard]] size_t size() const {
            size_t n = mDestroyed.size();
            for (const auto& slot : mStores) {
                if (slot) { n += slot->size(); }
            }
            return n;
        }

    private:
        struct IStore {
            virtual ~IStore() = default;
            virtual void apply(Reg&) = 0;
            virtual void clear() = 0;
            virtual bool empty() const = 0;
            virtual size_t size() const = 0;
        };

        template<typename T>
        struct Store final : IStore {
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

        std::vector<std::unique_ptr<IStore>> mStores;  
        std::vector<EntityId> mDestroyed;              
        uint64_t mSeq = 0;                             
    };
} // namespace ecss
```


