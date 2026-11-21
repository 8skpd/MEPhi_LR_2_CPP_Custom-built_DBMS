#pragma once
#include <stdexcept>
#include <cstddef>

template <typename T, std::size_t N>
class FixedArray {
private:
    T elements[N]; // Память выделяется внутри объекта, без new/delete

public:
    // Конструктор по умолчанию (элементы инициализируются значениями по умолчанию для типа T)
    FixedArray() : elements{} {} 

    // Безопасный доступ с проверкой границ
    T& at(std::size_t index) {
        if (index >= N) {
            throw std::out_of_range("FixedArray: индекс выходит за пределы массива (at)");
        }
        return elements[index];
    }

    const T& at(std::size_t index) const {
        if (index >= N) {
            throw std::out_of_range("FixedArray: индекс выходит за пределы массива (at const)");
        }
        return elements[index];
    }

    // Оператор []
    T& operator[](std::size_t index) {
        if (index >= N) {
            throw std::out_of_range("FixedArray: индекс выходит за пределы массива (operator[])");
        }
        return elements[index];
    }

    const T& operator[](std::size_t index) const {
        if (index >= N) {
            throw std::out_of_range("FixedArray: индекс выходит за пределы массива (operator[] const)");
        }
        return elements[index];
    }

    // Возвращает размер массива
    constexpr std::size_t size() const noexcept {
        return N;
    }

    // Заполняет весь массив указанным значением
    void fill(const T& value) {
        for (std::size_t i = 0; i < N; ++i) {
            elements[i] = value;
        }
    }

    // Возвращает сырой указатель на данные
    T* data() noexcept { 
        return elements;
    }
    const T* data() const noexcept { 
        return elements; 
    }
};