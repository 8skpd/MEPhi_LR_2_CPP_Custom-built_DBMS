#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <variant>
#include <vector>
#include <filesystem>

#include "../core/Database.h"
#include "../core/DbExceptions.h"
#include "../core/Query.h"
#include "../core/QueryExecutor.h"
#include "../core/SQLParser.h"
#include "../core/TreeExceptions.h"

// Вывод результата запроса

// Таблица для SELECT / SHOW / DESCRIBE
static void print_result_table(const QueryResult& r) {
    const size_t ncols = r.schema.size();
    if (ncols == 0 || r.rows.size() == 0) return;

    std::vector<std::string> headers(ncols);
    std::vector<size_t> widths(ncols, 0);
    for (size_t c = 0; c < ncols; ++c) {
        headers[c] = r.schema[c].name;
        widths[c] = headers[c].size();
    }

    std::vector<std::vector<std::string>> cells(r.rows.size());
    for (size_t i = 0; i < r.rows.size(); ++i) {
        cells[i].resize(ncols);
        for (size_t c = 0; c < ncols; ++c) {
            cells[i][c] = r.rows[i][c].to_string();
            widths[c] = std::max(widths[c], cells[i][c].size());
        }
    }

    auto separator = [&]() {
        std::cout << "+";
        for (size_t c = 0; c < ncols; ++c) {
            std::cout << std::string(widths[c] + 2, '-') << "+";
        }
        std::cout << "\n";
    };

    separator();
    std::cout << "|";
    for (size_t c = 0; c < ncols; ++c) {
        std::cout << " " << std::left << std::setw(static_cast<int>(widths[c]))
                  << headers[c] << " |";
    }
    std::cout << "\n";
    separator();

    for (size_t i = 0; i < cells.size(); ++i) {
        std::cout << "|";
        for (size_t c = 0; c < ncols; ++c) {
            std::cout << " " << std::left << std::setw(static_cast<int>(widths[c]))
                      << cells[i][c] << " |";
        }
        std::cout << "\n";
    }
    separator();
}

// Таблица для BENCHMARK
static void print_benchmark_table(const QueryResult& r) {
    std::cout << std::left
              << std::setw(20) << "Method"
              << std::setw(16) << "Build, ms"
              << std::setw(16) << "Search, ms"
              << "Found" << "\n";
    std::cout << std::string(58, '-') << "\n";
    std::cout << std::fixed << std::setprecision(3);
    for (size_t i = 0; i < r.benchmark.size(); ++i) {
        const BenchmarkRow& b = r.benchmark[i];
        std::cout << std::left << std::setw(20) << b.method;
        if (b.build_ms >= 0.0)  std::cout << std::setw(16) << b.build_ms;
        else                    std::cout << std::setw(16) << "-";
        if (b.search_ms >= 0.0) std::cout << std::setw(16) << b.search_ms;
        else                    std::cout << std::setw(16) << "-";
        std::cout << b.found << "\n";
    }
}

static void print_result(const QueryResult& r) {
    if (r.benchmark.size() > 0) {
        print_benchmark_table(r);
    }
    if (r.schema.size() > 0 && r.rows.size() > 0) {
        print_result_table(r);
    }
    if (!r.message.empty()) {
        std::cout << r.message << "\n";
    }
}

// Выгрузка бенчмарка в CSV
static void export_benchmark_csv(const Query& q, const QueryResult& r, size_t rows_in_table) {
    if (!std::holds_alternative<BenchmarkQuery>(q)) return;
    const BenchmarkQuery& bq = std::get<BenchmarkQuery>(q);

    std::filesystem::create_directories("./assets");
    std::ofstream f("./assets/benchmark_results.csv", std::ios::app);
    if (!f) return;
    if (f.tellp() == 0) {
        f << "mode,table,column,method,rows,build_ms,search_ms,found\n";
    }
    for (size_t i = 0; i < r.benchmark.size(); ++i) {
        const BenchmarkRow& b = r.benchmark[i];
        f << (bq.mode == BenchmarkQuery::Mode::Build ? "build" : "search") << ","
          << bq.table_name << ","
          << bq.column_name << ","
          << b.method << ","
          << rows_in_table << ","
          << b.build_ms << ","
          << b.search_ms << ","
          << b.found << "\n";
    }
    std::cout << "Результаты бенчмарка дописаны в ./assets/benchmark_results.csv\n";
}

// Баннер и REPL
static void print_banner() {
    std::cout << "======================================================\n";
    std::cout << "  Мини-СУБД на B-дереве и хеш-таблице\n";
    std::cout << "  Введите HELP для списка команд, EXIT для выхода.\n";
    std::cout << "======================================================\n";
}

int main() {
    print_banner();

    Database    db;
    SQLParser   parser;
    QueryExecutor executor(db);

    std::string line;
    std::cout << "sql> " << std::flush;
    while (std::getline(std::cin, line)) {
        // Пропустить пустые строки и комментарии
        const size_t first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            std::cout << "sql> " << std::flush;
            continue;
        }
        if (line.compare(first, 2, "--") == 0) {
            std::cout << "sql> " << std::flush;
            continue;
        }

        try {
            Query q = parser.parse(line);
            QueryResult r = executor.execute(q);
            print_result(r);

            if (std::holds_alternative<BenchmarkQuery>(q) && r.ok) {
                const BenchmarkQuery& bq = std::get<BenchmarkQuery>(q);
                size_t rows_in_table = 0;
                if (db.has_table(bq.table_name)) {
                    rows_in_table = db.table(bq.table_name).alive_count();
                }
                export_benchmark_csv(q, r, rows_in_table);
            }

            if (r.stop) break;
        } catch (const std::exception& e) {
            std::cout << "Ошибка: " << e.what() << "\n";
        }

        std::cout << "sql> " << std::flush;
    }

    std::cout << "\nРабота завершена.\n";
    return 0;
}