#pragma once
#include "Value.h"
#include "Tree23.h"
#include "HashTable.h"
#include "DbExceptions.h"
#include "lib/DynamicArray.h"
#include <limits>
#include <memory>

enum class IndexType { BTree, Hash };

// Ключ индекса: (значение, row_id).
// Лексикографическое сравнение делает ключ уникальным,
// сохраняя порядок по значению - обходит запрет дубликатов Tree23.
struct IndexKey {
    Value  value;
    size_t row_id = 0;

    IndexKey() = default;
    IndexKey(const Value& v, size_t id) : value(v), row_id(id) {}

    bool operator==(const IndexKey& o) const {
        return row_id == o.row_id && value == o.value;
    }

    bool operator<(const IndexKey& o) const {
        if (value == o.value) return row_id < o.row_id;
        return value < o.value;
    }

    // Остальные операторы выражаем через < - единая точка истины
    bool operator>(const IndexKey& o)  const { return o < *this; }
    bool operator<=(const IndexKey& o) const { return !(o < *this); }
    bool operator>=(const IndexKey& o) const { return !(*this < o); }
};

// Единый интерфейс метода доступа
class IIndex {
public:
    virtual ~IIndex() = default;

    virtual void insert(const Value& v, size_t row_id) = 0;
    virtual void erase (const Value& v, size_t row_id) = 0;

    virtual DynamicArray<size_t> find_eq(const Value& v) const = 0;
    virtual DynamicArray<size_t> find_range(const Value& lo, const Value& hi) const = 0;

    virtual bool        supports_range() const = 0;
    virtual size_t      size() const = 0;
    virtual const char* method_name() const = 0;
};

// B-дерево: поддерживает и точный поиск, и диапазоны
class BTreeIndex : public IIndex {
private:
    Tree23<IndexKey> tree_;

    static DynamicArray<size_t> to_ids(const DynamicArray<IndexKey>& keys) {
        DynamicArray<size_t> ids;
        for (size_t i = 0; i < keys.size(); ++i) ids.push_back(keys[i].row_id);
        return ids;
    }

public:
    void insert(const Value& v, size_t row_id) override {
        IndexKey key(v, row_id);
        if (tree_.search(key)) {
            throw tree23::DuplicateKey(__func__);
        }
        tree_.insert(key);
    }

    void erase(const Value& v, size_t row_id) override {
        tree_.remove(IndexKey(v, row_id));
    }

    DynamicArray<size_t> find_eq(const Value& v) const override {
        return to_ids(tree_.range(IndexKey(v, 0),
                                  IndexKey(v, std::numeric_limits<size_t>::max())));
    }

    DynamicArray<size_t> find_range(const Value& lo, const Value& hi) const override {
        return to_ids(tree_.range(IndexKey(lo, 0),
                                  IndexKey(hi, std::numeric_limits<size_t>::max())));
    }

    bool        supports_range() const override { return true; }
    size_t      size() const override { return tree_.get_size(); }
    const char* method_name() const override { return "BTREE"; }
};

// Хеш-таблица: O(1) точный поиск, диапазоны не поддерживает
// Значение - список row_id (неуникальный индекс)
class HashIndex : public IIndex {
private:
    HashTable<Value, DynamicArray<size_t>> table_;
    size_t entries_ = 0;

public:
    void insert(const Value& v, size_t row_id) override {
        if (table_.ContainsKey(v)) {
            DynamicArray<size_t> ids = table_.Get(v);
            ids.push_back(row_id);
            table_.Set(v, ids);
        } else {
            DynamicArray<size_t> ids;
            ids.push_back(row_id);
            table_.Add(v, ids);
        }
        ++entries_;
    }

    void erase(const Value& v, size_t row_id) override {
        if (!table_.ContainsKey(v)) throw tree23::KeyNotFound(__func__);
        DynamicArray<size_t> ids = table_.Get(v);
        for (size_t i = 0; i < ids.size(); ++i) {
            if (ids[i] == row_id) {
                ids.erase(i);
                if (ids.size() == 0) table_.Remove(v);
                else                 table_.Set(v, ids);
                --entries_;
                return;
            }
        }
        throw tree23::KeyNotFound(__func__);
    }

    DynamicArray<size_t> find_eq(const Value& v) const override {
        if (!table_.ContainsKey(v)) return DynamicArray<size_t>();
        return table_.Get(v);
    }

    DynamicArray<size_t> find_range(const Value&, const Value&) const override {
        throw db::UnsupportedIndexOperation("HashIndex::find_range");
    }

    bool        supports_range() const override { return false; }
    size_t      size() const override { return entries_; }
    const char* method_name() const override { return "HASH"; }
};

inline std::unique_ptr<IIndex> make_index(IndexType t) {
    if (t == IndexType::BTree) return std::make_unique<BTreeIndex>();
    return std::make_unique<HashIndex>();
}