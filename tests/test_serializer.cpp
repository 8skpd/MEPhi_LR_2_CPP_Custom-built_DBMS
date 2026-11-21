#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include "../core/QueryExecutor.h"
#include "../core/SQLParser.h"
#include "../core/Serializer.h"
#include "../core/DbExceptions.h"

#define TEST(name) void test_##name()
#define ASSERT(expr) \
    do { if (!(expr)) { \
        std::cerr << "✗ ПРОВАЛЕН: " << #expr << " в " << __FILE__ << ":" << __LINE__ << std::endl; \
        exit(1); } } while (0)
#define RUN_TEST(name) \
    do { std::cout << "Запуск теста: " << #name << "... "; \
         test_##name(); std::cout << "✓ ПРОЙДЕН" << std::endl; } while (0)

static const char* TMP = "assets/data/test_tmp_table.db";
static const char* CUT = "assets/data/test_tmp_cut.db";

static void cleanup() {
    std::filesystem::create_directories("assets/data");
    std::remove(TMP);
    std::remove(CUT);
}

static Schema users_schema() {
    Schema s;
    s.add_column({"id",   ColumnType::Int,    true});
    s.add_column({"name", ColumnType::String, false});
    s.add_column({"age",  ColumnType::Int,    false});
    return s;
}

static DynamicArray<Value> make_row(int id, const std::string& name, int age) {
    DynamicArray<Value> v;
    v.push_back(Value(id));
    v.push_back(Value(name));
    v.push_back(Value(age));
    return v;
}

static bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

// Базовый цикл: сохранить → загрузить → сравнить

TEST(serializer_roundtrip_basic) {
    cleanup();
    Table t("users", users_schema());
    t.insert(make_row(1, "Alice", 25));
    t.insert(make_row(2, "Bob",   30));
    t.create_index("idx_age", "age", IndexType::Hash);

    db::save_table(t, TMP);
    Table u = db::load_table(TMP, "users2");

    ASSERT(u.name() == "users2");
    ASSERT(u.alive_count() == 2);
    ASSERT(u.row(0)[0] == Value(1));
    ASSERT(u.row(0)[1] == Value("Alice"));
    ASSERT(u.row(1)[2] == Value(30));
    ASSERT(u.index_count() == 2);
    cleanup();
}

TEST(serializer_rebuilds_indexes) {
    cleanup();
    Table t("users", users_schema());
    for (int i = 1; i <= 5; ++i) t.insert(make_row(i, "u" + std::to_string(i), 20 + i));
    t.create_index("idx_age_tree", "age", IndexType::BTree);
    t.create_index("idx_age_hash", "age", IndexType::Hash);

    db::save_table(t, TMP);
    Table u = db::load_table(TMP, "loaded");

    ASSERT(u.index_count() == 3);
    ASSERT(u.get_index("idx_age_tree")->size() == 5);
    ASSERT(u.get_index("idx_age_hash")->size() == 5);
    auto ids = u.get_index("idx_age_hash")->find_eq(Value(23));
    ASSERT(ids.size() == 1);
    ASSERT(u.row(ids[0])[0] == Value(3));
    cleanup();
}

TEST(serializer_all_column_types) {
    cleanup();
    Schema s;
    s.add_column({"i", ColumnType::Int,    false});
    s.add_column({"d", ColumnType::Double, false});
    s.add_column({"s", ColumnType::String, false});
    Table t("t", std::move(s));

    DynamicArray<Value> v;
    v.push_back(Value(42));
    v.push_back(Value(3.14));
    v.push_back(Value("hello world"));
    t.insert(std::move(v));

    db::save_table(t, TMP);
    Table u = db::load_table(TMP, "t2");
    ASSERT(u.row(0)[0] == Value(42));
    ASSERT(u.row(0)[1] == Value(3.14));
    ASSERT(u.row(0)[2] == Value("hello world"));
    cleanup();
}

TEST(serializer_deleted_rows_not_saved) {
    cleanup();
    Table t("t", users_schema());
    t.insert(make_row(1, "a", 20));
    t.insert(make_row(2, "b", 21));
    t.insert(make_row(3, "c", 22));
    t.remove_row(1);

    db::save_table(t, TMP);
    Table u = db::load_table(TMP, "t2");

    ASSERT(u.alive_count() == 2);
    ASSERT(u.row_count() == 2);
    ASSERT(u.row(0)[0] == Value(1));
    ASSERT(u.row(1)[0] == Value(3));
    cleanup();
}

// Ошибки формата и доступа

TEST(serializer_load_missing_file) {
    bool caught = false;
    try { db::load_table("definitely_missing_file.db", "t"); }
    catch (const db::SerializationError&) { caught = true; }
    ASSERT(caught);
}

TEST(serializer_load_bad_magic) {
    cleanup();
    {
        std::ofstream f(TMP, std::ios::binary);
        const uint32_t bad = 0xDEADBEEF;
        f.write(reinterpret_cast<const char*>(&bad), sizeof(bad));
    }
    bool caught = false;
    try { db::load_table(TMP, "t"); }
    catch (const db::SerializationError&) { caught = true; }
    ASSERT(caught);
    cleanup();
}

TEST(serializer_load_truncated_file) {
    cleanup();
    Table t("t", users_schema());
    t.insert(make_row(1, "a", 20));
    db::save_table(t, TMP);

    {
        std::ifstream in(TMP, std::ios::binary | std::ios::ate);
        const std::streamsize size = in.tellg();
        in.close();
        std::ifstream src(TMP, std::ios::binary);
        std::ofstream dst(CUT, std::ios::binary);
        char c;
        for (std::streamsize i = 0; i < size / 2 && src.get(c); ++i) dst.put(c);
    }

    bool caught = false;
    try { db::load_table(CUT, "t"); }
    catch (const db::SerializationError&) { caught = true; }
    ASSERT(caught);
    cleanup();
}

// Сквозной путь через команды SAVE / LOAD

TEST(serializer_save_load_via_executor) {
    cleanup();
    SQLParser parser;

    Database db1;
    QueryExecutor exec1(db1);
    exec1.execute(parser.parse("CREATE TABLE users (id INT PRIMARY KEY, name STRING, age INT)"));
    exec1.execute(parser.parse("INSERT INTO users VALUES (1, 'Alice', 25)"));
    exec1.execute(parser.parse("INSERT INTO users VALUES (2, 'Bob', 30)"));

    const QueryResult s = exec1.execute(parser.parse("SAVE users TO 'test_tmp_table.db'"));
    ASSERT(s.ok);
    ASSERT(contains(s.message, "сохранена"));

    Database db2;
    QueryExecutor exec2(db2);
    const QueryResult l = exec2.execute(parser.parse("LOAD users_copy FROM 'test_tmp_table.db'"));
    ASSERT(l.ok);
    ASSERT(contains(l.message, "загружена"));

    const QueryResult r = exec2.execute(parser.parse("SELECT * FROM users_copy"));
    ASSERT(r.rows.size() == 2);
    ASSERT(r.rows[0][1] == Value("Alice"));
    ASSERT(r.rows[1][1] == Value("Bob"));
    cleanup();
}

TEST(serializer_load_into_existing_table_throws) {
    cleanup();
    SQLParser parser;
    Database db1;
    QueryExecutor exec1(db1);
    exec1.execute(parser.parse("CREATE TABLE users (id INT PRIMARY KEY, name STRING)"));
    exec1.execute(parser.parse("INSERT INTO users VALUES (1, 'Alice')"));
    exec1.execute(parser.parse("SAVE users TO 'test_tmp_table.db'"));

    bool caught = false;
    try { exec1.execute(parser.parse("LOAD users FROM 'test_tmp_table.db'")); }
    catch (const db::TableAlreadyExists&) { caught = true; }
    ASSERT(caught);
    cleanup();
}

int main() {
    std::cout << "=== Тесты сериализации ===\n" << std::endl;

    RUN_TEST(serializer_roundtrip_basic);
    RUN_TEST(serializer_rebuilds_indexes);
    RUN_TEST(serializer_all_column_types);
    RUN_TEST(serializer_deleted_rows_not_saved);

    RUN_TEST(serializer_load_missing_file);
    RUN_TEST(serializer_load_bad_magic);
    RUN_TEST(serializer_load_truncated_file);

    RUN_TEST(serializer_save_load_via_executor);
    RUN_TEST(serializer_load_into_existing_table_throws);

    cleanup();
    std::cout << "\n=== Все тесты сериализации пройдены ===\n";
    return 0;
}