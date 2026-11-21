#include <iostream>
#include <random>
#include <string>
#include <vector>
#include "../core/QueryExecutor.h"
#include "../core/SQLParser.h"
#include "../core/DbExceptions.h"

#define TEST(name) void test_##name()
#define ASSERT(expr) \
    do { if (!(expr)) { \
        std::cerr << "✗ ПРОВАЛЕН: " << #expr << " в " << __FILE__ << ":" << __LINE__ << std::endl; \
        exit(1); } } while (0)
#define RUN_TEST(name) \
    do { std::cout << "Запуск теста: " << #name << "... "; \
         test_##name(); std::cout << "✓ ПРОЙДЕН" << std::endl; } while (0)

// Окружение: свежая БД + парсер + исполнитель на каждый тест
struct Env {
    Database    db;
    SQLParser   parser;

    QueryResult run(const std::string& sql) {
        QueryExecutor exec(db);
        return exec.execute(parser.parse(sql));
    }

    void setup_users() {
        run("CREATE TABLE users (id INT PRIMARY KEY, name STRING, age INT)");
    }
};

static std::vector<int> col_int(const QueryResult& r, size_t c) {
    std::vector<int> out;
    for (size_t i = 0; i < r.rows.size(); ++i) {
        out.push_back(std::get<int>(r.rows[i][c].data));
    }
    return out;
}

static std::vector<std::string> col_str(const QueryResult& r, size_t c) {
    std::vector<std::string> out;
    for (size_t i = 0; i < r.rows.size(); ++i) {
        out.push_back(std::get<std::string>(r.rows[i][c].data));
    }
    return out;
}

static bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

// DDL и метаданные

TEST(exec_create_table_and_show_tables) {
    Env e;
    e.setup_users();
    e.run("CREATE TABLE logs (ts INT, msg STRING)");

    const QueryResult r = e.run("SHOW TABLES");
    ASSERT(r.rows.size() == 2);
    ASSERT(col_str(r, 0) == std::vector<std::string>({"users", "logs"}));
    ASSERT(col_int(r, 1) == std::vector<int>({0, 0}));
    ASSERT(col_int(r, 2) == std::vector<int>({1, 0}));
}

TEST(exec_create_table_duplicate_throws) {
    Env e;
    e.setup_users();
    bool caught = false;
    try { e.run("CREATE TABLE users (x INT)"); }
    catch (const db::TableAlreadyExists&) { caught = true; }
    ASSERT(caught);
}

TEST(exec_describe) {
    Env e;
    e.setup_users();
    const QueryResult r = e.run("DESCRIBE users");
    ASSERT(r.rows.size() == 3);
    ASSERT(col_str(r, 0) == std::vector<std::string>({"id", "name", "age"}));
    ASSERT(col_str(r, 1) == std::vector<std::string>({"INT", "STRING", "INT"}));
    ASSERT(col_str(r, 2) == std::vector<std::string>({"yes", "no", "no"}));
}

TEST(exec_unknown_table_throws) {
    Env e;
    bool caught = false;
    try { e.run("SELECT * FROM missing"); }
    catch (const db::TableNotFound&) { caught = true; }
    ASSERT(caught);
}

// Вставка и чтение

TEST(exec_insert_and_select_insertion_order) {
    Env e;
    e.setup_users();
    e.run("INSERT INTO users VALUES (3, 'Carl', 22)");
    e.run("INSERT INTO users VALUES (1, 'Alice', 25)");
    e.run("INSERT INTO users VALUES (2, 'Bob', 30)");

    const QueryResult r = e.run("SELECT * FROM users");
    ASSERT(r.rows.size() == 3);
    ASSERT(col_int(r, 0) == std::vector<int>({3, 1, 2}));
    ASSERT(r.schema.size() == 3);
}

TEST(exec_select_projection) {
    Env e;
    e.setup_users();
    e.run("INSERT INTO users VALUES (1, 'Alice', 25)");

    const QueryResult r = e.run("SELECT name FROM users");
    ASSERT(r.schema.size() == 1);
    ASSERT(r.schema[0].name == "name");
    ASSERT(col_str(r, 0) == std::vector<std::string>({"Alice"}));
}

TEST(exec_select_limit) {
    Env e;
    e.setup_users();
    for (int i = 0; i < 10; ++i) {
        e.run("INSERT INTO users VALUES (" + std::to_string(i) + ", 'u', 20)");
    }
    const QueryResult r = e.run("SELECT id FROM users LIMIT 4");
    ASSERT(r.rows.size() == 4);
    ASSERT(contains(r.message, "из 10"));
}

TEST(exec_pk_uniqueness_via_sql) {
    Env e;
    e.setup_users();
    e.run("INSERT INTO users VALUES (1, 'Alice', 25)");
    bool caught = false;
    try { e.run("INSERT INTO users VALUES (1, 'Bob', 30)"); }
    catch (const db::UniqueViolation&) { caught = true; }
    ASSERT(caught);
    ASSERT(e.run("SELECT * FROM users").rows.size() == 1);
}

TEST(exec_insert_type_mismatch_throws) {
    Env e;
    e.setup_users();
    bool caught = false;
    try { e.run("INSERT INTO users VALUES (1, 'Alice', 'old')"); }
    catch (const db::TypeMismatch&) { caught = true; }
    ASSERT(caught);
}

// WHERE, индексы и планировщик

TEST(exec_select_where_eq_reports_hash) {
    Env e;
    e.setup_users();
    e.run("CREATE INDEX idx_age_hash ON users(age) USING HASH");
    e.run("INSERT INTO users VALUES (1, 'Alice', 25)");
    e.run("INSERT INTO users VALUES (2, 'Bob', 30)");

    const QueryResult r = e.run("SELECT id FROM users WHERE age = 25");
    ASSERT(col_int(r, 0) == std::vector<int>({1}));
    ASSERT(contains(r.message, "метод поиска: HASH"));
}

TEST(exec_select_where_between_reports_btree) {
    Env e;
    e.setup_users();
    e.run("CREATE INDEX idx_age_tree ON users(age) USING BTREE");
    for (int i = 1; i <= 5; ++i) {
        e.run("INSERT INTO users VALUES (" + std::to_string(i) + ", 'u', " +
              std::to_string(20 + i) + ")");
    }
    const QueryResult r = e.run("SELECT id FROM users WHERE age BETWEEN 22 AND 24");
    ASSERT(col_int(r, 0) == std::vector<int>({2, 3, 4}));
    ASSERT(contains(r.message, "метод поиска: BTREE"));
}

TEST(exec_forced_index_on_between_unsupported_for_hash) {
    Env e;
    e.setup_users();
    e.run("CREATE INDEX idx_age_hash ON users(age) USING HASH");
    e.run("INSERT INTO users VALUES (1, 'Alice', 25)");

    bool caught = false;
    try { e.run("SELECT * FROM users WITH INDEX(idx_age_hash) WHERE age BETWEEN 20 AND 30"); }
    catch (const db::UnsupportedIndexOperation&) { caught = true; }
    ASSERT(caught);
}

TEST(exec_forced_index_on_eq_works) {
    Env e;
    e.setup_users();
    e.run("CREATE INDEX idx_age_hash ON users(age) USING HASH");
    e.run("INSERT INTO users VALUES (1, 'Alice', 25)");

    const QueryResult r = e.run("SELECT id FROM users WITH INDEX(idx_age_hash) WHERE age = 25");
    ASSERT(col_int(r, 0) == std::vector<int>({1}));
    ASSERT(contains(r.message, "HASH"));
}

TEST(exec_select_where_type_mismatch_throws) {
    Env e;
    e.setup_users();
    e.run("INSERT INTO users VALUES (1, 'Alice', 25)");
    bool caught = false;
    try { e.run("SELECT * FROM users WHERE age = 'twenty'"); }
    catch (const db::TypeMismatch&) { caught = true; }
    ASSERT(caught);
}

TEST(exec_select_and_chain_filters) {
    Env e;
    e.setup_users();
    e.run("INSERT INTO users VALUES (1, 'Alice', 25)");
    e.run("INSERT INTO users VALUES (2, 'Alice', 30)");
    e.run("INSERT INTO users VALUES (3, 'Bob',   25)");

    const QueryResult r = e.run("SELECT id FROM users WHERE name = 'Alice' AND age = 25");
    ASSERT(col_int(r, 0) == std::vector<int>({1}));
}

// Удаление

TEST(exec_delete_with_where_updates_indexes) {
    Env e;
    e.setup_users();
    e.run("CREATE INDEX idx_age_tree ON users(age) USING BTREE");
    for (int i = 1; i <= 4; ++i) {
        e.run("INSERT INTO users VALUES (" + std::to_string(i) + ", 'u', 25)");
    }

    const QueryResult d = e.run("DELETE FROM users WHERE age = 25 AND id = 2");
    ASSERT(d.affected_rows == 1);

    const QueryResult s = e.run("SELECT id FROM users");
    ASSERT(col_int(s, 0) == std::vector<int>({1, 3, 4}));

    const QueryResult si = e.run("SHOW INDEXES ON users");
    ASSERT(col_int(si, 5) == std::vector<int>({3, 3}));
}

TEST(exec_delete_all_keeps_tombstones) {
    Env e;
    e.setup_users();
    e.run("INSERT INTO users VALUES (1, 'a', 20)");
    e.run("INSERT INTO users VALUES (2, 'b', 21)");

    const QueryResult d = e.run("DELETE FROM users");
    ASSERT(d.affected_rows == 2);
    ASSERT(e.run("SELECT * FROM users").rows.size() == 0);
    ASSERT(e.db.table("users").row_count() == 2);
    ASSERT(e.db.table("users").alive_count() == 0);
}

TEST(exec_drop_index_via_sql) {
    Env e;
    e.setup_users();
    e.run("CREATE INDEX idx_age_hash ON users(age) USING HASH");
    e.run("DROP INDEX idx_age_hash ON users");

    const QueryResult si = e.run("SHOW INDEXES ON users");
    ASSERT(col_str(si, 0) == std::vector<std::string>({"pk_users_id"}));

    bool caught = false;
    try { e.run("SELECT * FROM users WITH INDEX(idx_age_hash) WHERE age = 1"); }
    catch (const db::IndexNotFound&) { caught = true; }
    ASSERT(caught);
}

// GENERATE: три порядка из сноски 1 методички

TEST(exec_generate_sorted_asc_and_desc) {
    Env e;
    e.run("CREATE TABLE t (id INT PRIMARY KEY, tag STRING)");
    e.run("GENERATE 20 ROWS FOR t WITH id SORTED ASC");
    ASSERT(col_int(e.run("SELECT id FROM t"), 0) ==
           std::vector<int>({0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19}));

    Env e2;
    e2.run("CREATE TABLE t (id INT PRIMARY KEY, tag STRING)");
    e2.run("GENERATE 20 ROWS FOR t WITH id SORTED DESC");
    ASSERT(col_int(e2.run("SELECT id FROM t"), 0) ==
           std::vector<int>({19,18,17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0}));
}

TEST(exec_generate_random_is_permutation_and_pk_safe) {
    Env e;
    e.run("CREATE TABLE t (id INT PRIMARY KEY, tag STRING)");
    e.run("GENERATE 200 ROWS FOR t WITH id RANDOM");

    std::vector<int> ids = col_int(e.run("SELECT id FROM t"), 0);
    ASSERT(ids.size() == 200);
    std::sort(ids.begin(), ids.end());
    for (int i = 0; i < 200; ++i) ASSERT(ids[i] == i);
}

TEST(exec_generate_non_int_column_throws) {
    Env e;
    e.setup_users();
    bool caught = false;
    try { e.run("GENERATE 5 ROWS FOR users WITH name RANDOM"); }
    catch (const db::TypeMismatch&) { caught = true; }
    ASSERT(caught);
}

// BENCHMARK

TEST(exec_benchmark_build_two_methods) {
    Env e;
    e.setup_users();
    e.run("GENERATE 500 ROWS FOR users WITH age SORTED ASC");

    const QueryResult r = e.run("BENCHMARK BUILD INDEX ON users(age)");
    ASSERT(r.benchmark.size() == 2);
    ASSERT(r.benchmark[0].method == "BTREE");
    ASSERT(r.benchmark[1].method == "HASH");
    ASSERT(r.benchmark[0].build_ms >= 0.0);
    ASSERT(r.benchmark[1].build_ms >= 0.0);
    ASSERT(r.benchmark[0].found == 500);
    ASSERT(r.benchmark[1].found == 500);
}

TEST(exec_benchmark_search_eq_all_methods_agree) {
    Env e;
    e.setup_users();
    e.run("CREATE INDEX idx_age_tree ON users(age) USING BTREE");
    e.run("CREATE INDEX idx_age_hash ON users(age) USING HASH");
    for (int i = 0; i < 100; ++i) {
        e.run("INSERT INTO users VALUES (" + std::to_string(i) + ", 'u', " +
              std::to_string(i % 10) + ")");
    }

    const QueryResult r = e.run("BENCHMARK SELECT * FROM users WHERE age = 5");
    ASSERT(r.benchmark.size() == 3);
    ASSERT(r.benchmark[2].method == "LINEAR");
    ASSERT(r.benchmark[0].found == r.benchmark[1].found);
    ASSERT(r.benchmark[1].found == r.benchmark[2].found);
    ASSERT(r.benchmark[0].found == 10);
    for (const auto& br : r.benchmark) ASSERT(br.search_ms >= 0.0);
}

TEST(exec_benchmark_search_between_marks_hash_na) {
    Env e;
    e.setup_users();
    e.run("CREATE INDEX idx_age_tree ON users(age) USING BTREE");
    e.run("CREATE INDEX idx_age_hash ON users(age) USING HASH");
    e.run("GENERATE 100 ROWS FOR users WITH age SORTED ASC");

    const QueryResult r = e.run("BENCHMARK SELECT * FROM users WHERE age BETWEEN 20 AND 30");
    ASSERT(r.benchmark.size() == 3);
    ASSERT(r.benchmark[0].method == "idx_age_tree");
    ASSERT(r.benchmark[0].search_ms >= 0.0);
    ASSERT(r.benchmark[1].method == "idx_age_hash");
    ASSERT(r.benchmark[1].search_ms == -1.0);
    ASSERT(r.benchmark[2].method == "LINEAR");
    ASSERT(r.benchmark[0].found == r.benchmark[2].found);
}

TEST(exec_benchmark_search_requires_where) {
    Env e;
    e.setup_users();
    bool caught = false;
    try { e.run("BENCHMARK SELECT * FROM users"); }
    catch (const db::SqlSyntaxError&) { caught = true; }
    ASSERT(caught);
}

// Сверка с эталонным перебором

TEST(exec_select_matches_manual_scan) {
    Env e;
    e.setup_users();
    e.run("CREATE INDEX idx_age_tree ON users(age) USING BTREE");
    e.run("CREATE INDEX idx_age_hash ON users(age) USING HASH");

    std::mt19937 rng(555);
    for (int i = 0; i < 200; ++i) {
        e.run("INSERT INTO users VALUES (" + std::to_string(i) + ", 'u" +
              std::to_string(i % 7) + "', " + std::to_string(rng() % 20) + ")");
    }
    for (int i = 0; i < 200; i += 9) {
        e.run("DELETE FROM users WHERE id = " + std::to_string(i));
    }

    const Table& t = e.db.table("users");
    for (int age = 0; age < 20; ++age) {
        std::vector<int> expected;
        for (size_t id = 0; id < t.row_count(); ++id) {
            const Row& r = t.row(id);
            if (!r.is_deleted() && r[2] == Value(age)) {
                expected.push_back(static_cast<int>(id));
            }
        }
        const std::string sql = " WHERE age = " + std::to_string(age);
        ASSERT(col_int(e.run("SELECT id FROM users WITH INDEX(idx_age_hash)" + sql), 0) == expected);
        ASSERT(col_int(e.run("SELECT id FROM users WITH INDEX(idx_age_tree)" + sql), 0) == expected);
        ASSERT(col_int(e.run("SELECT id FROM users" + sql), 0) == expected);
    }
}

// Служебные команды

TEST(exec_help_exit) {
    Env e;
    ASSERT(contains(e.run("HELP").message, "CREATE TABLE"));
    ASSERT(e.run("EXIT").stop);
}

int main() {
    std::cout << "=== Сквозные тесты исполнителя ===\n" << std::endl;

    RUN_TEST(exec_create_table_and_show_tables);
    RUN_TEST(exec_create_table_duplicate_throws);
    RUN_TEST(exec_describe);
    RUN_TEST(exec_unknown_table_throws);

    RUN_TEST(exec_insert_and_select_insertion_order);
    RUN_TEST(exec_select_projection);
    RUN_TEST(exec_select_limit);
    RUN_TEST(exec_pk_uniqueness_via_sql);
    RUN_TEST(exec_insert_type_mismatch_throws);

    RUN_TEST(exec_select_where_eq_reports_hash);
    RUN_TEST(exec_select_where_between_reports_btree);
    RUN_TEST(exec_forced_index_on_between_unsupported_for_hash);
    RUN_TEST(exec_forced_index_on_eq_works);
    RUN_TEST(exec_select_where_type_mismatch_throws);
    RUN_TEST(exec_select_and_chain_filters);

    RUN_TEST(exec_delete_with_where_updates_indexes);
    RUN_TEST(exec_delete_all_keeps_tombstones);
    RUN_TEST(exec_drop_index_via_sql);

    RUN_TEST(exec_generate_sorted_asc_and_desc);
    RUN_TEST(exec_generate_random_is_permutation_and_pk_safe);
    RUN_TEST(exec_generate_non_int_column_throws);

    RUN_TEST(exec_benchmark_build_two_methods);
    RUN_TEST(exec_benchmark_search_eq_all_methods_agree);
    RUN_TEST(exec_benchmark_search_between_marks_hash_na);
    RUN_TEST(exec_benchmark_search_requires_where);

    RUN_TEST(exec_select_matches_manual_scan);

    RUN_TEST(exec_help_exit);

    std::cout << "\n=== Все сквозные тесты пройдены ===\n";
    return 0;
}