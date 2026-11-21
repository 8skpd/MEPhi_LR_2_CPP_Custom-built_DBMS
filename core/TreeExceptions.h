#pragma once
#include <stdexcept>
#include <string>

namespace tree23 {

class InvalidArgument : public std::invalid_argument {
public:
    InvalidArgument(const std::string& where, const std::string& detail)
        : std::invalid_argument(where + ": " + detail) {}
};

class OutOfRange : public std::out_of_range {
public:
    OutOfRange(const std::string& where, const std::string& detail)
        : std::out_of_range(where + ": " + detail) {}
};

class InvariantError : public std::logic_error {
public:
    InvariantError(const std::string& where, const std::string& detail)
        : std::logic_error(where + ": " + detail) {}
};

// Конкретные ошибки дерева

class EmptyTree : public InvalidArgument {
public:
    explicit EmptyTree(const std::string& where)
        : InvalidArgument(where, "дерево пусто") {}
};

class KeyNotFound : public InvalidArgument {
public:
    explicit KeyNotFound(const std::string& where)
        : InvalidArgument(where, "элемент не найден в дереве") {}
};

class DuplicateKey : public InvalidArgument {
public:
    explicit DuplicateKey(const std::string& where)
        : InvalidArgument(where, "дубликат значения не разрешён") {}
};

class NullNode : public InvalidArgument {
public:
    explicit NullNode(const std::string& where)
        : InvalidArgument(where, "передан null узел") {}
};

// Ошибки формата пути ("0-abc", пустой индекс и т.п.)
class InvalidPath : public InvalidArgument {
public:
    explicit InvalidPath(const std::string& detail)
        : InvalidArgument("path", detail) {}
};

// Путь уходит за границы дерева
class PathOutOfRange : public OutOfRange {
public:
    explicit PathOutOfRange(const std::string& detail)
        : OutOfRange("path", detail) {}
};

// Ошибки сериализации / десериализации

namespace format {
    inline constexpr const char* fixed = "fixed_traversal";
    inline constexpr const char* pairs = "parent_child_pairs";
}

class SerializationError : public InvariantError {
public:
    explicit SerializationError(const std::string& detail)
        : InvariantError("serialization", detail) {}
};

class DeserializationError : public InvalidArgument {
public:
    DeserializationError(const std::string& fmt, const std::string& detail)
        : InvalidArgument(std::string("from_string_") + fmt, detail) {}
};

}