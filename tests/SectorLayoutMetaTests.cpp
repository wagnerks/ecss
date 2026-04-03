#include <gtest/gtest.h>
#include <ecss/memory/SectorLayoutMeta.h>
#include <ecss/memory/Reflection.h>
#include <string>

using namespace ecss;
using namespace ecss::Memory;

struct MetaTrivial1 {
	int x;
};
struct MetaTrivial2 {
	float y;
};
struct MetaTrivial3 {
	double z;
};
struct MetaNonTrivial {
	std::string s;
	MetaNonTrivial() = default;
	explicit MetaNonTrivial(std::string str) : s(std::move(str)) {}
	~MetaNonTrivial() = default;
	MetaNonTrivial(const MetaNonTrivial&) = default;
	MetaNonTrivial(MetaNonTrivial&&) = default;
	MetaNonTrivial& operator=(const MetaNonTrivial&) = default;
	MetaNonTrivial& operator=(MetaNonTrivial&&) = default;
};
struct MetaTiny {
	uint8_t v;
};

namespace {

size_t layoutTypeCount(const SectorLayoutMeta* meta) {
	size_t n = 0;
	for (auto it = meta->begin(); it != meta->end(); ++it) {
		++n;
	}
	return n;
}

bool isUniquePowerOfTwo(uint32_t mask) {
	return mask != 0 && (mask & (mask - 1)) == 0;
}

} // namespace

TEST(SectorMeta, CreateSingleType) {
	SectorLayoutMeta* meta = SectorLayoutMeta::create<MetaTrivial1>();
	ASSERT_NE(meta, nullptr);
	EXPECT_EQ(layoutTypeCount(meta), 1u);
	EXPECT_GT(meta->getTotalSize(), 0u);
	delete meta;
}

TEST(SectorMeta, CreateTwoTypes) {
	SectorLayoutMeta* meta = SectorLayoutMeta::create<MetaTrivial1, MetaTrivial2>();
	ASSERT_NE(meta, nullptr);
	EXPECT_EQ(layoutTypeCount(meta), 2u);
	delete meta;
}

TEST(SectorMeta, CreateThreeTypes) {
	SectorLayoutMeta* meta = SectorLayoutMeta::create<MetaTrivial1, MetaTrivial2, MetaTrivial3>();
	ASSERT_NE(meta, nullptr);
	EXPECT_EQ(layoutTypeCount(meta), 3u);
	delete meta;
}

TEST(SectorMeta, IsTrivialAllTrivial) {
	SectorLayoutMeta* meta = SectorLayoutMeta::create<MetaTrivial1, MetaTrivial2>();
	ASSERT_NE(meta, nullptr);
	EXPECT_TRUE(meta->isTrivial());
	delete meta;
}

TEST(SectorMeta, IsTrivialMixed) {
	SectorLayoutMeta* meta = SectorLayoutMeta::create<MetaTrivial1, MetaNonTrivial>();
	ASSERT_NE(meta, nullptr);
	EXPECT_FALSE(meta->isTrivial());
	delete meta;
}

TEST(SectorMeta, IsTrivialAllNonTrivial) {
	SectorLayoutMeta* meta = SectorLayoutMeta::create<MetaNonTrivial>();
	ASSERT_NE(meta, nullptr);
	EXPECT_FALSE(meta->isTrivial());
	delete meta;
}

TEST(SectorMeta, LayoutDataOffset_FirstType) {
	SectorLayoutMeta* meta = SectorLayoutMeta::create<MetaTrivial1, MetaTrivial2>();
	ASSERT_NE(meta, nullptr);
	EXPECT_EQ(meta->getLayoutData<MetaTrivial1>().offset, 0u);
	delete meta;
}

TEST(SectorMeta, LayoutDataOffset_SecondType) {
	SectorLayoutMeta* meta = SectorLayoutMeta::create<MetaTrivial1, MetaTrivial2>();
	ASSERT_NE(meta, nullptr);
	EXPECT_GT(meta->getLayoutData<MetaTrivial2>().offset, 0u);
	delete meta;
}

TEST(SectorMeta, LayoutDataMask_UniquePerType) {
	SectorLayoutMeta* meta = SectorLayoutMeta::create<MetaTrivial1, MetaTrivial2, MetaTrivial3>();
	ASSERT_NE(meta, nullptr);
	const uint32_t m0 = meta->getLayoutData<MetaTrivial1>().isAliveMask;
	const uint32_t m1 = meta->getLayoutData<MetaTrivial2>().isAliveMask;
	const uint32_t m2 = meta->getLayoutData<MetaTrivial3>().isAliveMask;
	EXPECT_TRUE(isUniquePowerOfTwo(m0));
	EXPECT_TRUE(isUniquePowerOfTwo(m1));
	EXPECT_TRUE(isUniquePowerOfTwo(m2));
	EXPECT_NE(m0, m1);
	EXPECT_NE(m1, m2);
	EXPECT_NE(m0, m2);
	delete meta;
}

TEST(SectorMeta, LayoutDataIndex) {
	SectorLayoutMeta* meta = SectorLayoutMeta::create<MetaTrivial1, MetaTrivial2>();
	ASSERT_NE(meta, nullptr);
	EXPECT_EQ(meta->getLayoutData<MetaTrivial1>().index, 0u);
	EXPECT_EQ(meta->getLayoutData<MetaTrivial2>().index, 1u);
	delete meta;
}

TEST(SectorMeta, LayoutDataReverseLookup) {
	SectorLayoutMeta* meta = SectorLayoutMeta::create<MetaTrivial2, MetaTrivial1>();
	ASSERT_NE(meta, nullptr);
	EXPECT_EQ(meta->getLayoutData<MetaTrivial1>().index, 1u);
	delete meta;
}

TEST(SectorMeta, IteratorCount) {
	SectorLayoutMeta* meta = SectorLayoutMeta::create<MetaTrivial1, MetaTrivial2, MetaTrivial3>();
	ASSERT_NE(meta, nullptr);
	EXPECT_EQ(layoutTypeCount(meta), 3u);
	delete meta;
}

TEST(SectorMeta, IteratorDereference) {
	SectorLayoutMeta* meta = SectorLayoutMeta::create<MetaTrivial1>();
	ASSERT_NE(meta, nullptr);
	const LayoutData& first = *meta->begin();
	EXPECT_EQ(first.offset, 0u);
	EXPECT_NE(first.isAliveMask, 0u);
	delete meta;
}

TEST(SectorMeta, TotalSizeSingleType) {
	SectorLayoutMeta* meta = SectorLayoutMeta::create<MetaTrivial1>();
	ASSERT_NE(meta, nullptr);
	EXPECT_GE(meta->getTotalSize(), sizeof(MetaTrivial1));
	delete meta;
}

TEST(SectorMeta, TotalSizeAligned) {
	SectorLayoutMeta* meta = SectorLayoutMeta::create<MetaTiny, MetaTrivial3>();
	ASSERT_NE(meta, nullptr);
	EXPECT_EQ(meta->getTotalSize() % alignof(MetaTrivial3), 0u);
	delete meta;
}

TEST(SectorMeta, TypeIdConsistent) {
	const ECSType a = DenseTypeIdGenerator::getTypeId<MetaTrivial1>();
	const ECSType b = DenseTypeIdGenerator::getTypeId<MetaTrivial1>();
	EXPECT_EQ(a, b);
}

TEST(SectorMeta, TypeIdUnique) {
	EXPECT_NE(
		DenseTypeIdGenerator::getTypeId<MetaTrivial1>(),
		DenseTypeIdGenerator::getTypeId<MetaTrivial2>());
}

TEST(SectorMeta, GlobalTypeIdConsistent) {
	const size_t a = GlobalTypeId<MetaTrivial1>();
	const size_t b = GlobalTypeId<MetaTrivial1>();
	EXPECT_EQ(a, b);
}

TEST(SectorMeta, GlobalTypeIdUnique) {
	EXPECT_NE(GlobalTypeId<MetaTrivial1>(), GlobalTypeId<MetaTrivial2>());
}

TEST(SectorMeta, IsNotAliveMaskComplement) {
	SectorLayoutMeta* meta = SectorLayoutMeta::create<MetaTrivial1>();
	ASSERT_NE(meta, nullptr);
	const LayoutData& ld = meta->getLayoutData<MetaTrivial1>();
	EXPECT_EQ(ld.isNotAliveMask, ~ld.isAliveMask);
	delete meta;
}
