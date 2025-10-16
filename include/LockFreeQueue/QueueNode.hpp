#pragma once 

#include <atomic>
#include <type_traits>
#include <utility>

template <typename T>
class QueueNode {
public:
    using value_type = T;

    std::atomic<QueueNode<T>*> next;
    value_type value;

public:
    QueueNode() noexcept 
        : next(nullptr), value{} {}

    explicit QueueNode(const value_type& val)
        : next(nullptr), value(val) {}

    explicit QueueNode(value_type&& val) noexcept(std::is_nothrow_move_constructible<T>::value)
        : next(nullptr), value(std::move(val)) {}

private:
    static void* operator new(size_t) = delete;
    static void* operator new[](size_t) = delete;
    static void operator delete(void*) = delete;
    static void operator delete[](void*) = delete;
};