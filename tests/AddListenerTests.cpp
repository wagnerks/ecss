#include <algorithm>
#include <utility>
#include <vector>

#include <ecss/CommandBuffer.h>
#include <ecss/Registry.h>

#include <gtest/gtest.h>

namespace AddListenerTests {
	using namespace ecss;

	struct Tagged { int v; };
	struct Untold { int v; };
	struct Marker { int from; };

	// What a listener was told, and what it found in the registry at that moment.
	template <bool TS>
	struct Heard {
		Registry<TS>* registry = nullptr;
		std::vector<EntityId> told;
		std::vector<int> found; // the stored value, or -1 when a view did not find the component
		EntityId addFromListener = INVALID_ID;

		static void listener(EntityId entity, void* context) {
			auto& self = *static_cast<Heard*>(context);
			self.told.push_back(entity);
			int value = -1;
			for (auto [id, tagged] : self.registry->template view<const Tagged>()) {
				if (id == entity) {
					value = tagged->v;
				}
			}
			self.found.push_back(value);
			// Nothing of the add is held any more: the listener may add, to another array and to this one.
			self.registry->template addComponent<Marker>(entity, static_cast<int>(entity));
			if (self.addFromListener != INVALID_ID) {
				const auto id = std::exchange(self.addFromListener, INVALID_ID);
				self.registry->template addComponent<Tagged>(id, 99);
			}
		}
	};

	template <bool TS>
	void toldOnEveryAddPath() {
		Registry<TS> registry;
		Heard<TS> heard;
		heard.registry = &registry;
		registry.template setAddListener<Tagged>(&Heard<TS>::listener, &heard);

		// addComponent, for an id appended and for one landing in the middle.
		const auto a = registry.takeEntity();
		const auto b = registry.takeEntity();
		registry.template addComponent<Tagged>(b, 2);
		registry.template addComponent<Tagged>(a, 1);
		EXPECT_EQ(heard.told, (std::vector<EntityId>{ b, a }));
		EXPECT_EQ(heard.found, (std::vector<int>{ 2, 1 }));
		EXPECT_TRUE(registry.template hasComponent<Marker>(a));

		// Overwriting is an add too.
		registry.template addComponent<Tagged>(a, 11);
		EXPECT_EQ(heard.told.back(), a);
		EXPECT_EQ(heard.found.back(), 11);

		// addComponents and insertBulk tell every id once the whole batch is stored.
		std::vector<EntityId> ids;
		for (int i = 0; i < 4; ++i) {
			ids.push_back(registry.takeEntity());
		}
		heard.told.clear();
		heard.found.clear();
		size_t next = 0;
		registry.template addComponents<Tagged>([&]() -> std::pair<EntityId, Tagged> {
			if (next == 2) {
				return { INVALID_ID, {} };
			}
			const auto id = ids[next];
			return { id, Tagged{ static_cast<int>(100 + next++) } };
		});
		std::vector<std::pair<EntityId, Tagged>> bulk{ { ids[3], Tagged{ 103 } }, { ids[2], Tagged{ 102 } } };
		registry.template insertBulk<Tagged>(bulk.begin(), bulk.end());
		EXPECT_EQ(heard.told, (std::vector<EntityId>{ ids[0], ids[1], ids[3], ids[2] }));
		EXPECT_EQ(heard.found, (std::vector<int>{ 100, 101, 103, 102 }));

		// A component the listener adds to the very array it was told of is told in turn.
		const auto fromListener = registry.takeEntity();
		heard.addFromListener = fromListener;
		heard.told.clear();
		registry.template addComponent<Tagged>(registry.takeEntity(), 5);
		ASSERT_EQ(heard.told.size(), 2u);
		EXPECT_EQ(heard.told[1], fromListener);
		EXPECT_EQ(heard.found.back(), 99);

		// Another type is not told; a cleared listener is not called.
		heard.told.clear();
		registry.template addComponent<Untold>(registry.takeEntity(), 1);
		registry.template setAddListener<Tagged>(nullptr);
		registry.template addComponent<Tagged>(registry.takeEntity(), 1);
		EXPECT_TRUE(heard.told.empty());
	}

	TEST(AddListener, ToldOnceStoredOnEveryAddPath) {
		toldOnEveryAddPath<true>();
	}

	TEST(AddListener, ToldOnceStoredOnEveryAddPath_NonTS) {
		toldOnEveryAddPath<false>();
	}

	// Why there is a listener: a component's constructor runs before its component is stored, so whatever it tells
	// arrives before anyone can find the component.
	struct TellsFromItsConstructor {
		static inline Registry<true>* registry = nullptr;
		static inline std::vector<bool> storedWhenTold;
		explicit TellsFromItsConstructor(EntityId self) {
			storedWhenTold.push_back(registry->hasComponent<TellsFromItsConstructor>(self));
		}
	};

	TEST(AddListener, AConstructorRunsBeforeItsComponentIsStored) {
		Registry<true> registry;
		TellsFromItsConstructor::registry = &registry;
		TellsFromItsConstructor::storedWhenTold.clear();
		const auto a = registry.takeEntity();
		const auto b = registry.takeEntity();
		registry.addComponent<TellsFromItsConstructor>(b, b);
		registry.addComponent<TellsFromItsConstructor>(a, a);
		EXPECT_EQ(TellsFromItsConstructor::storedWhenTold, (std::vector<bool>{ false, false }));
		EXPECT_TRUE(registry.hasComponent<TellsFromItsConstructor>(a));
		EXPECT_TRUE(registry.hasComponent<TellsFromItsConstructor>(b));
	}

	TEST(AddListener, ToldOfWhatACommandBufferAdds) {
		Registry<true> registry;
		Heard<true> heard;
		heard.registry = &registry;
		registry.setAddListener<Tagged>(&Heard<true>::listener, &heard);

		const auto e = registry.takeEntity();
		const auto f = registry.takeEntity();
		CommandBuffer<true> buffer;
		buffer.addComponent<Tagged>(f, 7);
		buffer.addComponent<Tagged>(e, 6);
		EXPECT_TRUE(heard.told.empty()) << "told of what was only recorded";
		buffer.apply(registry);

		auto told = heard.told;
		std::sort(told.begin(), told.end());
		EXPECT_EQ(told, (std::vector<EntityId>{ e, f }));
		EXPECT_EQ(std::count(heard.found.begin(), heard.found.end(), -1), 0) << "told before the component was stored";
	}
}
