#pragma once
#include <memory>
#include <functional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <type_traits>
#include "lib/FixedArray.h"
#include "lib/DynamicArray.h"
#include "lib/Comparable.h"
#include "core/TreeExceptions.h"

template <typename T> class Tree23Iterator;

template <Comparable T>
class Tree23
{
private:
    friend class Tree23Iterator<T>;
    using Iterator = Tree23Iterator<T>;
    friend class TreeVisualizer;

    // Индексы ключей
    static constexpr size_t left_key = 0;
    static constexpr size_t right_key = 1;    // второй ключ обычного узла
    static constexpr size_t middle_key = 1;   // он же средний при переполнении - поднимается вверх
    static constexpr size_t overflow_key = 2; // третий ключ, существует только во время split

    // Индексы детей
    static constexpr size_t left_child = 0;
    static constexpr size_t middle_child = 1;
    static constexpr size_t right_child = 2;
    static constexpr size_t overflow_child = 3; // четвёртый ребёнок, только во время split

    struct Node
    {
        FixedArray<T, 3> keys; // 3, чтобы вместить 3-й ключ перед сплитом
        size_t key_count = 0;

        FixedArray<std::unique_ptr<Node>, 4> children; // 4, чтобы вместить 4-го ребенка при сплите
        size_t child_count = 0;

        Node *parent = nullptr;

        Node() {}

        bool is_leaf() const
        {
            return child_count == 0;
        }
        bool is_overfull() const
        {
            return key_count >= 3;
        } // 3 ключа = пора делить

        bool is_underfull() const
        {
            return key_count == 0;
        }
    };

    std::unique_ptr<Node> root;
    size_t tree_size = 0;

public:
    // Порядок обхода дерева
    enum class TraversalOrder
    {
        PreOrder,  // узел -> дети слева направо
        InOrder,   // child0, key0, child1, key1, child2 - сортированный порядок
        PostOrder, // дети -> узел
        LevelOrder // по уровням (BFS)
    };

private:
    // Вставляет ключ в отсортированный массив ключей узла
    void insert_key_into_node(Node *node, const T &key)
    {
        if (!node)
            throw tree23::NullNode(__func__);

        int i = static_cast<int>(node->key_count) - 1;
        // Сдвигаем ключи вправо, чтобы освободить место для нового
        while (i >= 0 && node->keys[i] > key)
        {
            node->keys[i + 1] = node->keys[i];
            i--;
            // Защита от переполнения массива (на случай багов)
            if (static_cast<size_t>(i + 1) >= node->keys.size())
            {
                throw tree23::InvariantError(__func__, "переполнение массива ключей");
            }
        }
        node->keys[i + 1] = key;
        node->key_count++;
    }

    // Вставляет указатель на ребенка в отсортированный массив детей узла
    void insert_child_into_node(Node *node, std::unique_ptr<Node> child, size_t child_index)
    {
        if (!node)
            throw tree23::NullNode(__func__);
        // Сдвигаем детей вправо, начиная с конца
        for (size_t i = node->child_count; i > child_index; --i)
        {
            node->children[i] = std::move(node->children[i - 1]);
        }
        node->children[child_index] = std::move(child);
        node->child_count++;

        // Обновляем указатель на родителя у вставленного ребенка
        if (node->children[child_index])
        {
            node->children[child_index]->parent = node;
        }
    }

    // Возвращает индекс заданного узла среди детей parent.
    // Если узла там нет - это ошибка инвариантов дерева.
    size_t find_child_index(Node *parent, Node *child) const
    {
        if (!parent || !child)
        {
            throw tree23::NullNode(__func__);
        }
        for (size_t i = 0; i < parent->child_count; ++i)
        { // ВСЕ дети, без -1
            if (parent->children[i].get() == child)
            {
                return i;
            }
        }
        throw tree23::InvariantError(__func__, "узел не найден среди детей родителя");
    }

    void split(Node *node)
    {
        if (!node || !node->is_overfull())
            return;

        T mid_key = node->keys[middle_key];

        auto new_node = std::make_unique<Node>();
        new_node->keys[left_key] = node->keys[overflow_key];
        new_node->key_count = 1;
        new_node->parent = node->parent;

        if (!node->is_leaf())
        {
            new_node->children[left_child] = std::move(node->children[right_child]);
            new_node->children[middle_child] = std::move(node->children[overflow_child]);
            new_node->child_count = 2;

            if (new_node->children[left_child])
                new_node->children[left_child]->parent = new_node.get();
            if (new_node->children[middle_child])
                new_node->children[middle_child]->parent = new_node.get();

            node->child_count = 2;
        }

        node->key_count = 1;

        if (node->parent == nullptr)
        {
            // Создаем новый корень
            auto new_root = std::make_unique<Node>();
            new_root->keys[left_key] = mid_key;
            new_root->key_count = 1;

            // забираем владение из this->root через std::move
            std::unique_ptr<Node> old_root = std::move(this->root);

            old_root->parent = new_root.get();
            new_node->parent = new_root.get();

            new_root->children[left_child] = std::move(old_root);
            new_root->children[middle_child] = std::move(new_node);
            new_root->child_count = 2;

            this->root = std::move(new_root);
        }
        else
        {
            Node *parent = node->parent;

            // Находим позицию до вставки ключа
            size_t node_pos = find_child_index(parent, node);

            // Поднимаем средний ключ
            insert_key_into_node(parent, mid_key);

            // Новый узел занимает позицию сразу после исходного
            insert_child_into_node(parent, std::move(new_node), node_pos + 1);

            if (parent->is_overfull())
            {
                split(parent);
            }
        }
    }

    // Поиск узла, содержащего заданный ключ
    Node *find_node_with_key(Node &node, const T &value) const
    {
        if (node.key_count == 1)
        {
            if (value < node.keys[left_key])
            {
                if (node.is_leaf())
                    return nullptr;
                return find_node_with_key(*node.children[left_child], value);
            }
            else if (value == node.keys[left_key])
            {
                return &node;
            }
            else
            {
                if (node.is_leaf())
                    return nullptr;
                return find_node_with_key(*node.children[middle_child], value);
            }
        }
        else
        {
            if (value < node.keys[left_key])
            {
                if (node.is_leaf())
                    return nullptr;
                return find_node_with_key(*node.children[left_child], value);
            }
            else if (value == node.keys[left_key])
            {
                return &node;
            }
            else if (value < node.keys[right_key])
            {
                if (node.is_leaf())
                    return nullptr;
                return find_node_with_key(*node.children[middle_child], value);
            }
            else if (value == node.keys[right_key])
            {
                return &node;
            }
            else
            {
                if (node.is_leaf())
                    return nullptr;
                return find_node_with_key(*node.children[right_child], value);
            }
        }
    }

    // Поиск минимального узла в поддереве (самый левый)
    Node *find_min_node(Node &node) const
    {
        Node *current = &node;
        while (!current->is_leaf())
            current = current->children[left_child].get();
        return current;
    }

    // Удаление ключа из листа
    void remove_from_leaf(Node *node, const T &value)
    {
        if (!node || !node->is_leaf())
        {
            throw tree23::InvalidArgument(__func__, "узел не является листом");
        }

        // Находим индекс ключа
        size_t key_index = node->key_count;
        for (size_t i = 0; i < node->key_count; ++i)
        {
            if (node->keys[i] == value)
            {
                key_index = i;
                break;
            }
        }

        if (key_index == node->key_count)
        {
            throw tree23::InvariantError(__func__, "ключ не найден");
        }

        // Сдвигаем ключи влево
        for (size_t i = key_index; i < node->key_count - 1; ++i)
        {
            node->keys[i] = node->keys[i + 1];
        }
        node->key_count--;
    }

    // Восстановление инвариантов после удаления
    void fix_underflow(Node *node)
    {
        if (!node || !node->parent)
        {
            return; // Это корень или null, ничего не делаем
        }

        Node *parent = node->parent;

        // Находим индекс этого узла среди детей родителя
        size_t node_index = 0;
        for (size_t i = 0; i < parent->child_count; ++i)
        {
            if (parent->children[i].get() == node)
            {
                node_index = i;
                break;
            }
        }

        // Пытаемся занять ключ у левого брата
        if (node_index > 0)
        {
            Node *left_sibling = parent->children[node_index - 1].get();
            if (left_sibling->key_count > 1)
            {
                redistribute_from_left(parent, node_index);
                return;
            }
        }

        // Пытаемся занять ключ у правого брата
        if (node_index < parent->child_count - 1)
        {
            Node *right_sibling = parent->children[node_index + 1].get();
            if (right_sibling->key_count > 1)
            {
                redistribute_from_right(parent, node_index);
                return;
            }
        }

        // Нечего занимать - сливаем с братом
        if (node_index > 0)
        {
            merge_with_left(parent, node_index);
        }
        else
        {
            merge_with_right(parent, node_index);
        }
    }

    // Заимствование ключа у левого брата
    void redistribute_from_left(Node *parent, size_t child_index)
    {
        Node *node = parent->children[child_index].get();
        Node *left_sibling = parent->children[child_index - 1].get();

        // Берем последний ключ из левого брата
        T borrowed_key = left_sibling->keys[left_sibling->key_count - 1];
        left_sibling->key_count--;

        // Ключ из родителя спускается в текущий узел (в начало)
        // Сдвигаем ключи вправо
        for (size_t i = node->key_count; i > 0; --i)
        {
            node->keys[i] = node->keys[i - 1];
        }
        node->keys[left_key] = parent->keys[child_index - 1];
        node->key_count++;

        // Забранный ключ поднимается в родителя
        parent->keys[child_index - 1] = borrowed_key;

        // Если у левого брата были дети, передаем последнего
        if (!left_sibling->is_leaf())
        {
            auto moved_child = std::move(left_sibling->children[left_sibling->child_count - 1]);
            left_sibling->child_count--;

            // Сдвигаем детей в node вправо
            for (size_t i = node->child_count; i > 0; --i)
            {
                node->children[i] = std::move(node->children[i - 1]);
            }
            node->children[left_child] = std::move(moved_child);
            node->child_count++;

            if (node->children[left_child])
            {
                node->children[left_child]->parent = node;
            }
        }
    }

    // Заимствование ключа у правого брата
    void redistribute_from_right(Node *parent, size_t child_index)
    {
        Node *node = parent->children[child_index].get();
        Node *right_sibling = parent->children[child_index + 1].get();

        // Берем первый ключ из правого брата
        T borrowed_key = right_sibling->keys[left_key];

        // Сдвигаем ключи в правом брате влево
        for (size_t i = 0; i < right_sibling->key_count - 1; ++i)
        {
            right_sibling->keys[i] = right_sibling->keys[i + 1];
        }
        right_sibling->key_count--;

        // Ключ из родителя спускается в текущий узел (в конец)
        node->keys[node->key_count] = parent->keys[child_index];
        node->key_count++;

        // Забранный ключ поднимается в родителя
        parent->keys[child_index] = borrowed_key;

        // Если у правого брата были дети, передаем первого
        if (!right_sibling->is_leaf())
        {
            auto moved_child = std::move(right_sibling->children[left_child]);

            // Сдвигаем детей в правом брате влево
            for (size_t i = 0; i < right_sibling->child_count - 1; ++i)
            {
                right_sibling->children[i] = std::move(right_sibling->children[i + 1]);
            }
            right_sibling->child_count--;

            // Добавляем ребенка в текущий узел
            node->children[node->child_count] = std::move(moved_child);
            node->child_count++;

            if (node->children[node->child_count - 1])
            {
                node->children[node->child_count - 1]->parent = node;
            }
        }
    }

    // Слияние с левым братом
    void merge_with_left(Node *parent, size_t child_index)
    {
        Node *node = parent->children[child_index].get();
        Node *left_sibling = parent->children[child_index - 1].get();

        // Добавляем ключ из родителя в левый брат
        left_sibling->keys[left_sibling->key_count] = parent->keys[child_index - 1];
        left_sibling->key_count++;

        // Добавляем все ключи из текущего узла в левый брат
        for (size_t i = 0; i < node->key_count; ++i)
        {
            left_sibling->keys[left_sibling->key_count + i] = node->keys[i];
        }
        left_sibling->key_count += node->key_count;

        // Если есть дети, передаем их левому брату
        if (!node->is_leaf())
        {
            for (size_t i = 0; i < node->child_count; ++i)
            {
                left_sibling->children[left_sibling->child_count + i] = std::move(node->children[i]);
                if (left_sibling->children[left_sibling->child_count + i])
                {
                    left_sibling->children[left_sibling->child_count + i]->parent = left_sibling;
                }
            }
            left_sibling->child_count += node->child_count;
        }

        // Удаляем ключ и ребенка из родителя
        // Сдвигаем ключи влево
        for (size_t i = child_index - 1; i < parent->key_count - 1; ++i)
        {
            parent->keys[i] = parent->keys[i + 1];
        }
        parent->key_count--;

        // Сдвигаем детей влево
        for (size_t i = child_index; i < parent->child_count - 1; ++i)
        {
            parent->children[i] = std::move(parent->children[i + 1]);
        }
        parent->child_count--;

        // Если родитель стал пустым, рекурсивно чиним
        if (parent->is_underfull() && parent != this->root.get())
        {
            fix_underflow(parent);
        }
    }

    // Слияние с правым братом
    void merge_with_right(Node *parent, size_t child_index)
    {
        Node *node = parent->children[child_index].get();
        Node *right_sibling = parent->children[child_index + 1].get();

        // Добавляем ключ из родителя в текущий узел
        node->keys[node->key_count] = parent->keys[child_index];
        node->key_count++;

        // Добавляем все ключи из правого брата
        for (size_t i = 0; i < right_sibling->key_count; ++i)
        {
            node->keys[node->key_count + i] = right_sibling->keys[i];
        }
        node->key_count += right_sibling->key_count;

        // Если есть дети, передаем их текущему узлу
        if (!right_sibling->is_leaf())
        {
            for (size_t i = 0; i < right_sibling->child_count; ++i)
            {
                node->children[node->child_count + i] = std::move(right_sibling->children[i]);
                if (node->children[node->child_count + i])
                {
                    node->children[node->child_count + i]->parent = node;
                }
            }
            node->child_count += right_sibling->child_count;
        }

        // Удаляем ключ и ребенка из родителя
        // Сдвигаем ключи влево
        for (size_t i = child_index; i < parent->key_count - 1; ++i)
        {
            parent->keys[i] = parent->keys[i + 1];
        }
        parent->key_count--;

        // Сдвигаем детей влево
        for (size_t i = child_index + 1; i < parent->child_count - 1; ++i)
        {
            parent->children[i] = std::move(parent->children[i + 1]);
        }
        parent->child_count--;

        // Если родитель стал пустым, рекурсивно чиним
        if (parent->is_underfull() && parent != this->root.get())
        {
            fix_underflow(parent);
        }
    }

    // Парсинг строки пути в вектор индексов
    // "0-1-2" -> [0, 1, 2]
    DynamicArray<size_t> parse_path(const std::string &path) const
    {
        DynamicArray<size_t> indices;
        std::string current_number;

        for (char c : path)
        {
            if (c == '-')
            {
                if (current_number.empty())
                {
                    throw tree23::InvalidPath("пустой индекс");
                }
                indices.push_back(std::stoul(current_number));
                current_number.clear();
            }
            else if (std::isdigit(c))
            {
                current_number += c;
            }
            else
            {
                throw tree23::InvalidPath("некорректный символ: '" + std::string(1, c) + "'");
            }
        }

        // Добавляем последнее число
        if (!current_number.empty())
        {
            indices.push_back(std::stoul(current_number));
        }

        return indices;
    }

    // ВСПОМОГАТЕЛЬНЫЕ МЕТОДЫ СЕРИАЛИЗАЦИИ

    static size_t parse_size_t_token(const std::string &token)
    {
        size_t pos = 0;
        unsigned long long value = 0;

        try
        {
            value = std::stoull(token, &pos);
        }
        catch (...)
        {
            throw tree23::DeserializationError(tree23::format::fixed, "некорректное число");
        }

        if (pos != token.size())
        {
            throw tree23::DeserializationError(tree23::format::fixed, "некорректный числовой токен");
        }

        return static_cast<size_t>(value);
    }

    void serialize_fixed_node(const Node &node, std::ostringstream &out) const
    {
        out << node.key_count;
        for (size_t i = 0; i < node.key_count; ++i)
            out << ' ' << node.keys[i];
        out << ' ' << node.child_count << '\n';
        for (size_t i = 0; i < node.child_count; ++i)
            serialize_fixed_node(*node.children[i], out);
    }

    static std::unique_ptr<Node> deserialize_fixed_node(
        std::istringstream &in,
        Node *parent,
        size_t key_count)
    {
        if (key_count < 1 || key_count > 2)
        {
            throw tree23::DeserializationError(tree23::format::fixed, "у узла должно быть 1 или 2 ключа");
        }

        auto node = std::make_unique<Node>();
        node->parent = parent;
        node->key_count = key_count;

        for (size_t i = 0; i < key_count; ++i)
        {
            if (!(in >> node->keys[i]))
            {
                throw tree23::DeserializationError(tree23::format::fixed, "не хватает ключей");
            }
        }

        for (size_t i = 1; i < key_count; ++i)
        {
            if (!(node->keys[i - 1] < node->keys[i]))
            {
                throw tree23::DeserializationError(tree23::format::fixed, "ключи внутри узла не отсортированы");
            }
        }

        size_t child_count = 0;

        if (!(in >> child_count))
        {
            throw tree23::DeserializationError(tree23::format::fixed, "не хватает child_count");
        }

        if (child_count != 0 && child_count != key_count + 1)
        {
            throw tree23::DeserializationError(tree23::format::fixed, "число детей должно быть 0 или key_count + 1");
        }

        node->child_count = child_count;

        for (size_t i = 0; i < child_count; ++i)
        {
            std::string child_key_count_token;

            if (!(in >> child_key_count_token))
            {
                throw tree23::DeserializationError(tree23::format::fixed, "не хватает данных о ребенке");
            }

            size_t child_key_count = parse_size_t_token(child_key_count_token);

            node->children[i] = deserialize_fixed_node(
                in,
                node.get(),
                child_key_count);
        }

        return node;
    }

    size_t count_keys(const Node &node) const
    {
        size_t n = 0;
        auto counter = [&](const T &)
        { ++n; };
        traverse_preorder(node, counter);
        return n;
    }

    // Глубокое копирование узла и всех его потомков
    std::unique_ptr<Node> deep_copy_node(const Node &source, Node *parent) const
    {
        auto copy = std::make_unique<Node>();
        copy->parent = parent;
        copy->key_count = source.key_count;
        for (size_t i = 0; i < source.key_count; ++i)
            copy->keys[i] = source.keys[i];
        copy->child_count = source.child_count;
        for (size_t i = 0; i < source.child_count; ++i)
            copy->children[i] = deep_copy_node(*source.children[i], copy.get());
        return copy;
    }

    // Проверка структурного равенства двух поддеревьев
    bool are_subtrees_equal(const Node &node1, const Node &node2) const
    {
        if (node1.key_count != node2.key_count)
            return false;
        for (size_t i = 0; i < node1.key_count; ++i)
            if (!(node1.keys[i] == node2.keys[i]))
                return false;
        if (node1.child_count != node2.child_count)
            return false;
        for (size_t i = 0; i < node1.child_count; ++i)
            if (!are_subtrees_equal(*node1.children[i], *node2.children[i]))
                return false;
        return true;
    }

    // Обход всех узлов дерева в поисках поддерева, идентичного target
    bool contains_subtree_helper(const Node &current, const Node &target) const
    {
        if (are_subtrees_equal(current, target))
            return true;
        for (size_t i = 0; i < current.child_count; ++i)
            if (contains_subtree_helper(*current.children[i], target))
                return true;
        return false;
    }

    // Десериализация формата "узел-родитель": внутренние типы

    // Одна строка формата: id parent_id child_index key_count key1 key2 ...
    struct PairRecord
    {
        size_t id = 0;
        long long parent_id = -1;
        int child_index = -1;

        size_t key_count = 0;
        FixedArray<T, 3> keys;

        FixedArray<size_t, 3> children_ids;
        FixedArray<bool, 3> has_child;
        size_t child_count = 0;

        PairRecord()
        {
            for (size_t i = 0; i < 3; ++i)
            {
                has_child[i] = false;
                children_ids[i] = 0;
            }
        }
    };

    // Результат разбора текста: все записи + индекс по id + метаинформация
    struct ParsedPairs
    {
        DynamicArray<PairRecord> records;
        std::unordered_map<size_t, size_t> id_to_index;
        long long root_index = -1;
        size_t total_keys = 0;
    };

    // Десериализация формата "узел-родитель"

    // разбор и валидация одной строки
    static PairRecord parse_pair_record(const std::string &line)
    {
        std::istringstream in(line);
        PairRecord record;

        if (!(in >> record.id >> record.parent_id >> record.child_index >> record.key_count))
        {
            throw tree23::DeserializationError(tree23::format::pairs, "некорректная строка");
        }

        if (record.key_count < 1 || record.key_count > 2)
        {
            throw tree23::DeserializationError(tree23::format::pairs, "у узла должно быть 1 или 2 ключа");
        }

        for (size_t i = 0; i < record.key_count; ++i)
        {
            if (!(in >> record.keys[i]))
            {
                throw tree23::DeserializationError(tree23::format::pairs, "не хватает ключей");
            }
        }

        for (size_t i = 1; i < record.key_count; ++i)
        {
            if (!(record.keys[i - 1] < record.keys[i]))
            {
                throw tree23::DeserializationError(tree23::format::pairs, "ключи внутри узла не отсортированы");
            }
        }

        std::string extra;
        if (in >> extra)
        {
            throw tree23::DeserializationError(tree23::format::pairs, "лишние данные в строке");
        }

        if (record.parent_id == -1)
        {
            if (record.child_index != -1)
            {
                throw tree23::DeserializationError(tree23::format::pairs, "у корня child_index должен быть -1");
            }
        }
        else
        {
            if (record.parent_id < 0)
            {
                throw tree23::DeserializationError(tree23::format::pairs, "некорректный parent_id");
            }
            if (record.child_index < 0 || record.child_index > 2)
            {
                throw tree23::DeserializationError(tree23::format::pairs, "child_index должен быть 0, 1 или 2");
            }
        }

        return record;
    }

    // разбор всего текста, построение индекса id -> позиция
    static ParsedPairs parse_pairs(const std::string &data)
    {
        ParsedPairs parsed;

        std::istringstream input(data);
        std::string line;

        while (std::getline(input, line))
        {
            if (line.empty())
            {
                continue;
            }

            PairRecord record = parse_pair_record(line);

            if (parsed.id_to_index.find(record.id) != parsed.id_to_index.end())
            {
                throw tree23::DeserializationError(tree23::format::pairs, "повторяющийся id узла");
            }

            if (record.parent_id == -1)
            {
                if (parsed.root_index != -1)
                {
                    throw tree23::DeserializationError(tree23::format::pairs, "в дереве больше одного корня");
                }
                parsed.root_index = static_cast<long long>(parsed.records.size());
            }

            parsed.total_keys += record.key_count;
            parsed.id_to_index[record.id] = parsed.records.size();
            parsed.records.push_back(std::move(record));
        }

        return parsed;
    }

    // проставить каждому узлу id его детей по индексам 0..2
    static void link_children(ParsedPairs &parsed)
    {
        for (size_t i = 0; i < parsed.records.size(); ++i)
        {
            PairRecord &child = parsed.records[i];

            if (child.parent_id == -1)
            {
                continue;
            }

            auto parent_it = parsed.id_to_index.find(static_cast<size_t>(child.parent_id));
            if (parent_it == parsed.id_to_index.end())
            {
                throw tree23::DeserializationError(tree23::format::pairs, "родитель не найден");
            }

            PairRecord &parent = parsed.records[parent_it->second];
            int idx = child.child_index;

            if (parent.has_child[idx])
            {
                throw tree23::DeserializationError(tree23::format::pairs, "два ребенка с одинаковым индексом");
            }

            parent.has_child[idx] = true;
            parent.children_ids[idx] = child.id;
            parent.child_count++;
        }
    }

    // рекурсивная сборка узла и его поддеревьев
    // state: 0 - не посещён, 1 - в обходе (детект цикла), 2 - построен
    static std::unique_ptr<Node> build_node_from_records(
        size_t record_index,
        Node *parent,
        ParsedPairs &parsed,
        DynamicArray<unsigned char> &state)
    {
        if (state[record_index] == 1)
        {
            throw tree23::DeserializationError(tree23::format::pairs, "обнаружен цикл");
        }
        if (state[record_index] == 2)
        {
            throw tree23::DeserializationError(tree23::format::pairs, "узел используется повторно");
        }

        state[record_index] = 1;

        PairRecord &record = parsed.records[record_index];

        auto node = std::make_unique<Node>();
        node->parent = parent;
        node->key_count = record.key_count;

        for (size_t i = 0; i < record.key_count; ++i)
        {
            node->keys[i] = record.keys[i];
        }

        if (record.child_count != 0 && record.child_count != record.key_count + 1)
        {
            throw tree23::DeserializationError(tree23::format::pairs, "число детей должно быть key_count + 1");
        }

        for (size_t i = 0; i < record.child_count; ++i)
        {
            if (!record.has_child[i])
            {
                throw tree23::DeserializationError(tree23::format::pairs, "пропущен индекс ребенка");
            }

            size_t child_record_index = parsed.id_to_index.at(record.children_ids[i]);
            node->children[i] = build_node_from_records(child_record_index, node.get(), parsed, state);
        }

        node->child_count = record.child_count;
        state[record_index] = 2;

        return node;
    }

    // все записи должны быть использованы (нет "висячих" поддеревьев)
    static void ensure_all_nodes_built(DynamicArray<unsigned char> &state)
    {
        for (size_t i = 0; i < state.size(); ++i)
        {
            if (state[i] != 2)
            {
                throw tree23::DeserializationError(tree23::format::pairs, "есть недостижимые узлы");
            }
        }
    }

    // сборка дерева целиком
    static Tree23<T> build_tree_from_pairs(ParsedPairs &parsed)
    {
        Tree23<T> result;

        if (parsed.records.size() == 0)
        {
            return result;
        }

        if (parsed.root_index == -1)
        {
            throw tree23::DeserializationError(tree23::format::pairs, "корень не найден");
        }

        DynamicArray<unsigned char> state(parsed.records.size(), 0);

        result.root = build_node_from_records(
            static_cast<size_t>(parsed.root_index), nullptr, parsed, state);
        result.tree_size = parsed.total_keys;

        ensure_all_nodes_built(state);

        return result;
    }

    // Обходы дерева

    template <typename Visitor>
    void traverse_preorder(const Node &node, Visitor &visit) const
    {
        for (size_t i = 0; i < node.key_count; ++i)
            visit(node.keys[i]);
        for (size_t i = 0; i < node.child_count; ++i)
            traverse_preorder(*node.children[i], visit);
    }

    // child0 < key0 < child1 < key1 < child2  =>  возрастающая последовательность
    template <typename Visitor>
    void traverse_inorder(const Node &node, Visitor &visit) const
    {
        for (size_t i = 0; i < node.key_count; ++i)
        {
            if (i < node.child_count)
                traverse_inorder(*node.children[i], visit);
            visit(node.keys[i]);
        }
        if (node.child_count > node.key_count)
            traverse_inorder(*node.children[node.key_count], visit);
    }

    template <typename Visitor>
    void traverse_postorder(const Node &node, Visitor &visit) const
    {
        for (size_t i = 0; i < node.child_count; ++i)
            traverse_postorder(*node.children[i], visit);
        for (size_t i = 0; i < node.key_count; ++i)
            visit(node.keys[i]);
    }

    template <typename Visitor>
    void traverse_levelorder(Visitor &visit) const
    {
        if (!root)
            return;
        DynamicArray<const Node *> queue;
        queue.push_back(root.get()); // оставляем: нужен указатель для очереди
        size_t head = 0;
        while (head < queue.size())
        {
            const Node *node = queue[head++];
            for (size_t i = 0; i < node->key_count; ++i)
                visit(node->keys[i]);
            for (size_t i = 0; i < node->child_count; ++i)
                if (node->children[i])
                    queue.push_back(node->children[i].get());
        }
    }

    void range_collect(const Node* node, const T& lo, const T& hi,
                       DynamicArray<T>& out) const {
        for (size_t i = 0; i < node->key_count; ++i) {
            const T& k = node->keys[i];

            // ребёнок i содержит ключи < k => отсекаем, если k <= lo
            // и если предыдущий ключ уже > hi
            bool left_ok  = (i == 0) || !(hi < node->keys[i - 1]);
            bool right_ok = lo < k;
            if (i < node->child_count && left_ok && right_ok) {
                range_collect(node->children[i].get(), lo, hi, out);
            }

            if (!(k < lo) && !(hi < k)) out.push_back(k);
        }

        // последний ребёнок: ключи > последнего ключа узла
        if (node->child_count > node->key_count) {
            const T& last = node->keys[node->key_count - 1];
            if (!(hi < last)) {
                range_collect(node->children[node->key_count].get(), lo, hi, out);
            }
        }
    }

    std::string check_subtree(const Node* n, const Node* parent_expected, int depth,
                              size_t& counted, int& leaf_depth,
                              const T* lo, const T* hi) const {
        if (!n) return "пустой дочерний указатель";

        if (n->parent != parent_expected)
            return "некорректный parent на глубине " + std::to_string(depth);

        if (n->key_count < 1 || n->key_count > 2)
            return "недопустимое число ключей (" + std::to_string(n->key_count) +
                   ") на глубине " + std::to_string(depth);

        for (size_t i = 1; i < n->key_count; ++i)
            if (!(n->keys[i - 1] < n->keys[i]))
                return "ключи в узле не отсортированы на глубине " + std::to_string(depth);

        if (lo && !(*lo < n->keys[0]))
            return "нарушена нижняя граница поддерева на глубине " + std::to_string(depth);
        if (hi && !(n->keys[n->key_count - 1] < *hi))
            return "нарушена верхняя граница поддерева на глубине " + std::to_string(depth);

        counted += n->key_count;

        if (n->is_leaf()) {
            if (n->child_count != 0)
                return "лист имеет детей на глубине " + std::to_string(depth);
            if (leaf_depth == -1) leaf_depth = depth;
            else if (leaf_depth != depth)
                return "листья на разной глубине (" + std::to_string(leaf_depth) +
                       " и " + std::to_string(depth) + ")";
            return std::string();
        }

        if (n->child_count != n->key_count + 1)
            return "child_count != key_count + 1 на глубине " + std::to_string(depth);

        for (size_t i = 0; i < n->child_count; ++i) {
            const T* child_lo = (i == 0) ? lo : &n->keys[i - 1];
            const T* child_hi = (i == n->key_count) ? hi : &n->keys[i];
            std::string err = check_subtree(n->children[i].get(), n, depth + 1,
                                            counted, leaf_depth, child_lo, child_hi);
            if (!err.empty()) return err;
        }
        return std::string();
    }

public:
    Tree23() = default;

    // Правило 5: запрещаем копирование, разрешаем перемещение
    Tree23(const Tree23 &) = delete;
    Tree23 &operator=(const Tree23 &) = delete;
    Tree23(Tree23 &&) = default;
    Tree23 &operator=(Tree23 &&) = default;
    ~Tree23() = default; // unique_ptr сам очистит память

    void insert(const T &value)
    {
        if (!this->root)
        {
            this->root = std::make_unique<Node>();
            this->root->keys[left_key] = value;
            this->root->key_count = 1;
            this->tree_size++;
            return;
        }

        // 1. Спуск к подходящему листу
        Node *current = this->root.get();
        while (!current->is_leaf())
        {
            size_t child_index = 0;
            // Определяем, в какого ребенка идти
            if (current->key_count == 1)
            {
                child_index = (value < current->keys[left_key]) ? 0 : 1;
            }
            else
            { // key_count == 2
                if (value < current->keys[left_key])
                    child_index = 0;
                else if (value < current->keys[right_key])
                    child_index = 1;
                else
                    child_index = 2;
            }
            current = current->children[child_index].get();
        }

        // 2. Вставка в лист
        // проверка на дубликаты:
        /*
        for (size_t i = 0; i < current->key_count; ++i) {
            if (current->keys[i] == value) {
                throw tree23::DuplicateKey(__func__);
            }
        }
        */

        insert_key_into_node(current, value);
        this->tree_size++;

        // 3. Если лист переполнился, запускаем каскад расщеплений вверх
        if (current->is_overfull())
        {
            split(current);
        }
    }

    // ФУНКЦИОНАЛЬНЫЕ МЕТОДЫ (п. 3.a)

    // map: создает новое дерево, применяя функцию f к каждому элементу
    template <typename Func>
    Tree23<std::invoke_result_t<Func, const T &>> map(Func f) const
    {
        using R = std::invoke_result_t<Func, const T &>;
        Tree23<R> result;
        for (const auto &key : *this)
        {
            result.insert(f(key));
        }
        return result;
    }

    // reduce: сворачивает дерево в одно значение
    template <typename Acc, typename Func>
    Acc reduce(Acc init, Func f) const
    {
        Acc acc = std::move(init);
        for (const auto &key : *this)
        {
            acc = f(acc, key);
        }
        return acc;
    }

    // Базовые операции

    // Поиск элемента в дереве
    bool search(const T &value) const
    {
        if (!this->root)
        {
            return false; // Пустое дерево
        }

        Node *current = this->root.get();

        while (current != nullptr)
        {
            // Проверяем все ключи в текущем узле
            for (size_t i = 0; i < current->key_count; ++i)
            {
                if (current->keys[i] == value)
                {
                    return true; // Нашли!
                }
            }

            // Если это лист и мы не нашли значение, значит его нет в дереве
            if (current->is_leaf())
            {
                return false;
            }

            // Определяем, в какого ребенка идти
            size_t child_index = 0;

            if (current->key_count == 1)
            {
                // Один ключ: сравниваем с ним
                child_index = (value < current->keys[left_key]) ? 0 : 1;
            }
            else
            { // key_count == 2
                // Два ключа: сравниваем с обоими
                if (value < current->keys[left_key])
                {
                    child_index = 0; // Меньше первого ключа
                }
                else if (value < current->keys[right_key])
                {
                    child_index = 1; // Между первым и вторым ключом
                }
                else
                {
                    child_index = 2; // Больше второго ключа
                }
            }

            // Переходим к соответствующему ребенку
            current = current->children[child_index].get();
        }

        return false; // Не нашли (должны были выйти из цикла раньше)
    }

    // Проверка инвариантов 2-3 дерева.
    // Пустая строка - всё корректно, иначе описание первого нарушения.
    std::string check_invariants() const {
        if (!root) {
            return tree_size == 0 ? std::string() : "tree_size != 0 при пустом дереве";
        }
        size_t counted = 0;
        int leaf_depth = -1;
        std::string err = check_subtree(root.get(), nullptr, 0,
                                        counted, leaf_depth, nullptr, nullptr);
        if (!err.empty()) return err;
        if (counted != tree_size) return "tree_size расходится с фактическим числом ключей";
        return std::string();
    }


    void remove(const T &value)
    {
        if (!this->root)
        {
            throw tree23::EmptyTree(__func__);
        }

        Node *node_to_remove = find_node_with_key(*this->root, value);

        if (!node_to_remove)
        {
            throw tree23::KeyNotFound(__func__);
        }

        T key_to_delete_from_leaf = value;

        if (!node_to_remove->is_leaf())
        {
            // 1. Определяем ИНДЕКС удаляемого ключа в узле
            size_t key_index = 0;
            while (key_index < node_to_remove->key_count &&
                   !(node_to_remove->keys[key_index] == value))
            {
                ++key_index;
            }

            // 2. Преемник - минимум поддерева СПРАВА от этого ключа:
            //    для keys[0] это children[1], для keys[1] - children[2]
            Node *successor = find_min_node(*node_to_remove->children[key_index + 1]);

            key_to_delete_from_leaf = successor->keys[left_key];
            node_to_remove->keys[key_index] = key_to_delete_from_leaf;
            node_to_remove = successor;
        }

        remove_from_leaf(node_to_remove, key_to_delete_from_leaf);
        this->tree_size--;

        if (node_to_remove->is_underfull() && node_to_remove != this->root.get())
        {
            fix_underflow(node_to_remove);
        }

        if (this->root && this->root->key_count == 0)
        {
            if (this->root->child_count == 1)
            {
                auto old_root = std::move(this->root);
                this->root = std::move(old_root->children[left_child]);
                this->root->parent = nullptr;
            }
            else if (this->root->child_count == 0)
            {
                this->root.reset();
            }
        }
    }

    // НАВИГАЦИЯ ПО ПУТЯМ (п. 3.f)

    // Поиск узла по абсолютному пути от корня
    // Путь: строка вида "0-1-2" (0-й ребенок, затем 1-й, затем 2-й)
    // Пустая строка "" возвращает корень
    Node *get_node_by_absolute_path(const std::string &path) const
    {
        if (!this->root)
        {
            throw tree23::EmptyTree(__func__);
        }

        // Пустой путь = корень
        if (path.empty())
        {
            return this->root.get();
        }

        Node *current = this->root.get();
        DynamicArray<size_t> indices = parse_path(path);

        for (size_t idx : indices)
        {
            if (current->is_leaf())
            {
                throw tree23::PathOutOfRange("достигнут лист, путь продолжается");
            }

            if (idx >= current->child_count)
            {
                throw tree23::PathOutOfRange("индекс ребенка вне диапазона: " + std::to_string(idx) + ", доступно детей: " + std::to_string(current->child_count));
            }

            current = current->children[idx].get();
        }

        return current;
    }

    // Поиск узла по относительному пути от заданного узла
    // start_node: узел, от которого начинаем движение
    // path: строка вида "1-2" (1-й ребенок, затем 2-й)
    Node *get_node_by_relative_path(Node *start_node, const std::string &path) const
    {
        if (!start_node)
        {
            throw tree23::NullNode(__func__);
        }

        // Пустой путь = сам узел
        if (path.empty())
        {
            return start_node;
        }

        Node *current = start_node;
        DynamicArray<size_t> indices = parse_path(path);

        for (size_t idx : indices)
        {
            if (current->is_leaf())
            {
                throw tree23::PathOutOfRange("достигнут лист, путь продолжается");
            }

            if (idx >= current->child_count)
            {
                throw tree23::PathOutOfRange("индекс ребенка вне диапазона");
            }

            current = current->children[idx].get();
        }

        return current;
    }

    // СЕРИАЛИЗАЦИЯ

    // 3.d.i
    // Сохранение по фиксированному обходу:
    // узел -> дети слева направо
    std::string to_string_fixed_traversal() const
    {
        if (!root)
        {
            return "EMPTY";
        }

        std::ostringstream out;
        serialize_fixed_node(*root, out);
        return out.str();
    }

    // 3.e.i
    // Чтение из строки по фиксированному обходу
    static Tree23<T> from_string_fixed_traversal(const std::string &data)
    {
        std::istringstream in(data);

        std::string first;
        if (!(in >> first))
        {
            throw tree23::DeserializationError(tree23::format::fixed, "пустая строка");
        }

        Tree23<T> result;

        if (first == "EMPTY")
        {
            std::string extra;
            if (in >> extra)
            {
                throw tree23::DeserializationError(tree23::format::fixed, "лишние данные после EMPTY");
            }
            return result;
        }

        size_t key_count = parse_size_t_token(first);

        result.root = deserialize_fixed_node(in, nullptr, key_count);
        result.tree_size = result.count_keys(*result.root);

        std::string extra;
        if (in >> extra)
        {
            throw tree23::DeserializationError(tree23::format::fixed, "лишние данные после дерева");
        }

        return result;
    }

    // 3.d.iii
    // Сохранение в формате списка "узел-родитель".
    //
    // Формат одной строки:
    // node_id parent_id child_index key_count key1 key2 ...
    //
    // Для корня:
    // node_id -1 -1 key_count key1 key2 ...
    //
    // Пример:
    // 0 -1 -1 1 10
    // 1 0 0 1 5
    // 2 0 1 2 15 20
    std::string to_string_parent_child_pairs() const
    {
        if (!root)
        {
            return "";
        }

        std::ostringstream out;
        size_t next_id = 0;

        std::function<void(const Node *, long long, int)> serialize_node;

        serialize_node = [&](const Node *node, long long parent_id, int child_index)
        {
            if (!node)
            {
                throw tree23::SerializationError("null node внутри дерева");
            }

            size_t current_id = next_id++;

            out << current_id << ' '
                << parent_id << ' '
                << child_index << ' '
                << node->key_count;

            for (size_t i = 0; i < node->key_count; ++i)
            {
                out << ' ' << node->keys[i];
            }

            out << '\n';

            for (size_t i = 0; i < node->child_count; ++i)
            {
                serialize_node(
                    node->children[i].get(),
                    static_cast<long long>(current_id),
                    static_cast<int>(i));
            }
        };

        serialize_node(root.get(), -1, -1);

        return out.str();
    }

    // 3.e.iii
    // Чтение из формата списка "узел-родитель"
    static Tree23<T> from_string_parent_child_pairs(const std::string &data)
    {
        ParsedPairs parsed = parse_pairs(data); // разбор текста
        link_children(parsed);                  // связывание записей
        return build_tree_from_pairs(parsed);   // сборка дерева
    }

    // 3.b: Извлечение поддерева по заданному элементу
    // Возвращает новое дерево, корнем которого является узел с заданным значением
    Tree23<T> extract_subtree(const T &root_value) const
    {
        if (!this->root)
        {
            throw tree23::EmptyTree(__func__);
        }

        Node *subtree_root = find_node_with_key(*this->root, root_value);

        if (!subtree_root)
        {
            throw tree23::KeyNotFound(__func__);
        }

        Tree23<T> result;
        result.root = deep_copy_node(*subtree_root, nullptr);
        result.tree_size = count_keys(*result.root);

        return result;
    }

    // 3.c: Поиск на вхождение поддерева
    // Проверяет, есть ли в этом дереве поддерево, структурно идентичное other
    bool contains_subtree(const Tree23 &other) const
    {
        if (!other.root)
        {
            return true; // Пустое дерево содержится в любом дереве
        }

        if (!this->root)
        {
            return false; // В пустом дереве ничего нет
        }

        // Обходим все узлы текущего дерева и проверяем, есть ли совпадение
        return contains_subtree_helper(*this->root, *other.root);
    }

    // Единая точка обхода: visit вызывается для каждого ключа
    template <typename Visitor>
    void traverse(TraversalOrder order, Visitor visit) const
    {
        switch (order)
        {
        case TraversalOrder::PreOrder:
            if (root)
                traverse_preorder(*root, visit);
            break;
        case TraversalOrder::InOrder:
            if (root)
                traverse_inorder(*root, visit);
            break;
        case TraversalOrder::PostOrder:
            if (root)
                traverse_postorder(*root, visit);
            break;
        case TraversalOrder::LevelOrder:
            traverse_levelorder(visit);
            break;
        }
    }

    // Для визуализации
    const Node *get_root() const { return root.get(); }

    // Получение размера дерева
    size_t get_size() const { return tree_size; }

    // Доступ к данным узла (для визуализатора)
    static size_t get_node_key_count(const Node *node)
    {
        return node ? node->key_count : 0;
    }

    static T get_node_key(const Node *node, size_t index)
    {
        if (!node || index >= node->key_count)
        {
            throw tree23::OutOfRange(__func__, "индекс ключа вне диапазона");
        }
        return node->keys[index];
    }

    static size_t get_node_child_count(const Node *node)
    {
        return node ? node->child_count : 0;
    }

    static const Node *get_node_child(const Node *node, size_t index)
    {
        if (!node || index >= node->child_count)
        {
            throw tree23::OutOfRange(__func__, "индекс ребенка вне диапазона");
        }
        return node->children[index].get();
    }

    static bool is_node_leaf(const Node *node)
    {
        return node ? node->is_leaf() : true;
    }

    Iterator begin() const;
    Iterator end() const;

    DynamicArray<T> range(const T& lo, const T& hi) const {
        DynamicArray<T> out;
        if (root) range_collect(root.get(), lo, hi, out);
        return out;
    }
};

#include "Tree23Iterator.h"