CREATE TABLE users (id INT PRIMARY KEY, name STRING, age INT);
INSERT INTO users VALUES (1, 'Alice', 25);
SAVE users TO 'users.db';
LOAD users2 FROM 'users.db';
SELECT * FROM users2;