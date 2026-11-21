#pragma once
#include <algorithm>
#include <chrono>
#include <random>
#include <string>
#include "lib/DynamicArray.h"

#include "Database.h"
#include "Query.h"
#include "Serializer.h"

// Исполнитель запросов: превращает AST в QueryResult.
// Вся работа с данными делегируется Table (индексы, планировщик),
// здесь - только маршрутизация, вычисление предикатов и замеры.
class QueryExecutor {
private:
    using Clock = std::chrono::steady_clock;
    static constexpr int kSearchReps = 10;   // повторов замера поиска

    Database&  db_;
    std::mt19937 rng_;

    static double ms_between(Clock::time_point a, Clock::time_point b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    }

    // вычисление предикатов
    static bool row_matches(const Row& r, const Schema& s, const Condition& c) {
        return std::visit(Overload{
            [&](const ComparisonCondition& cc) -> bool {
                const Value& v = r[s.column_index(cc.column)];
                switch (cc.op) {
                    case CompareOp::Eq: return v == cc.value;
                    case CompareOp::Ne: return !(v == cc.value);
                    case CompareOp::Lt: return v < cc.value;
                    case CompareOp::Le: return !(cc.value < v);
                    case CompareOp::Gt: return cc.value < v;
                    case CompareOp::Ge: return !(v < cc.value);
                }
                return false;
            },
            [&](const BetweenCondition& bc) -> bool {
                const Value& v = r[s.column_index(bc.column)];
                return !(v < bc.lo) && !(bc.hi < v);
            }
        }, c);
    }

    static DynamicArray<size_t> all_alive_ids(const Table& t) {
        DynamicArray<size_t> out;
        for (size_t id = 0; id < t.row_count(); ++id) {
            if (!t.row(id).is_deleted()) out.push_back(id);
        }
        return out;
    }

    static DynamicArray<size_t> scan_condition(const Table& t, const Condition& c) {
        DynamicArray<size_t> out;
        for (size_t id = 0; id < t.row_count(); ++id) {
            const Row& r = t.row(id);
            if (r.is_deleted()) continue;
            if (row_matches(r, t.schema(), c)) out.push_back(id);
        }
        return out;
    }

    // Применение условий 1..N-1 к уже найденным кандидатам
    static DynamicArray<size_t> filter_rest(const Table& t, const WhereClause& w,
                                            const DynamicArray<size_t>& base) {
        DynamicArray<size_t> out;
        for (size_t i = 0; i < base.size(); ++i) {
            const Row& r = t.row(base[i]);
            if (r.is_deleted()) continue;
            bool ok = true;
            for (size_t k = 1; k < w.conditions.size() && ok; ++k) {
                ok = row_matches(r, t.schema(), w.conditions[k]);
            }
            if (ok) out.push_back(base[i]);
        }
        return out;
    }

    static bool index_usable_for(const Condition& c) {
        if (std::holds_alternative<BetweenCondition>(c)) return true;
        return std::get<ComparisonCondition>(c).op == CompareOp::Eq;
    }

    // Кандидаты по первому условию (индекс, если подходит) + фильтрация остальными
    DynamicArray<size_t> resolve_where(const Table& t, const WhereClause& w,
                                       const IIndex* forced, std::string* used_out) const {
        // Единая проверка типов всех условий - до любых обращений к данным
        for (size_t i = 0; i < w.conditions.size(); ++i) {
            std::visit(Overload{
                [&](const ComparisonCondition& c) {
                    t.check_column_type(t.schema().column_index(c.column), c.value);
                },
                [&](const BetweenCondition& c) {
                    const size_t col = t.schema().column_index(c.column);
                    t.check_column_type(col, c.lo);
                    t.check_column_type(col, c.hi);
                }
            }, w.conditions[i]);
        }
        if (w.conditions.empty()) return all_alive_ids(t);

        const Condition& first = w.conditions[0];
        const bool is_between = std::holds_alternative<BetweenCondition>(first);
        const std::string col_name = is_between
            ? std::get<BetweenCondition>(first).column
            : std::get<ComparisonCondition>(first).column;
        const size_t col = t.schema().column_index(col_name);

        std::string used = "LINEAR";
        DynamicArray<size_t> base;

        if (forced) {
            if (!index_usable_for(first)) {
                throw db::UnsupportedIndexOperation(__func__,
                    "индекс применим только к '=' и BETWEEN, получен другой оператор");
            }
            if (is_between && !forced->supports_range()) {
                throw db::UnsupportedIndexOperation(__func__,
                    "индекс не поддерживает диапазонные запросы");
            }
            used = forced->method_name();
            base = is_between
                ? forced->find_range(std::get<BetweenCondition>(first).lo,
                                     std::get<BetweenCondition>(first).hi)
                : forced->find_eq(std::get<ComparisonCondition>(first).value);
        } else if (index_usable_for(first)) {
            const IIndex* idx = t.pick_index(col, is_between);
            if (idx) {
                used = idx->method_name();
                base = is_between
                    ? idx->find_range(std::get<BetweenCondition>(first).lo,
                                      std::get<BetweenCondition>(first).hi)
                    : idx->find_eq(std::get<ComparisonCondition>(first).value);
            } else {
                base = scan_condition(t, first);
            }
        } else {
            base = scan_condition(t, first);
        }

        if (used_out) *used_out = used;
        return filter_rest(t, w, base);
    }

    // проекция колонок и LIMIT
    static QueryResult project(const Table& t, const DynamicArray<size_t>& ids,
                               const SelectQuery& q) {
        QueryResult r;
        DynamicArray<size_t> col_idx;

        if (q.all_columns) {
            r.schema = t.schema();
            for (size_t i = 0; i < t.schema().size(); ++i) col_idx.push_back(i);
        } else {
            for (size_t i = 0; i < q.columns.size(); ++i) {
                const size_t ci = t.schema().column_index(q.columns[i]);
                col_idx.push_back(ci);
                r.schema.add_column(t.schema()[ci]);
            }
        }

        const size_t total = ids.size();
        const size_t take = (q.limit.has_value() && *q.limit < total) ? *q.limit : total;
        for (size_t i = 0; i < take; ++i) {
            const Row& src = t.row(ids[i]);
            DynamicArray<Value> vals;
            for (size_t k = 0; k < col_idx.size(); ++k) vals.push_back(src[col_idx[k]]);
            r.rows.push_back(Row(std::move(vals)));
        }
        r.affected_rows = take;
        r.message = "строк: " + std::to_string(take) +
                    (take < total ? " из " + std::to_string(total) : "");
        return r;
    }

    // обработчики команд
    QueryResult exec_create_table(const CreateTableQuery& q) {
        db_.create_table(q.table_name, q.schema);
        QueryResult r;
        r.message = "таблица '" + q.table_name + "' создана, колонок: " +
                    std::to_string(q.schema.size());
        return r;
    }

    QueryResult exec_create_index(const CreateIndexQuery& q) {
        Table& t = db_.table(q.table_name);
        t.create_index(q.index_name, q.column_name, q.type, q.unique);
        QueryResult r;
        r.message = std::string("индекс '") + q.index_name + "' (" +
                    (q.type == IndexType::Hash ? "HASH" : "BTREE") +
                    ") построен по колонке " + q.column_name + ", записей: " +
                    std::to_string(t.get_index(q.index_name)->size());
        return r;
    }

    QueryResult exec_drop_index(const DropIndexQuery& q) {
        db_.table(q.table_name).drop_index(q.index_name);
        QueryResult r;
        r.message = "индекс '" + q.index_name + "' удалён";
        return r;
    }

    QueryResult exec_insert(const InsertQuery& q) {
        Table& t = db_.table(q.table_name);
        const size_t id = t.insert(q.values);
        QueryResult r;
        r.affected_rows = 1;
        r.message = "вставлена строка, row_id=" + std::to_string(id);
        return r;
    }

    QueryResult exec_delete(const DeleteQuery& q) {
        Table& t = db_.table(q.table_name);
        const DynamicArray<size_t> ids = q.where.has_value()
            ? resolve_where(t, *q.where, nullptr, nullptr)
            : all_alive_ids(t);
        for (size_t i = 0; i < ids.size(); ++i) t.remove_row(ids[i]);
        QueryResult r;
        r.affected_rows = ids.size();
        r.message = "удалено строк: " + std::to_string(ids.size());
        return r;
    }

    QueryResult exec_select(const SelectQuery& q) {
        const Table& t = db_.table(q.table_name);
        const IIndex* forced = q.force_index.has_value()
            ? t.get_index(*q.force_index) : nullptr;

        std::string used = "LINEAR";
        const DynamicArray<size_t> ids = q.where.has_value()
            ? resolve_where(t, *q.where, forced, &used)
            : all_alive_ids(t);

        QueryResult r = project(t, ids, q);
        r.message += ", метод поиска: " + used;
        return r;
    }

    QueryResult exec_show_tables() {
        QueryResult r;
        r.schema.add_column({"name",    ColumnType::String, false});
        r.schema.add_column({"rows",    ColumnType::Int,    false});
        r.schema.add_column({"indexes", ColumnType::Int,    false});
        for (const std::string& name : db_.table_names()) {
            const Table& t = db_.table(name);
            DynamicArray<Value> v;
            v.push_back(Value(name));
            v.push_back(Value(static_cast<int>(t.alive_count())));
            v.push_back(Value(static_cast<int>(t.index_count())));
            r.rows.push_back(Row(std::move(v)));
        }
        r.message = "таблиц: " + std::to_string(db_.table_count());
        return r;
    }

    QueryResult exec_show_indexes(const ShowIndexesQuery& q) {
        QueryResult r;
        r.schema.add_column({"index",   ColumnType::String, false});
        r.schema.add_column({"table",   ColumnType::String, false});
        r.schema.add_column({"column",  ColumnType::String, false});
        r.schema.add_column({"type",    ColumnType::String, false});
        r.schema.add_column({"unique",  ColumnType::String, false});
        r.schema.add_column({"entries", ColumnType::Int,    false});

        DynamicArray<std::string> names;
        if (q.table_name.empty()) {
            names = db_.table_names();
        } else {
            names.push_back(q.table_name);   // бросит TableNotFound, если нет
        }
        return exec_show_indexes_for(names, std::move(r));
    }

    QueryResult exec_show_indexes_for(const DynamicArray<std::string>& names,
                                      QueryResult r) {
        for (size_t i = 0; i < names.size(); ++i) {
            const Table& t = db_.table(names[i]);
            for (const Table::IndexInfo& info : t.all_indexes()) {
                DynamicArray<Value> v;
                v.push_back(Value(info.name));
                v.push_back(Value(names[i]));
                v.push_back(Value(t.schema()[info.column].name));
                v.push_back(Value(info.type == IndexType::Hash ? "HASH" : "BTREE"));
                v.push_back(Value(info.unique ? "yes" : "no"));
                v.push_back(Value(static_cast<int>(t.get_index(info.name)->size())));
                r.rows.push_back(Row(std::move(v)));
            }
        }
        r.message = "индексов: " + std::to_string(r.rows.size());
        return r;
    }

    QueryResult exec_describe(const DescribeQuery& q) {
        const Table& t = db_.table(q.table_name);
        QueryResult r;
        r.schema.add_column({"column",  ColumnType::String, false});
        r.schema.add_column({"type",    ColumnType::String, false});
        r.schema.add_column({"primary", ColumnType::String, false});
        for (size_t i = 0; i < t.schema().size(); ++i) {
            const ColumnDef& c = t.schema()[i];
            DynamicArray<Value> v;
            v.push_back(Value(c.name));
            v.push_back(Value(c.type == ColumnType::Int ? "INT"
                              : c.type == ColumnType::Double ? "DOUBLE" : "STRING"));
            v.push_back(Value(c.primary_key ? "yes" : "no"));
            r.rows.push_back(Row(std::move(v)));
        }
        r.message = "колонок: " + std::to_string(t.schema().size());
        return r;
    }

    QueryResult exec_generate(const GenerateQuery& q) {
        Table& t = db_.table(q.table_name);
        const size_t col = t.schema().column_index(q.column_name);
        if (t.schema()[col].type != ColumnType::Int) {
            throw db::TypeMismatch(__func__,
                "GENERATE поддерживает только целочисленные колонки");
        }

        // Перестановка 0..n-1: три порядка,
        // значения уникальны - безопасно даже для PRIMARY KEY
        DynamicArray<int> seq(q.count);
        for (size_t i = 0; i < q.count; ++i) seq[i] = static_cast<int>(i);
        if (q.order == DataOrder::SortedDesc) std::reverse(seq.begin(), seq.end());
        if (q.order == DataOrder::Random)     std::shuffle(seq.begin(), seq.end(), rng_);

        for (size_t i = 0; i < q.count; ++i) {
            DynamicArray<Value> vals;
            for (size_t c = 0; c < t.schema().size(); ++c) {
                if (c == col) {
                    vals.push_back(Value(seq[i]));
                } else if (t.schema()[c].type == ColumnType::Int) {
                    vals.push_back(Value(static_cast<int>(i)));
                } else if (t.schema()[c].type == ColumnType::Double) {
                    vals.push_back(Value(static_cast<double>(i)));
                } else {
                    vals.push_back(Value("row" + std::to_string(i)));
                }
            }
            t.insert(std::move(vals));
        }

        QueryResult r;
        r.affected_rows = q.count;
        r.message = "сгенерировано строк: " + std::to_string(q.count) +
                    ", порядок: " + to_string(q.order);
        return r;
    }

    QueryResult exec_benchmark(const BenchmarkQuery& q) {
        QueryResult r;
        Table& t = db_.table(q.table_name);

        // режим построения: сравнение времени создания индексов
        if (q.mode == BenchmarkQuery::Mode::Build) {
            const size_t col = t.schema().column_index(q.column_name);
            for (IndexType type : {IndexType::BTree, IndexType::Hash}) {
                const auto a = Clock::now();
                const auto idx = t.make_temp_index(col, type);
                const auto b = Clock::now();
                BenchmarkRow br;
                br.method   = (type == IndexType::Hash) ? "HASH" : "BTREE";
                br.build_ms = ms_between(a, b);
                br.found    = idx->size();
                r.benchmark.push_back(br);
            }
            r.message = "замер построения индексов по колонке " + q.column_name;
            return r;
        }

        // режим поиска
        if (!q.select.where.has_value() || q.select.where->conditions.empty()) {
            throw db::SqlSyntaxError(__func__,
                "BENCHMARK SELECT требует условие WHERE (точное или диапазон)");
        }
        const WhereClause& w = *q.select.where;
        const Condition& first = w.conditions[0];
        const bool is_between = std::holds_alternative<BetweenCondition>(first);
        const std::string col_name = is_between
            ? std::get<BetweenCondition>(first).column
            : std::get<ComparisonCondition>(first).column;
        const size_t col = t.schema().column_index(col_name);

        auto run_method = [&](const IIndex* idx) -> DynamicArray<size_t> {
            if (!idx) return filter_rest(t, w, scan_condition(t, first));
            const DynamicArray<size_t> base = is_between
                ? idx->find_range(std::get<BetweenCondition>(first).lo,
                                  std::get<BetweenCondition>(first).hi)
                : idx->find_eq(std::get<ComparisonCondition>(first).value);
            return filter_rest(t, w, base);
        };

        auto measure = [&](const IIndex* idx) -> BenchmarkRow {
            double total = 0.0;
            size_t found = 0;
            for (int rep = 0; rep < kSearchReps; ++rep) {
                const auto a = Clock::now();
                const DynamicArray<size_t> ids = run_method(idx);
                const auto b = Clock::now();
                total += ms_between(a, b);
                found = ids.size();
            }
            BenchmarkRow br;
            br.search_ms = total / kSearchReps;
            br.found     = found;
            return br;
        };

        if (q.select.force_index.has_value()) {
            const IIndex* idx = t.get_index(*q.select.force_index);
            if (is_between && !idx->supports_range()) {
                throw db::UnsupportedIndexOperation(__func__,
                    "индекс не поддерживает диапазонные запросы");
            }
            BenchmarkRow br = measure(idx);
            br.method = *q.select.force_index;
            r.benchmark.push_back(br);
        } else {
            for (const Table::IndexInfo& info : t.indexes_on(col)) {
                const IIndex* idx = t.get_index(info.name);
                BenchmarkRow br;
                if (is_between && !idx->supports_range()) {
                    br.method    = info.name;
                    br.search_ms = -1.0;   // неприменим к диапазону
                } else {
                    br = measure(idx);
                    br.method = info.name;
                }
                r.benchmark.push_back(br);
            }
        }

        BenchmarkRow linear = measure(nullptr);
        linear.method = "LINEAR";
        r.benchmark.push_back(linear);

        r.message = "замер поиска, повторов на метод: " + std::to_string(kSearchReps);
        return r;
    }

    static QueryResult exec_help() {
        QueryResult r;
        r.message =
            "Команды:\n"
            "  CREATE TABLE t (col INT|DOUBLE|STRING [PRIMARY KEY], ...)\n"
            "  CREATE [UNIQUE] INDEX i ON t(col) [USING BTREE|HASH]\n"
            "  DROP INDEX i ON t\n"
            "  INSERT INTO t VALUES (v1, v2, ...)\n"
            "  SELECT {* | cols} FROM t [WITH INDEX(i)] [WHERE ...] [LIMIT n]\n"
            "  DELETE FROM t [WHERE ...]\n"
            "  SHOW TABLES | SHOW INDEXES [ON t] | DESCRIBE t\n"
            "  GENERATE n ROWS FOR t WITH col SORTED ASC|SORTED DESC|RANDOM\n"
            "  BENCHMARK SELECT ... | BENCHMARK BUILD INDEX ON t(col)\n"
            "  SAVE t TO 'path' | LOAD t FROM 'path'   (следующий этап)\n"
            "  HELP | EXIT";
        return r;
    }

    QueryResult exec_save(const SaveQuery& q) {
        const Table& t = db_.table(q.table_name);
        db::save_table(t, q.path);
        QueryResult r;
        r.message = "таблица '" + q.table_name + "' сохранена в '" + q.path +
                    "', строк: " + std::to_string(t.alive_count());
        return r;
    }

    QueryResult exec_load(const LoadQuery& q) {
        Table t = db::load_table(q.path, q.table_name);
        const size_t rows = t.alive_count();
        db_.create_table(std::move(t));
        QueryResult r;
        r.message = "таблица '" + q.table_name + "' загружена из '" + q.path +
                    "', строк: " + std::to_string(rows);
        return r;
    }

public:
    explicit QueryExecutor(Database& db) : db_(db), rng_(2026) {}

    QueryResult execute(const Query& q) {
        return std::visit(Overload{
            [this](const CreateTableQuery& c) { return exec_create_table(c); },
            [this](const CreateIndexQuery& c) { return exec_create_index(c); },
            [this](const DropIndexQuery& c)   { return exec_drop_index(c); },
            [this](const InsertQuery& c)      { return exec_insert(c); },
            [this](const DeleteQuery& c)      { return exec_delete(c); },
            [this](const SelectQuery& c)      { return exec_select(c); },
            [this](const ShowTablesQuery&)    { return exec_show_tables(); },
            [this](const ShowIndexesQuery& c) { return exec_show_indexes(c); },
            [this](const DescribeQuery& c)    { return exec_describe(c); },
            [this](const GenerateQuery& c)    { return exec_generate(c); },
            [this](const BenchmarkQuery& c)   { return exec_benchmark(c); },
            [this](const SaveQuery& c)          { return exec_save(c); },
            [this](const LoadQuery& c)          { return exec_load(c); },
            [this](const HelpQuery&)          { return exec_help(); },
            [this](const ExitQuery&) {
                QueryResult r;
                r.stop = true;
                r.message = "завершение работы";
                return r;
            }
        }, q);
    }
};