#pragma once

#include <optional>
#include <string>
#include <variant>

#include "Index.h"   // IndexType
#include "Row.h"     // Schema, Row
#include "Value.h"   // Value

// AST SQL-подобных запросов мини-СУБД.
// Запрос - std::variant: компилятор гарантирует, что исполнитель обработает все виды запросов (std::visit).

// операторы сравнения в WHERE 
enum class CompareOp { Eq, Ne, Lt, Le, Gt, Ge };

inline const char* to_string(CompareOp op) {
    switch (op) {
        case CompareOp::Eq: return "=";
        case CompareOp::Ne: return "<>";
        case CompareOp::Lt: return "<";
        case CompareOp::Le: return "<=";
        case CompareOp::Gt: return ">";
        case CompareOp::Ge: return ">=";
    }
    return "?";
}

// условия WHERE
struct ComparisonCondition {
    std::string column;
    CompareOp   op = CompareOp::Eq;
    Value       value;
};

struct BetweenCondition {
    std::string column;
    Value       lo;
    Value       hi;
};

// Условие: сравнение ИЛИ диапазон
using Condition = std::variant<ComparisonCondition, BetweenCondition>;

// WHERE - конъюнкция условий (AND)
struct WhereClause {
    DynamicArray<Condition> conditions;
};

// DDL
struct CreateTableQuery {
    std::string table_name;
    Schema      schema;
};

struct CreateIndexQuery {
    std::string index_name;
    std::string table_name;
    std::string column_name;
    IndexType   type = IndexType::BTree;
    bool        unique = false;
};

struct DropIndexQuery {
    std::string index_name;
    std::string table_name;
};

// DML
struct InsertQuery {
    std::string         table_name;
    DynamicArray<Value> values;
};

struct DeleteQuery {
    std::string                table_name;
    std::optional<WhereClause> where;   // пусто = удалить все живые строки
};

// чтение
struct SelectQuery {
    std::string                table_name;
    bool                       all_columns = true;
    DynamicArray<std::string>  columns;        // используется, если !all_columns
    std::optional<WhereClause> where;
    std::optional<std::string> force_index;   // WITH INDEX(name)
    std::optional<size_t>      limit;         // LIMIT n
};

// служебные
struct ShowTablesQuery {};
struct ShowIndexesQuery { std::string table_name; };
struct DescribeQuery    { std::string table_name; };

// генерация тестовых данных
enum class DataOrder { SortedAsc, SortedDesc, Random };

inline const char* to_string(DataOrder o) {
    switch (o) {
        case DataOrder::SortedAsc:  return "SORTED ASC";
        case DataOrder::SortedDesc: return "SORTED DESC";
        case DataOrder::Random:     return "RANDOM";
    }
    return "?";
}

struct GenerateQuery {
    std::string table_name;
    std::string column_name;
    size_t      count = 0;
    DataOrder   order = DataOrder::Random;
};

// бенчмарки
struct BenchmarkQuery {
    enum class Mode { Search, Build };
    Mode        mode = Mode::Search;
    SelectQuery select;        // режим Search
    std::string table_name;    // режим Build
    std::string column_name;   // режим Build
};

//  сохранение / загрузка
struct SaveQuery { std::string table_name; std::string path; };
struct LoadQuery { std::string table_name; std::string path; };

// REPL
struct HelpQuery {};
struct ExitQuery {};

// Запрос
using Query = std::variant<
    CreateTableQuery,
    CreateIndexQuery,
    DropIndexQuery,
    InsertQuery,
    DeleteQuery,
    SelectQuery,
    ShowTablesQuery,
    ShowIndexesQuery,
    DescribeQuery,
    GenerateQuery,
    BenchmarkQuery,
    SaveQuery,
    LoadQuery,
    HelpQuery,
    ExitQuery>;

// Помощник для std::visit в исполнителе
template <class... Ts>
struct Overload : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
Overload(Ts...) -> Overload<Ts...>;

// Результат выполнения запроса
struct BenchmarkRow {
    std::string method;          // "BTREE" / "HASH" / "LINEAR"
    double      build_ms  = -1.0;  // -1 = не измерялось
    double      search_ms = -1.0;
    size_t      found     = 0;
};

struct QueryResult {
    bool        ok = true;
    bool        stop = false;
    std::string message;             // подтверждение DDL/DML или текст ошибки
    size_t      affected_rows = 0;   // для INSERT / DELETE

    Schema            schema;        // для SELECT / DESCRIBE / SHOW
    DynamicArray<Row> rows;

    DynamicArray<BenchmarkRow> benchmark;   // для BENCHMARK
};