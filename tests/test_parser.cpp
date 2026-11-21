#include <iostream>
#include <string>
#include <variant>
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

// Helpers

static Query parse_ok(const std::string& sql) {
    SQLParser p;
    return p.parse(sql);
}

static bool is_syntax_error(const std::string& sql) {
    SQLParser p;
    try {
        p.parse(sql);
        return false;
    } catch (const db::SqlSyntaxError&) {
        return true;
    }
}

template <class T>
static const T& as(const Query& q) { return std::get<T>(q); }

// Корректные запросы: DDL

TEST(parser_create_table) {
    const Query q = parse_ok("CREATE TABLE users (id INT PRIMARY KEY, name STRING, age INT);");
    const auto& c = as<CreateTableQuery>(q);
    ASSERT(c.table_name == "users");
    ASSERT(c.schema.size() == 3);
    ASSERT(c.schema[0].name == "id");
    ASSERT(c.schema[0].type == ColumnType::Int);
    ASSERT(c.schema[0].primary_key);
    ASSERT(c.schema[1].name == "name");
    ASSERT(c.schema[1].type == ColumnType::String);
    ASSERT(!c.schema[1].primary_key);
    ASSERT(c.schema[2].name == "age");
}

TEST(parser_create_table_aliases_and_case) {
    const Query q = parse_ok("create table t (a integer, b varchar, c double)");
    const auto& c = as<CreateTableQuery>(q);
    ASSERT(c.schema[0].type == ColumnType::Int);
    ASSERT(c.schema[1].type == ColumnType::String);
    ASSERT(c.schema[2].type == ColumnType::Double);
}

TEST(parser_create_index_variants) {
    const Query d = parse_ok("CREATE INDEX idx ON users(age)");
    ASSERT(as<CreateIndexQuery>(d).type == IndexType::BTree);
    ASSERT(!as<CreateIndexQuery>(d).unique);

    const Query h = parse_ok("CREATE UNIQUE INDEX idx2 ON users(name) USING HASH");
    const auto& ci = as<CreateIndexQuery>(h);
    ASSERT(ci.unique);
    ASSERT(ci.type == IndexType::Hash);
    ASSERT(ci.index_name == "idx2");
    ASSERT(ci.table_name == "users");
    ASSERT(ci.column_name == "name");
}

TEST(parser_drop_index) {
    const Query q = parse_ok("DROP INDEX idx ON users");
    ASSERT(as<DropIndexQuery>(q).index_name == "idx");
    ASSERT(as<DropIndexQuery>(q).table_name == "users");
}

// Корректные запросы: литералы и DML

TEST(parser_insert_literals) {
    const Query q = parse_ok("INSERT INTO users VALUES (1, 'Alice', -25, 3.5)");
    const auto& ins = as<InsertQuery>(q);
    ASSERT(ins.table_name == "users");
    ASSERT(ins.values.size() == 4);
    ASSERT(ins.values[0] == Value(1));
    ASSERT(ins.values[1] == Value("Alice"));
    ASSERT(ins.values[2] == Value(-25));
    ASSERT(ins.values[3] == Value(3.5));
}

TEST(parser_insert_string_escape) {
    const Query q = parse_ok("INSERT INTO t VALUES ('It''s ok')");
    ASSERT(as<InsertQuery>(q).values[0] == Value("It's ok"));
}

TEST(parser_delete) {
    const Query w = parse_ok("DELETE FROM users WHERE id = 3");
    ASSERT(as<DeleteQuery>(w).table_name == "users");
    ASSERT(as<DeleteQuery>(w).where.has_value());

    const Query all = parse_ok("DELETE FROM users");
    ASSERT(!as<DeleteQuery>(all).where.has_value());
}

// Корректные запросы: SELECT

TEST(parser_select_star_and_columns) {
    const Query s = parse_ok("SELECT * FROM users");
    const auto& sel = as<SelectQuery>(s);
    ASSERT(sel.all_columns);
    ASSERT(sel.table_name == "users");
    ASSERT(!sel.where.has_value());
    ASSERT(!sel.limit.has_value());
    ASSERT(!sel.force_index.has_value());

    const Query c = parse_ok("SELECT id, name FROM users");
    const auto& cols = as<SelectQuery>(c);
    ASSERT(!cols.all_columns);
    ASSERT(cols.columns.size() == 2);
    ASSERT(cols.columns[0] == "id");
    ASSERT(cols.columns[1] == "name");
}

TEST(parser_select_all_comparison_ops) {
    const char* ops[]      = {"=",  "<>", "!=", "<",  "<=", ">",  ">="};
    const CompareOp want[] = {CompareOp::Eq, CompareOp::Ne, CompareOp::Ne,
                              CompareOp::Lt, CompareOp::Le, CompareOp::Gt, CompareOp::Ge};
    for (int i = 0; i < 7; ++i) {
        const Query q = parse_ok(std::string("SELECT * FROM t WHERE age ") + ops[i] + " 30");
        const auto& sel = as<SelectQuery>(q);
        ASSERT(sel.where.has_value());
        ASSERT(sel.where->conditions.size() == 1);
        const auto& cc = std::get<ComparisonCondition>(sel.where->conditions[0]);
        ASSERT(cc.column == "age");
        ASSERT(cc.op == want[i]);
        ASSERT(cc.value == Value(30));
    }
}

TEST(parser_select_between) {
    const Query q = parse_ok("SELECT * FROM t WHERE age BETWEEN 20 AND 30");
    const auto& bc = std::get<BetweenCondition>(as<SelectQuery>(q).where->conditions[0]);
    ASSERT(bc.column == "age");
    ASSERT(bc.lo == Value(20));
    ASSERT(bc.hi == Value(30));
}

TEST(parser_select_and_chain) {
    const Query q = parse_ok("SELECT * FROM t WHERE a = 1 AND b = 'x' AND c BETWEEN 2 AND 3");
    const auto& w = as<SelectQuery>(q).where;
    ASSERT(w.has_value());
    ASSERT(w->conditions.size() == 3);
    ASSERT(std::holds_alternative<ComparisonCondition>(w->conditions[0]));
    ASSERT(std::holds_alternative<ComparisonCondition>(w->conditions[1]));
    ASSERT(std::holds_alternative<BetweenCondition>(w->conditions[2]));
}

TEST(parser_select_clauses_any_order) {
    const Query q = parse_ok("SELECT * FROM t WHERE a = 1 WITH INDEX(idx) LIMIT 5");
    const auto& sel = as<SelectQuery>(q);
    ASSERT(sel.where.has_value());
    ASSERT(sel.force_index.has_value() && *sel.force_index == "idx");
    ASSERT(sel.limit.has_value() && *sel.limit == 5);
}

// Служебные команды

TEST(parser_show_and_describe) {
    ASSERT(std::holds_alternative<ShowTablesQuery>(parse_ok("SHOW TABLES")));

    const Query si = parse_ok("SHOW INDEXES ON users");
    ASSERT(as<ShowIndexesQuery>(si).table_name == "users");

    const Query si2 = parse_ok("SHOW INDEXES");
    ASSERT(as<ShowIndexesQuery>(si2).table_name.empty());

    ASSERT(as<DescribeQuery>(parse_ok("DESCRIBE users")).table_name == "users");
}

TEST(parser_generate_orders) {
    const Query a = parse_ok("GENERATE 10000 ROWS FOR users WITH age SORTED ASC");
    ASSERT(as<GenerateQuery>(a).count == 10000);
    ASSERT(as<GenerateQuery>(a).table_name == "users");
    ASSERT(as<GenerateQuery>(a).column_name == "age");
    ASSERT(as<GenerateQuery>(a).order == DataOrder::SortedAsc);

    ASSERT(as<GenerateQuery>(parse_ok("GENERATE 5 ROWS FOR t WITH x SORTED DESC")).order
           == DataOrder::SortedDesc);
    ASSERT(as<GenerateQuery>(parse_ok("GENERATE 5 ROWS FOR t WITH x RANDOM")).order
           == DataOrder::Random);
}

TEST(parser_benchmark_modes) {
    const Query s = parse_ok("BENCHMARK SELECT * FROM users WHERE age = 25");
    ASSERT(as<BenchmarkQuery>(s).mode == BenchmarkQuery::Mode::Search);
    ASSERT(as<BenchmarkQuery>(s).select.table_name == "users");
    ASSERT(as<BenchmarkQuery>(s).select.where.has_value());

    const Query b = parse_ok("BENCHMARK BUILD INDEX ON users(age)");
    ASSERT(as<BenchmarkQuery>(b).mode == BenchmarkQuery::Mode::Build);
    ASSERT(as<BenchmarkQuery>(b).table_name == "users");
    ASSERT(as<BenchmarkQuery>(b).column_name == "age");
}

TEST(parser_save_load) {
    ASSERT(as<SaveQuery>(parse_ok("SAVE users TO 'data/users.db'")).path == "data/users.db");
    ASSERT(as<LoadQuery>(parse_ok("LOAD users FROM 'data/users.db'")).path == "data/users.db");
}

TEST(parser_help_exit) {
    ASSERT(std::holds_alternative<HelpQuery>(parse_ok("HELP")));
    ASSERT(std::holds_alternative<ExitQuery>(parse_ok("EXIT")));
    ASSERT(std::holds_alternative<ExitQuery>(parse_ok("quit")));
}

TEST(parser_comments_and_semicolon) {
    const Query q = parse_ok("-- комментарий до команды\nSELECT * FROM t; -- и после");
    ASSERT(as<SelectQuery>(q).table_name == "t");
}

TEST(parser_reusable_between_calls) {
    SQLParser p;
    const Query a = p.parse("SELECT * FROM aaa");
    const Query b = p.parse("INSERT INTO bbb VALUES (1)");
    ASSERT(as<SelectQuery>(a).table_name == "aaa");
    ASSERT(as<InsertQuery>(b).table_name == "bbb");
}

// Ошибки синтаксиса

TEST(parser_error_empty_and_unknown) {
    ASSERT(is_syntax_error(""));
    ASSERT(is_syntax_error("   \n-- только комментарий"));
    ASSERT(is_syntax_error("FLY TO MOON"));
}

TEST(parser_error_unterminated_string) {
    ASSERT(is_syntax_error("INSERT INTO t VALUES ('abc)"));
    ASSERT(is_syntax_error("SAVE t TO 'path"));
}

TEST(parser_error_unexpected_symbol) {
    ASSERT(is_syntax_error("SELECT * FROM t WHERE a ! 5"));
    ASSERT(is_syntax_error("SELECT * FROM t WHERE a = 5 @"));
}

TEST(parser_error_values_malformed) {
    ASSERT(is_syntax_error("INSERT INTO t VALUES (1, 2,)"));
    ASSERT(is_syntax_error("INSERT INTO t VALUES ()"));
    ASSERT(is_syntax_error("INSERT INTO t VALUES (1, 2"));
}

TEST(parser_error_missing_closing_paren) {
    ASSERT(is_syntax_error("CREATE TABLE t (a INT"));
    ASSERT(is_syntax_error("CREATE INDEX i ON t(a"));
    ASSERT(is_syntax_error("SELECT * FROM t WITH INDEX(idx"));
}

TEST(parser_error_duplicate_clauses) {
    ASSERT(is_syntax_error("SELECT * FROM t WHERE a = 1 WHERE b = 2"));
    ASSERT(is_syntax_error("SELECT * FROM t LIMIT 1 LIMIT 2"));
    ASSERT(is_syntax_error("SELECT * FROM t WITH INDEX(i) WITH INDEX(j)"));
}

TEST(parser_error_keyword_as_identifier) {
    ASSERT(is_syntax_error("CREATE TABLE select (a INT)"));
    ASSERT(is_syntax_error("CREATE TABLE t (from INT)"));
    ASSERT(is_syntax_error("SELECT * FROM where"));
    ASSERT(is_syntax_error("CREATE INDEX between ON t(a)"));
}

TEST(parser_error_number_out_of_range) {
    ASSERT(is_syntax_error("INSERT INTO t VALUES (99999999999999999999)"));
}

TEST(parser_error_trailing_garbage) {
    ASSERT(is_syntax_error("SELECT * FROM t extra"));
    ASSERT(is_syntax_error("HELP HELP"));
    ASSERT(is_syntax_error("INSERT INTO t VALUES (1) (2)"));
}

TEST(parser_error_unquoted_string) {
    ASSERT(is_syntax_error("INSERT INTO t VALUES (abc)"));
    ASSERT(is_syntax_error("SELECT * FROM t WHERE name = Alice"));
}

TEST(parser_error_unknown_column_type) {
    ASSERT(is_syntax_error("CREATE TABLE t (a UNKNOWN)"));
    ASSERT(is_syntax_error("CREATE TABLE t (a)"));
}

TEST(parser_error_bad_using) {
    ASSERT(is_syntax_error("CREATE INDEX i ON t(a) USING AVL"));
    ASSERT(is_syntax_error("CREATE INDEX i ON t(a) USING"));
}

TEST(parser_error_between_missing_and) {
    ASSERT(is_syntax_error("SELECT * FROM t WHERE a BETWEEN 1 5"));
    ASSERT(is_syntax_error("SELECT * FROM t WHERE a BETWEEN 1 AND"));
}

TEST(parser_error_negative_limit) {
    ASSERT(is_syntax_error("SELECT * FROM t LIMIT -5"));
    ASSERT(is_syntax_error("SELECT * FROM t LIMIT 'abc'"));
}

TEST(parser_error_message_contains_position) {
    SQLParser p;
    bool caught = false;
    try {
        p.parse("SELECT * FROM t WHERE a = ");
    } catch (const db::SqlSyntaxError& e) {
        caught = true;
        const std::string msg = e.what();
        ASSERT(msg.find("позиция") != std::string::npos);
    }
    ASSERT(caught);
}

int main() {
    std::cout << "=== Тесты SQL-парсера ===\n" << std::endl;

    // Корректные запросы
    RUN_TEST(parser_create_table);
    RUN_TEST(parser_create_table_aliases_and_case);
    RUN_TEST(parser_create_index_variants);
    RUN_TEST(parser_drop_index);
    RUN_TEST(parser_insert_literals);
    RUN_TEST(parser_insert_string_escape);
    RUN_TEST(parser_delete);
    RUN_TEST(parser_select_star_and_columns);
    RUN_TEST(parser_select_all_comparison_ops);
    RUN_TEST(parser_select_between);
    RUN_TEST(parser_select_and_chain);
    RUN_TEST(parser_select_clauses_any_order);
    RUN_TEST(parser_show_and_describe);
    RUN_TEST(parser_generate_orders);
    RUN_TEST(parser_benchmark_modes);
    RUN_TEST(parser_save_load);
    RUN_TEST(parser_help_exit);
    RUN_TEST(parser_comments_and_semicolon);
    RUN_TEST(parser_reusable_between_calls);

    // Ошибки
    RUN_TEST(parser_error_empty_and_unknown);
    RUN_TEST(parser_error_unterminated_string);
    RUN_TEST(parser_error_unexpected_symbol);
    RUN_TEST(parser_error_values_malformed);
    RUN_TEST(parser_error_missing_closing_paren);
    RUN_TEST(parser_error_duplicate_clauses);
    RUN_TEST(parser_error_keyword_as_identifier);
    RUN_TEST(parser_error_number_out_of_range);
    RUN_TEST(parser_error_trailing_garbage);
    RUN_TEST(parser_error_unquoted_string);
    RUN_TEST(parser_error_unknown_column_type);
    RUN_TEST(parser_error_bad_using);
    RUN_TEST(parser_error_between_missing_and);
    RUN_TEST(parser_error_negative_limit);
    RUN_TEST(parser_error_message_contains_position);

    std::cout << "\n=== Все тесты парсера пройдены ===\n";
    return 0;
}