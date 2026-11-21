#pragma once
#include <filesystem>
#include <string>

// =========================================================
// Политика путей проекта:
//   assets/benchmark_results.csv — результаты бенчмарков
//   assets/data/*.db             — дампы таблиц
// =========================================================
namespace paths {

namespace fs = std::filesystem;

inline fs::path assets_dir() { return fs::path("assets"); }
inline fs::path data_dir()   { return assets_dir() / "data"; }

inline void ensure_parent_dir(const fs::path& p) {
    const fs::path parent = p.parent_path();
    if (!parent.empty()) fs::create_directories(parent);
}

inline fs::path benchmark_csv_path() {
    fs::create_directories(assets_dir());
    return assets_dir() / "benchmark_results.csv";
}

// Относительные пути трактуем как пути внутри assets/data.
// Абсолютные пути и явные пути вида assets/... уважаем как есть.
inline fs::path resolve_db_path(const std::string& raw) {
    fs::path p(raw);
    if (p.is_absolute()) {
        ensure_parent_dir(p);
        return p;
    }

    const fs::path norm = p.lexically_normal();
    auto it = norm.begin();
    if (it != norm.end() && *it == fs::path("assets")) {
        ensure_parent_dir(norm);
        return norm;
    }

    const fs::path result = data_dir() / norm;
    ensure_parent_dir(result);
    return result;
}

} // namespace paths