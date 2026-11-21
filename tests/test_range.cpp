#include <iostream>
#include <vector>
#include <algorithm>
#include <random>
#include "../core/Tree23.h"
#include "../core/HashTable.h"
#include "../lib/DynamicArray.h"

#define TEST(name) void test_##name()
#define ASSERT(expr) \
    do { if (!(expr)) { \
        std::cerr << "✗ ПРОВАЛЕН: " << #expr << " в " << __FILE__ << ":" << __LINE__ << std::endl; \
        exit(1); } } while (0)
#define RUN_TEST(name) \
    do { std::cout << "Запуск теста: " << #name << "... "; \
         test_##name(); std::cout << "✓ ПРОЙДЕН" << std::endl; } while (0)

static std::vector<int> to_vec(const DynamicArray<int>& a) {
    std::vector<int> v;
    for (size_t i = 0; i < a.size(); ++i) v.push_back(a[i]);
    return v;
}

static Tree23<int> make_tree(const std::vector<int>& values) {
    Tree23<int> t;
    for (int v : values) t.insert(v);
    return t;
}

TEST(range_empty_tree) {
    Tree23<int> t;
    auto r = t.range(0, 100);
    ASSERT(r.size() == 0);
}

TEST(range_full_cover_is_sorted) {
    std::vector<int> vals = {50, 30, 70, 20, 40, 60, 80, 10, 90};
    auto t = make_tree(vals);

    auto r = to_vec(t.range(0, 100));
    std::vector<int> expected = vals;
    std::sort(expected.begin(), expected.end());

    ASSERT(r == expected);
}

TEST(range_middle_subset) {
    std::vector<int> vals;
    for (int i = 1; i <= 20; ++i) vals.push_back(i);
    auto t = make_tree(vals);

    auto r = to_vec(t.range(7, 13));
    ASSERT(r == std::vector<int>({7, 8, 9, 10, 11, 12, 13}));
}

TEST(range_bounds_are_inclusive) {
    auto t = make_tree({10, 20, 30, 40, 50});
    ASSERT(to_vec(t.range(20, 40)) == std::vector<int>({20, 30, 40}));
    ASSERT(to_vec(t.range(20, 20)) == std::vector<int>({20}));
}

TEST(range_single_element_tree) {
    auto t = make_tree({42});
    ASSERT(to_vec(t.range(42, 42)) == std::vector<int>({42}));
    ASSERT(t.range(43, 100).size() == 0);
    ASSERT(t.range(0, 41).size() == 0);
}

TEST(range_outside_data) {
    auto t = make_tree({10, 20, 30});
    ASSERT(t.range(100, 200).size() == 0);
    ASSERT(t.range(-50, -1).size() == 0);
    ASSERT(t.range(11, 19).size() == 0);
}

TEST(range_lo_greater_than_hi) {
    auto t = make_tree({10, 20, 30, 40});
    ASSERT(t.range(30, 10).size() == 0);
}

TEST(range_random_oracle) {
    std::mt19937 rng(12345);
    std::vector<int> vals;
    for (int i = 0; i < 500; ++i) vals.push_back(static_cast<int>(rng() % 1000));
    std::sort(vals.begin(), vals.end());
    vals.erase(std::unique(vals.begin(), vals.end()), vals.end());

    std::vector<int> shuffled = vals;
    std::shuffle(shuffled.begin(), shuffled.end(), rng);
    auto t = make_tree(shuffled);

    for (int iter = 0; iter < 300; ++iter) {
        int lo = static_cast<int>(rng() % 1100) - 50;
        int hi = static_cast<int>(rng() % 1100) - 50;

        std::vector<int> expected;
        for (int v : vals) if (v >= lo && v <= hi) expected.push_back(v);

        auto actual = to_vec(t.range(lo, hi));
        ASSERT(actual == expected);
    }
}

TEST(set_replaces_existing_value) {
    HashTable<std::string, int> t;
    t.Add("a", 1);
    t.Set("a", 99);

    ASSERT(t.Get("a") == 99);
    ASSERT(t.GetCount() == 1);
}

TEST(set_inserts_new_key) {
    HashTable<std::string, int> t;
    t.Set("a", 1);

    ASSERT(t.ContainsKey("a"));
    ASSERT(t.Get("a") == 1);
    ASSERT(t.GetCount() == 1);
}

TEST(set_repeated_same_key) {
    HashTable<int, int> t;
    for (int i = 0; i < 100; ++i) t.Set(7, i);

    ASSERT(t.GetCount() == 1);
    ASSERT(t.Get(7) == 99);
}

TEST(set_with_list_value) {
    HashTable<int, DynamicArray<size_t>> t;

    for (size_t id = 0; id < 5; ++id) {
        if (t.ContainsKey(25)) {
            auto ids = t.Get(25);
            ids.push_back(id);
            t.Set(25, ids);
        } else {
            DynamicArray<size_t> ids;
            ids.push_back(id);
            t.Add(25, ids);
        }
    }

    auto ids = t.Get(25);
    ASSERT(ids.size() == 5);
    for (size_t i = 0; i < 5; ++i) ASSERT(ids[i] == i);
    ASSERT(t.GetCount() == 1);
}

TEST(set_triggers_rehash) {
    HashTable<int, int> t(4);
    for (int i = 0; i < 50; ++i) t.Set(i, i * 2);

    ASSERT(t.GetCount() == 50);
    ASSERT(t.GetCapacity() >= 50);
    for (int i = 0; i < 50; ++i) ASSERT(t.Get(i) == i * 2);
}

int main() {
    std::cout << "=== Тесты Tree23::range и HashTable::Set ===\n" << std::endl;

    RUN_TEST(range_empty_tree);
    RUN_TEST(range_full_cover_is_sorted);
    RUN_TEST(range_middle_subset);
    RUN_TEST(range_bounds_are_inclusive);
    RUN_TEST(range_single_element_tree);
    RUN_TEST(range_outside_data);
    RUN_TEST(range_lo_greater_than_hi);
    RUN_TEST(range_random_oracle);

    RUN_TEST(set_replaces_existing_value);
    RUN_TEST(set_inserts_new_key);
    RUN_TEST(set_repeated_same_key);
    RUN_TEST(set_with_list_value);
    RUN_TEST(set_triggers_rehash);

    std::cout << "\n=== Все тесты пройдены ===\n";
    return 0;
}