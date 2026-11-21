#pragma once
#include "Value.h"
#include "HashTable.h"
#include "DbExceptions.h"
#include "lib/DynamicArray.h"
#include <string>

enum class ColumnType { Int, Double, String };

inline bool value_matches_type(ColumnType t, const Value& v) {
    switch (t) {
        case ColumnType::Int:    return v.is_int();
        case ColumnType::Double: return v.is_double();
        case ColumnType::String: return v.is_string();
    }
    return false;
}

struct ColumnDef {
    std::string name;
    ColumnType  type = ColumnType::Int;
    bool        primary_key = false;
};

// Схема таблицы: имена колонок фиксированы при CREATE TABLE
class Schema {
private:
    DynamicArray<ColumnDef>      columns_;
    HashTable<std::string, size_t> name_to_pos_;   // своя хеш-таблица

public:
    void add_column(const ColumnDef& col) {
        if (name_to_pos_.ContainsKey(col.name)) {
            throw db::ColumnAlreadyExists(__func__);
        }
        name_to_pos_.Add(col.name, columns_.size());
        columns_.push_back(col);
    }

    size_t size() const { return columns_.size(); }

    const ColumnDef& operator[](size_t i) const { return columns_[i]; }

    bool has_column(const std::string& name) const {
        return name_to_pos_.ContainsKey(name);
    }

    size_t column_index(const std::string& name) const {
        if (!name_to_pos_.ContainsKey(name)) throw db::ColumnNotFound(__func__);
        return name_to_pos_.Get(name);
    }

    // -1, если первичного ключа нет
    int primary_key_index() const {
        for (size_t i = 0; i < columns_.size(); ++i) {
            if (columns_[i].primary_key) return static_cast<int>(i);
        }
        return -1;
    }
};

// Строка: значения позиционно, схеме.
// deleted - tombstone: row_id остаётся стабильным навсегда.
class Row {
private:
    DynamicArray<Value> values_;
    bool deleted_ = false;

public:
    Row() = default;
    explicit Row(DynamicArray<Value> v) : values_(std::move(v)) {}

    size_t size() const { return values_.size(); }

    const Value& operator[](size_t i) const { return values_[i]; }
    Value&       operator[](size_t i)       { return values_[i]; }

    bool is_deleted() const { return deleted_; }
    void mark_deleted()     { deleted_ = true; }
};