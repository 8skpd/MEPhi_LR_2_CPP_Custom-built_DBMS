#!/bin/bash

mkdir -p bin

if [ "$1" == "1" ]; then
    g++ -std=c++20 -O2 -Wall -I. tests/test_tree.cpp -o bin/test_tree
    if [ $? -eq 0 ]; then ./bin/test_tree; else echo "Ошибка компиляции"; exit 1; fi

elif [ "$1" == "2" ]; then
    g++ -std=c++20 -O2 -Wall -I. tests/test_hash.cpp -o bin/test_hash
    if [ $? -eq 0 ]; then ./bin/test_hash; else echo "Ошибка компиляции"; exit 1; fi

elif [ "$1" == "3" ]; then
    g++ -std=c++20 -O2 -Wall -I. tests/test_range.cpp -o bin/test_range
    if [ $? -eq 0 ]; then ./bin/test_range; else echo "Ошибка компиляции"; exit 1; fi

elif [ "$1" == "4" ]; then
    g++ -std=c++20 -O2 -Wall -I. tests/test_index.cpp -o bin/test_index
    if [ $? -eq 0 ]; then ./bin/test_index; else echo "Ошибка компиляции"; exit 1; fi

elif [ "$1" == "5" ]; then
    g++ -std=c++20 -O2 -Wall -I. tests/test_table.cpp -o bin/test_table
    if [ $? -eq 0 ]; then ./bin/test_table; else echo "Ошибка компиляции"; exit 1; fi

elif [ "$1" == "6" ]; then
    g++ -std=c++20 -O2 -Wall -I. tests/test_parser.cpp -o bin/test_parser
    if [ $? -eq 0 ]; then ./bin/test_parser; else echo "Ошибка компиляции"; exit 1; fi

elif [ "$1" == "7" ]; then
    g++ -std=c++20 -O2 -Wall -I. tests/test_executor.cpp -o bin/test_executor
    if [ $? -eq 0 ]; then ./bin/test_executor; else echo "Ошибка компиляции"; exit 1; fi

elif [ "$1" == "8" ]; then
    g++ -std=c++20 -O2 -Wall -I. tests/test_serializer.cpp -o bin/test_serializer
    if [ $? -eq 0 ]; then ./bin/test_serializer; else echo "Ошибка компиляции"; exit 1; fi

elif [ "$1" == "9" ]; then
    g++ -std=c++20 -O2 -Wall -I. cli/main.cpp -o bin/db_cli
    if [ $? -eq 0 ]; then ./bin/db_cli; else echo "Ошибка компиляции"; exit 1; fi

elif [ "$1" == "all" ]; then
    for n in 1 2 3 4 5 6 7 8; do ./c.sh $n || exit 1; done

elif [ "$1" == "clean" ]; then
    rm -rf bin
    echo "✓ bin/ очищена"

else
    echo "Использование: ./c.sh {1|2|3|4|all|clean}"
    echo "  1 - тесты дерева (ЛР-1)"
    echo "  2 - тесты хеш-таблицы"
    echo "  3 - тесты range / Set"
    echo "  4 - тесты Value / Schema / индексов"
    echo "  5 - тесты Table"
    echo "  6 - тесты SQL-парсера"
    echo "  7 - сквозные тесты исполнителя"
    echo "  8 - тесты сериализации"
    echo "  9 - интерактивный режим"
    echo "  all - все тесты подряд"
    exit 1
fi