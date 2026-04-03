#include <gtest/gtest.h>

#include <ecss/Ranges.h>

using namespace ecss;

namespace {

std::vector<EntityId> SortedIds(std::initializer_list<EntityId> ids) {
	return std::vector<EntityId>(ids);
}

} // namespace

TEST(RangesFull, SortedVectorCtor_Empty) {
	Ranges<EntityId> r(SortedIds({}));
	EXPECT_TRUE(r.empty());
}

TEST(RangesFull, SortedVectorCtor_SingleElement) {
	Ranges<EntityId> r(SortedIds({5}));
	EXPECT_TRUE(r.contains(5));
	EXPECT_EQ(r.size(), 1u);
}

TEST(RangesFull, SortedVectorCtor_AllDuplicates) {
	Ranges<EntityId> r(SortedIds({3, 3, 3, 3}));
	EXPECT_TRUE(r.contains(3));
	EXPECT_EQ(r.size(), 1u);
}

TEST(RangesFull, SortedVectorCtor_LargeGaps) {
	Ranges<EntityId> r(SortedIds({0, 100, 200}));
	EXPECT_EQ(r.size(), 3u);
	EXPECT_TRUE(r.contains(0));
	EXPECT_TRUE(r.contains(100));
	EXPECT_TRUE(r.contains(200));
	EXPECT_FALSE(r.contains(50));
}

TEST(RangesFull, TakeAfterFragmentation) {
	Ranges<EntityId> r;
	for (int i = 0; i < 10; ++i) {
		(void)r.take();
	}
	r.erase(3);
	r.erase(5);
	r.erase(7);
	EXPECT_EQ(r.take(), static_cast<EntityId>(3));
}

TEST(RangesFull, TakeAfterClear) {
	Ranges<EntityId> r;
	for (int i = 0; i < 5; ++i) {
		(void)r.take();
	}
	r.clear();
	EXPECT_EQ(r.take(), static_cast<EntityId>(0));
}

TEST(RangesFull, InsertIntoEmpty) {
	Ranges<EntityId> r;
	r.insert(42);
	EXPECT_TRUE(r.contains(42));
	EXPECT_EQ(r.size(), 1u);
}

TEST(RangesFull, InsertAtBeginning) {
	Ranges<EntityId> r(std::vector<Ranges<EntityId>::Range>{{5, 10}});
	r.insert(4);
	EXPECT_TRUE(r.contains(4));
	EXPECT_EQ(r.size(), 1u);
}

TEST(RangesFull, InsertAtEnd) {
	Ranges<EntityId> r(std::vector<Ranges<EntityId>::Range>{{5, 10}});
	r.insert(10);
	EXPECT_TRUE(r.contains(10));
	EXPECT_EQ(r.size(), 1u);
}

TEST(RangesFull, InsertAdjacentMergesBothSides) {
	Ranges<EntityId> r(std::vector<Ranges<EntityId>::Range>{{0, 3}, {5, 8}});
	r.insert(3);
	r.insert(4);
	EXPECT_EQ(r.size(), 1u);
}

TEST(RangesFull, InsertFarApart) {
	Ranges<EntityId> r;
	r.insert(0);
	r.insert(1000);
	EXPECT_EQ(r.size(), 2u);
}

TEST(RangesFull, EraseOnlyElement) {
	Ranges<EntityId> r;
	(void)r.take();
	r.erase(0);
	EXPECT_TRUE(r.empty());
}

TEST(RangesFull, EraseAlreadyErased) {
	Ranges<EntityId> r;
	for (int i = 0; i < 5; ++i) {
		(void)r.take();
	}
	r.erase(2);
	r.erase(2);
	EXPECT_FALSE(r.contains(2));
}

TEST(RangesFull, ContainsBelowFirst) {
	Ranges<EntityId> r(std::vector<Ranges<EntityId>::Range>{{5, 10}});
	EXPECT_FALSE(r.contains(4));
}

TEST(RangesFull, ContainsAboveLast) {
	Ranges<EntityId> r(std::vector<Ranges<EntityId>::Range>{{5, 10}});
	EXPECT_FALSE(r.contains(10));
}

TEST(RangesFull, ContainsAtBoundary) {
	Ranges<EntityId> r(std::vector<Ranges<EntityId>::Range>{{5, 10}});
	EXPECT_TRUE(r.contains(5));
	EXPECT_TRUE(r.contains(9));
}

TEST(RangesFull, GetAllAfterMixedOps) {
	Ranges<EntityId> r;
	for (int i = 0; i < 10; ++i) {
		(void)r.take();
	}
	r.erase(2);
	r.erase(4);
	r.erase(6);
	const auto all = r.getAll();
	EXPECT_EQ(all.size(), 7u);
	const std::vector<EntityId> expected{0, 1, 3, 5, 7, 8, 9};
	EXPECT_EQ(all, expected);
}

TEST(RangesFull, GetAllEmpty) {
	Ranges<EntityId> r;
	EXPECT_EQ(r.getAll().size(), 0u);
}

TEST(RangesFull, MergeOverlapping) {
	Ranges<EntityId> r(std::vector<Ranges<EntityId>::Range>{{0, 5}, {3, 8}, {10, 15}});
	for (EntityId i = 0; i < 8; ++i) {
		EXPECT_TRUE(r.contains(i));
	}
	EXPECT_FALSE(r.contains(8));
	EXPECT_FALSE(r.contains(9));
	for (EntityId i = 10; i < 15; ++i) {
		EXPECT_TRUE(r.contains(i));
	}
}

TEST(RangesFull, MergeAdjacent) {
	Ranges<EntityId> r(std::vector<Ranges<EntityId>::Range>{{0, 5}, {5, 10}});
	EXPECT_EQ(r.size(), 1u);
	for (EntityId i = 0; i < 10; ++i) {
		EXPECT_TRUE(r.contains(i));
	}
}
