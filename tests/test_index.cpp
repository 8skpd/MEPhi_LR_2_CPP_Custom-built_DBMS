#include <iostream>
#include <vector>
#include <algorithm>
#include <random>
#include "../core/Value.h"
#include "../core/Row.h"
#include "../core/Index.h"
#include "../core/DbExceptions.h"

#define TEST(name) void test_##name()
#define ASSERT(expr) \
    do { if (!(expr)) { \
        std::cerr << "✗ ПРОВАЛЕН: " << #expr << " в " << __FILE__ << ":" << __LINE__ << std::endl; \
        exit(1); } } while (0)
#define RUN_TEST(name) \
    do { std::cout << "Запуск теста: " << #name << "... "; \
         test_##name(); std::cout << "✓ ПРОЙДЕН" << std::endl; } while (0)

static std::vector<size_t> sorted_ids(const DynamicArray<size_t>& a) {
    std::vector<size_t> v;
    for (size_t i = 0; i < a.size(); ++i) v.push_back(a[i]);
    std::sort(v.begin(), v.end());
    return v;
}

// Value

TEST(value_type_checks) {
    ASSERT(Value(42).is_int());
    ASSERT(Value(3.14).is_double());
    ASSERT(Value("abc").is_string());
    ASSERT(!Value(42).is_string());
}

TEST(value_equality_and_order) {
    ASSERT(Value(10) == Value(10));
    ASSERT(!(Value(10) == Value(11)));
    ASSERT(Value(10) < Value(11));
    ASSERT(!(Value(11) < Value(10)));

    ASSERT(Value("abc") == Value("abc"));
    ASSERT(Value("abc") < Value("abd"));
}

TEST(value_cross_type_equality_is_false) {
    ASSERT(!(Value(1) == Value(1.0)));
    ASSERT(!(Value(1) == Value("1")));
}

TEST(value_cross_type_order_throws) {
    bool caught = false;
    try { bool r = Value(1) < Value("1"); (void)r; }
    catch (const db::TypeMismatch&) { caught = true; }
    ASSERT(caught);
}

TEST(value_to_string) {
    ASSERT(Value(42).to_string() == "42");
    ASSERT(Value("hello").to_string() == "hello");
}

TEST(value_hash_consistency) {
    std::hash<Value> h;
    ASSERT(h(Value(42)) == h(Value(42)));
    ASSERT(h(Value("abc")) == h(Value("abc")));
    Value a(7), b(7);
    ASSERT((a == b) && (h(a) == h(b)));
}

TEST(value_relational_operators) {
    ASSERT(Value(11) > Value(10));
    ASSERT(Value(10) <= Value(10));
    ASSERT(Value(11) >= Value(10));

    bool caught = false;
    try { bool r = Value(1) > Value("1"); (void)r; }
    catch (const db::TypeMismatch&) { caught = true; }
    ASSERT(caught);
}

// Schema / Row

TEST(schema_columns) {
    Schema s;
    s.add_column({"id",   ColumnType::Int,    true});
    s.add_column({"name", ColumnType::String, false});
    s.add_column({"age",  ColumnType::Int,    false});

    ASSERT(s.size() == 3);
    ASSERT(s.column_index("name") == 1);
    ASSERT(s.has_column("age"));
    ASSERT(!s.has_column("salary"));
    ASSERT(s.primary_key_index() == 0);
}

TEST(schema_duplicate_column_throws) {
    Schema s;
    s.add_column({"id", ColumnType::Int, false});

    bool caught = false;
    try { s.add_column({"id", ColumnType::Int, false}); }
    catch (const tree23::InvalidArgument&) { caught = true; }
    ASSERT(caught);
    ASSERT(s.size() == 1);
}

TEST(schema_missing_column_throws) {
    Schema s;
    s.add_column({"id", ColumnType::Int, false});

    bool caught = false;
    try { (void)s.column_index("nope"); }
    catch (const db::ColumnNotFound&) { caught = true; }
    ASSERT(caught);
}

TEST(schema_without_primary_key) {
    Schema s;
    s.add_column({"x", ColumnType::Int, false});
    ASSERT(s.primary_key_index() == -1);
}

TEST(schema_type_matching) {
    ASSERT(value_matches_type(ColumnType::Int, Value(1)));
    ASSERT(value_matches_type(ColumnType::String, Value("s")));
    ASSERT(!value_matches_type(ColumnType::Int, Value("s")));
}

TEST(row_tombstone) {
    DynamicArray<Value> vals;
    vals.push_back(Value(1));
    vals.push_back(Value("Alice"));
    Row r(std::move(vals));

    ASSERT(r.size() == 2);
    ASSERT(r[1] == Value("Alice"));
    ASSERT(!r.is_deleted());
    r.mark_deleted();
    ASSERT(r.is_deleted());
}

// IndexKey

TEST(indexkey_lexicographic_order) {
    IndexKey a(Value(25), 3), b(Value(25), 7), c(Value(30), 1);

    ASSERT(a < b);
    ASSERT(b < c);
    ASSERT(!(b < a));
    ASSERT(a == IndexKey(Value(25), 3));
    ASSERT(!(a == b));
}

TEST(indexkey_relational_operators) {
    IndexKey a(Value(25), 3), b(Value(25), 7), c(Value(30), 1);

    ASSERT(b > a);
    ASSERT(!(a > b));
    ASSERT(a <= b);
    ASSERT(a <= IndexKey(Value(25), 3));
    ASSERT(c >= b);
    ASSERT(a != b);
}

TEST(tree_allows_full_duplicate) {
    Tree23<IndexKey> t;
    t.insert(IndexKey(Value(25), 0));
    t.insert(IndexKey(Value(25), 1));
    t.insert(IndexKey(Value(25), 2));
    ASSERT(t.get_size() == 3);

    t.insert(IndexKey(Value(25), 1));
    ASSERT(t.get_size() == 4);
}

// BTreeIndex

TEST(btree_duplicate_values) {
    BTreeIndex idx;
    idx.insert(Value(25), 10);
    idx.insert(Value(25), 20);
    idx.insert(Value(30), 30);

    ASSERT(idx.size() == 3);
    ASSERT(sorted_ids(idx.find_eq(Value(25))) == std::vector<size_t>({10, 20}));
    ASSERT(idx.supports_range());
}

TEST(btree_range_inclusive) {
    BTreeIndex idx;
    for (int v = 10; v <= 50; v += 10) idx.insert(Value(v), static_cast<size_t>(v));

    auto ids = sorted_ids(idx.find_range(Value(20), Value(40)));
    ASSERT(ids == std::vector<size_t>({20, 30, 40}));
}

TEST(btree_erase_one_of_duplicates) {
    BTreeIndex idx;
    idx.insert(Value(25), 1);
    idx.insert(Value(25), 2);
    idx.erase(Value(25), 1);

    ASSERT(idx.size() == 1);
    ASSERT(sorted_ids(idx.find_eq(Value(25))) == std::vector<size_t>({2}));
}

TEST(btree_missing_key_returns_empty) {
    BTreeIndex idx;
    idx.insert(Value(1), 0);
    ASSERT(idx.find_eq(Value(999)).size() == 0);
    ASSERT(idx.find_range(Value(100), Value(200)).size() == 0);
}

TEST(btree_wrong_type_throws) {
    BTreeIndex idx;
    idx.insert(Value(1), 0);
    idx.insert(Value(2), 1);

    bool caught = false;
    try { (void)idx.find_range(Value("a"), Value("z")); }
    catch (const db::TypeMismatch&) { caught = true; }
    ASSERT(caught);
}

TEST(btree_rejects_full_duplicate) {
    BTreeIndex idx;
    idx.insert(Value(25), 1);
    idx.insert(Value(25), 2);
    ASSERT(idx.size() == 2);

    bool caught = false;
    try { idx.insert(Value(25), 1); }
    catch (const tree23::DuplicateKey&) { caught = true; }
    ASSERT(caught);
    ASSERT(idx.size() == 2);
}

// HashIndex

TEST(hash_duplicate_values) {
    HashIndex idx;
    idx.insert(Value(25), 10);
    idx.insert(Value(25), 20);
    idx.insert(Value(30), 30);

    ASSERT(idx.size() == 3);
    ASSERT(sorted_ids(idx.find_eq(Value(25))) == std::vector<size_t>({10, 20}));
    ASSERT(!idx.supports_range());
}

TEST(hash_range_unsupported) {
    HashIndex idx;
    idx.insert(Value(1), 0);

    bool caught = false;
    try { (void)idx.find_range(Value(1), Value(2)); }
    catch (const db::UnsupportedIndexOperation&) { caught = true; }
    ASSERT(caught);
}

TEST(hash_erase_last_id_removes_key) {
    HashIndex idx;
    idx.insert(Value(25), 1);
    idx.insert(Value(25), 2);

    idx.erase(Value(25), 1);
    ASSERT(sorted_ids(idx.find_eq(Value(25))) == std::vector<size_t>({2}));

    idx.erase(Value(25), 2);
    ASSERT(idx.find_eq(Value(25)).size() == 0);
    ASSERT(idx.size() == 0);
}

TEST(hash_erase_missing_throws) {
    HashIndex idx;
    idx.insert(Value(1), 0);

    bool caught = false;
    try { idx.erase(Value(999), 0); }
    catch (const tree23::KeyNotFound&) { caught = true; }
    ASSERT(caught);

    caught = false;
    try { idx.erase(Value(1), 777); }
    catch (const tree23::KeyNotFound&) { caught = true; }
    ASSERT(caught);
}

// Регрессия дефекта балансировки дерева

TEST(remove_right_key_of_two_key_internal_node) {
    Tree23<int> t;
    for (int v : {5, 6, 2, 4, 1, 3, 0}) t.insert(v);
    ASSERT(t.check_invariants().empty());

    t.remove(5);
    ASSERT(t.check_invariants().empty());
    ASSERT(t.get_size() == 6);

    for (int v : {0, 1, 2, 3, 4, 6}) ASSERT(t.search(v));
    ASSERT(!t.search(5));
}

TEST(mass_erase_keeps_invariants) {
    std::mt19937 rng(2026);
    std::vector<int> vals;
    for (int i = 0; i < 2000; ++i) vals.push_back(i);
    std::shuffle(vals.begin(), vals.end(), rng);

    Tree23<int> t;
    for (int v : vals) t.insert(v);
    ASSERT(t.check_invariants().empty());

    size_t removed = 0;
    for (size_t i = 0; i < vals.size(); i += 3) {
        t.remove(vals[i]);
        ++removed;
        std::string err = t.check_invariants();
        if (!err.empty()) {
            std::cout << "\n  нарушение после удаления ключа " << vals[i]
                      << ": " << err << std::endl;
            ASSERT(false);
        }
    }
    ASSERT(t.get_size() == 2000 - removed);

    for (size_t i = 0; i < vals.size(); ++i) {
        ASSERT(t.search(vals[i]) == (i % 3 != 0));
    }
}

TEST(erase_all_keys) {
    std::mt19937 rng(99);
    std::vector<int> vals;
    for (int i = 0; i < 300; ++i) vals.push_back(i);
    std::shuffle(vals.begin(), vals.end(), rng);

    Tree23<int> t;
    for (int v : vals) t.insert(v);

    for (int v : vals) {
        t.remove(v);
        ASSERT(t.check_invariants().empty());
    }
    ASSERT(t.get_size() == 0);
}

TEST(find_minimal_failing_case) {
    for (int n = 5; n <= 60; ++n) {
        for (unsigned seed = 1; seed <= 20; ++seed) {
            std::vector<int> vals(n);
            for (int i = 0; i < n; ++i) vals[i] = i;
            std::mt19937 rng(seed);
            std::shuffle(vals.begin(), vals.end(), rng);

            Tree23<int> t;
            for (int v : vals) t.insert(v);

            std::string err = t.check_invariants();
            if (!err.empty()) {
                std::cout << "\n  сломано уже вставкой: n=" << n << " seed=" << seed
                          << ": " << err << "\n  порядок:";
                for (int v : vals) std::cout << " " << v;
                std::cout << std::endl;
                ASSERT(false);
            }

            for (int i = 0; i < n; ++i) {
                bool threw = false;
                try { t.remove(vals[i]); }
                catch (const std::exception& e) {
                    threw = true;
                    std::cout << "\n  исключение: n=" << n << " seed=" << seed
                              << " удаление #" << i << " ключ=" << vals[i]
                              << ": " << e.what();
                }
                err = t.check_invariants();
                if (threw || !err.empty()) {
                    if (!threw) std::cout << "\n  инвариант нарушен: n=" << n
                                          << " seed=" << seed << " после удаления #" << i
                                          << " ключ=" << vals[i] << ": " << err;
                    std::cout << "\n  порядок вставки/удаления:";
                    for (int v : vals) std::cout << " " << v;
                    std::cout << std::endl;
                    return;
                }
            }
        }
    }
}

// Дифференциальный тест и эталон диапазона

TEST(differential_btree_vs_hash) {
    std::mt19937 rng(2026);
    BTreeIndex btree;
    HashIndex  hash;

    for (size_t id = 0; id < 2000; ++id) {
        Value v(static_cast<int>(rng() % 50));
        btree.insert(v, id);
        hash.insert(v, id);
    }
    ASSERT(btree.size() == hash.size());

    for (int v = 0; v < 50; ++v) {
        auto a = sorted_ids(btree.find_eq(Value(v)));
        auto b = sorted_ids(hash.find_eq(Value(v)));
        ASSERT(a == b);
    }

    std::mt19937 rng2(2026);
    for (size_t id = 0; id < 2000; ++id) {
        Value v(static_cast<int>(rng2() % 50));
        if (id % 3 == 0) {
            btree.erase(v, id);
            hash.erase(v, id);
        }
    }
    ASSERT(btree.size() == hash.size());

    for (int v = 0; v < 50; ++v) {
        ASSERT(sorted_ids(btree.find_eq(Value(v))) == sorted_ids(hash.find_eq(Value(v))));
    }
}

TEST(btree_range_oracle) {
    std::mt19937 rng(777);
    BTreeIndex idx;
    std::vector<std::pair<int, size_t>> data;

    for (size_t id = 0; id < 1000; ++id) {
        int v = static_cast<int>(rng() % 200);
        idx.insert(Value(v), id);
        data.push_back({v, id});
    }

    for (int iter = 0; iter < 100; ++iter) {
        int lo = static_cast<int>(rng() % 220) - 10;
        int hi = static_cast<int>(rng() % 220) - 10;

        std::vector<size_t> expected;
        for (auto& [v, id] : data) if (v >= lo && v <= hi) expected.push_back(id);
        std::sort(expected.begin(), expected.end());

        ASSERT(sorted_ids(idx.find_range(Value(lo), Value(hi))) == expected);
    }
}

int main() {
    std::cout << "=== Тесты слоя данных и индексов ===\n" << std::endl;

    RUN_TEST(value_type_checks);
    RUN_TEST(value_equality_and_order);
    RUN_TEST(value_cross_type_equality_is_false);
    RUN_TEST(value_cross_type_order_throws);
    RUN_TEST(value_to_string);
    RUN_TEST(value_hash_consistency);
    RUN_TEST(value_relational_operators);

    RUN_TEST(schema_columns);
    RUN_TEST(schema_duplicate_column_throws);
    RUN_TEST(schema_missing_column_throws);
    RUN_TEST(schema_without_primary_key);
    RUN_TEST(schema_type_matching);
    RUN_TEST(row_tombstone);

    RUN_TEST(indexkey_lexicographic_order);
    RUN_TEST(indexkey_relational_operators);
    RUN_TEST(tree_allows_full_duplicate);

    RUN_TEST(btree_duplicate_values);
    RUN_TEST(btree_range_inclusive);
    RUN_TEST(btree_erase_one_of_duplicates);
    RUN_TEST(btree_missing_key_returns_empty);
    RUN_TEST(btree_wrong_type_throws);
    RUN_TEST(btree_rejects_full_duplicate);

    RUN_TEST(hash_duplicate_values);
    RUN_TEST(hash_range_unsupported);
    RUN_TEST(hash_erase_last_id_removes_key);
    RUN_TEST(hash_erase_missing_throws);

    RUN_TEST(remove_right_key_of_two_key_internal_node);
    RUN_TEST(mass_erase_keeps_invariants);
    RUN_TEST(erase_all_keys);
    RUN_TEST(find_minimal_failing_case);

    RUN_TEST(differential_btree_vs_hash);
    RUN_TEST(btree_range_oracle);

    std::cout << "\n=== Все тесты пройдены ===\n";
    return 0;
}