// StackNode.hpp
#pragma once
#include <atomic>


// Treiber 链式栈的最小节点
template <class T>
class StackNode {
public:
    StackNode* next{nullptr};          // 这里实现为普通指针即可，没必要为原子
    T value;                           // 存储的值

    // 构造：支持默认、拷贝、移动
    StackNode() = default;
    explicit StackNode(const T& v) : next(nullptr), value(v) {}
    explicit StackNode(T&& v) : next(nullptr), value(std::move(v)) {}

    // 默认析构/拷贝/移动即可（目前不考虑释放策略）
    ~StackNode() = default;
    StackNode(const StackNode&) = default;
    StackNode& operator=(const StackNode&) = default;
    StackNode(StackNode&&) noexcept = default;
    StackNode& operator=(StackNode&&) noexcept = default;
};

