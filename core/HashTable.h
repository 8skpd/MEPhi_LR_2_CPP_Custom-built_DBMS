#pragma once

#include "TreeExceptions.h"
#include "lib/DynamicArray.h"
#include <functional>
#include <cstddef>

// Пара ключ-значение для хеш-таблицы
template <typename TKey, typename TValue>
struct KeyValuePair {
    TKey key;
    TValue value;

    KeyValuePair() = default;
    KeyValuePair(const TKey& k, const TValue& v) : key(k), value(v) {}

    bool operator==(const KeyValuePair& other) const {
        return key == other.key && value == other.value;
    }
};

// Хеш-таблица, реализующая интерфейс IDictionary<TKey, TValue>
// Поддерживает разрешение коллизий методом цепочек
// и автоматическую перестройку при изменении коэффициента заполнения.
template <typename TKey, typename TValue, typename Hash = std::hash<TKey>>
class HashTable {
private:
    using Bucket = DynamicArray<KeyValuePair<TKey, TValue>>;
    
    DynamicArray<Bucket> buckets;
    size_t count = 0;
    size_t capacity = 0;
    Hash hasher;

    // Коэффициенты для перестройки
    // p: сжимать, если count <= capacity / p
    // q: множитель изменения размера
    double p_factor = 2.0; 
    double q_factor = 2.0;
    size_t min_capacity = 16;

    size_t get_bucket_index(const TKey& key) const {
        return hasher(key) % capacity;
    }

    void rehash(size_t new_capacity) {
        DynamicArray<Bucket> new_buckets;
        new_buckets.resize(new_capacity);  // Создаем new_capacity пустых корзин
        
        for (size_t i = 0; i < buckets.size(); ++i) {
            for (size_t j = 0; j < buckets[i].size(); ++j) {
                const auto& pair = buckets[i][j];
                size_t idx = hasher(pair.key) % new_capacity;
                new_buckets[idx].push_back(pair);
            }
        }
        buckets = std::move(new_buckets);
        capacity = new_capacity;
    }

    void check_resize() {
        if (capacity == 0) {
            rehash(min_capacity);
            return;
        }
        // Если таблица заполнилась (n == c), расширяем в q раз
        if (count >= capacity) {
            rehash(static_cast<size_t>(capacity * q_factor));
        } 
        // Если таблица стала слишком разреженной (n <= c / p), сжимаем в q раз
        else if (count <= capacity / p_factor && capacity > min_capacity) {
            rehash(static_cast<size_t>(capacity / q_factor));
        }
    }

public:
    HashTable(size_t initial_capacity = 16, Hash h = Hash()) 
        : hasher(h) {
        rehash(initial_capacity);
    }

    // Добавление элемента
    void Add(const TKey& key, const TValue& value) {
        if (ContainsKey(key)) {
            throw tree23::DuplicateKey(__func__);
        }
        size_t idx = get_bucket_index(key);
        buckets[idx].push_back(KeyValuePair<TKey, TValue>(key, value));
        count++;
        check_resize();
    }

    // Получение элемента по ключу
    TValue Get(const TKey& key) const {
        size_t idx = get_bucket_index(key);
        for (size_t i = 0; i < buckets[idx].size(); ++i) {
            if (buckets[idx][i].key == key) {
                return buckets[idx][i].value;
            }
        }
        throw tree23::KeyNotFound(__func__);
    }

    // Проверка наличия ключа
    bool ContainsKey(const TKey& key) const {
        size_t idx = get_bucket_index(key);
        for (size_t i = 0; i < buckets[idx].size(); ++i) {
            if (buckets[idx][i].key == key) {
                return true;
            }
        }
        return false;
    }
    
    // Удаление элемента
    void Remove(const TKey& key) {
        size_t idx = get_bucket_index(key);
        auto& bucket = buckets[idx];

        // Ищем элемент в корзине
        size_t found_index = bucket.size(); // Используем размер как маркер "не найдено"
        for (size_t i = 0; i < bucket.size(); ++i) {
            if (bucket[i].key == key) {
                found_index = i;
                break;
            }
        }

        // Если не нашли, выбрасываем исключение
        if (found_index == bucket.size()) {
            throw tree23::KeyNotFound(__func__);
        }

        // Удаляем элемент с помощью вашего метода erase
        bucket.erase(found_index);

        // Обновляем счетчик и проверяем необходимость сжатия таблицы
        count--;
        check_resize();
    }

    void Set(const TKey& key, const TValue& value) {
        size_t idx = get_bucket_index(key);
        auto& bucket = buckets[idx];
        for (size_t i = 0; i < bucket.size(); ++i) {
            if (bucket[i].key == key) {
                bucket[i].value = value;
                return;
            }
        }
        bucket.push_back(KeyValuePair<TKey, TValue>(key, value));
        count++;
        check_resize();
    }

    size_t GetCount() const { return count; }
    size_t GetCapacity() const { return capacity; }

    // Итератор
    class Iterator {
    private:
        const DynamicArray<Bucket>* buckets_ptr;
        size_t bucket_idx;
        size_t element_idx;  // Индекс элемента внутри корзины

        void advance_to_next_valid() {
            while (bucket_idx < buckets_ptr->size()) {
                if (element_idx < (*buckets_ptr)[bucket_idx].size()) {
                    return;  // Нашли непустую ячейку
                }
                bucket_idx++;
                element_idx = 0;
            }
        }

    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = KeyValuePair<TKey, TValue>;
        using difference_type = std::ptrdiff_t;
        using pointer = const KeyValuePair<TKey, TValue>*;
        using reference = const KeyValuePair<TKey, TValue>&;

        Iterator() : buckets_ptr(nullptr), bucket_idx(0), element_idx(0) {}

        Iterator(const DynamicArray<Bucket>* b, size_t idx)
            : buckets_ptr(b), bucket_idx(idx), element_idx(0) {
            if (buckets_ptr && bucket_idx < buckets_ptr->size()) {
                advance_to_next_valid();
            }
        }

        reference operator*() const {
            return (*buckets_ptr)[bucket_idx][element_idx];
        }

        pointer operator->() const {
            return &(*buckets_ptr)[bucket_idx][element_idx];
        }

        Iterator& operator++() {
            element_idx++;
            advance_to_next_valid();
            return *this;
        }

        Iterator operator++(int) {
            Iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        bool operator==(const Iterator& other) const {
            if (!buckets_ptr && !other.buckets_ptr) return true;
            if (!buckets_ptr || !other.buckets_ptr) return false;
            if (bucket_idx == buckets_ptr->size() && other.bucket_idx == other.buckets_ptr->size()) {
                return true;
            }
            return bucket_idx == other.bucket_idx && element_idx == other.element_idx;
        }

        bool operator!=(const Iterator& other) const {
            return !(*this == other);
        }
    };

    Iterator begin() const {
        if (capacity == 0) return end();
        return Iterator(&buckets, 0);
    }

    Iterator end() const {
        return Iterator(&buckets, capacity);
    }
};