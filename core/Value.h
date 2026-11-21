#pragma once
#include <string>
#include <variant>
#include <typeinfo>
#include "DbExceptions.h"

// Универсальный тип данных для СУБД
class Value {
public:
    // Варианты значений: целое число, дробное число, строка
    using ValueType = std::variant<int, double, std::string>;
    ValueType data;

    Value() : data(0) {}
    Value(int v) : data(v) {}
    Value(double v) : data(v) {}
    Value(const std::string& v) : data(v) {}
    Value(const char* v) : data(std::string(v)) {}

    // Хелперы для проверки типа
    bool is_int() const { return std::holds_alternative<int>(data); }
    bool is_double() const { return std::holds_alternative<double>(data); }
    bool is_string() const { return std::holds_alternative<std::string>(data); }

    // Операторы сравнения
    bool operator==(const Value& other) const {
        // Если типы разные, они заведомо не равны
        if (data.index() != other.data.index()) {
            return false;
        }
        return data == other.data;
    }

    bool operator<(const Value& other) const {
        if (data.index() != other.data.index()) {
            throw db::TypeMismatch(__func__,
                "Сравнение значений разных типов не поддерживается");
        }
        return data < other.data;
    }

    bool operator>(const Value& other) const { return other < *this; }
    bool operator<=(const Value& other) const { return !(other < *this); }
    bool operator>=(const Value& other) const { return !(*this < other); }

    // Вывод в строку
    std::string to_string() const {
        return std::visit([](auto&& arg) -> std::string {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::string>) return arg;
            else if constexpr (std::is_same_v<T, int>) return std::to_string(arg);
            else if constexpr (std::is_same_v<T, double>) return std::to_string(arg);
            else return "unknown";
        }, data);
    }
};

// Хеш для Value: комбинирует хеш активного типа и значения.
// Требование: равные Value - равные хеши.
namespace std {
template <>
struct hash<Value> {
    size_t operator()(const Value& v) const noexcept {
        size_t h_type = std::hash<size_t>{}(v.data.index());
        size_t h_val = std::visit([](auto&& arg) -> size_t {
            using T = std::decay_t<decltype(arg)>;
            return std::hash<T>{}(arg);
        }, v.data);
        // boost::hash_combine
        return h_type ^ (h_val + 0x9e3779b97f4a7c15ULL + (h_type << 6) + (h_type >> 2));
    }
};
} // namespace std