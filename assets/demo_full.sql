--  Демонстрационный сценарий мини-СУБД
--  Покрывает: таблицы, индексы (дерево/хеш), запросы,
--  сравнение алгоритмов на 3 типах данных, сохранение.
--  Вводите команды ПО ОДНОЙ в интерактивном режиме (./c.sh 9).


-- 0. Справка
HELP

-- 1. Создание таблицы и просмотр метаданных
CREATE TABLE users (id INT PRIMARY KEY, name STRING, age INT);
SHOW TABLES
DESCRIBE users

-- 2. Ручная вставка и чтение исходных данных
INSERT INTO users VALUES (1, 'Alice', 25)
INSERT INTO users VALUES (2, 'Bob', 30)
INSERT INTO users VALUES (3, 'Charlie', 22)
INSERT INTO users VALUES (4, 'Diana', 28)
INSERT INTO users VALUES (5, 'Eva', 30)
SELECT * FROM users
SELECT name, age FROM users

-- 3. WHERE: все операторы сравнения + BETWEEN + AND
SELECT * FROM users WHERE age = 30;
SELECT * FROM users WHERE age <> 30;
SELECT * FROM users WHERE age != 30;
SELECT * FROM users WHERE age > 25;
SELECT * FROM users WHERE age < 28;
SELECT * FROM users WHERE age >= 28;
SELECT * FROM users WHERE age <= 25;
SELECT * FROM users WHERE age BETWEEN 22 AND 28;
SELECT * FROM users WHERE age = 30 AND name = 'Bob';

-- 4. Создание индексов: дерево (диапазоны) и хеш (равенство)
CREATE INDEX idx_age_tree ON users(age) USING BTREE
CREATE INDEX idx_age_hash ON users(age) USING HASH
CREATE INDEX idx_name_hash ON users(name) USING HASH
SHOW INDEXES ON users

-- 5. Планировщик и принудительный выбор индекса (сравнение)
SELECT * FROM users WHERE age = 30
SELECT * FROM users WITH INDEX(idx_age_tree) WHERE age = 30
SELECT * FROM users WITH INDEX(idx_age_hash) WHERE age = 30
SELECT * FROM users WHERE age BETWEEN 22 AND 28
SELECT * FROM users LIMIT 3

-- 6. Удаление с условием
DELETE FROM users WHERE id = 3
SELECT * FROM users

-- 7. Сравнение алгоритмов на 3 типах данных (требование ЛР-2)
--    Для деревьев измеряется ВРЕМЯ ПОСТРОЕНИЯ (сноска 1).
CREATE TABLE perf (id INT PRIMARY KEY, value INT)
CREATE INDEX idx_perf_tree ON perf(value) USING BTREE
CREATE INDEX idx_perf_hash ON perf(value) USING HASH

-- Случай 1: последовательность отсортирована по возрастанию
DELETE FROM perf
GENERATE 10000 ROWS FOR perf WITH value SORTED ASC
BENCHMARK BUILD INDEX ON perf(value)
BENCHMARK SELECT * FROM perf WHERE value = 5000
BENCHMARK SELECT * FROM perf WHERE value BETWEEN 4000 AND 6000

-- Случай 2: последовательность отсортирована в обратном направлении
DELETE FROM perf
GENERATE 10000 ROWS FOR perf WITH value SORTED DESC
BENCHMARK BUILD INDEX ON perf(value)
BENCHMARK SELECT * FROM perf WHERE value = 5000

-- Случай 3: последовательность не отсортирована
DELETE FROM perf
GENERATE 10000 ROWS FOR perf WITH value RANDOM
BENCHMARK BUILD INDEX ON perf(value)
BENCHMARK SELECT * FROM perf WHERE value = 5000

-- 8. Сохранение и загрузка (О-2)
--    Путь относительный: файл появится в текущей директории.
SAVE users TO 'demo_users.db'
DELETE FROM users
SELECT * FROM users
LOAD users_backup FROM 'demo_users.db'
SELECT * FROM users_backup

-- 9. Демонстрация обработки ошибок (валидация)
--    Каждая команда выведет сообщение об ошибке и НЕ упадёт.
INSERT INTO users VALUES (999, 'Frank', 'not_a_number')
SELECT * FROM nonexistent_table
SELECT * FROM users WHERE age = 'abc'
CREATE TABLE users (x INT)
SELECT * FROM users WHERE

-- 10. Завершение
EXIT