#include <iostream>
#include <string>
#include <cassert>
#include <vector>
#include <algorithm>
#include <chrono>
#include "../core/HashTable.h"
#include "../core/TreeExceptions.h"

#define TEST(name) void test_##name()
#define ASSERT(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "✗ ПРОВАЛЕН: " << #expr \
                      << " в " << __FILE__ << ":" << __LINE__ << std::endl; \
            exit(1); \
        } \
    } while (0)

#define RUN_TEST(name) \
    do { \
        std::cout << "Запуск теста: " << #name << "... "; \
        test_##name(); \
        std::cout << "✓ ПРОЙДЕН" << std::endl; \
    } while (0)

TEST(hash_basic_operations) {
    HashTable<std::string, int> table;
    table.Add("Alice", 25);
    table.Add("Bob", 30);
    table.Add("Charlie", 22);

    ASSERT(table.Get("Alice") == 25);
    ASSERT(table.Get("Bob") == 30);
    ASSERT(table.Get("Charlie") == 22);

    ASSERT(table.ContainsKey("Alice") == true);
    ASSERT(table.ContainsKey("Bob") == true);
    ASSERT(table.ContainsKey("Diana") == false);

    ASSERT(table.GetCount() == 3);
}

TEST(hash_duplicate_key) {
    HashTable<std::string, int> table;
    table.Add("Alice", 25);

    bool exception_caught = false;
    try {
        table.Add("Alice", 30);
    } catch (const tree23::DuplicateKey&) {
        exception_caught = true;
    }
    ASSERT(exception_caught);
    ASSERT(table.GetCount() == 1);
}

TEST(hash_get_nonexistent) {
    HashTable<std::string, int> table;
    table.Add("Alice", 25);

    bool exception_caught = false;
    try {
        table.Get("Bob");
    } catch (const tree23::KeyNotFound&) {
        exception_caught = true;
    }
    ASSERT(exception_caught);
}

TEST(hash_remove) {
    HashTable<std::string, int> table;
    table.Add("Alice", 25);
    table.Add("Bob", 30);
    table.Add("Charlie", 22);

    table.Remove("Bob");
    ASSERT(table.ContainsKey("Bob") == false);
    ASSERT(table.GetCount() == 2);

    bool exception_caught = false;
    try {
        table.Remove("Diana");
    } catch (const tree23::KeyNotFound&) {
        exception_caught = true;
    }
    ASSERT(exception_caught);
}

struct ConstantHash {
    size_t operator()(const std::string&) const {
        return 0;
    }
};

TEST(hash_collisions) {
    HashTable<std::string, int, ConstantHash> table;

    table.Add("key1", 1);
    table.Add("key2", 2);
    table.Add("key3", 3);

    ASSERT(table.GetCount() == 3);
    ASSERT(table.Get("key1") == 1);
    ASSERT(table.Get("key2") == 2);
    ASSERT(table.Get("key3") == 3);

    table.Remove("key2");
    ASSERT(table.GetCount() == 2);
    ASSERT(table.ContainsKey("key2") == false);
    ASSERT(table.Get("key1") == 1);
    ASSERT(table.Get("key3") == 3);
}

TEST(hash_rehash_on_full) {
    HashTable<int, int> table(4);
    size_t initial_capacity = table.GetCapacity();
    ASSERT(initial_capacity == 4);

    for (int i = 0; i < 4; ++i) {
        table.Add(i, i * 10);
    }

    ASSERT(table.GetCapacity() > initial_capacity);
    ASSERT(table.GetCount() == 4);

    for (int i = 0; i < 4; ++i) {
        ASSERT(table.Get(i) == i * 10);
    }
}

TEST(hash_rehash_on_sparse) {
    HashTable<int, int> table(16);
    for (int i = 0; i < 16; ++i) {
        table.Add(i, i * 10);
    }
    size_t capacity_after_fill = table.GetCapacity();
    ASSERT(capacity_after_fill >= 16);

    for (int i = 0; i < 12; ++i) {
        table.Remove(i);
    }

    ASSERT(table.GetCapacity() < capacity_after_fill);
    ASSERT(table.GetCount() == 4);

    for (int i = 12; i < 16; ++i) {
        ASSERT(table.Get(i) == i * 10);
    }
}

TEST(hash_large_dataset) {
    HashTable<std::string, int> table;

    for (int i = 0; i < 1000; ++i) {
        table.Add("key" + std::to_string(i), i);
    }
    ASSERT(table.GetCount() == 1000);

    for (int i = 0; i < 1000; ++i) {
        ASSERT(table.Get("key" + std::to_string(i)) == i);
    }

    for (int i = 0; i < 500; ++i) {
        table.Remove("key" + std::to_string(i));
    }
    ASSERT(table.GetCount() == 500);

    for (int i = 500; i < 1000; ++i) {
        ASSERT(table.Get("key" + std::to_string(i)) == i);
    }
}

TEST(hash_iterator_basic) {
    HashTable<std::string, int> table;
    table.Add("Alice", 25);
    table.Add("Bob", 30);
    table.Add("Charlie", 22);

    std::vector<std::pair<std::string, int>> collected;
    for (const auto& [key, value] : table) {
        collected.push_back({key, value});
    }

    ASSERT(collected.size() == 3);
    std::sort(collected.begin(), collected.end());
    ASSERT(collected[0] == std::make_pair(std::string("Alice"), 25));
    ASSERT(collected[1] == std::make_pair(std::string("Bob"), 30));
    ASSERT(collected[2] == std::make_pair(std::string("Charlie"), 22));
}

TEST(hash_iterator_empty) {
    HashTable<std::string, int> table;

    size_t count = 0;
    for (const auto& [key, value] : table) {
        (void)key;
        (void)value;
        ++count;
    }
    ASSERT(count == 0);
}

TEST(hash_iterator_after_rehash) {
    HashTable<int, int> table(4);
    for (int i = 0; i < 10; ++i) {
        table.Add(i, i * 10);
    }

    std::vector<std::pair<int, int>> collected;
    for (const auto& [key, value] : table) {
        collected.push_back({key, value});
    }

    ASSERT(collected.size() == 10);
    std::sort(collected.begin(), collected.end());
    for (int i = 0; i < 10; ++i) {
        ASSERT(collected[i] == std::make_pair(i, i * 10));
    }
}

TEST(hash_performance_basic) {
    HashTable<int, int> table;
    for (int i = 0; i < 10000; ++i) {
        table.Add(i, i);
    }
    ASSERT(table.GetCount() == 10000);

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 10000; ++i) {
        ASSERT(table.Get(i) == i);
    }
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    std::cout << "\n  Время поиска 10000 элементов: " << duration << " мкс\n";
    ASSERT(duration < 10000);
}

int main() {
    std::cout << "=== Тесты HashTable ===\n" << std::endl;

    RUN_TEST(hash_basic_operations);
    RUN_TEST(hash_duplicate_key);
    RUN_TEST(hash_get_nonexistent);
    RUN_TEST(hash_remove);
    RUN_TEST(hash_collisions);
    RUN_TEST(hash_rehash_on_full);
    RUN_TEST(hash_rehash_on_sparse);
    RUN_TEST(hash_large_dataset);
    RUN_TEST(hash_iterator_basic);
    RUN_TEST(hash_iterator_empty);
    RUN_TEST(hash_iterator_after_rehash);
    RUN_TEST(hash_performance_basic);

    std::cout << "\n=== Все тесты HashTable пройдены ===\n";
    return 0;
}