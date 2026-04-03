#include <gtest/gtest.h>
#include <ecss/memory/SectorLayoutMeta.h>
#include <ecss/memory/Sector.h>
#include <string>
#include <cstring>
#include <cstdlib>

using namespace ecss;
using namespace ecss::Memory;
namespace S = Sector;

struct SOTrivial {
	int x = 0;
};
struct SOTrivial2 {
	float y = 0.f;
};
struct SONonTrivial {
	std::string s;
	SONonTrivial() = default;
	explicit SONonTrivial(std::string str) : s(std::move(str)) {}
	~SONonTrivial() = default;
	SONonTrivial(const SONonTrivial&) = default;
	SONonTrivial(SONonTrivial&&) = default;
	SONonTrivial& operator=(const SONonTrivial&) = default;
	SONonTrivial& operator=(SONonTrivial&&) = default;
};

template<typename... Ts>
struct SectorBuffer {
	SectorLayoutMeta* meta = SectorLayoutMeta::create<Ts...>();
	std::byte* data = static_cast<std::byte*>(std::calloc(1, meta->getTotalSize()));

	SectorBuffer() = default;

	~SectorBuffer() {
		std::free(data);
		delete meta;
	}

	SectorBuffer(const SectorBuffer&) = delete;
	SectorBuffer& operator=(const SectorBuffer&) = delete;
	SectorBuffer(SectorBuffer&&) = delete;
	SectorBuffer& operator=(SectorBuffer&&) = delete;
};

TEST(SectorOps, EmplaceMember_Fresh) {
	SectorBuffer<SOTrivial> sb;
	const LayoutData& l = sb.meta->getLayoutData<SOTrivial>();
	uint32_t alive = 0;
	S::emplaceMember<SOTrivial>(sb.data, alive, l, 123);
	EXPECT_TRUE(S::isAlive(alive, l.isAliveMask));
	auto* p = S::getMember<SOTrivial>(sb.data, l, alive);
	ASSERT_NE(p, nullptr);
	EXPECT_EQ(p->x, 123);
}

TEST(SectorOps, EmplaceMember_Overwrite) {
	SectorBuffer<SOTrivial> sb;
	const LayoutData& l = sb.meta->getLayoutData<SOTrivial>();
	uint32_t alive = 0;
	S::emplaceMember<SOTrivial>(sb.data, alive, l, 1);
	S::emplaceMember<SOTrivial>(sb.data, alive, l, 99);
	EXPECT_TRUE(S::isAlive(alive, l.isAliveMask));
	auto* p = S::getMember<SOTrivial>(sb.data, l, alive);
	ASSERT_NE(p, nullptr);
	EXPECT_EQ(p->x, 99);
}

TEST(SectorOps, DestroyMember_AliveToDead) {
	SectorBuffer<SOTrivial> sb;
	const LayoutData& l = sb.meta->getLayoutData<SOTrivial>();
	uint32_t alive = 0;
	S::emplaceMember<SOTrivial>(sb.data, alive, l, 7);
	S::destroyMember(sb.data, alive, l);
	EXPECT_FALSE(S::isAlive(alive, l.isAliveMask));
}

TEST(SectorOps, DestroyMember_AlreadyDead) {
	SectorBuffer<SOTrivial> sb;
	const LayoutData& l = sb.meta->getLayoutData<SOTrivial>();
	uint32_t alive = 0;
	S::destroyMember(sb.data, alive, l);
	EXPECT_FALSE(S::isAlive(alive, l.isAliveMask));
	S::destroyMember(sb.data, alive, l);
	EXPECT_FALSE(S::isAlive(alive, l.isAliveMask));
}

TEST(SectorOps, DestroySectorData_AllAlive) {
	SectorBuffer<SOTrivial, SOTrivial2> sb;
	const LayoutData& l0 = sb.meta->getLayoutData<SOTrivial>();
	const LayoutData& l1 = sb.meta->getLayoutData<SOTrivial2>();
	uint32_t alive = 0;
	S::emplaceMember<SOTrivial>(sb.data, alive, l0, 3);
	S::emplaceMember<SOTrivial2>(sb.data, alive, l1, 2.5f);
	S::destroySectorData(sb.data, alive, sb.meta);
	EXPECT_EQ(alive, 0u);
}

TEST(SectorOps, DestroySectorData_PartialAlive) {
	SectorBuffer<SOTrivial, SOTrivial2> sb;
	const LayoutData& l0 = sb.meta->getLayoutData<SOTrivial>();
	uint32_t alive = 0;
	S::emplaceMember<SOTrivial>(sb.data, alive, l0, 8);
	S::destroySectorData(sb.data, alive, sb.meta);
	EXPECT_EQ(alive, 0u);
}

TEST(SectorOps, DestroySectorData_AllDead) {
	SectorBuffer<SOTrivial> sb;
	uint32_t alive = 0;
	S::destroySectorData(sb.data, alive, sb.meta);
	EXPECT_EQ(alive, 0u);
}

TEST(SectorOps, CopyMember_Trivial) {
	SectorBuffer<SOTrivial> from;
	SectorBuffer<SOTrivial> to;
	const LayoutData& l = from.meta->getLayoutData<SOTrivial>();
	uint32_t fromAlive = 0;
	uint32_t toAlive = 0;
	S::emplaceMember<SOTrivial>(from.data, fromAlive, l, 42);
	const SOTrivial* src = S::getMember<SOTrivial>(from.data, l, fromAlive);
	ASSERT_NE(src, nullptr);
	S::copyMember<SOTrivial>(*src, to.data, toAlive, l);
	auto* dp = S::getMember<SOTrivial>(to.data, l, toAlive);
	ASSERT_NE(dp, nullptr);
	EXPECT_EQ(dp->x, 42);
	EXPECT_EQ(fromAlive & l.isAliveMask, l.isAliveMask);
}

TEST(SectorOps, CopyMember_NonTrivial) {
	SectorBuffer<SONonTrivial> from;
	SectorBuffer<SONonTrivial> to;
	const LayoutData& l = from.meta->getLayoutData<SONonTrivial>();
	uint32_t fromAlive = 0;
	uint32_t toAlive = 0;
	S::emplaceMember<SONonTrivial>(from.data, fromAlive, l, std::string("abc"));
	const SONonTrivial* src = S::getMember<SONonTrivial>(from.data, l, fromAlive);
	ASSERT_NE(src, nullptr);
	S::copyMember<SONonTrivial>(*src, to.data, toAlive, l);
	auto* dp = S::getMember<SONonTrivial>(to.data, l, toAlive);
	ASSERT_NE(dp, nullptr);
	EXPECT_EQ(dp->s, "abc");
	EXPECT_EQ(src->s, "abc");
}

TEST(SectorOps, MoveMember_Trivial) {
	SectorBuffer<SOTrivial> from;
	SectorBuffer<SOTrivial> to;
	const LayoutData& l = from.meta->getLayoutData<SOTrivial>();
	uint32_t fromAlive = 0;
	uint32_t toAlive = 0;
	S::emplaceMember<SOTrivial>(from.data, fromAlive, l, 77);
	SOTrivial* src = S::getMember<SOTrivial>(from.data, l, fromAlive);
	ASSERT_NE(src, nullptr);
	S::moveMember(std::move(*src), to.data, toAlive, l);
	auto* dp = S::getMember<SOTrivial>(to.data, l, toAlive);
	ASSERT_NE(dp, nullptr);
	EXPECT_EQ(dp->x, 77);
}

TEST(SectorOps, MoveMember_NonTrivial) {
	SectorBuffer<SONonTrivial> srcBuf;
	SectorBuffer<SONonTrivial> dstBuf;
	const LayoutData& l = srcBuf.meta->getLayoutData<SONonTrivial>();
	uint32_t srcAlive = 0;
	uint32_t dstAlive = 0;
	S::emplaceMember<SONonTrivial>(srcBuf.data, srcAlive, l, std::string("moved"));
	SONonTrivial* srcPtr = S::getMember<SONonTrivial>(srcBuf.data, l, srcAlive);
	ASSERT_NE(srcPtr, nullptr);
	S::moveMember(std::move(*srcPtr), dstBuf.data, dstAlive, l);
	S::destroyMember(srcBuf.data, srcAlive, l);
	auto* dp = S::getMember<SONonTrivial>(dstBuf.data, l, dstAlive);
	ASSERT_NE(dp, nullptr);
	EXPECT_EQ(dp->s, "moved");
	EXPECT_FALSE(S::isAlive(srcAlive, l.isAliveMask));
}

TEST(SectorOps, CopySectorData_TwoTypes) {
	SectorBuffer<SOTrivial, SOTrivial2> from;
	SectorBuffer<SOTrivial, SOTrivial2> to;
	const LayoutData& l0 = from.meta->getLayoutData<SOTrivial>();
	const LayoutData& l1 = from.meta->getLayoutData<SOTrivial2>();
	uint32_t fromAlive = 0;
	uint32_t toAlive = 0;
	S::emplaceMember<SOTrivial>(from.data, fromAlive, l0, 11);
	S::emplaceMember<SOTrivial2>(from.data, fromAlive, l1, 3.25f);
	S::copySectorData(from.data, fromAlive, to.data, toAlive, from.meta);
	EXPECT_EQ(toAlive, fromAlive);
	auto* p0 = S::getMember<SOTrivial>(to.data, l0, toAlive);
	auto* p1 = S::getMember<SOTrivial2>(to.data, l1, toAlive);
	ASSERT_NE(p0, nullptr);
	ASSERT_NE(p1, nullptr);
	EXPECT_EQ(p0->x, 11);
	EXPECT_FLOAT_EQ(p1->y, 3.25f);
	EXPECT_EQ(S::getMember<SOTrivial>(from.data, l0, fromAlive)->x, 11);
	EXPECT_FLOAT_EQ(S::getMember<SOTrivial2>(from.data, l1, fromAlive)->y, 3.25f);
}

TEST(SectorOps, CopySectorData_PartialAlive) {
	SectorBuffer<SOTrivial, SOTrivial2> from;
	SectorBuffer<SOTrivial, SOTrivial2> to;
	const LayoutData& l0 = from.meta->getLayoutData<SOTrivial>();
	const LayoutData& l1 = from.meta->getLayoutData<SOTrivial2>();
	uint32_t fromAlive = 0;
	uint32_t toAlive = 0;
	S::emplaceMember<SOTrivial>(from.data, fromAlive, l0, 5);
	S::copySectorData(from.data, fromAlive, to.data, toAlive, from.meta);
	EXPECT_EQ(toAlive, fromAlive);
	EXPECT_TRUE(S::isAlive(toAlive, l0.isAliveMask));
	EXPECT_FALSE(S::isAlive(toAlive, l1.isAliveMask));
	ASSERT_NE(S::getMember<SOTrivial>(to.data, l0, toAlive), nullptr);
	EXPECT_EQ(S::getMember<SOTrivial>(to.data, l0, toAlive)->x, 5);
	EXPECT_EQ(S::getMember<SOTrivial2>(to.data, l1, toAlive), nullptr);
}

TEST(SectorOps, MoveSectorData_TwoTypes) {
	SectorBuffer<SOTrivial, SOTrivial2> from;
	SectorBuffer<SOTrivial, SOTrivial2> to;
	const LayoutData& l0 = from.meta->getLayoutData<SOTrivial>();
	const LayoutData& l1 = from.meta->getLayoutData<SOTrivial2>();
	uint32_t fromAlive = 0;
	uint32_t toAlive = 0;
	S::emplaceMember<SOTrivial>(from.data, fromAlive, l0, 100);
	S::emplaceMember<SOTrivial2>(from.data, fromAlive, l1, 1.5f);
	const uint32_t before = fromAlive;
	S::moveSectorData(from.data, fromAlive, to.data, toAlive, from.meta);
	EXPECT_EQ(toAlive, before);
	EXPECT_EQ(fromAlive, 0u);
	ASSERT_NE(S::getMember<SOTrivial>(to.data, l0, toAlive), nullptr);
	ASSERT_NE(S::getMember<SOTrivial2>(to.data, l1, toAlive), nullptr);
	EXPECT_EQ(S::getMember<SOTrivial>(to.data, l0, toAlive)->x, 100);
	EXPECT_FLOAT_EQ(S::getMember<SOTrivial2>(to.data, l1, toAlive)->y, 1.5f);
}

TEST(SectorOps, IsAlive_Bitmask) {
	const uint32_t bit1 = 1u << 3;
	const uint32_t bit2 = 1u << 5;
	const uint32_t isAliveData = bit1;
	EXPECT_TRUE(S::isAlive(isAliveData, bit1));
	EXPECT_FALSE(S::isAlive(isAliveData, bit2));
}

TEST(SectorOps, IsSectorAlive_Zero) {
	EXPECT_FALSE(S::isSectorAlive(0));
}

TEST(SectorOps, IsSectorAlive_NonZero) {
	EXPECT_TRUE(S::isSectorAlive(1));
	EXPECT_TRUE(S::isSectorAlive(0xFFu));
}

TEST(SectorOps, MarkAlive_SetsBit) {
	const uint32_t mask = 1u << 4;
	uint32_t isAliveData = 0;
	S::markAlive(isAliveData, mask);
	EXPECT_TRUE(S::isAlive(isAliveData, mask));
	EXPECT_EQ(isAliveData & ~mask, 0u);
}

TEST(SectorOps, MarkNotAlive_ClearsBit) {
	const uint32_t mask = 1u << 2;
	uint32_t isAliveData = 0xFFu;
	S::markNotAlive(isAliveData, ~mask);
	EXPECT_FALSE(S::isAlive(isAliveData, mask));
	EXPECT_EQ(isAliveData, 0xFFu & ~mask);
}

TEST(SectorOps, GetMember_ReturnsNull_WhenDead) {
	SectorBuffer<SOTrivial> sb;
	const LayoutData& l = sb.meta->getLayoutData<SOTrivial>();
	EXPECT_EQ(S::getMember<SOTrivial>(sb.data, l, 0u), nullptr);
}
