#pragma once

//#include "Tree23.h"
//#include "TreeExceptions.h"
//#include "lib/DynamicArray.h"
#include <iterator>
#include <cstddef>

template <typename T>
class Tree23Iterator {
private:
    // Тип узла
    using Node = typename Tree23<T>::Node;

    DynamicArray<T> cache;
    size_t index = 0;

    // Заполнение кэша в порядке InOrder (симметричный обход)
    void fill_inorder(const Node* node) {
        if (!node) return;
        
        for (size_t i = 0; i < node->key_count; ++i) {
            if (i < node->child_count) {
                fill_inorder(node->children[i].get());
            }
            cache.push_back(node->keys[i]);
        }
        if (node->child_count > node->key_count) {
            fill_inorder(node->children[node->key_count].get());
        }
    }

public:
    // для STL-совместимости
    using iterator_category = std::forward_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = const T*;
    using reference = const T&;

    // Конструктор по умолчанию (для end())
    Tree23Iterator() = default;

    // Конструктор для begin()
    explicit Tree23Iterator(const Node* root) {
        fill_inorder(root);
    }

    // Разыменование
    reference operator*() const {
        if (index >= cache.size()) {
            throw tree23::OutOfRange("Tree23Iterator::operator*", "разыменование итератора за концом");
        }
        return cache[index];
    }

    pointer operator->() const {
        if (index >= cache.size()) {
            throw tree23::OutOfRange("Tree23Iterator::operator->", "разыменование итератора за концом");
        }
        return &cache[index];
    }

    // Инкремент
    Tree23Iterator& operator++() {
        ++index;
        return *this;
    }

    Tree23Iterator operator++(int) {
        Tree23Iterator tmp = *this;
        ++(*this);
        return tmp;
    }

    // Сравнение
    bool operator==(const Tree23Iterator& other) const {
        // Определяем, находится ли итератор "за концом"
        bool this_end = (cache.size() == 0) || (index >= cache.size());
        bool other_end = (other.cache.size() == 0) || (other.index >= other.cache.size());

        // Оба за концом - равны
        if (this_end && other_end) return true;
        // Только один за концом - не равны
        if (this_end || other_end) return false;

        // Оба валидны - сравниваем позиции
        return index == other.index && cache.size() == other.cache.size();
    }

    bool operator!=(const Tree23Iterator& other) const {
        return !(*this == other);
    }
};

template <typename T>
requires Comparable<T>
typename Tree23<T>::Iterator Tree23<T>::begin() const {
    return Iterator(root.get());
}

template <typename T>
requires Comparable<T>
typename Tree23<T>::Iterator Tree23<T>::end() const {
    return Iterator();
}