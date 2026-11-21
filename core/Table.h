#pragma once
#include "Row.h"
#include "Index.h"
#include "DbExceptions.h"
#include "HashTable.h"
#include "lib/DynamicArray.h"
#include <memory>
#include <string>

// Таблица: строки в порядке вставки + набор индексов.
//
// Инварианты:
//  - rows_ никогда не сортируется и не уплотняется;
//  - row_id == индекс в rows_, выдаётся монотонно, не переиспользуется;
//  - удаление = tombstone, поэтому все индексы остаются валидными;
//  - в индексах присутствуют только живые строки.
class Table {
public:
    struct IndexInfo {
        std::string name;
        size_t      column = 0;
        IndexType   type = IndexType::BTree;
        bool        unique = false;
        bool        is_primary = false;
    };

private:
    struct IndexEntry {
        IndexInfo               info;
        std::unique_ptr<IIndex> index;
    };

    std::string       name_;
    Schema            schema_;
    DynamicArray<Row> rows_;
    size_t            alive_ = 0;

    DynamicArray<IndexEntry>       indexes_;
    HashTable<std::string, size_t> index_by_name_;   // имя индекса - позиция

    // Наполнение индекса живыми строками (используется при CREATE и REBUILD)
    void fill_index(IIndex& idx, size_t column, bool unique) const {
        for (size_t id = 0; id < rows_.size(); ++id) {
            const Row& r = rows_[id];
            if (r.is_deleted()) continue;
            if (unique && idx.find_eq(r[column]).size() != 0) {
                throw db::UniqueViolation(__func__,
                    "дубликат значения в уникальном индексе");
            }
            idx.insert(r[column], id);
        }
    }

    // После удаления индекса позиции сдвигаются - карта имён перестраивается
    void rebuild_name_map() {
        HashTable<std::string, size_t> fresh;
        for (size_t i = 0; i < indexes_.size(); ++i) {
            fresh.Add(indexes_[i].info.name, i);
        }
        index_by_name_ = std::move(fresh);
    }

    size_t index_pos(const std::string& index_name) const {
        if (!index_by_name_.ContainsKey(index_name)) throw db::IndexNotFound(__func__);
        return index_by_name_.Get(index_name);
    }

    DynamicArray<size_t> scan_eq_impl(size_t column, const Value& v) const {
        DynamicArray<size_t> out;
        for (size_t id = 0; id < rows_.size(); ++id) {
            const Row& r = rows_[id];
            if (!r.is_deleted() && r[column] == v) out.push_back(id);
        }
        return out;
    }

    DynamicArray<size_t> scan_range_impl(size_t column,
                                         const Value& lo, const Value& hi) const {
        DynamicArray<size_t> out;
        for (size_t id = 0; id < rows_.size(); ++id) {
            const Row& r = rows_[id];
            if (r.is_deleted()) continue;
            if (!(r[column] < lo) && !(hi < r[column])) out.push_back(id);
        }
        return out;
    }

public:
    Table(std::string name, Schema schema)
        : name_(std::move(name)), schema_(std::move(schema)) {
        // PRIMARY KEY автоматически получает уникальный B-дерево-индекс
        int pk = schema_.primary_key_index();
        if (pk >= 0) {
            const std::string pk_name = "pk_" + name_ + "_" + schema_[pk].name;
            IndexEntry e;
            e.info = IndexInfo{pk_name, static_cast<size_t>(pk),
                               IndexType::BTree, true, true};
            e.index = make_index(IndexType::BTree);
            index_by_name_.Add(pk_name, indexes_.size());
            indexes_.push_back(std::move(e));
        }
    }

    //метаданные
    const std::string& name()   const { return name_; }
    const Schema&      schema() const { return schema_; }

    size_t alive_count() const { return alive_; }          // живых строк
    size_t row_count()   const { return rows_.size(); }    // включая tombstones

    const Row& row(size_t row_id) const {
        if (row_id >= rows_.size()) throw db::RowNotFound(__func__);
        return rows_[row_id];
    }

    // вставка
    // Возвращает row_id. Все проверки выполняются до мутаций:
    // при отказе таблица и индексы остаются в исходном состоянии.
    size_t insert(DynamicArray<Value> values) {
        // 1. Проверка арности
        if (values.size() != schema_.size()) {
            throw db::SchemaViolation(__func__,
                "число значений не совпадает с числом колонок");
        }

        // 2. Проверка типов через единую точку политики
        for (size_t i = 0; i < schema_.size(); ++i) {
            check_column_type(i, values[i]);
        }

        // 3. Проверка уникальности для всех индексов до вставки
        for (size_t k = 0; k < indexes_.size(); ++k) {
            const IndexEntry& e = indexes_[k];
            if (e.info.unique) {
                const DynamicArray<size_t> existing = e.index->find_eq(values[e.info.column]);
                if (existing.size() != 0) {
                    throw db::UniqueViolation(__func__,
                        "нарушена уникальность индекса " + e.info.name);
                }
            }
        }

        // 4. Вычисляем будущий row_id до любых мутаций
        const size_t row_id = rows_.size();

        // 5. Обновляем все индексы до добавления строки в rows_
        // Если какой-то индекс упадёт (теоретически), таблица останется в исходном состоянии
        for (size_t k = 0; k < indexes_.size(); ++k) {
            indexes_[k].index->insert(values[indexes_[k].info.column], row_id);
        }

        // 6. Только теперь добавляем строку и увеличиваем счётчик
        rows_.push_back(Row(std::move(values)));
        ++alive_;

        return row_id;
    }

    // Проверка типа значения против типа колонки.
    // Единая точка политики: используется и при вставке, и при запросах.
    void check_column_type(size_t column, const Value& v) const {
        if (column >= schema_.size()) throw db::ColumnNotFound(__func__);
        if (!value_matches_type(schema_[column].type, v)) {
            throw db::TypeMismatch(__func__,
                "тип значения не соответствует колонке " + schema_[column].name);
        }
    }

    // Линейный просмотр живых строк: эталон и fallback без индекса. O(N).
    DynamicArray<size_t> scan_eq(size_t column, const Value& v) const {
        check_column_type(column, v);
        return scan_eq_impl(column, v);
    }

    DynamicArray<size_t> scan_range(size_t column,
                                    const Value& lo, const Value& hi) const {
        check_column_type(column, lo);
        check_column_type(column, hi);
        return scan_range_impl(column, lo, hi);
    }

    // Запрос через планировщик: индекс, если подходит, иначе просмотр.
    DynamicArray<size_t> select_eq(size_t column, const Value& v) const {
        check_column_type(column, v);
        const IIndex* idx = pick_index(column, false);
        return idx ? idx->find_eq(v) : scan_eq_impl(column, v);
    }

    DynamicArray<size_t> select_range(size_t column,
                                      const Value& lo, const Value& hi) const {
        check_column_type(column, lo);
        check_column_type(column, hi);
        const IIndex* idx = pick_index(column, true);
        return idx ? idx->find_range(lo, hi) : scan_range_impl(column, lo, hi);
    }

    // удаление
    void remove_row(size_t row_id) {
        if (row_id >= rows_.size()) throw db::RowNotFound(__func__);
        Row& r = rows_[row_id];
        if (r.is_deleted()) throw db::RowNotFound(__func__, "строка уже удалена");

        for (size_t k = 0; k < indexes_.size(); ++k) {
            indexes_[k].index->erase(r[indexes_[k].info.column], row_id);
        }
        r.mark_deleted();
        --alive_;
    }

    // индексы 
    void create_index(const std::string& index_name, const std::string& column_name,
                      IndexType type, bool unique = false) {
        if (index_by_name_.ContainsKey(index_name)) {
            throw db::IndexAlreadyExists(__func__);
        }
        const size_t col = schema_.column_index(column_name);   // ColumnNotFound

        IndexEntry e;
        e.info = IndexInfo{index_name, col, type, unique, false};
        e.index = make_index(type);
        fill_index(*e.index, col, unique);      // может бросить UniqueViolation

        index_by_name_.Add(index_name, indexes_.size());
        indexes_.push_back(std::move(e));
    }

    void drop_index(const std::string& index_name) {
        const size_t pos = index_pos(index_name);
        if (indexes_[pos].info.is_primary) {
            throw db::UnsupportedIndexOperation(__func__,
                "нельзя удалить индекс, обеспечивающий PRIMARY KEY");
        }
        indexes_.erase(pos);
        rebuild_name_map();
    }

    // Перестроение индекса на текущих данных - для замера времени построения
    void rebuild_index(const std::string& index_name) {
        IndexEntry& e = indexes_[index_pos(index_name)];
        e.index = make_index(e.info.type);
        fill_index(*e.index, e.info.column, e.info.unique);
    }

    const IIndex* get_index(const std::string& index_name) const {
        return indexes_[index_pos(index_name)].index.get();
    }

    const IndexInfo& index_info(const std::string& index_name) const {
        return indexes_[index_pos(index_name)].info;
    }

    size_t index_count() const { return indexes_.size(); }

    DynamicArray<IndexInfo> indexes_on(size_t column) const {
        DynamicArray<IndexInfo> out;
        for (size_t i = 0; i < indexes_.size(); ++i) {
            if (indexes_[i].info.column == column) out.push_back(indexes_[i].info);
        }
        return out;
    }

        // Список всех индексов таблицы (для SHOW INDEXES)
    DynamicArray<IndexInfo> all_indexes() const {
        DynamicArray<IndexInfo> out;
        for (size_t i = 0; i < indexes_.size(); ++i) out.push_back(indexes_[i].info);
        return out;
    }

    // Построить временный индекс данного типа по колонке (для замеров построения)
    std::unique_ptr<IIndex> make_temp_index(size_t column, IndexType type) const {
        auto idx = make_index(type);
        fill_index(*idx, column, false);
        return idx;
    }

    // планировщик
    // Точный запрос  - предпочитаем HASH (O(1)), иначе BTREE.
    // Диапазонный    - только BTREE; хеш не подходит.
    // nullptr        - индекса нет, исполнитель делает линейный просмотр.
    const IIndex* pick_index(size_t column, bool need_range) const {
        const IIndex* fallback = nullptr;
        for (size_t i = 0; i < indexes_.size(); ++i) {
            const IndexEntry& e = indexes_[i];
            if (e.info.column != column) continue;
            if (need_range) {
                if (e.index->supports_range()) return e.index.get();
            } else {
                if (e.info.type == IndexType::Hash) return e.index.get();
                fallback = e.index.get();
            }
        }
        return need_range ? nullptr : fallback;
    }
};