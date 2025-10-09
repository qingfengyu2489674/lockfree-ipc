#pragma once
#include <utility>

// 节点结构体（放在类外）
template <typename T>
struct LockFreeNode {
    T               value;
    LockFreeNode*   next;

    LockFreeNode() = delete;
    explicit LockFreeNode(const T& v) : value(v), next(nullptr) {}
    explicit LockFreeNode(T&& v)      : value(std::move(v)), next(nullptr) {}
};
