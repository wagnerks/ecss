#include <gtest/gtest.h>

#include <ecss/memory/SectorsArray.h>
#include <ecss/memory/Sector.h>
#include <string>
#include <vector>
#include <random>
#include <memory>

using namespace ecss;
using namespace ecss::Memory;

struct SAT {
	int v = 0;
};
struct SAT2 {
	float f = 0.f;
};
struct SAT3 {
	double d = 0.0;
};
struct SAStr {
	std::string s;
	SAStr() = default;
	explicit SAStr(std::string str) : s(std::move(str)) {}
	~SAStr() = default;
	SAStr(const SAStr&) = default;
	SAStr(SAStr&&) = default;
	SAStr& operator=(const SAStr&) = default;
	SAStr& operator=(SAStr&&) = default;
};

struct SAMoveOnly {
	int val;
	SAMoveOnly(int v = 0) : val(v) {}
	SAMoveOnly(SAMoveOnly&& o) noexcept : val(o.val) { o.val = -1; }
	SAMoveOnly& operator=(SAMoveOnly&& o) noexcept {
		val = o.val;
		o.val = -1;
		return *this;
	}
	SAMoveOnly(const SAMoveOnly&) = delete;
	SAMoveOnly& operator=(const SAMoveOnly&) = delete;
};

TEST(SAFull, CrossTemplateCopy_TStoNonTS) {
	std::unique_ptr<SectorsArray<true>> src(SectorsArray<true>::create<SAT>());
	src->emplace<SAT>(1, 42);
	std::unique_ptr<SectorsArray<false>> dst(SectorsArray<false>::create<SAT>());
	*dst = *src;
	std::byte* d = dst->findSectorData(1);
	ASSERT_NE(d, nullptr);
	EXPECT_EQ(Sector::getComponent<SAT>(d, dst->getIsAlive(1), dst->getLayout())->v, 42);
}

TEST(SAFull, CrossTemplateCopy_NonTStoTS) {
	std::unique_ptr<SectorsArray<false>> src(SectorsArray<false>::create<SAT>());
	src->emplace<SAT>(2, 99);
	std::unique_ptr<SectorsArray<true>> dst(SectorsArray<true>::create<SAT>());
	*dst = *src;
	std::byte* d = dst->findSectorData(2);
	ASSERT_NE(d, nullptr);
	EXPECT_EQ(Sector::getComponent<SAT>(d, dst->getIsAlive(2), dst->getLayout())->v, 99);
}

TEST(SAFull, CrossTemplateMove_TStoNonTS) {
	std::unique_ptr<SectorsArray<true>> src(SectorsArray<true>::create<SAT>());
	src->emplace<SAT>(3, 7);
	std::unique_ptr<SectorsArray<false>> dst(SectorsArray<false>::create<SAT>());
	*dst = std::move(*src);
	EXPECT_EQ(src->size(), 0u);
	EXPECT_EQ(dst->size(), 1u);
	std::byte* d = dst->findSectorData(3);
	ASSERT_NE(d, nullptr);
	EXPECT_EQ(Sector::getComponent<SAT>(d, dst->getIsAlive(3), dst->getLayout())->v, 7);
}

TEST(SAFull, NonTrivialLifecycle_InsertEraseDefrag) {
	std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<SAStr>());
	for (SectorId i = 0; i < 20; ++i) {
		arr->emplace<SAStr>(i, SAStr(std::string("s") + std::to_string(i)));
	}
	for (SectorId i = 0; i < 20; i += 2) {
		size_t idx = arr->findLinearIdx(i);
		ASSERT_NE(idx, INVALID_IDX);
		arr->erase(idx, 1, false);
	}
	arr->defragment();
	EXPECT_EQ(arr->size(), 10u);
	for (SectorId i = 1; i < 20; i += 2) {
		std::byte* d = arr->findSectorData(i);
		ASSERT_NE(d, nullptr);
		const SAStr* p = Sector::getComponent<SAStr>(d, arr->getIsAlive(i), arr->getLayout());
		ASSERT_NE(p, nullptr);
		EXPECT_EQ(p->s, std::string("s") + std::to_string(i));
	}
}

TEST(SAFull, NonTrivialLifecycle_CopyArray) {
	std::unique_ptr<SectorsArray<false>> a(SectorsArray<false>::create<SAStr>());
	a->emplace<SAStr>(10, SAStr("hello"));
	a->emplace<SAStr>(20, SAStr("world"));
	std::unique_ptr<SectorsArray<false>> b(SectorsArray<false>::create<SAStr>());
	*b = *a;
	for (SectorId id : {SectorId{10}, SectorId{20}}) {
		std::byte* da = a->findSectorData(id);
		std::byte* db = b->findSectorData(id);
		ASSERT_NE(da, nullptr);
		ASSERT_NE(db, nullptr);
		EXPECT_EQ(
			Sector::getComponent<SAStr>(da, a->getIsAlive(id), a->getLayout())->s,
			Sector::getComponent<SAStr>(db, b->getIsAlive(id), b->getLayout())->s);
	}
}

TEST(SAFull, Grouped3Types_AllPresent) {
	std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<SAT, SAT2, SAT3>());
	const SectorId id = 100;
	arr->emplace<SAT>(id, 1);
	arr->emplace<SAT2>(id, 2.5f);
	arr->emplace<SAT3>(id, 3.25);
	std::byte* d = arr->findSectorData(id);
	ASSERT_NE(d, nullptr);
	uint32_t alive = arr->getIsAlive(id);
	EXPECT_EQ(Sector::getMember<SAT>(d, arr->getLayoutData<SAT>(), alive)->v, 1);
	EXPECT_EQ(Sector::getMember<SAT2>(d, arr->getLayoutData<SAT2>(), alive)->f, 2.5f);
	EXPECT_EQ(Sector::getMember<SAT3>(d, arr->getLayoutData<SAT3>(), alive)->d, 3.25);
}

TEST(SAFull, Grouped3Types_PartialPresent) {
	std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<SAT, SAT2, SAT3>());
	const SectorId id = 101;
	arr->emplace<SAT>(id, 11);
	arr->emplace<SAT3>(id, 9.0);
	std::byte* d = arr->findSectorData(id);
	ASSERT_NE(d, nullptr);
	uint32_t alive = arr->getIsAlive(id);
	EXPECT_NE(Sector::getMember<SAT>(d, arr->getLayoutData<SAT>(), alive), nullptr);
	EXPECT_EQ(Sector::getMember<SAT2>(d, arr->getLayoutData<SAT2>(), alive), nullptr);
	EXPECT_NE(Sector::getMember<SAT3>(d, arr->getLayoutData<SAT3>(), alive), nullptr);
}

TEST(SAFull, Grouped3Types_Defragment) {
	std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<SAT, SAT2, SAT3>());
	for (SectorId i = 0; i < 8; ++i) {
		arr->emplace<SAT>(i, static_cast<int>(i));
		arr->emplace<SAT2>(i, static_cast<float>(i));
		arr->emplace<SAT3>(i, static_cast<double>(i));
	}
	for (SectorId i = 0; i < 8; i += 2) {
		size_t idx = arr->findLinearIdx(i);
		arr->erase(idx, 1, false);
	}
	arr->defragment();
	EXPECT_EQ(arr->size(), 4u);
	std::vector<SectorId> seen;
	for (auto it = arr->begin(), en = arr->end(); it != en; ++it) {
		if (Sector::isSectorAlive((*it).isAlive)) {
			seen.push_back((*it).id);
		}
	}
	EXPECT_EQ(seen, (std::vector<SectorId>{1, 3, 5, 7}));
	for (SectorId id : seen) {
		std::byte* d = arr->findSectorData(id);
		uint32_t al = arr->getIsAlive(id);
		EXPECT_EQ(Sector::getMember<SAT>(d, arr->getLayoutData<SAT>(), al)->v, static_cast<int>(id));
	}
}

TEST(SAFull, IteratorAlive_SkipsDead) {
	std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<SAT>());
	for (SectorId i = 0; i < 10; ++i) {
		arr->emplace<SAT>(i, static_cast<int>(i));
	}
	for (SectorId i = 0; i < 3; ++i) {
		size_t idx = arr->findLinearIdx(i);
		arr->erase(idx, 1, false);
	}
	size_t n = 0;
	for (auto it = arr->template beginAlive<SAT>(), en = arr->endAlive(); it != en; ++it) {
		++n;
	}
	EXPECT_EQ(n, 7u);
}

TEST(SAFull, IteratorAlive_AllDead) {
	std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<SAT>());
	for (SectorId i = 0; i < 5; ++i) {
		arr->emplace<SAT>(i, static_cast<int>(i));
	}
	for (SectorId i = 0; i < 5; ++i) {
		size_t idx = arr->findLinearIdx(i);
		arr->erase(idx, 1, false);
	}
	size_t n = 0;
	for (auto it = arr->template beginAlive<SAT>(), en = arr->endAlive(); it != en; ++it) {
		++n;
	}
	EXPECT_EQ(n, 0u);
}

TEST(SAFull, IteratorAlive_AllAlive) {
	std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<SAT>());
	for (SectorId i = 0; i < 5; ++i) {
		arr->emplace<SAT>(i, static_cast<int>(i));
	}
	size_t n = 0;
	for (auto it = arr->template beginAlive<SAT>(), en = arr->endAlive(); it != en; ++it) {
		++n;
	}
	EXPECT_EQ(n, 5u);
}

TEST(SAFull, RangedIterator_SingleRange) {
	std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<SAT>());
	for (SectorId i = 0; i < 20; ++i) {
		arr->emplace<SAT>(i, static_cast<int>(i));
	}
	Ranges<SectorId> r(std::pair<SectorId, SectorId>{5, 15});
	size_t n = 0;
	for (auto it = arr->beginRanged(r), en = arr->endRanged(); it != en; ++it) {
		++n;
	}
	EXPECT_EQ(n, 10u);
}

TEST(SAFull, RangedIterator_MultipleRanges) {
	std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<SAT>());
	for (SectorId i = 0; i < 30; ++i) {
		arr->emplace<SAT>(i, static_cast<int>(i));
	}
	Ranges<SectorId> r(std::vector<std::pair<SectorId, SectorId>>{{0, 5}, {20, 25}});
	size_t n = 0;
	for (auto it = arr->beginRanged(r), en = arr->endRanged(); it != en; ++it) {
		++n;
	}
	EXPECT_EQ(n, 10u);
}

TEST(SAFull, RangedIterator_PastEnd) {
	std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<SAT>());
	for (SectorId i = 0; i < 10; ++i) {
		arr->emplace<SAT>(i, static_cast<int>(i));
	}
	Ranges<SectorId> r(std::pair<SectorId, SectorId>{0, 100});
	size_t n = 0;
	for (auto it = arr->beginRanged(r), en = arr->endRanged(); it != en; ++it) {
		++n;
	}
	EXPECT_EQ(n, 10u);
}

TEST(SAFull, RangedIterator_Empty) {
	std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<SAT>());
	for (SectorId i = 0; i < 10; ++i) {
		arr->emplace<SAT>(i, static_cast<int>(i));
	}
	Ranges<SectorId> r;
	size_t n = 0;
	for (auto it = arr->beginRanged(r), en = arr->endRanged(); it != en; ++it) {
		++n;
	}
	EXPECT_EQ(n, 0u);
}

TEST(SAFull, IsPacked_AfterInserts) {
	std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<SAT>());
	for (SectorId i = 0; i < 10; ++i) {
		arr->emplace<SAT>(i, static_cast<int>(i));
	}
	EXPECT_TRUE(arr->isPacked());
}

TEST(SAFull, IsPacked_AfterErase) {
	std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<SAT>());
	for (SectorId i = 0; i < 10; ++i) {
		arr->emplace<SAT>(i, static_cast<int>(i));
	}
	arr->erase(0, 1, false);
	EXPECT_FALSE(arr->isPacked());
}

TEST(SAFull, IsPacked_AfterDefragment) {
	std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<SAT>());
	for (SectorId i = 0; i < 10; ++i) {
		arr->emplace<SAT>(i, static_cast<int>(i));
	}
	arr->erase(0, 1, false);
	arr->defragment();
	EXPECT_TRUE(arr->isPacked());
}

TEST(SAFull, NeedDefragment_BelowThreshold) {
	std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<SAT>());
	for (SectorId i = 0; i < 100; ++i) {
		arr->emplace<SAT>(i, static_cast<int>(i));
	}
	arr->erase(0, 1, false);
	EXPECT_FALSE(arr->needDefragment());
}

TEST(SAFull, NeedDefragment_AboveThreshold) {
	std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<SAT>());
	for (SectorId i = 0; i < 10; ++i) {
		arr->emplace<SAT>(i, static_cast<int>(i));
	}
	for (size_t k = 0; k < 5; ++k) {
		size_t idx = arr->findLinearIdx(static_cast<SectorId>(k));
		arr->erase(idx, 1, false);
	}
	EXPECT_TRUE(arr->needDefragment());
}

TEST(SAFull, SetDefragmentThreshold) {
	std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<SAT>());
	arr->setDefragmentThreshold(0.01f);
	for (SectorId i = 0; i < 100; ++i) {
		arr->emplace<SAT>(i, static_cast<int>(i));
	}
	arr->erase(0, 1, false);
	arr->erase(1, 1, false);
	EXPECT_TRUE(arr->needDefragment());
}

TEST(SAFull, ProcessPendingErases_TS) {
	std::unique_ptr<SectorsArray<true>> arr(SectorsArray<true>::create<SAT>());
	arr->emplace<SAT>(5, 123);
	{
		auto pin = arr->pinSector(5);
		arr->eraseAsync(5);
	}
	arr->processPendingErases(true);
	EXPECT_FALSE(arr->containsSector(5));
}

TEST(SAFull, SparseConsistency_ReinsertSameId) {
	std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<SAT>());
	arr->emplace<SAT>(5, 1);
	size_t idx = arr->findLinearIdx(5);
	arr->erase(idx, 1, true);
	arr->emplace<SAT>(5, 2);
	EXPECT_TRUE(arr->findSlot(5).isValid());
	EXPECT_EQ(Sector::getComponent<SAT>(arr->findSectorData(5), arr->getIsAlive(5), arr->getLayout())->v, 2);
}

TEST(SAFull, ReserveThenInsert) {
	std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<SAT>());
	arr->reserve(1000);
	const size_t capAfterReserve = arr->capacity();
	for (SectorId i = 0; i < 500; ++i) {
		arr->emplace<SAT>(i, static_cast<int>(i));
	}
	EXPECT_EQ(arr->capacity(), capAfterReserve);
}

TEST(SAFull, EraseRange_FirstN) {
	std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<SAT>());
	for (SectorId i = 0; i < 10; ++i) {
		arr->emplace<SAT>(i, static_cast<int>(i));
	}
	arr->erase(0, 3, true);
	EXPECT_EQ(arr->size(), 7u);
	EXPECT_EQ(arr->getId(0), 3u);
}

TEST(SAFull, EraseRange_LastN) {
	std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<SAT>());
	for (SectorId i = 0; i < 10; ++i) {
		arr->emplace<SAT>(i, static_cast<int>(i));
	}
	arr->erase(7, 3, true);
	EXPECT_EQ(arr->size(), 7u);
}

TEST(SAFull, EraseRange_All) {
	std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<SAT>());
	for (SectorId i = 0; i < 5; ++i) {
		arr->emplace<SAT>(i, static_cast<int>(i));
	}
	arr->erase(0, 5, true);
	EXPECT_EQ(arr->size(), 0u);
}

TEST(SAFull, InsertMiddle_OrderPreserved) {
	std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<SAT>());
	for (SectorId id : {SectorId{0}, SectorId{10}, SectorId{20}}) {
		arr->emplace<SAT>(id, static_cast<int>(id));
	}
	arr->emplace<SAT>(5, 5);
	arr->emplace<SAT>(15, 15);
	std::vector<SectorId> order;
	for (auto it = arr->begin(), en = arr->end(); it != en; ++it) {
		order.push_back((*it).id);
	}
	EXPECT_EQ(order, (std::vector<SectorId>{0, 5, 10, 15, 20}));
}

TEST(SAFull, LargeScale_100K) {
	std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<SAT>());
	for (SectorId i = 0; i < 100000; ++i) {
		arr->emplace<SAT>(i, static_cast<int>(i));
	}
	EXPECT_EQ(arr->size(), 100000u);
	std::vector<SectorId> perm(100000);
	for (SectorId i = 0; i < 100000; ++i) {
		perm[i] = i;
	}
	std::mt19937 gen(12345u);
	for (size_t k = 0; k < perm.size(); ++k) {
		std::uniform_int_distribution<size_t> dist(k, perm.size() - 1);
		size_t j = dist(gen);
		SectorId tmp = perm[k];
		perm[k] = perm[j];
		perm[j] = tmp;
	}
	for (size_t k = 0; k < 50000; ++k) {
		SectorId id = perm[k];
		size_t idx = arr->findLinearIdx(id);
		ASSERT_NE(idx, INVALID_IDX);
		arr->erase(idx, 1, false);
	}
	arr->defragment();
	EXPECT_EQ(arr->size(), 50000u);
}

TEST(SAFull, MoveOnlyType) {
	std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<SAMoveOnly>());
	arr->emplace<SAMoveOnly>(0, SAMoveOnly(42));
	EXPECT_EQ(Sector::getComponent<SAMoveOnly>(arr->findSectorData(0), arr->getIsAlive(0), arr->getLayout())->val, 42);
}

TEST(SAFull, EmptyArrayClear) {
	std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<SAT>());
	arr->clear();
	EXPECT_EQ(arr->size(), 0u);
}

TEST(SAFull, EmptyArrayShrinkToFit) {
	std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<SAT>());
	arr->shrinkToFit();
	EXPECT_EQ(arr->size(), 0u);
}

TEST(SAFull, SizeAfterOperations) {
	std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<SAT>());
	for (SectorId i = 0; i < 5; ++i) {
		arr->emplace<SAT>(i, static_cast<int>(i));
	}
	EXPECT_EQ(arr->size(), 5u);
	arr->erase(0, 1, true);
	EXPECT_EQ(arr->size(), 4u);
	arr->clear();
	EXPECT_EQ(arr->size(), 0u);
}

TEST(SAFull, FindSlot_InvalidId) {
	std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<SAT>());
	arr->emplace<SAT>(0, 1);
	EXPECT_FALSE(arr->findSlot(999).isValid());
}

TEST(SAFull, ContainsSector_TrueAndFalse) {
	std::unique_ptr<SectorsArray<false>> arr(SectorsArray<false>::create<SAT>());
	arr->emplace<SAT>(5, 1);
	EXPECT_TRUE(arr->containsSector(5));
	EXPECT_FALSE(arr->containsSector(6));
}
