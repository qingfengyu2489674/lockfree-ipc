// LockFreeStack/LockFreeStack.hpp
#pragma once
#include <atomic>
#include <cstddef>

#include "LockFreeStack/StackNode.hpp"
// *** 关键修改：现在只包含组织器，不再需要单独的管理类头文件 ***
#include "Hazard/HazardPointerOrganizer.hpp"

// 前向声明 AllocPolicy 的默认类型
class DefaultHeapPolicy;

template <class T, class AllocPolicy = DefaultHeapPolicy>
class LockFreeStack {
public:
    using value_type = T;
    using node_type  = StackNode<T>;
    using size_type  = std::size_t;

    static constexpr std::size_t kHazardPointers = 1;
    using hp_organizer_type = HazardPointerOrganizer<node_type, kHazardPointers, AllocPolicy>;

public:
    explicit LockFreeStack(hp_organizer_type& hp_organizer) noexcept;
    ~LockFreeStack() noexcept;

    void push(const value_type& v);
    void push(value_type&& v);
    bool tryPop(value_type& out) noexcept;
    bool isEmpty() const noexcept;


private:
    std::atomic<node_type*> head_{nullptr};
    hp_organizer_type& hp_organizer_;
};

// 在头文件末尾包含实现，实现 Header-Only
#include "LockFreeStack_impl.hpp"