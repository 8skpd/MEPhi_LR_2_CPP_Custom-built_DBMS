#pragma once
#include "TreeExceptions.h"

namespace db {

// Каждый класс наследует конструктор (where, detail) от базовой категории
// и добавляет удобный (where) с описанием по умолчанию.

class SqlSyntaxError : public tree23::InvalidArgument {
public:
    using tree23::InvalidArgument::InvalidArgument;
    explicit SqlSyntaxError(const char* where)
        : tree23::InvalidArgument(where, "некорректный синтаксис SQL-команды") {}
};

class TableNotFound : public tree23::OutOfRange {
public:
    using tree23::OutOfRange::OutOfRange;
    explicit TableNotFound(const char* where)
        : tree23::OutOfRange(where, "таблица с указанным именем не найдена") {}
};

class TableAlreadyExists : public tree23::InvalidArgument {
public:
    using tree23::InvalidArgument::InvalidArgument;
    explicit TableAlreadyExists(const char* where)
        : tree23::InvalidArgument(where, "таблица с таким именем уже существует") {}
};

class ColumnNotFound : public tree23::OutOfRange {
public:
    using tree23::OutOfRange::OutOfRange;
    explicit ColumnNotFound(const char* where)
        : tree23::OutOfRange(where, "колонка с указанным именем не найдена") {}
};

class ColumnAlreadyExists : public tree23::InvalidArgument {
public:
    using tree23::InvalidArgument::InvalidArgument;
    explicit ColumnAlreadyExists(const char* where)
        : tree23::InvalidArgument(where, "колонка с таким именем уже существует") {}
};

class IndexNotFound : public tree23::OutOfRange {
public:
    using tree23::OutOfRange::OutOfRange;
    explicit IndexNotFound(const char* where)
        : tree23::OutOfRange(where, "индекс с указанным именем не найден") {}
};

class IndexAlreadyExists : public tree23::InvalidArgument {
public:
    using tree23::InvalidArgument::InvalidArgument;
    explicit IndexAlreadyExists(const char* where)
        : tree23::InvalidArgument(where, "индекс с таким именем уже существует") {}
};

class TypeMismatch : public tree23::InvalidArgument {
public:
    using tree23::InvalidArgument::InvalidArgument;
    explicit TypeMismatch(const char* where)
        : tree23::InvalidArgument(where, "несоответствие типов значений") {}
};

class UniqueViolation : public tree23::InvalidArgument {
public:
    using tree23::InvalidArgument::InvalidArgument;
    explicit UniqueViolation(const char* where)
        : tree23::InvalidArgument(where, "нарушение ограничения уникальности") {}
};

class UnsupportedIndexOperation : public tree23::InvalidArgument {
public:
    using tree23::InvalidArgument::InvalidArgument;
    explicit UnsupportedIndexOperation(const char* where)
        : tree23::InvalidArgument(where, "операция не поддерживается данным типом индекса") {}
};

class SchemaViolation : public tree23::InvalidArgument {
public:
    using tree23::InvalidArgument::InvalidArgument;
    explicit SchemaViolation(const char* where)
        : tree23::InvalidArgument(where, "строка не соответствует схеме таблицы") {}
};

class RowNotFound : public tree23::OutOfRange {
public:
    using tree23::OutOfRange::OutOfRange;
    explicit RowNotFound(const char* where)
        : tree23::OutOfRange(where, "строка с указанным идентификатором не найдена") {}
};

class SerializationError : public tree23::InvalidArgument {
public:
    using tree23::InvalidArgument::InvalidArgument;
    explicit SerializationError(const char* where)
        : tree23::InvalidArgument(where, "ошибка сериализации/десериализации") {}
};

} // namespace db