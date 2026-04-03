#include <gtest/gtest.h>
#include <ecss/memory/ChunksAllocator.h>
#include <ecss/memory/SectorLayoutMeta.h>
#include <cstring>

using namespace ecss;
using namespace ecss::Memory;

struct ChTrivial {
	int x;
};

static ChunksAllocator<64> makeAlloc() {
	static SectorLayoutMeta* meta = SectorLayoutMeta::create<ChTrivial>();
	ChunksAllocator<64> a;
	a.init(meta);
	return a;
}

static size_t sectorStride(const ChunksAllocator<64>& a) {
	return static_cast<size_t>(a.getSectorLayout()->getTotalSize());
}

static const std::byte* chunkBase(const ChunksAllocator<64>& a, size_t linearIdx) {
	const size_t shift = sectorStride(a);
	return a.at(linearIdx) - static_cast<std::ptrdiff_t>((linearIdx & (64u - 1)) * shift);
}

TEST(ChunksAlloc, DefaultEmpty) {
	auto a = makeAlloc();
	EXPECT_EQ(a.capacity(), 0u);
}

TEST(ChunksAlloc, AllocateOne) {
	auto a = makeAlloc();
	a.allocate(1);
	EXPECT_GE(a.capacity(), 1u);
}

TEST(ChunksAlloc, AllocateExactChunk) {
	auto a = makeAlloc();
	a.allocate(64);
	EXPECT_EQ(a.capacity(), 64u);
}

TEST(ChunksAlloc, AllocateMultipleChunks) {
	auto a = makeAlloc();
	a.allocate(200);
	EXPECT_GE(a.capacity(), 200u);
}

TEST(ChunksAlloc, AllocateZero) {
	auto a = makeAlloc();
	a.allocate(0);
	EXPECT_EQ(a.capacity(), 0u);
}

TEST(ChunksAlloc, AtFirstElement) {
	auto a = makeAlloc();
	a.allocate(10);
	auto* p = reinterpret_cast<ChTrivial*>(a.at(0));
	ASSERT_NE(p, nullptr);
	p->x = 42;
	EXPECT_EQ(reinterpret_cast<ChTrivial*>(a.at(0))->x, 42);
}

TEST(ChunksAlloc, AtLastElement) {
	auto a = makeAlloc();
	a.allocate(10);
	auto* p = reinterpret_cast<ChTrivial*>(a.at(9));
	ASSERT_NE(p, nullptr);
	p->x = -7;
	EXPECT_EQ(reinterpret_cast<ChTrivial*>(a.at(9))->x, -7);
}

TEST(ChunksAlloc, AtCrossChunk) {
	auto a = makeAlloc();
	a.allocate(128);
	EXPECT_NE(chunkBase(a, 63), chunkBase(a, 64));
}

TEST(ChunksAlloc, OperatorBracket) {
	auto a = makeAlloc();
	a.allocate(5);
	EXPECT_EQ(a[3], a.at(3));
}

TEST(ChunksAlloc, DeallocateFullRange) {
	auto a = makeAlloc();
	a.allocate(64);
	a.deallocate(0, 64);
	EXPECT_EQ(a.capacity(), 0u);
}

TEST(ChunksAlloc, DeallocatePartial) {
	auto a = makeAlloc();
	a.allocate(128);
	a.deallocate(64, 128);
	EXPECT_LT(a.capacity(), 128u);
}

TEST(ChunksAlloc, DeallocateAlreadyEmpty) {
	auto a = makeAlloc();
	a.deallocate(0, 0);
	EXPECT_EQ(a.capacity(), 0u);
}

TEST(ChunksAlloc, CursorStepSingleChunk) {
	auto a = makeAlloc();
	a.allocate(10);
	auto c = a.getCursor(0);
	for (int i = 0; i < 10; ++i) {
		EXPECT_EQ(c.linearIndex(), static_cast<size_t>(i));
		++c;
	}
	EXPECT_EQ(c.linearIndex(), 10u);
}

TEST(ChunksAlloc, CursorStepCrossChunk) {
	auto a = makeAlloc();
	a.allocate(128);
	auto c = a.getCursor(62);
	for (int i = 0; i < 4; ++i) {
		++c;
	}
	EXPECT_EQ(c.linearIndex(), 66u);
	EXPECT_NE(chunkBase(a, 62), chunkBase(a, 66));
}

TEST(ChunksAlloc, CursorSetLinear) {
	auto a = makeAlloc();
	a.allocate(128);
	auto c = a.getCursor(0);
	c + 100;
	EXPECT_EQ(c.linearIndex(), 100u);
}

TEST(ChunksAlloc, RangesCursorSingleSpan) {
	auto a = makeAlloc();
	a.allocate(100);
	Ranges<SectorId> ranges(std::vector<Ranges<SectorId>::Range>{{0, 10}});
	auto rc = a.getRangesCursor(ranges, 100);
	ASSERT_TRUE(static_cast<bool>(rc));
	for (int i = 0; i < 10; ++i) {
		rc.step();
	}
	EXPECT_EQ(rc.linearIndex(), 10u);
	EXPECT_FALSE(static_cast<bool>(rc));
}

TEST(ChunksAlloc, RangesCursorMultiSpans) {
	auto a = makeAlloc();
	a.allocate(100);
	Ranges<SectorId> ranges(std::vector<Ranges<SectorId>::Range>{{0, 5}, {50, 55}});
	auto rc = a.getRangesCursor(ranges, 100);
	ASSERT_TRUE(static_cast<bool>(rc));
	for (int i = 0; i < 10; ++i) {
		rc.step();
	}
	EXPECT_EQ(rc.linearIndex(), 55u);
	EXPECT_FALSE(static_cast<bool>(rc));
}

TEST(ChunksAlloc, RangesCursorEmpty) {
	auto a = makeAlloc();
	a.allocate(10);
	Ranges<SectorId> ranges;
	auto rc = a.getRangesCursor(ranges, 10);
	EXPECT_FALSE(static_cast<bool>(rc));
}

TEST(ChunksAlloc, FindFirstElement) {
	auto a = makeAlloc();
	a.allocate(10);
	EXPECT_EQ(a.find(a.at(0)), 0u);
}

TEST(ChunksAlloc, FindMiddleElement) {
	auto a = makeAlloc();
	a.allocate(10);
	EXPECT_EQ(a.find(a.at(5)), 5u);
}

TEST(ChunksAlloc, FindNotInChunks) {
	auto a = makeAlloc();
	a.allocate(10);
	ChTrivial stackVar{};
	EXPECT_EQ(a.find(reinterpret_cast<const std::byte*>(&stackVar)), a.capacity());
}

TEST(ChunksAlloc, MoveTrivialSameChunk) {
	auto a = makeAlloc();
	a.allocate(10);
	reinterpret_cast<ChTrivial*>(a.at(0))->x = 123;
	a.moveSectorsDataTrivial(5, 0, 1);
	EXPECT_EQ(reinterpret_cast<ChTrivial*>(a.at(5))->x, 123);
}

TEST(ChunksAlloc, MoveTrivialCrossChunk) {
	auto a = makeAlloc();
	a.allocate(128);
	reinterpret_cast<ChTrivial*>(a.at(0))->x = 999;
	a.moveSectorsDataTrivial(64, 0, 1);
	EXPECT_EQ(reinterpret_cast<ChTrivial*>(a.at(64))->x, 999);
}

TEST(ChunksAlloc, CopySameCapacity) {
	auto a = makeAlloc();
	a.allocate(10);
	for (size_t i = 0; i < 10; ++i) {
		reinterpret_cast<ChTrivial*>(a.at(i))->x = static_cast<int>(i) * 3;
	}
	ChunksAllocator<64> b = makeAlloc();
	b = a;
	for (size_t i = 0; i < 10; ++i) {
		EXPECT_EQ(reinterpret_cast<ChTrivial*>(b.at(i))->x, static_cast<int>(i) * 3);
	}
	reinterpret_cast<ChTrivial*>(b.at(0))->x = -1;
	EXPECT_EQ(reinterpret_cast<ChTrivial*>(a.at(0))->x, 0);
}

TEST(ChunksAlloc, MoveSameCapacity) {
	auto a = makeAlloc();
	a.allocate(10);
	for (size_t i = 0; i < 10; ++i) {
		reinterpret_cast<ChTrivial*>(a.at(i))->x = static_cast<int>(i) + 1;
	}
	ChunksAllocator<64> b = makeAlloc();
	b = std::move(a);
	EXPECT_EQ(a.capacity(), 0u);
	for (size_t i = 0; i < 10; ++i) {
		EXPECT_EQ(reinterpret_cast<ChTrivial*>(b.at(i))->x, static_cast<int>(i) + 1);
	}
}
