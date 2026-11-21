#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <random>
#include "../core/Table.h"
#include "../core/DbExceptions.h"

#define TEST(name) void test_##name()
#define ASSERT(expr) \
    do { if (!(expr)) { \
        std::cerr << "✗ ПРОВАЛЕН: " << #expr << " в " << __FILE__ << ":" << __LINE__ << std::endl; \
        exit(1); } } while (0)
#define RUN_TEST(name) \
    do { std::cout << "Запуск теста: " << #name << "... "; \
         test_##name(); std::cout << "✓ ПРОЙДЕН" << std::endl; } while (0)

// Helpers

static std::vector<size_t> to_vec(const DynamicArray<size_t>& a) {
    std::vector<size_t> v;
    for (size_t i = 0; i < a.size(); ++i) v.push_back(a[i]);
    return v;
}

static std::vector<size_t> scan_eq(const Table& t, size_t col, const Value& v) {
    return to_vec(t.scan_eq(col, v));
}

static std::vector<size_t> scan_range(const Table& t, size_t col,
                                      const Value& lo, const Value& hi) {
    return to_vec(t.scan_range(col, lo, hi));
}

static std::vector<size_t> sorted_ids(const DynamicArray<size_t>& a) {
    std::vector<size_t> v;
    for (size_t i = 0; i < a.size(); ++i) v.push_back(a[i]);
    std::sort(v.begin(), v.end());
    return v;
}

static Schema users_schema() {
    Schema s;
    s.add_column({"id",   ColumnType::Int,    true});
    s.add_column({"name", ColumnType::String, false});
    s.add_column({"age",  ColumnType::Int,    false});
    return s;
}

static size_t add_user(Table& t, int id, const std::string& name, int age) {
    DynamicArray<Value> v;
    v.push_back(Value(id));
    v.push_back(Value(name));
    v.push_back(Value(age));
    return t.insert(std::move(v));
}

// Создание и метаданные

TEST(table_create_empty) {
    Table t("users", users_schema());
    ASSERT(t.name() == "users");
    ASSERT(t.schema().size() == 3);
    ASSERT(t.alive_count() == 0);
    ASSERT(t.row_count() == 0);
}

TEST(table_primary_key_auto_index) {
    Table t("users", users_schema());

    ASSERT(t.index_count() == 1);
    const Table::IndexInfo& pk = t.index_info("pk_users_id");
    ASSERT(pk.column == 0);
    ASSERT(pk.unique);
    ASSERT(pk.is_primary);
    ASSERT(pk.type == IndexType::BTree);
}

TEST(table_without_primary_key_has_no_indexes) {
    Schema s;
    s.add_column({"x", ColumnType::Int, false});
    Table t("plain", std::move(s));
    ASSERT(t.index_count() == 0);
}

// Вставка: чтение, валидация, атомарность

TEST(table_insert_and_read) {
    Table t("users", users_schema());

    size_t id0 = add_user(t, 1, "Alice", 25);
    size_t id1 = add_user(t, 2, "Bob",   30);

    ASSERT(id0 == 0);
    ASSERT(id1 == 1);
    ASSERT(t.alive_count() == 2);
    ASSERT(t.row_count() == 2);

    ASSERT(t.row(id0)[0] == Value(1));
    ASSERT(t.row(id0)[1] == Value("Alice"));
    ASSERT(t.row(id1)[2] == Value(30));
    ASSERT(!t.row(id0).is_deleted());
}

TEST(table_insert_arity_mismatch) {
    Table t("users", users_schema());

    DynamicArray<Value> v;
    v.push_back(Value(1));
    v.push_back(Value("Alice"));

    bool caught = false;
    try { t.insert(std::move(v)); }
    catch (const db::SchemaViolation&) { caught = true; }
    ASSERT(caught);
    ASSERT(t.row_count() == 0);
}

TEST(table_insert_type_mismatch) {
    Table t("users", users_schema());

    DynamicArray<Value> v;
    v.push_back(Value(1));
    v.push_back(Value("Alice"));
    v.push_back(Value("двадцать пять"));

    bool caught = false;
    try { t.insert(std::move(v)); }
    catch (const db::TypeMismatch&) { caught = true; }
    ASSERT(caught);
    ASSERT(t.row_count() == 0);
}

TEST(table_primary_key_uniqueness) {
    Table t("users", users_schema());
    add_user(t, 1, "Alice", 25);

    bool caught = false;
    try { add_user(t, 1, "Bob", 30); }
    catch (const db::UniqueViolation&) { caught = true; }
    ASSERT(caught);
}

TEST(table_insert_is_atomic_on_failure) {
    Table t("users", users_schema());
    add_user(t, 1, "Alice", 25);
    t.create_index("idx_age", "age", IndexType::BTree);

    const size_t rows_before  = t.row_count();
    const size_t alive_before = t.alive_count();
    const size_t pk_before    = t.get_index("pk_users_id")->size();
    const size_t age_before   = t.get_index("idx_age")->size();

    try { add_user(t, 1, "Bob", 30); } catch (const db::UniqueViolation&) {}

    ASSERT(t.row_count()  == rows_before);
    ASSERT(t.alive_count() == alive_before);
    ASSERT(t.get_index("pk_users_id")->size() == pk_before);
    ASSERT(t.get_index("idx_age")->size()     == age_before);
    ASSERT(scan_eq(t, 0, Value(1)) == std::vector<size_t>({0}));
}

// Удаление: tombstones и стабильность row_id

TEST(table_row_ids_are_stable) {
    Table t("users", users_schema());
    add_user(t, 1, "Alice", 25);
    add_user(t, 2, "Bob",   30);
    add_user(t, 3, "Carl",  22);

    t.remove_row(1);
    ASSERT(t.row(1).is_deleted());
    ASSERT(t.alive_count() == 2);
    ASSERT(t.row_count() == 3);

    size_t new_id = add_user(t, 4, "Dana", 28);
    ASSERT(new_id == 3);
    ASSERT(t.row(0)[0] == Value(1));
    ASSERT(t.row(2)[0] == Value(3));
}

TEST(table_remove_row_updates_all_indexes) {
    Table t("users", users_schema());
    add_user(t, 1, "Alice", 25);
    add_user(t, 2, "Bob",   25);
    add_user(t, 3, "Carl",  30);

    t.create_index("idx_age_tree", "age", IndexType::BTree);
    t.create_index("idx_age_hash", "age", IndexType::Hash);

    t.remove_row(0);

    ASSERT(t.get_index("pk_users_id")->size() == 2);
    ASSERT(t.get_index("idx_age_tree")->size() == 2);
    ASSERT(t.get_index("idx_age_hash")->size() == 2);

    ASSERT(sorted_ids(t.get_index("idx_age_tree")->find_eq(Value(25)))
           == std::vector<size_t>({1}));
    ASSERT(sorted_ids(t.get_index("idx_age_hash")->find_eq(Value(25)))
           == std::vector<size_t>({1}));
    ASSERT(t.get_index("pk_users_id")->find_eq(Value(1)).size() == 0);
}

TEST(table_remove_twice_throws) {
    Table t("users", users_schema());
    add_user(t, 1, "Alice", 25);
    t.remove_row(0);

    bool caught = false;
    try { t.remove_row(0); }
    catch (const db::RowNotFound&) { caught = true; }
    ASSERT(caught);
    ASSERT(t.alive_count() == 0);
}

TEST(table_remove_missing_throws) {
    Table t("users", users_schema());
    bool caught = false;
    try { t.remove_row(42); }
    catch (const db::RowNotFound&) { caught = true; }
    ASSERT(caught);
}

TEST(table_deleted_primary_key_can_be_reused) {
    Table t("users", users_schema());
    add_user(t, 1, "Alice", 25);
    t.remove_row(0);

    size_t id = add_user(t, 1, "Alice2", 26);
    ASSERT(id == 1);
    ASSERT(sorted_ids(t.get_index("pk_users_id")->find_eq(Value(1)))
           == std::vector<size_t>({1}));
}

// Создание и удаление индексов

TEST(table_create_index_on_existing_rows) {
    Table t("users", users_schema());
    for (int i = 0; i < 10; ++i) add_user(t, i, "u" + std::to_string(i), 20 + i % 3);
    t.remove_row(2);
    t.remove_row(5);

    t.create_index("idx_age", "age", IndexType::BTree);

    ASSERT(t.get_index("idx_age")->size() == t.alive_count());
    for (int a = 20; a < 23; ++a) {
        ASSERT(sorted_ids(t.get_index("idx_age")->find_eq(Value(a))) == scan_eq(t, 2, Value(a)));
    }
}

TEST(table_create_unique_index_rejects_duplicates) {
    Table t("users", users_schema());
    add_user(t, 1, "Alice", 25);
    add_user(t, 2, "Bob",   25);

    bool caught = false;
    try { t.create_index("uq_age", "age", IndexType::BTree, true); }
    catch (const db::UniqueViolation&) { caught = true; }
    ASSERT(caught);
    ASSERT(t.index_count() == 1);
}

TEST(table_create_index_atomic_on_failure) {
    Table t("users", users_schema());
    add_user(t, 1, "Alice", 25);

    bool caught = false;
    try { t.create_index("bad", "no_such_column", IndexType::BTree); }
    catch (const db::ColumnNotFound&) { caught = true; }
    ASSERT(caught);
    ASSERT(t.index_count() == 1);

    caught = false;
    try { t.create_index("pk_users_id", "age", IndexType::Hash); }
    catch (const db::IndexAlreadyExists&) { caught = true; }
    ASSERT(caught);
    ASSERT(t.index_count() == 1);
}

TEST(table_drop_index_keeps_name_map_valid) {
    Table t("users", users_schema());
    add_user(t, 1, "Alice", 25);

    t.create_index("idx_a", "age",  IndexType::BTree);
    t.create_index("idx_b", "age",  IndexType::Hash);
    t.create_index("idx_c", "name", IndexType::BTree);
    ASSERT(t.index_count() == 4);

    t.drop_index("idx_b");
    ASSERT(t.index_count() == 3);

    ASSERT(t.get_index("idx_a")->size() == 1);
    ASSERT(t.get_index("idx_c")->size() == 1);
    ASSERT(t.index_info("idx_c").column == 1);
    ASSERT(std::string(t.get_index("idx_c")->method_name()) == "BTREE");

    bool caught = false;
    try { t.get_index("idx_b"); }
    catch (const db::IndexNotFound&) { caught = true; }
    ASSERT(caught);
}

TEST(table_drop_primary_index_forbidden) {
    Table t("users", users_schema());
    bool caught = false;
    try { t.drop_index("pk_users_id"); }
    catch (const db::UnsupportedIndexOperation&) { caught = true; }
    ASSERT(caught);
    ASSERT(t.index_count() == 1);
}

TEST(table_indexes_on_column) {
    Table t("users", users_schema());
    t.create_index("idx_age_tree", "age", IndexType::BTree);
    t.create_index("idx_age_hash", "age", IndexType::Hash);
    t.create_index("idx_name",     "name", IndexType::BTree);

    auto on_age = t.indexes_on(2);
    ASSERT(on_age.size() == 2);
    ASSERT(t.indexes_on(1).size() == 1);
    ASSERT(t.indexes_on(0).size() == 1);
}

// Планировщик выбора индекса

TEST(table_planner_prefers_hash_for_equality) {
    Table t("users", users_schema());
    t.create_index("idx_age_tree", "age", IndexType::BTree);
    t.create_index("idx_age_hash", "age", IndexType::Hash);

    const IIndex* chosen = t.pick_index(2, false);
    ASSERT(chosen != nullptr);
    ASSERT(std::string(chosen->method_name()) == "HASH");
}

TEST(table_planner_range_requires_btree) {
    Table t("users", users_schema());
    t.create_index("idx_age_hash", "age", IndexType::Hash);

    ASSERT(t.pick_index(2, true) == nullptr);

    t.create_index("idx_age_tree", "age", IndexType::BTree);
    const IIndex* chosen = t.pick_index(2, true);
    ASSERT(chosen != nullptr);
    ASSERT(std::string(chosen->method_name()) == "BTREE");
}

TEST(table_planner_falls_back_to_btree_for_equality) {
    Table t("users", users_schema());
    t.create_index("idx_age_tree", "age", IndexType::BTree);

    const IIndex* chosen = t.pick_index(2, false);
    ASSERT(chosen != nullptr);
    ASSERT(std::string(chosen->method_name()) == "BTREE");
}

TEST(table_planner_returns_null_without_index) {
    Table t("users", users_schema());
    ASSERT(t.pick_index(1, false) == nullptr);
    ASSERT(t.pick_index(2, true)  == nullptr);
}

// Согласованность индексов и эталонного просмотра

TEST(table_two_indexes_agree) {
    std::mt19937 rng(4242);
    Table t("users", users_schema());
    t.create_index("idx_age_tree", "age", IndexType::BTree);
    t.create_index("idx_age_hash", "age", IndexType::Hash);

    for (int i = 0; i < 500; ++i) {
        add_user(t, i, "u" + std::to_string(i), static_cast<int>(rng() % 40));
    }

    for (int a = 0; a < 40; ++a) {
        auto expected = scan_eq(t, 2, Value(a));
        ASSERT(sorted_ids(t.get_index("idx_age_tree")->find_eq(Value(a))) == expected);
        ASSERT(sorted_ids(t.get_index("idx_age_hash")->find_eq(Value(a))) == expected);
    }
}

TEST(table_range_query_matches_scan) {
    std::mt19937 rng(777);
    Table t("users", users_schema());
    t.create_index("idx_age", "age", IndexType::BTree);

    for (int i = 0; i < 400; ++i) {
        add_user(t, i, "u" + std::to_string(i), static_cast<int>(rng() % 100));
    }
    for (int i = 0; i < 400; i += 7) t.remove_row(static_cast<size_t>(i));

    for (int iter = 0; iter < 60; ++iter) {
        int lo = static_cast<int>(rng() % 110) - 5;
        int hi = static_cast<int>(rng() % 110) - 5;
        ASSERT(sorted_ids(t.get_index("idx_age")->find_range(Value(lo), Value(hi)))
               == scan_range(t, 2, Value(lo), Value(hi)));
    }
}

TEST(table_rebuild_index) {
    Table t("users", users_schema());
    t.create_index("idx_age", "age", IndexType::Hash);
    for (int i = 0; i < 50; ++i) add_user(t, i, "u" + std::to_string(i), i % 5);
    t.remove_row(3);

    t.rebuild_index("idx_age");
    ASSERT(t.get_index("idx_age")->size() == t.alive_count());

    t.rebuild_index("idx_age");
    ASSERT(t.get_index("idx_age")->size() == t.alive_count());

    for (int a = 0; a < 5; ++a) {
        ASSERT(sorted_ids(t.get_index("idx_age")->find_eq(Value(a))) == scan_eq(t, 2, Value(a)));
    }
}

TEST(table_mass_operations_consistency) {
    std::mt19937 rng(2026);
    Table t("users", users_schema());
    t.create_index("idx_age_tree", "age", IndexType::BTree);
    t.create_index("idx_age_hash", "age", IndexType::Hash);
    t.create_index("idx_name",     "name", IndexType::BTree);

    std::vector<size_t> alive;
    for (int i = 0; i < 600; ++i) {
        size_t id = add_user(t, i, "u" + std::to_string(i % 100),
                             static_cast<int>(rng() % 30));
        alive.push_back(id);

        if (i % 5 == 4) {
            size_t pick = rng() % alive.size();
            t.remove_row(alive[pick]);
            alive.erase(alive.begin() + static_cast<long>(pick));
        }
    }

    ASSERT(t.alive_count() == alive.size());
    ASSERT(t.get_index("pk_users_id")->size()  == alive.size());
    ASSERT(t.get_index("idx_age_tree")->size() == alive.size());
    ASSERT(t.get_index("idx_age_hash")->size() == alive.size());
    ASSERT(t.get_index("idx_name")->size()     == alive.size());

    for (int a = 0; a < 30; ++a) {
        auto expected = scan_eq(t, 2, Value(a));
        ASSERT(sorted_ids(t.get_index("idx_age_tree")->find_eq(Value(a))) == expected);
        ASSERT(sorted_ids(t.get_index("idx_age_hash")->find_eq(Value(a))) == expected);
    }
    for (int n = 0; n < 100; ++n) {
        Value v("u" + std::to_string(n));
        ASSERT(sorted_ids(t.get_index("idx_name")->find_eq(v)) == scan_eq(t, 1, v));
    }
}

// Проверка типов на чтении и select через планировщик

TEST(table_query_type_mismatch_throws) {
    Table t("users", users_schema());
    add_user(t, 1, "Alice", 25);

    bool caught = false;
    try { (void)t.select_eq(2, Value("двадцать пять")); }
    catch (const db::TypeMismatch&) { caught = true; }
    ASSERT(caught);

    caught = false;
    try { (void)t.scan_range(1, Value(1), Value(9)); }
    catch (const db::TypeMismatch&) { caught = true; }
    ASSERT(caught);

    caught = false;
    try { (void)t.select_eq(7, Value(1)); }
    catch (const db::ColumnNotFound&) { caught = true; }
    ASSERT(caught);
}

TEST(table_select_matches_scan) {
    std::mt19937 rng(31337);
    Table t("users", users_schema());
    t.create_index("idx_age_tree", "age", IndexType::BTree);
    t.create_index("idx_age_hash", "age", IndexType::Hash);

    for (int i = 0; i < 300; ++i) {
        add_user(t, i, "u" + std::to_string(i), static_cast<int>(rng() % 25));
    }
    for (int i = 0; i < 300; i += 11) t.remove_row(static_cast<size_t>(i));

    for (int a = 0; a < 25; ++a) {
        ASSERT(sorted_ids(t.select_eq(2, Value(a))) == scan_eq(t, 2, Value(a)));
    }
    for (int iter = 0; iter < 40; ++iter) {
        int lo = static_cast<int>(rng() % 30) - 3;
        int hi = static_cast<int>(rng() % 30) - 3;
        ASSERT(sorted_ids(t.select_range(2, Value(lo), Value(hi)))
               == scan_range(t, 2, Value(lo), Value(hi)));
    }
}

int main() {
    std::cout << "=== Тесты Table ===\n" << std::endl;

    RUN_TEST(table_create_empty);
    RUN_TEST(table_primary_key_auto_index);
    RUN_TEST(table_without_primary_key_has_no_indexes);

    RUN_TEST(table_insert_and_read);
    RUN_TEST(table_insert_arity_mismatch);
    RUN_TEST(table_insert_type_mismatch);
    RUN_TEST(table_primary_key_uniqueness);
    RUN_TEST(table_insert_is_atomic_on_failure);

    RUN_TEST(table_row_ids_are_stable);
    RUN_TEST(table_remove_row_updates_all_indexes);
    RUN_TEST(table_remove_twice_throws);
    RUN_TEST(table_remove_missing_throws);
    RUN_TEST(table_deleted_primary_key_can_be_reused);

    RUN_TEST(table_create_index_on_existing_rows);
    RUN_TEST(table_create_unique_index_rejects_duplicates);
    RUN_TEST(table_create_index_atomic_on_failure);
    RUN_TEST(table_drop_index_keeps_name_map_valid);
    RUN_TEST(table_drop_primary_index_forbidden);
    RUN_TEST(table_indexes_on_column);

    RUN_TEST(table_planner_prefers_hash_for_equality);
    RUN_TEST(table_planner_range_requires_btree);
    RUN_TEST(table_planner_falls_back_to_btree_for_equality);
    RUN_TEST(table_planner_returns_null_without_index);

    RUN_TEST(table_two_indexes_agree);
    RUN_TEST(table_range_query_matches_scan);
    RUN_TEST(table_rebuild_index);
    RUN_TEST(table_mass_operations_consistency);

    RUN_TEST(table_query_type_mismatch_throws);
    RUN_TEST(table_select_matches_scan);

    std::cout << "\n=== Все тесты Table пройдены ===\n";
    return 0;
}