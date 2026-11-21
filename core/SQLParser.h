#pragma once

#include <cctype>
#include <climits>
#include <string>

#include "DbExceptions.h"
#include "Query.h"

// Лексема
enum class TokenKind { Ident, Number, String, Symbol, End };

struct Token {
    TokenKind   kind = TokenKind::End;
    std::string text;    // идентификатор / символ / исходный текст литерала
    Value       value;   // для Number и String - готовое значение
    size_t      pos = 0; // позиция в исходной строке (для сообщений об ошибках)
};

// SQL-парсер: токенизатор + рекурсивный спуск по командам.
// Ключевые слова нечувствительны к регистру, идентификаторы - чувствительны.
// Все ошибки - db::SqlSyntaxError с указанием позиции.
class SQLParser {
private:
    DynamicArray<Token> tokens_;
    size_t idx_ = 0;

        // Лексический анализ
        static DynamicArray<Token> tokenize(const std::string& sql) {
        DynamicArray<Token> out;
        size_t i = 0;
        const size_t n = sql.size();

        auto skip_space_and_comments = [&]() {
            while (i < n) {
                if (std::isspace(static_cast<unsigned char>(sql[i]))) { ++i; continue; }
                if (i + 1 < n && sql[i] == '-' && sql[i + 1] == '-') {   // комментарий до конца строки
                    while (i < n && sql[i] != '\n') ++i;
                    continue;
                }
                break;
            }
        };

        while (true) {
            skip_space_and_comments();
            if (i >= n) break;

            Token t;
            t.pos = i;
            const char c = sql[i];

            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                const size_t start = i;
                while (i < n && (std::isalnum(static_cast<unsigned char>(sql[i])) || sql[i] == '_')) ++i;
                t.kind = TokenKind::Ident;
                t.text = sql.substr(start, i - start);
            }
            else if (std::isdigit(static_cast<unsigned char>(c)) ||
                     (c == '-' && i + 1 < n && std::isdigit(static_cast<unsigned char>(sql[i + 1])))) {
                const size_t start = i;
                if (sql[i] == '-') ++i;
                while (i < n && std::isdigit(static_cast<unsigned char>(sql[i]))) ++i;
                bool is_double = false;
                if (i + 1 < n && sql[i] == '.' &&
                    std::isdigit(static_cast<unsigned char>(sql[i + 1]))) {
                    is_double = true;
                    ++i;
                    while (i < n && std::isdigit(static_cast<unsigned char>(sql[i]))) ++i;
                }
                t.kind = TokenKind::Number;
                t.text = sql.substr(start, i - start);
                try {
                    if (is_double) {
                        t.value = Value(std::stod(t.text));
                    } else {
                        const long long ll = std::stoll(t.text);
                        if (ll < INT_MIN || ll > INT_MAX) {
                            throw std::out_of_range(t.text);
                        }
                        t.value = Value(static_cast<int>(ll));
                    }
                } catch (const std::exception&) {
                    throw db::SqlSyntaxError(__func__,
                        "позиция " + std::to_string(t.pos) +
                        ": число вне допустимого диапазона: " + t.text);
                }
            }
            else if (c == '\'') {
                ++i;
                std::string acc;
                bool closed = false;
                while (i < n) {
                    if (sql[i] == '\'') {
                        if (i + 1 < n && sql[i + 1] == '\'') { acc += '\''; i += 2; continue; }
                        ++i; closed = true; break;
                    }
                    acc += sql[i];
                    ++i;
                }
                if (!closed) {
                    throw db::SqlSyntaxError(__func__,
                        "позиция " + std::to_string(t.pos) + ": незакрытая строковая константа");
                }
                t.kind = TokenKind::String;
                t.text = acc;
                t.value = Value(acc);
            }
            else {
                // сначала двухсимвольные операторы сравнения
                if (i + 1 < n) {
                    const std::string two = sql.substr(i, 2);
                    if (two == "<=" || two == ">=" || two == "<>" || two == "!=") {
                        t.kind = TokenKind::Symbol;
                        t.text = two;
                        i += 2;
                        out.push_back(t);
                        continue;
                    }
                }
                if (c == '(' || c == ')' || c == ',' || c == '*' ||
                    c == '=' || c == '<' || c == '>' || c == ';') {
                    t.kind = TokenKind::Symbol;
                    t.text = std::string(1, c);
                    ++i;
                } else {
                    throw db::SqlSyntaxError(__func__,
                        "позиция " + std::to_string(i) +
                        ": неожиданный символ '" + std::string(1, c) + "'");
                }
            }
            out.push_back(t);
        }

        Token end;
        end.kind = TokenKind::End;
        end.pos = n;
        out.push_back(end);
        return out;
    }

        // Курсор по лексемам
        const Token& peek() const {
        const size_t i = idx_ < tokens_.size() ? idx_ : tokens_.size() - 1;
        return tokens_[i];
    }

    const Token& advance() {
        const Token& t = peek();
        if (t.kind != TokenKind::End) ++idx_;
        return t;
    }

    static std::string upper(const std::string& s) {
        std::string r = s;
        for (char& ch : r) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        return r;
    }

    static bool is_reserved(const std::string& word) {
        static const char* kw[] = {
            "SELECT", "FROM", "WHERE", "TABLE", "INDEX", "ON", "USING", "VALUES",
            "INSERT", "INTO", "DELETE", "CREATE", "DROP", "SHOW", "DESCRIBE",
            "GENERATE", "BENCHMARK", "SAVE", "LOAD", "HELP", "EXIT", "QUIT",
            "LIMIT", "WITH", "BETWEEN", "AND", "SORTED", "ASC", "DESC",
            "RANDOM", "ROWS", "FOR", "PRIMARY", "KEY", "UNIQUE", "TO"
        };
        for (const char* k : kw) if (word == k) return true;
        return false;
    }

    bool check_keyword(const char* kw) const {
        return peek().kind == TokenKind::Ident && upper(peek().text) == kw;
    }

    bool match_keyword(const char* kw) {
        if (!check_keyword(kw)) return false;
        advance();
        return true;
    }

    void expect_keyword(const char* kw) {
        if (!match_keyword(kw)) {
            throw db::SqlSyntaxError(__func__,
                "позиция " + std::to_string(peek().pos) + ": ожидалось ключевое слово " +
                kw + ", получено '" + peek().text + "'");
        }
    }

    bool check_symbol(const char* sym) const {
        return peek().kind == TokenKind::Symbol && peek().text == sym;
    }

    bool match_symbol(const char* sym) {
        if (!check_symbol(sym)) return false;
        advance();
        return true;
    }

    void expect_symbol(const char* sym) {
        if (!match_symbol(sym)) {
            throw db::SqlSyntaxError(__func__,
                "позиция " + std::to_string(peek().pos) + ": ожидался символ '" +
                sym + "', получено '" + peek().text + "'");
        }
    }

    std::string parse_identifier(const char* what) {
        const Token& t = peek();
        if (t.kind != TokenKind::Ident) {
            throw db::SqlSyntaxError(__func__,
                "позиция " + std::to_string(t.pos) + ": ожидалось имя (" + what +
                "), получено '" + t.text + "'");
        }
        const std::string u = upper(t.text);
        if (is_reserved(u)) {
            throw db::SqlSyntaxError(__func__,
                "позиция " + std::to_string(t.pos) + ": имя '" + t.text +
                "' совпадает с ключевым словом");
        }
        return advance().text;
    }

    Value parse_literal() {
        const Token& t = peek();
        if (t.kind == TokenKind::Number || t.kind == TokenKind::String) {
            advance();
            return t.value;
        }
        throw db::SqlSyntaxError(__func__,
            "позиция " + std::to_string(t.pos) +
            ": ожидалось значение (число или 'строка'), получено '" + t.text + "'");
    }

    static ColumnType parse_column_type(const Token& t) {
        const std::string u = upper(t.text);
        if (u == "INT" || u == "INTEGER")                return ColumnType::Int;
        if (u == "DOUBLE" || u == "REAL" || u == "FLOAT") return ColumnType::Double;
        if (u == "STRING" || u == "TEXT" || u == "VARCHAR") return ColumnType::String;
        throw db::SqlSyntaxError(__func__,
            "позиция " + std::to_string(t.pos) + ": неизвестный тип колонки '" + t.text + "'");
    }

        // Разбор отдельных конструкций
        Schema parse_schema() {
        Schema s;
        expect_symbol("(");
        while (true) {
            ColumnDef col;
            col.name = parse_identifier("имя колонки");
            if (peek().kind != TokenKind::Ident) {
                throw db::SqlSyntaxError(__func__,
                    "позиция " + std::to_string(peek().pos) +
                    ": ожидался тип колонки (INT, DOUBLE, STRING)");
            }
            col.type = parse_column_type(advance());
            if (match_keyword("PRIMARY")) {
                expect_keyword("KEY");
                col.primary_key = true;
            }
            s.add_column(col);
            if (match_symbol(",")) continue;
            break;
        }
        expect_symbol(")");
        return s;
    }

    Condition parse_condition() {
        const std::string col = parse_identifier("имя колонки");

        if (match_keyword("BETWEEN")) {
            const Value lo = parse_literal();
            expect_keyword("AND");
            const Value hi = parse_literal();
            return BetweenCondition{col, lo, hi};
        }

        const Token& t = peek();
        CompareOp op;
        if (t.kind == TokenKind::Symbol && t.text == "=")              op = CompareOp::Eq;
        else if (t.kind == TokenKind::Symbol && (t.text == "<>" || t.text == "!=")) op = CompareOp::Ne;
        else if (t.kind == TokenKind::Symbol && t.text == "<")         op = CompareOp::Lt;
        else if (t.kind == TokenKind::Symbol && t.text == "<=")        op = CompareOp::Le;
        else if (t.kind == TokenKind::Symbol && t.text == ">")         op = CompareOp::Gt;
        else if (t.kind == TokenKind::Symbol && t.text == ">=")        op = CompareOp::Ge;
        else {
            throw db::SqlSyntaxError(__func__,
                "позиция " + std::to_string(t.pos) +
                ": ожидался оператор сравнения (=, <>, <, <=, >, >=) или BETWEEN");
        }
        advance();
        return ComparisonCondition{col, op, parse_literal()};
    }

    WhereClause parse_where() {
        WhereClause w;
        w.conditions.push_back(parse_condition());
        while (match_keyword("AND")) {
            w.conditions.push_back(parse_condition());
        }
        return w;
    }

    SelectQuery parse_select() {
        SelectQuery q;

        if (match_symbol("*")) {
            q.all_columns = true;
        } else {
            q.all_columns = false;
            q.columns.push_back(parse_identifier("имя колонки"));
            while (match_symbol(",")) {
                q.columns.push_back(parse_identifier("имя колонки"));
            }
        }

        expect_keyword("FROM");
        q.table_name = parse_identifier("имя таблицы");

        // Необязательные клаузы в любом порядке, но без повторов
        bool saw_with = false, saw_where = false, saw_limit = false;
        while (true) {
            if (check_keyword("WITH")) {
                if (saw_with) throw db::SqlSyntaxError(__func__, "повторная клауза WITH INDEX");
                advance();
                expect_keyword("INDEX");
                expect_symbol("(");
                q.force_index = parse_identifier("имя индекса");
                expect_symbol(")");
                saw_with = true;
            } else if (check_keyword("WHERE")) {
                if (saw_where) throw db::SqlSyntaxError(__func__, "повторная клауза WHERE");
                advance();
                q.where = parse_where();
                saw_where = true;
            } else if (check_keyword("LIMIT")) {
                if (saw_limit) throw db::SqlSyntaxError(__func__, "повторная клауза LIMIT");
                advance();
                const Value v = parse_literal();
                if (!v.is_int() || std::get<int>(v.data) < 0) {
                    throw db::SqlSyntaxError(__func__, "LIMIT ожидает неотрицательное целое");
                }
                q.limit = static_cast<size_t>(std::get<int>(v.data));
                saw_limit = true;
            } else {
                break;
            }
        }
        return q;
    }

        // Команды верхнего уровня
        Query parse_create_table() {
        expect_keyword("TABLE");
        CreateTableQuery q;
        q.table_name = parse_identifier("имя таблицы");
        q.schema = parse_schema();
        return q;
    }

    Query parse_create_index() {
        CreateIndexQuery q;
        q.unique = match_keyword("UNIQUE");
        expect_keyword("INDEX");
        q.index_name = parse_identifier("имя индекса");
        expect_keyword("ON");
        q.table_name = parse_identifier("имя таблицы");
        expect_symbol("(");
        q.column_name = parse_identifier("имя колонки");
        expect_symbol(")");
        if (match_keyword("USING")) {
            if (match_keyword("HASH"))            q.type = IndexType::Hash;
            else if (match_keyword("BTREE"))      q.type = IndexType::BTree;
            else {
                throw db::SqlSyntaxError(__func__,
                    "позиция " + std::to_string(peek().pos) + ": ожидался BTREE или HASH");
            }
        }
        return q;
    }

    Query parse_drop_index() {
        expect_keyword("INDEX");
        DropIndexQuery q;
        q.index_name = parse_identifier("имя индекса");
        expect_keyword("ON");
        q.table_name = parse_identifier("имя таблицы");
        return q;
    }

    Query parse_insert() {
        expect_keyword("INTO");
        InsertQuery q;
        q.table_name = parse_identifier("имя таблицы");
        expect_keyword("VALUES");
        expect_symbol("(");
        q.values.push_back(parse_literal());
        while (match_symbol(",")) {
            q.values.push_back(parse_literal());
        }
        expect_symbol(")");
        return q;
    }

    Query parse_delete() {
        expect_keyword("FROM");
        DeleteQuery q;
        q.table_name = parse_identifier("имя таблицы");
        if (match_keyword("WHERE")) q.where = parse_where();
        return q;
    }

    Query parse_show() {
        if (match_keyword("TABLES")) return ShowTablesQuery{};
        if (match_keyword("INDEXES")) {
            ShowIndexesQuery q;
            if (match_keyword("ON")) q.table_name = parse_identifier("имя таблицы");
            return q;
        }
        throw db::SqlSyntaxError(__func__, "после SHOW ожидалось TABLES или INDEXES");
    }

    Query parse_generate() {
        GenerateQuery q;
        const Value cnt = parse_literal();
        if (!cnt.is_int() || std::get<int>(cnt.data) < 0) {
            throw db::SqlSyntaxError(__func__, "GENERATE ожидает неотрицательное число строк");
        }
        q.count = static_cast<size_t>(std::get<int>(cnt.data));
        expect_keyword("ROWS");
        expect_keyword("FOR");
        q.table_name = parse_identifier("имя таблицы");
        expect_keyword("WITH");
        q.column_name = parse_identifier("имя колонки");
        if (match_keyword("SORTED")) {
            if (match_keyword("ASC"))           q.order = DataOrder::SortedAsc;
            else if (match_keyword("DESC"))     q.order = DataOrder::SortedDesc;
            else throw db::SqlSyntaxError(__func__, "после SORTED ожидалось ASC или DESC");
        } else if (match_keyword("RANDOM")) {
            q.order = DataOrder::Random;
        } else {
            throw db::SqlSyntaxError(__func__,
                "ожидалось SORTED ASC, SORTED DESC или RANDOM");
        }
        return q;
    }

    Query parse_benchmark() {
        BenchmarkQuery q;
        if (check_keyword("SELECT")) {
            advance();
            q.mode = BenchmarkQuery::Mode::Search;
            q.select = parse_select();
            q.table_name = q.select.table_name;
        } else {
            expect_keyword("BUILD");
            expect_keyword("INDEX");
            expect_keyword("ON");
            q.mode = BenchmarkQuery::Mode::Build;
            q.table_name = parse_identifier("имя таблицы");
            expect_symbol("(");
            q.column_name = parse_identifier("имя колонки");
            expect_symbol(")");
        }
        return q;
    }

    Query parse_statement() {
        if (match_keyword("CREATE")) {
            if (check_keyword("TABLE")) return parse_create_table();
            if (check_keyword("INDEX") || check_keyword("UNIQUE")) return parse_create_index();
            throw db::SqlSyntaxError(__func__, "после CREATE ожидалось TABLE или INDEX");
        }
        if (match_keyword("DROP"))      return parse_drop_index();
        if (match_keyword("INSERT"))    return parse_insert();
        if (match_keyword("DELETE"))    return parse_delete();
        if (match_keyword("SELECT"))    return parse_select();
        if (match_keyword("SHOW"))      return parse_show();
        if (match_keyword("DESCRIBE"))  { DescribeQuery q; q.table_name = parse_identifier("имя таблицы"); return q; }
        if (match_keyword("GENERATE"))  return parse_generate();
        if (match_keyword("BENCHMARK")) return parse_benchmark();
        if (match_keyword("SAVE")) {
            SaveQuery q;
            q.table_name = parse_identifier("имя таблицы");
            expect_keyword("TO");
            if (peek().kind != TokenKind::String) {
                throw db::SqlSyntaxError(__func__, "путь задаётся строкой в одинарных кавычках");
            }
            q.path = advance().text;
            return q;
        }
        if (match_keyword("LOAD")) {
            LoadQuery q;
            q.table_name = parse_identifier("имя таблицы");
            expect_keyword("FROM");
            if (peek().kind != TokenKind::String) {
                throw db::SqlSyntaxError(__func__, "путь задаётся строкой в одинарных кавычках");
            }
            q.path = advance().text;
            return q;
        }
        if (match_keyword("HELP")) return HelpQuery{};
        if (match_keyword("EXIT") || match_keyword("QUIT")) return ExitQuery{};

        throw db::SqlSyntaxError(__func__,
            "позиция " + std::to_string(peek().pos) + ": неизвестная команда '" + peek().text + "'");
    }

public:
    // Разбор одной команды. Парсер не хранит состояние между вызовами.
    Query parse(const std::string& sql) {
        tokens_ = tokenize(sql);
        idx_ = 0;

        Query q = parse_statement();

        match_symbol(";");   // необязательная точка с запятой
        if (peek().kind != TokenKind::End) {
            throw db::SqlSyntaxError(__func__,
                "позиция " + std::to_string(peek().pos) +
                ": лишние символы после команды: '" + peek().text + "'");
        }
        return q;
    }
};