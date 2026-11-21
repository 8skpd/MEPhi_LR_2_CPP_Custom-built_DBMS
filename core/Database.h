#pragma once
#include <memory>
#include <string>

#include "DbExceptions.h"
#include "HashTable.h"
#include "Table.h"
#include "lib/DynamicArray.h"

// Реестр таблиц. Хранит таблицы по имени, обеспечивает
// создание, поиск и удаление с перестройкой карты имён.
class Database {
private:
    DynamicArray<std::unique_ptr<Table>> tables_;
    HashTable<std::string, size_t>       table_by_name_;

    void rebuild_name_map() {
        HashTable<std::string, size_t> fresh;
        for (size_t i = 0; i < tables_.size(); ++i) {
            fresh.Add(tables_[i]->name(), i);
        }
        table_by_name_ = std::move(fresh);
    }

    size_t table_pos(const std::string& name) const {
        if (!table_by_name_.ContainsKey(name)) {
            throw db::TableNotFound(__func__, "таблица '" + name + "' не найдена");
        }
        return table_by_name_.Get(name);
    }

public:
    void create_table(const std::string& name, Schema schema) {
        if (table_by_name_.ContainsKey(name)) {
            throw db::TableAlreadyExists(__func__, "таблица '" + name + "' уже существует");
        }
        table_by_name_.Add(name, tables_.size());
        tables_.push_back(std::make_unique<Table>(name, std::move(schema)));
    }

    // Загрузка готовой таблицы (используется командой LOAD)
    void create_table(Table table) {
        const std::string name = table.name();
        if (table_by_name_.ContainsKey(name)) {
            throw db::TableAlreadyExists(__func__, "таблица '" + name + "' уже существует");
        }
        table_by_name_.Add(name, tables_.size());
        tables_.push_back(std::make_unique<Table>(std::move(table)));
    }

    Table&       table(const std::string& name)       { return *tables_[table_pos(name)]; }
    const Table& table(const std::string& name) const { return *tables_[table_pos(name)]; }

    bool has_table(const std::string& name) const {
        return table_by_name_.ContainsKey(name);
    }

    void drop_table(const std::string& name) {
        const size_t pos = table_pos(name);
        tables_.erase(pos);
        rebuild_name_map();
    }

    DynamicArray<std::string> table_names() const {
        DynamicArray<std::string> out;
        for (size_t i = 0; i < tables_.size(); ++i) out.push_back(tables_[i]->name());
        return out;
    }

    size_t table_count() const { return tables_.size(); }
};