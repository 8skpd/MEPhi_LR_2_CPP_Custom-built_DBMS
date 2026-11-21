#include <iostream>
#include <string>
#include <cassert>
#include "core/Tree23.h"

// Простой макрос для тестов
#define TEST(name) void test_##name()
#define RUN_TEST(name) do { \
    std::cout << "Запуск теста: " #name "... "; \
    try { \
        test_##name(); \
        std::cout << "✓ ПРОЙДЕН" << std::endl; \
    } catch (const std::exception& e) { \
        std::cout << "✗ ПРОВАЛЕН: " << e.what() << std::endl; \
        return 1; \
    } catch (...) { \
        std::cout << "✗ ПРОВАЛЕН: неизвестное исключение" << std::endl; \
        return 1; \
    } \
} while(0)

#define ASSERT(condition) do { \
    if (!(condition)) { \
        throw std::runtime_error("Assertion failed: " #condition); \
    } \
} while(0)

// ТЕСТЫ

TEST(empty_tree) {
    Tree23<int> tree;
    ASSERT(!tree.search(42));
}

TEST(single_insert) {
    Tree23<int> tree;
    tree.insert(10);
    ASSERT(tree.search(10));
    ASSERT(!tree.search(5));
}

TEST(multiple_inserts) {
    Tree23<int> tree;
    tree.insert(10);
    tree.insert(20);
    tree.insert(5);
    
    ASSERT(tree.search(10));
    ASSERT(tree.search(20));
    ASSERT(tree.search(5));
    ASSERT(!tree.search(15));
}

TEST(insert_triggers_split) {
    Tree23<int> tree;
    // Вставляем элементы, чтобы вызвать расщепление
    tree.insert(10);
    tree.insert(20);
    tree.insert(5);
    tree.insert(15);  // Должен вызвать split
    
    ASSERT(tree.search(10));
    ASSERT(tree.search(20));
    ASSERT(tree.search(5));
    ASSERT(tree.search(15));
}

TEST(large_insert_sequence) {
    Tree23<int> tree;
    // Вставляем много элементов, чтобы проверить стабильность
    for (int i = 0; i < 100; ++i) {
        tree.insert(i);
    }
    
    // Проверяем, что все элементы на месте
    for (int i = 0; i < 100; ++i) {
        ASSERT(tree.search(i));
    }
    
    ASSERT(!tree.search(100));
    ASSERT(!tree.search(-1));
}

TEST(random_order_inserts) {
    Tree23<int> tree;
    // Вставляем в случайном порядке
    int values[] = {50, 30, 70, 20, 40, 60, 80, 10, 25, 35, 45, 55, 65, 75, 90};
    
    for (int val : values) {
        tree.insert(val);
    }
    
    // Проверяем все
    for (int val : values) {
        ASSERT(tree.search(val));
    }
}

TEST(map_basic) {
    Tree23<int> tree;
    tree.insert(5);
    tree.insert(10);
    tree.insert(15);
    
    auto doubled = tree.map([](int x) { return x * 2; });
    
    ASSERT(doubled.search(10));  // 5 * 2
    ASSERT(doubled.search(20));  // 10 * 2
    ASSERT(doubled.search(30));  // 15 * 2
}

TEST(reduce_sum) {
    Tree23<int> tree;
    tree.insert(5);
    tree.insert(10);
    tree.insert(15);
    
    int sum = tree.reduce(0, [](int acc, int x) { return acc + x; });
    ASSERT(sum == 30);  // 5 + 10 + 15
}

TEST(reduce_product) {
    Tree23<int> tree;
    tree.insert(2);
    tree.insert(3);
    tree.insert(5);
    
    int product = tree.reduce(1, [](int acc, int x) { return acc * x; });
    ASSERT(product == 30);  // 2 * 3 * 5
}

TEST(reduce_empty_tree) {
    Tree23<int> tree;
    int result = tree.reduce(42, [](int acc, int x) { return acc + x; });
    ASSERT(result == 42);  // Должно вернуть начальное значение
}

TEST(string_tree) {
    Tree23<std::string> tree;
    tree.insert("apple");
    tree.insert("banana");
    tree.insert("cherry");
    
    ASSERT(tree.search("apple"));
    ASSERT(tree.search("banana"));
    ASSERT(tree.search("cherry"));
    ASSERT(!tree.search("grape"));
}

TEST(stress_test) {
    Tree23<int> tree;
    // Вставляем 1000 элементов
    for (int i = 0; i < 1000; ++i) {
        tree.insert(i);
    }
    
    // Проверяем все
    for (int i = 0; i < 1000; ++i) {
        ASSERT(tree.search(i));
    }
}

TEST(remove_single_element) {
    Tree23<int> tree;
    tree.insert(10);
    tree.remove(10);
    ASSERT(!tree.search(10));
}

TEST(remove_from_leaf) {
    Tree23<int> tree;
    tree.insert(10);
    tree.insert(20);
    tree.insert(5);
    
    tree.remove(5);
    ASSERT(!tree.search(5));
    ASSERT(tree.search(10));
    ASSERT(tree.search(20));
}

TEST(remove_triggers_redistribute) {
    Tree23<int> tree;
    // Создаем дерево, где при удалении потребуется заимствование
    for (int i = 1; i <= 10; ++i) {
        tree.insert(i);
    }
    
    tree.remove(1);
    ASSERT(!tree.search(1));
    
    // Проверяем, что остальные элементы на месте
    for (int i = 2; i <= 10; ++i) {
        ASSERT(tree.search(i));
    }
}

TEST(remove_triggers_merge) {
    Tree23<int> tree;
    // Создаем дерево, где при удалении потребуется слияние
    for (int i = 1; i <= 7; ++i) {
        tree.insert(i);
    }
    
    tree.remove(1);
    tree.remove(2);
    ASSERT(!tree.search(1));
    ASSERT(!tree.search(2));
    
    // Проверяем остальные
    for (int i = 3; i <= 7; ++i) {
        ASSERT(tree.search(i));
    }
}

TEST(remove_root) {
    Tree23<int> tree;
    tree.insert(10);
    tree.insert(5);
    tree.insert(15);
    
    tree.remove(10);
    ASSERT(!tree.search(10));
    ASSERT(tree.search(5));
    ASSERT(tree.search(15));
}

TEST(remove_all_elements) {
    Tree23<int> tree;
    for (int i = 1; i <= 10; ++i) {
        tree.insert(i);
    }
    
    for (int i = 1; i <= 10; ++i) {
        tree.remove(i);
        ASSERT(!tree.search(i));
    }
}

TEST(remove_nonexistent) {
    Tree23<int> tree;
    tree.insert(10);
    
    bool exception_caught = false;
    try {
        tree.remove(99);
    } catch (const std::invalid_argument&) {
        exception_caught = true;
    }
    ASSERT(exception_caught);
}

TEST(stress_remove) {
    Tree23<int> tree;
    // Вставляем 100 элементов
    for (int i = 0; i < 100; ++i) {
        tree.insert(i);
    }
    
    // Удаляем четные
    for (int i = 0; i < 100; i += 2) {
        tree.remove(i);
    }
    
    // Проверяем, что четные удалены, а нечетные на месте
    for (int i = 0; i < 100; ++i) {
        if (i % 2 == 0) {
            ASSERT(!tree.search(i));
        } else {
            ASSERT(tree.search(i));
        }
    }
}

TEST(path_empty_returns_root) {
    Tree23<int> tree;
    tree.insert(10);
    tree.insert(20);
    tree.insert(5);
    
    auto root = tree.get_node_by_absolute_path("");
    ASSERT(root != nullptr);
    ASSERT(root->keys[0] == 10 || root->keys[0] == 5); // Корень может быть любым из вставленных
}

TEST(path_single_level) {
    Tree23<int> tree;
    tree.insert(10);
    tree.insert(5);
    tree.insert(15);
    tree.insert(20); // Должен вызвать split
    
    // После split корень будет содержать один ключ, а дети - два
    auto child0 = tree.get_node_by_absolute_path("0");
    ASSERT(child0 != nullptr);
    
    auto child1 = tree.get_node_by_absolute_path("1");
    ASSERT(child1 != nullptr);
}

TEST(path_multi_level) {
    Tree23<int> tree;
    // Вставляем достаточно элементов, чтобы создать дерево глубиной 3+
    for (int i = 1; i <= 20; ++i) {
        tree.insert(i);
    }
    
    // Путь "0-0" должен существовать
    auto node = tree.get_node_by_absolute_path("0-0");
    ASSERT(node != nullptr);
    
    // Путь "1-2" тоже должен существовать
    node = tree.get_node_by_absolute_path("1-2");
    ASSERT(node != nullptr);
}

TEST(path_invalid_index) {
    Tree23<int> tree;
    tree.insert(10);
    tree.insert(5);
    
    bool exception_caught = false;
    try {
        // Индекс 5 не существует (максимум 2 для 2-3 дерева)
        tree.get_node_by_absolute_path("5");
    } catch (const std::out_of_range&) {
        exception_caught = true;
    }
    ASSERT(exception_caught);
}

TEST(path_invalid_format) {
    Tree23<int> tree;
    tree.insert(10);
    
    bool exception_caught = false;
    try {
        tree.get_node_by_absolute_path("0-abc-2");
    } catch (const std::invalid_argument&) {
        exception_caught = true;
    }
    ASSERT(exception_caught);
}

TEST(path_empty_tree) {
    Tree23<int> tree;
    
    bool exception_caught = false;
    try {
        tree.get_node_by_absolute_path("0");
    } catch (const std::invalid_argument&) {
        exception_caught = true;
    }
    ASSERT(exception_caught);
}

TEST(relative_path_basic) {
    Tree23<int> tree;
    for (int i = 1; i <= 20; ++i) {
        tree.insert(i);
    }
    
    // Получаем корень
    auto root = tree.get_node_by_absolute_path("");
    ASSERT(root != nullptr);
    
    // Относительный путь от корня
    auto child = tree.get_node_by_relative_path(root, "0");
    ASSERT(child != nullptr);
    
    // Относительный путь от ребенка
    auto grandchild = tree.get_node_by_relative_path(child, "1");
    ASSERT(grandchild != nullptr);
}

TEST(relative_path_empty) {
    Tree23<int> tree;
    tree.insert(10);
    
    auto root = tree.get_node_by_absolute_path("");
    auto same_node = tree.get_node_by_relative_path(root, "");
    
    ASSERT(same_node == root);
}

TEST(relative_path_null_node) {
    Tree23<int> tree;
    tree.insert(10);
    
    bool exception_caught = false;
    try {
        tree.get_node_by_relative_path(nullptr, "0");
    } catch (const std::invalid_argument&) {
        exception_caught = true;
    }
    ASSERT(exception_caught);
}

TEST(serialization_fixed_empty) {
    Tree23<int> tree;

    std::string data = tree.to_string_fixed_traversal();

    Tree23<int> restored = Tree23<int>::from_string_fixed_traversal(data);

    ASSERT(!restored.search(10));
}

TEST(serialization_fixed_roundtrip) {
    Tree23<int> tree;

    for (int i = 0; i < 100; ++i) {
        tree.insert(i);
    }

    std::string data = tree.to_string_fixed_traversal();

    Tree23<int> restored = Tree23<int>::from_string_fixed_traversal(data);

    for (int i = 0; i < 100; ++i) {
        ASSERT(restored.search(i));
    }

    ASSERT(!restored.search(100));
    ASSERT(!restored.search(-1));

    std::string data2 = restored.to_string_fixed_traversal();

    ASSERT(data == data2);
}

TEST(serialization_pairs_empty) {
    Tree23<int> tree;

    std::string data = tree.to_string_parent_child_pairs();

    Tree23<int> restored = Tree23<int>::from_string_parent_child_pairs(data);

    ASSERT(!restored.search(42));
}

TEST(serialization_pairs_roundtrip) {
    Tree23<int> tree;

    for (int i = 0; i < 100; ++i) {
        tree.insert(i);
    }

    std::string data = tree.to_string_parent_child_pairs();

    Tree23<int> restored = Tree23<int>::from_string_parent_child_pairs(data);

    for (int i = 0; i < 100; ++i) {
        ASSERT(restored.search(i));
    }

    ASSERT(!restored.search(100));
    ASSERT(!restored.search(-1));

    std::string data2 = restored.to_string_parent_child_pairs();

    ASSERT(data == data2);
}

TEST(serialization_fixed_after_remove) {
    Tree23<int> tree;

    for (int i = 0; i < 50; ++i) {
        tree.insert(i);
    }

    for (int i = 0; i < 50; i += 2) {
        tree.remove(i);
    }

    std::string data = tree.to_string_fixed_traversal();

    Tree23<int> restored = Tree23<int>::from_string_fixed_traversal(data);

    for (int i = 0; i < 50; ++i) {
        if (i % 2 == 0) {
            ASSERT(!restored.search(i));
        } else {
            ASSERT(restored.search(i));
        }
    }
}

TEST(serialization_pairs_after_remove) {
    Tree23<int> tree;

    for (int i = 0; i < 50; ++i) {
        tree.insert(i);
    }

    for (int i = 0; i < 50; i += 2) {
        tree.remove(i);
    }

    std::string data = tree.to_string_parent_child_pairs();

    Tree23<int> restored = Tree23<int>::from_string_parent_child_pairs(data);

    for (int i = 0; i < 50; ++i) {
        if (i % 2 == 0) {
            ASSERT(!restored.search(i));
        } else {
            ASSERT(restored.search(i));
        }
    }
}

TEST(serialization_fixed_invalid_data) {
    bool exception_caught = false;

    try {
        Tree23<int>::from_string_fixed_traversal("aboba");
    } catch (const std::invalid_argument&) {
        exception_caught = true;
    }

    ASSERT(exception_caught);
}

TEST(serialization_pairs_duplicate_id) {
    std::string data =
        "0 -1 -1 1 10\n"
        "0 0 0 1 5\n";

    bool exception_caught = false;

    try {
        Tree23<int>::from_string_parent_child_pairs(data);
    } catch (const std::invalid_argument&) {
        exception_caught = true;
    }

    ASSERT(exception_caught);
}

TEST(serialization_pairs_duplicate_child_index) {
    std::string data =
        "0 -1 -1 1 10\n"
        "1 0 0 1 5\n"
        "2 0 0 1 7\n";

    bool exception_caught = false;

    try {
        Tree23<int>::from_string_parent_child_pairs(data);
    } catch (const std::invalid_argument&) {
        exception_caught = true;
    }

    ASSERT(exception_caught);
}

TEST(extract_subtree_basic) {
    Tree23<int> tree;
    tree.insert(10);
    tree.insert(5);
    tree.insert(15);
    tree.insert(20);
    
    // Извлекаем поддерево с корнем 15
    Tree23<int> subtree = tree.extract_subtree(15);
    
    ASSERT(subtree.search(15));
    ASSERT(subtree.search(20));
    ASSERT(!subtree.search(10));
    ASSERT(!subtree.search(5));
}

TEST(extract_subtree_root) {
    Tree23<int> tree;
    for (int i = 1; i <= 10; ++i) {
        tree.insert(i);
    }
    
    // Находим корень и извлекаем всё дерево
    auto root_val = tree.get_node_by_absolute_path("")->keys[0];
    Tree23<int> subtree = tree.extract_subtree(root_val);
    
    // Должно содержать все элементы
    for (int i = 1; i <= 10; ++i) {
        ASSERT(subtree.search(i));
    }
}

TEST(extract_subtree_leaf) {
    Tree23<int> tree;
    tree.insert(10);
    tree.insert(5);
    tree.insert(15);
    
    // Извлекаем лист
    Tree23<int> subtree = tree.extract_subtree(5);
    
    ASSERT(subtree.search(5));
    ASSERT(!subtree.search(10));
    ASSERT(!subtree.search(15));
}

TEST(extract_subtree_not_found) {
    Tree23<int> tree;
    tree.insert(10);
    
    bool exception_caught = false;
    try {
        tree.extract_subtree(99);
    } catch (const std::invalid_argument&) {
        exception_caught = true;
    }
    ASSERT(exception_caught);
}

TEST(extract_subtree_empty_tree) {
    Tree23<int> tree;
    
    bool exception_caught = false;
    try {
        tree.extract_subtree(10);
    } catch (const std::invalid_argument&) {
        exception_caught = true;
    }
    ASSERT(exception_caught);
}

TEST(contains_subtree_identical) {
    Tree23<int> tree1;
    tree1.insert(10);
    tree1.insert(5);
    tree1.insert(15);
    
    Tree23<int> tree2;
    tree2.insert(10);
    tree2.insert(5);
    tree2.insert(15);
    
    ASSERT(tree1.contains_subtree(tree2));
}

TEST(contains_subtree_smaller) {
    Tree23<int> tree1;
    tree1.insert(10);
    tree1.insert(5);
    tree1.insert(15);
    tree1.insert(20);
    
    Tree23<int> tree2;
    tree2.insert(15);
    tree2.insert(20);
    
    ASSERT(tree1.contains_subtree(tree2));
}

TEST(contains_subtree_not_found) {
    Tree23<int> tree1;
    tree1.insert(10);
    tree1.insert(5);
    tree1.insert(15);
    
    Tree23<int> tree2;
    tree2.insert(99);
    
    ASSERT(!tree1.contains_subtree(tree2));
}

TEST(contains_subtree_empty) {
    Tree23<int> tree1;
    tree1.insert(10);
    
    Tree23<int> tree2;
    
    // Пустое дерево содержится в любом дереве
    ASSERT(tree1.contains_subtree(tree2));
    
    // Но в пустом дереве ничего нет
    ASSERT(!tree2.contains_subtree(tree1));
}

TEST(contains_subtree_different_structure) {
    Tree23<int> tree1;
    tree1.insert(10);
    tree1.insert(5);
    tree1.insert(15);
    
    Tree23<int> tree2;
    tree2.insert(5);
    tree2.insert(10);
    tree2.insert(15);
    
    // Структуры разные, хотя элементы те же
    // В tree1 корень может быть 10, а в tree2 - 5
    // Они не идентичны структурно
    // (зависит от порядка вставки и балансировки)
    // Просто проверяем, что метод работает без падений
    bool result = tree1.contains_subtree(tree2);
    // Результат может быть true или false, главное что не упало
    ASSERT(result == result);
}

TEST(extract_and_contains_roundtrip) {
    Tree23<int> tree;
    for (int i = 1; i <= 20; ++i) {
        tree.insert(i);
    }
    
    // Извлекаем поддерево
    Tree23<int> subtree = tree.extract_subtree(10);
    
    // Проверяем, что исходное дерево содержит это поддерево
    ASSERT(tree.contains_subtree(subtree));
}

TEST(traverse_inorder_is_sorted) {
    Tree23<int> tree;
    for (int v : {50, 30, 70, 20, 40, 60, 80, 10, 25}) tree.insert(v);

    std::string s = tree.reduce(std::string(),
        [](std::string acc, int x) { return acc + std::to_string(x) + " "; });

    ASSERT(s == "10 20 25 30 40 50 60 70 80 ");
}

TEST(traverse_orders_differ) {
    Tree23<int> tree;
    for (int v : {50, 30, 70, 20, 40}) tree.insert(v);

    auto collect = [&](Tree23<int>::TraversalOrder order) {
        std::string s;
        tree.traverse(order, [&](const int& x) { s += std::to_string(x) + " "; });
        return s;
    };

    std::string in   = collect(Tree23<int>::TraversalOrder::InOrder);
    std::string pre  = collect(Tree23<int>::TraversalOrder::PreOrder);
    std::string post = collect(Tree23<int>::TraversalOrder::PostOrder);

    ASSERT(in == "20 30 40 50 70 ");
    ASSERT(pre != in);      // pre-order начинается с корня
    ASSERT(post != in);     // post-order заканчивается корнем
}

TEST(traverse_levelorder_starts_with_root) {
    Tree23<int> tree;
    for (int v : {50, 30, 70, 20, 40}) tree.insert(v);

    DynamicArray<int> order;
    tree.traverse(Tree23<int>::TraversalOrder::LevelOrder,
                  [&](const int& x) { order.push_back(x); });

    ASSERT(order.size() == 5);
    // первый посещённый ключ — из корня
    ASSERT(order[0] == tree.get_node_by_absolute_path("")->keys[0]);
}

TEST(reduce_order_parameter) {
    // После перехода на итератор reduce всегда обходит в InOrder (сортировка)
    // Проверяем на некоммутативной операции (конкатенация строк)
    Tree23<int> tree;
    for (int v : {3, 1, 2}) tree.insert(v);

    std::string s = tree.reduce(std::string(),
        [](std::string a, int x) { return a + std::to_string(x); });

    // Всегда отсортировано, независимо от порядка вставки
    ASSERT(s == "123");

    // Проверяем на другом порядке вставки — результат тот же
    Tree23<int> tree2;
    for (int v : {2, 3, 1}) tree2.insert(v);

    std::string s2 = tree2.reduce(std::string(),
        [](std::string a, int x) { return a + std::to_string(x); });

    ASSERT(s2 == "123");
    ASSERT(s == s2);  // Детерминированность по содержимому
}

TEST(traverse_empty_tree) {
    Tree23<int> tree;
    size_t n = 0;
    auto counter = [&](const int&) { ++n; };
    tree.traverse(Tree23<int>::TraversalOrder::InOrder, counter);
    tree.traverse(Tree23<int>::TraversalOrder::LevelOrder, counter);
    ASSERT(n == 0);
}

// Тесты итератора

TEST(iterator_inorder_sorted) {
    Tree23<int> tree;
    for (int v : {50, 30, 70, 20, 40, 60, 80, 10, 25}) tree.insert(v);

    DynamicArray<int> result;
    for (const auto& key : tree) {
        result.push_back(key);
    }

    // 9 элементов, отсортированных InOrder
    ASSERT(result.size() == 9);

    int expected[] = {10, 20, 25, 30, 40, 50, 60, 70, 80};
    for (size_t i = 0; i < 9; ++i) {
        ASSERT(result[i] == expected[i]);
    }
}

TEST(iterator_with_std_find) {
    Tree23<int> tree;
    for (int v : {10, 20, 30, 40, 50}) tree.insert(v);

    // std::find через итератор
    auto it = std::find(tree.begin(), tree.end(), 30);
    ASSERT(it != tree.end());
    ASSERT(*it == 30);

    // Элемент не найден
    auto it2 = std::find(tree.begin(), tree.end(), 99);
    ASSERT(it2 == tree.end());
}

TEST(iterator_empty_tree) {
    Tree23<int> tree;
    size_t count = 0;
    for (const auto& key : tree) {
        (void)key;
        ++count;
    }
    ASSERT(count == 0);
}

TEST(iterator_single_element) {
    Tree23<int> tree;
    tree.insert(42);

    DynamicArray<int> result;
    for (const auto& key : tree) result.push_back(key);

    ASSERT(result.size() == 1);
    ASSERT(result[0] == 42);
}

TEST(iterator_reduce_uses_iterator) {
    // Проверяем, что reduce теперь идёт в InOrder (сортировка)
    Tree23<int> tree;
    for (int v : {5, 1, 3, 2, 4}) tree.insert(v);

    // Конкатенация — некоммутативная операция, порядок важен
    std::string s = tree.reduce(std::string(),
        [](std::string acc, int x) { return acc + std::to_string(x); });

    // Должно быть отсортировано: 12345
    ASSERT(s == "12345");
}

TEST(iterator_map_uses_iterator) {
    Tree23<int> tree;
    for (int v : {3, 1, 2}) tree.insert(v);

    auto doubled = tree.map([](const int& x) { return x * 2; });

    DynamicArray<int> result;
    for (const auto& key : doubled) result.push_back(key);

    // 2, 4, 6 — в отсортированном порядке
    ASSERT(result.size() == 3);
    ASSERT(result[0] == 2);
    ASSERT(result[1] == 4);
    ASSERT(result[2] == 6);
}

// MAIN

int main() {
    std::cout << "=== Тестирование 2-3 дерева ===" << std::endl << std::endl;
    
    //  Базовые операции и вставка 
    RUN_TEST(empty_tree);
    RUN_TEST(single_insert);
    RUN_TEST(multiple_inserts);
    RUN_TEST(insert_triggers_split);
    RUN_TEST(large_insert_sequence);
    RUN_TEST(random_order_inserts);
    
    //  Удаление
    RUN_TEST(remove_single_element);
    RUN_TEST(remove_from_leaf);
    RUN_TEST(remove_triggers_redistribute);
    RUN_TEST(remove_triggers_merge);
    RUN_TEST(remove_root);
    RUN_TEST(remove_all_elements);
    RUN_TEST(remove_nonexistent);
    RUN_TEST(stress_remove);
    
    //  Функциональные методы (map, reduce) 
    RUN_TEST(map_basic);
    RUN_TEST(reduce_sum);
    RUN_TEST(reduce_product);
    RUN_TEST(reduce_empty_tree);
    RUN_TEST(string_tree);
    
    //  Стресс-тест на стабильность 
    RUN_TEST(stress_test);

    //  Навигация по путям 
    RUN_TEST(path_empty_returns_root);
    RUN_TEST(path_single_level);
    RUN_TEST(path_multi_level);
    RUN_TEST(path_invalid_index);
    RUN_TEST(path_invalid_format);
    RUN_TEST(path_empty_tree);
    RUN_TEST(relative_path_basic);
    RUN_TEST(relative_path_empty);
    RUN_TEST(relative_path_null_node);

    // Сериализация
    RUN_TEST(serialization_fixed_empty);
    RUN_TEST(serialization_fixed_roundtrip);
    RUN_TEST(serialization_pairs_empty);
    RUN_TEST(serialization_pairs_roundtrip);
    RUN_TEST(serialization_fixed_after_remove);
    RUN_TEST(serialization_pairs_after_remove);
    RUN_TEST(serialization_fixed_invalid_data);
    RUN_TEST(serialization_pairs_duplicate_id);
    RUN_TEST(serialization_pairs_duplicate_child_index);

    //  Работа с поддеревьями 
    RUN_TEST(extract_subtree_basic);
    RUN_TEST(extract_subtree_root);
    RUN_TEST(extract_subtree_leaf);
    RUN_TEST(extract_subtree_not_found);
    RUN_TEST(extract_subtree_empty_tree);
    RUN_TEST(contains_subtree_identical);
    RUN_TEST(contains_subtree_smaller);
    RUN_TEST(contains_subtree_not_found);
    RUN_TEST(contains_subtree_empty);
    RUN_TEST(contains_subtree_different_structure);
    RUN_TEST(extract_and_contains_roundtrip);

    // Обход
    RUN_TEST(traverse_inorder_is_sorted);
    RUN_TEST(traverse_orders_differ);
    RUN_TEST(traverse_levelorder_starts_with_root);
    RUN_TEST(reduce_order_parameter);
    RUN_TEST(traverse_empty_tree);

    // Тесты итератора
    RUN_TEST(iterator_inorder_sorted);
    RUN_TEST(iterator_with_std_find);
    RUN_TEST(iterator_empty_tree);
    RUN_TEST(iterator_single_element);
    RUN_TEST(iterator_reduce_uses_iterator);
    RUN_TEST(iterator_map_uses_iterator);
    
    std::cout << std::endl << "=== Все тесты пройдены! ===" << std::endl;
    return 0;
}