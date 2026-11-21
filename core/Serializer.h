#pragma once
#include <cstdint>
#include <fstream>
#include <string>

#include "DbExceptions.h"
#include "Table.h"

// Бинарный формат файла таблицы.
//
//   [4] magic "MDB1"
//   [4] version
//   [4] число колонок
//   для каждой колонки: [4] длина имени, имя, [1] тип, [1] primary
//   [8] число строк (только живые; удалённые не сохраняются)
//   для каждой строки: значения колонок по типу
//   [4] число индексов (без авто-первичного ключа)
//   для каждого индекса: имя, [4] колонка, [1] тип, [1] unique
//
// При загрузке индексы перестраиваются по данным, поэтому
// файл хранит лишь их описание, а не содержимое деревьев/хешей.

namespace db {
namespace detail {

constexpr uint32_t DB_MAGIC   = 0x3142444D;  // "MDB1" в памяти
constexpr uint32_t DB_VERSION = 1;

template <typename T>
void write_raw(std::ofstream& f, const T& v) {
    f.write(reinterpret_cast<const char*>(&v), sizeof(T));
}

template <typename T>
T read_raw(std::ifstream& f, const char* what) {
    T v{};
    f.read(reinterpret_cast<char*>(&v), sizeof(T));
    if (!f) {
        throw db::SerializationError(__func__,
            std::string("файл обрезан при чтении: ") + what);
    }
    return v;
}

inline void write_string(std::ofstream& f, const std::string& s) {
    write_raw<uint32_t>(f, static_cast<uint32_t>(s.size()));
    f.write(s.data(), static_cast<std::streamsize>(s.size()));
}

inline std::string read_string(std::ifstream& f) {
    const uint32_t len = read_raw<uint32_t>(f, "длина строки");
    std::string s(len, '\0');
    if (len > 0) {
        f.read(s.data(), len);
        if (!f) throw db::SerializationError(__func__, "файл обрезан при чтении строки");
    }
    return s;
}

} // namespace detail

// Сохранение таблицы в файл
inline void save_table(const Table& t, const std::string& path) {
    using namespace detail;
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        throw db::SerializationError(__func__, "не удалось открыть файл для записи: " + path);
    }

    write_raw<uint32_t>(f, DB_MAGIC);
    write_raw<uint32_t>(f, DB_VERSION);

    // ---- Схема ----
    const Schema& s = t.schema();
    write_raw<uint32_t>(f, static_cast<uint32_t>(s.size()));
    for (size_t i = 0; i < s.size(); ++i) {
        write_string(f, s[i].name);
        write_raw<uint8_t>(f, static_cast<uint8_t>(s[i].type));
        write_raw<uint8_t>(f, s[i].primary_key ? 1 : 0);
    }

    // ---- Строки (только живые) ----
    write_raw<uint64_t>(f, static_cast<uint64_t>(t.alive_count()));
    for (size_t id = 0; id < t.row_count(); ++id) {
        const Row& r = t.row(id);
        if (r.is_deleted()) continue;
        for (size_t c = 0; c < s.size(); ++c) {
            const Value& v = r[c];
            switch (s[c].type) {
                case ColumnType::Int:
                    write_raw<int32_t>(f, std::get<int>(v.data));
                    break;
                case ColumnType::Double:
                    write_raw<double>(f, std::get<double>(v.data));
                    break;
                case ColumnType::String:
                    write_string(f, std::get<std::string>(v.data));
                    break;
            }
        }
    }

    // Индексы (пропускаем авто-первичный ключ)
    const DynamicArray<Table::IndexInfo> idxs = t.all_indexes();
    uint32_t idx_count = 0;
    for (size_t i = 0; i < idxs.size(); ++i) {
        if (!idxs[i].is_primary) ++idx_count;
    }
    write_raw<uint32_t>(f, idx_count);
    for (size_t i = 0; i < idxs.size(); ++i) {
        if (idxs[i].is_primary) continue;
        write_string(f, idxs[i].name);
        write_raw<uint32_t>(f, static_cast<uint32_t>(idxs[i].column));
        write_raw<uint8_t>(f, idxs[i].type == IndexType::Hash ? 1 : 0);
        write_raw<uint8_t>(f, idxs[i].unique ? 1 : 0);
    }
}

// Загрузка таблицы из файла.
// Имя таблицы задаётся вызывающим, что позволяет переименовать таблицу при загрузке.
inline Table load_table(const std::string& path, const std::string& table_name) {
    using namespace detail;
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        throw db::SerializationError(__func__, "не удалось открыть файл для чтения: " + path);
    }

    const uint32_t magic = read_raw<uint32_t>(f, "магик");
    if (magic != DB_MAGIC) {
        throw db::SerializationError(__func__, "неверный формат файла (магик)");
    }
    const uint32_t version = read_raw<uint32_t>(f, "версия");
    if (version != DB_VERSION) {
        throw db::SerializationError(__func__, "неподдерживаемая версия формата");
    }

    // Схема
    Schema schema;
    const uint32_t ncols = read_raw<uint32_t>(f, "число колонок");
    for (uint32_t i = 0; i < ncols; ++i) {
        ColumnDef col;
        col.name = read_string(f);
        col.type = static_cast<ColumnType>(read_raw<uint8_t>(f, "тип колонки"));
        col.primary_key = read_raw<uint8_t>(f, "флаг первичного ключа") != 0;
        schema.add_column(col);
    }

    Table t(table_name, std::move(schema));   // конструктор создаст авто-ПК, если есть

    // Строки
    const uint64_t nrows = read_raw<uint64_t>(f, "число строк");
    for (uint64_t i = 0; i < nrows; ++i) {
        DynamicArray<Value> vals;
        for (size_t c = 0; c < t.schema().size(); ++c) {
            switch (t.schema()[c].type) {
                case ColumnType::Int:
                    vals.push_back(Value(static_cast<int>(read_raw<int32_t>(f, "int"))));
                    break;
                case ColumnType::Double:
                    vals.push_back(Value(read_raw<double>(f, "double")));
                    break;
                case ColumnType::String:
                    vals.push_back(Value(read_string(f)));
                    break;
            }
        }
        t.insert(std::move(vals));
    }

    // Индексы (перестраиваются по загруженным данным)
    const uint32_t nidx = read_raw<uint32_t>(f, "число индексов");
    for (uint32_t i = 0; i < nidx; ++i) {
        const std::string name = read_string(f);
        const uint32_t col = read_raw<uint32_t>(f, "колонка индекса");
        const uint8_t type = read_raw<uint8_t>(f, "тип индекса");
        const uint8_t uniq = read_raw<uint8_t>(f, "уникальность индекса");
        const std::string col_name = t.schema()[col].name;
        t.create_index(name, col_name,
                       type == 1 ? IndexType::Hash : IndexType::BTree,
                       uniq != 0);
    }

    return t;
}

} // namespace db