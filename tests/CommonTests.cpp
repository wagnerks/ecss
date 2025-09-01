#include <atomic>
#include <future>
#include <random>
#include <thread>
#include <unordered_set>
#include <chrono>

#include <ecss/Registry.h>
#include <ecss/memory/SectorsArray.h>
#include <ecss/memory/Sector.h>


#include <gtest/gtest.h>

using namespace ecss;
using namespace ecss::Memory;

namespace CommonTests {

    // Примитивные компоненты
    struct Pos { int x; };
    struct Vel { float v; };
    struct Tag { char c; };

    // Хелпер на создание массива с заданными типами и малыми чанками
    template<size_t chunk, typename... Ts>
    ecss::Memory::SectorsArray<true, ChunksAllocator<chunk>>* MakeArray(uint32_t capacity = 0) {
        // chunk — это "ёмкость чанка в секторах", выбираем маленький, чтобы насильно бегать по границам чанков
        auto arr = ecss::Memory::SectorsArray<true, ChunksAllocator<chunk>>::template create<Ts...>();
        arr->reserve(capacity);
        return arr;
    }

    // Возвращает все id из обычного итератора
    template<typename T>
    static std::vector<SectorId> CollectIds(ecss::Memory::SectorsArray<true, T>* arr) {
        std::vector<SectorId> out;
        for (auto it = arr->begin(); it != arr->end(); ++it) {
            out.push_back((*it)->id);
        }
        return out;
    }

    // Возвращает все id из IteratorAlive<T>
    template<typename T, typename Alloc>
    static std::vector<SectorId> CollectAliveIds(ecss::Memory::SectorsArray<true, Alloc>* arr) {
        std::vector<SectorId> out;
        for (auto it = arr->template beginAlive<T>(); it != arr->endAlive(); ++it) {
            out.push_back((*it)->id);
        }
        return out;
    }

    // ---------- БАЗОВОЕ СОЗДАНИЕ И ЛЭЙАУТ ----------
    TEST(SectorsArray_Basics, CreateInsertOrderAndLookup) {
        auto* arr = MakeArray<4, Pos, Vel>(/*capacity*/0);

        // Вставки (не по порядку)
        arr->insert<Pos>(5, Pos{ 50 });
        arr->insert<Vel>(1, Vel{ 1.0f });
        arr->insert<Pos>(3, Pos{ 30 });
        arr->insert<Vel>(2, Vel{ 2.0f });
        arr->insert<Pos>(4, Pos{ 40 });

        // Упорядоченность по id
        auto ids = CollectIds(arr);
        ASSERT_TRUE(std::is_sorted(ids.begin(), ids.end()));

        // Найти сектора по id
        {
            auto s3 = arr->findSector(3);
            ASSERT_NE(s3, nullptr);
            EXPECT_EQ(s3->id, 3u);
            EXPECT_TRUE(s3->isSectorAlive()); // хотя бы один компонент должен быть alive
        }

        // Повторная вставка в существующий сектор не должна раздувать размер
        auto sizeBefore = arr->size();
        arr->insert<Pos>(3, Pos{ 300 });
        auto sizeAfter = arr->size();
        EXPECT_EQ(sizeBefore, sizeAfter);

        delete arr;
    }

    // ---------- ПРОВЕРКИ ALIVE-ФЛАГОВ И ИТЕРАЦИИ ----------
    TEST(SectorsArray_Alive, IteratorAliveFilters) {
        auto* arr = MakeArray<4, Pos, Vel>(0);

        // Добавим 10 секторов. Отметим alive для Pos только на чётных id.
        for (SectorId id = 0; id < 10; ++id) {
            arr->insert<Vel>(id, Vel{ float(id) });
            if ((id % 2) == 0) {
                arr->insert<Pos>(id, Pos{ int(id) });
            }
        }
        auto alive = CollectAliveIds<Pos>(arr);
        ASSERT_EQ(alive.size(), 5u);
        for (auto i = 0u; i < alive.size(); ++i) {
            EXPECT_EQ(alive[i] % 2, 0u);
        }

        // Снять alive у пары чётных — итератор должен пропустить
        arr->insert<Pos>(2, {}); // не меняет alive
        {
            auto s2 = arr->findSector(2);
            s2->destroyMember(arr->getLayoutData<Pos>());
        }
        {
            auto s8 = arr->findSector(8);
            s8->destroyMember(arr->getLayoutData<Pos>());
        }
        alive = CollectAliveIds<Pos>(arr);
        std::unordered_set<SectorId> got{ alive.begin(), alive.end() };
        EXPECT_FALSE(got.contains(2));
        EXPECT_FALSE(got.contains(8));

        delete arr;
    }

    // ---------- ВСТАВКИ В КОНЕЦ И В СЕРЕДИНУ ----------
    TEST(SectorsArray_Insert, AppendAndMiddleInsertShifts) {
        auto* arr = MakeArray<3, Pos>(0);

        // Добавления по возрастанию (быстрый путь)
        for (SectorId id = 0; id < 6; ++id) {
            arr->insert<Pos>(id, Pos{ int(id) });
        }
        auto ids = CollectIds(arr);
        ASSERT_EQ(ids.size(), 6u);
        ASSERT_TRUE(std::is_sorted(ids.begin(), ids.end()));

        // Вставка в середину: 2.5 (условно — реальный id 2 не существует, вставим id=7, а потом id=4 чтобы произошёл сдвиг)
        arr->insert<Pos>(7, Pos{ 70 });
        arr->insert<Pos>(4, Pos{ 40 }); // должен вызвать shiftDataRight
        ids = CollectIds(arr);
        ASSERT_TRUE(std::is_sorted(ids.begin(), ids.end()));

        delete arr;
    }

    // ---------- ERASE/SHIFT ----------
    TEST(SectorsArray_Erase, EraseBeginMiddleEndAndNoShift) {
        
        auto* arr = MakeArray<3, Pos>(0);
        for (SectorId id = 0; id < 7; ++id) {
            arr->insert<Pos>(id, Pos{ int(id) });
        }
        ASSERT_EQ(arr->size(), 7u);

        // Удаление из начала (со сдвигом)
        arr->erase(0, 1, /*shift*/ true);
        auto ids = CollectIds(arr);
        ASSERT_EQ(ids.size(), 6u);
        EXPECT_EQ(ids.front(), 1u);

        // Удаление из середины (со сдвигом)
        arr->erase(2, 2, /*shift*/ true); // сотрём текущие индексы 2 и 3
        ids = CollectIds(arr);
        ASSERT_EQ(ids.size(), 4u);

        // Удаление из конца без сдвига (оставим "дыру" для последующей дефрагментации)
        const auto sizeBefore = arr->size();
        arr->erase(ids.size() - 1, 1, /*shift*/false);
        EXPECT_EQ(arr->size(), sizeBefore); // размер не уменьшился
        // запускаем defragment
        arr->defragment();
        EXPECT_LT(arr->size(), sizeBefore);

        delete arr;
    }

    // ---------- DEFRAGMENT ----------
    TEST(SectorsArray_Defrag, RemoveDeadAndCompact) {
        
        auto* arr = MakeArray<4, Pos, Vel>(0);

        // 0..9, метим часть как dead для Pos и Vel по-разному
        for (SectorId id = 0; id < 10; ++id) {
            arr->insert<Pos>(id, Pos{ int(id) });
            if ((id % 3) != 0) {
                arr->insert<Vel>(id, Vel{ float(id) });
            }
        }
        // Помечаем not alive для нескольких
        for (SectorId id : {1u, 4u, 8u}) {
            auto s = arr->findSector(id);
            s->destroyMember(arr->getLayoutData<Pos>());
            s->destroyMember(arr->getLayoutData<Vel>());
        }
        const auto sizeBefore = arr->size();
        arr->defragment();
        const auto sizeAfter = arr->size();
        EXPECT_LT(sizeAfter, sizeBefore);

        // Порядок должен сохраниться (возрастающий id)
        auto ids = CollectIds(arr);
        EXPECT_TRUE(std::is_sorted(ids.begin(), ids.end()));

        // Нет dangling в SectorsMap для живых id
        for (auto id : ids) {
            auto s = arr->findSector(id);
            ASSERT_NE(s, nullptr);
            EXPECT_EQ(s->id, id);
        }

        delete arr;
    }

    // ---------- COPY/MOVE КОНТЕЙНЕРОВ ----------
    TEST(SectorsArray_CopyMove, CopyAndMoveSemanticsPreserveData) {
        
        auto* a = MakeArray<4, Pos, Vel>(0);

        for (SectorId id = 0; id < 8; ++id) {
            if (id & 1) a->insert<Pos>(id, Pos{ int(id) });
            if (!(id & 1)) a->insert<Vel>(id, Vel{ float(id) });
        }
        auto idsA = CollectIds(a);

        // Копия
        {
            auto b = *a;
            auto idsB = CollectIds(&b);
            EXPECT_EQ(idsA, idsB);


            // Муув
            auto c = std::move(b);
            auto idsC = CollectIds(&c);
            EXPECT_EQ(idsA, idsC);
        }

        delete a;
    }

    // ---------- ЧАНК-ГРАНИЦЫ ----------
    TEST(SectorsArray_Chunks, IteratorAcrossChunkBoundaries) {
        
        // chunk=3 → принудительно пересекаем границы
        auto* arr = MakeArray<3, Pos>(0);
        const int N = 17;
        for (int i = 0; i < N; ++i) {
            arr->insert<Pos>(i, Pos{ i });
        }

        // Идём обычным итератором — должны получить ровно N id по порядку
        auto ids = CollectIds(arr);
        ASSERT_EQ((int)ids.size(), N);
        for (int i = 0; i < N; ++i) {
            EXPECT_EQ(ids[i], (SectorId)i);
        }

        // Удалим несколько в середине, дефрагментируем — снова проверим
        arr->erase(4, 2, true); // сдвиг
        arr->erase(5, 1, false); // без сдвига
        arr->defragment();
        ids = CollectIds(arr);
        EXPECT_TRUE(std::is_sorted(ids.begin(), ids.end()));

        delete arr;
    }

    // ---------- RANGED ----------
    TEST(SectorsArray_Ranged, RangedAndRangedAliveIterators) {
        
        auto* arr = MakeArray<4, Pos, Vel>(0);
        for (SectorId id = 0; id < 20; ++id) {
            // Пусть Pos живёт у кратных 2, Vel у кратных 3
            if (id % 2 == 0) arr->insert<Pos>(id, Pos{ int(id) });
            if (id % 3 == 0) arr->insert<Vel>(id, Vel{ float(id) });
        }

        EntitiesRanges ranges;
        // допустим хотим [3..10) и [8..15)
        ranges.ranges.push_back({ 3, 10 });
        ranges.ranges.push_back({ 8, 15 });
        ranges.mergeIntersections();
        // Рейндж-итератор
        {
            std::vector<SectorId> got;
            auto it = arr->beginRanged(ranges);
            auto end = arr->endRanged(ranges);
            for (; it != end; ++it) got.push_back((*it)->id);
            ASSERT_FALSE(got.empty());
            EXPECT_TRUE(std::is_sorted(got.begin(), got.end()));
            EXPECT_GE(got.front(), 3u);
            EXPECT_LT(got.back(), 19u);
        }

        // RangedAlive по Pos (чётные только)
        {
            std::vector<SectorId> got;
            auto it = arr->beginRangedAlive<Pos>(ranges);
            auto end = arr->endRangedAlive(ranges);
            for (; it != end; ++it) got.push_back((*it)->id);
            for (auto id : got) {
                EXPECT_EQ(id % 2, 0u);
                EXPECT_GE(id, 3u);
                EXPECT_LT(id, 19u);
            }
        }

        delete arr;
    }

    // ---------- REGISTRY ----------
    TEST(Registry_API, AddHasGetDestroyComponents) {
        Registry reg;
        reg.registerArray<Pos, Vel>();

        // Выдать сущности и добавить компоненты
        std::vector<EntityId> ids;
        for (int i = 0; i < 10; ++i) {
            ids.push_back(reg.takeEntity());
        }

        for (auto id : ids) {
            reg.addComponent<Pos>(id, Pos{ int(id) });
            if (id % 3 == 0) reg.addComponent<Vel>(id, Vel{ float(id) });
        }

        for (auto id : ids) {
            EXPECT_TRUE(reg.hasComponent<Pos>(id));
            auto* p = reg.pinComponent<Pos>(id).get();
            ASSERT_NE(p, nullptr);
            EXPECT_EQ(p->x, (int)id);
            if (id % 3 == 0) {
                auto* v = reg.pinComponent<Vel>(id).get();
                ASSERT_NE(v, nullptr);
                EXPECT_FLOAT_EQ(v->v, float(id));
            }
            else {
                EXPECT_EQ(reg.pinComponent<Vel>(id).get(), nullptr);
            }
        }

        // Уничтожить компоненты, проверить отсутствие
        reg.destroyComponent<Pos>(ids[0]);
        EXPECT_FALSE(reg.hasComponent<Pos>(ids[0]));

        // Массовое удаление
        std::vector<EntityId> kill(ids.begin() + 1, ids.begin() + 5);
        reg.destroyComponent<Vel>(kill);
        for (auto id : kill) {
            EXPECT_EQ(reg.pinComponent<Vel>(id).get(), nullptr);
        }
    }

    // ---------- МНОГОПОТОЧНОСТЬ: ЧТЕНИЕ ----------
    TEST(SectorsArray_MT, ParallelReadIterators) {
        
        auto* arr = MakeArray<8, Pos, Vel>(0);

        const int N = 20000;
        for (int i = 0; i < N; ++i) {
            if (i & 1) arr->insert<Pos>(i, Pos{ i });
            if (!(i & 1)) arr->insert<Vel>(i, Vel{ float(i) });
        }

        // Параллельно считаем сумму id через обычный итератор (read-only)
        auto reader = [&]() -> uint64_t {
            uint64_t sum = 0;
            for (auto it = arr->begin(); it != arr->end(); ++it) {
                sum += arr->pinSector(*it)->id;
            }
            return sum;
        };

        auto f1 = std::async(std::launch::async, reader);
        auto f2 = std::async(std::launch::async, reader);
        auto f3 = std::async(std::launch::async, reader);

        const auto s1 = f1.get();
        const auto s2 = f2.get();
        const auto s3 = f3.get();
        EXPECT_EQ(s1, s2);
        EXPECT_EQ(s2, s3);

        delete arr;
    }

    // ---------- МНОГОПОТОЧНОСТЬ: ЧТЕНИЕ + БЕЗОПАСНЫЕ ИЗМЕНЕНИЯ ----------
    TEST(SectorsArray_MT, ConcurrentReadersWithOccasionalWriter) {
        
        auto* arr = MakeArray<8, Pos, Vel>(0);

        const int N = 5000;
        for (int i = 0; i < N; ++i) {
            arr->insert<Pos>(i, Pos{ i });
            if (i % 4 == 0) arr->insert<Vel>(i, Vel{ float(i) });
        }

        std::atomic<bool> stop;
        stop.store(false);

        // Читатели (shared lock)
        auto reader = [&]() {
            uint64_t total = 0;
            while (!stop.load(std::memory_order_relaxed)) {
                int count = 0;
                for (auto it = arr->begin(); it != arr->end() && count < 256; ++it, ++count) {
                    total += arr->pinSector(*it)->id;
                }
            }
            return total;
        };

        // Писатель (unique lock, изредка)
        auto writer = [&]() {
            std::mt19937 rng(123);
            std::uniform_int_distribution<int> dist(0, N - 1);
            for (int iter = 0; iter < 200; ++iter) {
                {
                    // Случайно “мигнём” Vel для id
                    const int id = dist(rng);
                    auto s = arr->pinSector(id);
                    if (s) {
                        // Тоггл: если жив – разрушим; если мертв – добавим
                        if (s->isAlive(arr->getLayoutData<Vel>().isAliveMask)) {
                            s->destroyMember(arr->getLayoutData<Vel>());
                        }
                        else {
                            Sector::emplaceMember<Vel>(s.get(), arr->getLayoutData<Vel>(), float(id));
                        }
                    }
                }
                std::this_thread::sleep_for(std::chrono::microseconds(200));
            }
            stop.store(true);
        };

        auto fW = std::async(std::launch::async, writer);
        auto fR1 = std::async(std::launch::async, reader);
        auto fR2 = std::async(std::launch::async, reader);
        auto fR3 = std::async(std::launch::async, reader);

        fW.get();
        (void)fR1.get();
        (void)fR2.get();
        (void)fR3.get();

        // Базовая проверка: контейнер валиден
        auto ids = CollectIds(arr);
        EXPECT_TRUE(std::is_sorted(ids.begin(), ids.end()));

        delete arr;
    }

    // ---------- Ranged + Registry view ----------
    TEST(Registry_ForEach, RangedAndPlain) {
        Registry reg;
        reg.registerArray<Pos, Vel>();

        std::vector<EntityId> ids;
        for (int i = 0; i < 100; ++i) {
            auto id = reg.takeEntity();
            ids.push_back(id);
            if (i % 2 == 0) reg.addComponent<Pos>(id, Pos{ i });
            if (i % 3 == 0) reg.addComponent<Vel>(id, Vel{ float(i) });
        }

        // view (plain)
        {
            auto entry = reg.view<Pos, Vel>();
            if (!entry.empty()) {
                auto seen = 0;
                for (auto it = entry.begin(); it != entry.end(); ++it) {
                    auto [eid, p, v] = *it;
                    // p может быть null (если сектор без Pos)
                    if (p) EXPECT_EQ(p->x, (int)eid);
                    if (v) EXPECT_FLOAT_EQ(v->v, float(eid));
                    ++seen;
                }
                EXPECT_GT(seen, 0u);
            }
        }

        // view (ranged)
        EntitiesRanges ranges;
        ranges.ranges.push_back({ 10, 25 });
        ranges.ranges.push_back({ 20, 35 });
        ranges.mergeIntersections();

        {
            auto entry = reg.view<Pos>(ranges);
            
            for (auto it = entry.begin(); it != entry.end(); ++it) {
                auto [eid, p] = *it;
                EXPECT_GE(eid, 10u);
                EXPECT_LT(eid, 35u);
                if (eid % 2 == 0) {
                    ASSERT_NE(p, nullptr);
                    EXPECT_EQ(p->x, (int)eid);
                }
                else {
                    EXPECT_EQ(p, nullptr);
                }
            }
            
        }
    }
}
