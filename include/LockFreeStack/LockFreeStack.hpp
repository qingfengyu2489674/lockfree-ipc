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
    // *** 关键修改：定义一个组织器类型的别名 ***
    using hp_organizer_type = HazardPointerOrganizer<node_type, kHazardPointers, AllocPolicy>;

public:
    // *** 关键修改：构造函数现在只接收组织器的引用 ***
    explicit LockFreeStack(hp_organizer_type& hp_organizer) noexcept;
    
    ~LockFreeStack() noexcept;

    // 核心接口保持不变
    void push(const value_type& v);
    void push(value_type&& v);
    bool tryPop(value_type& out) noexcept;
    bool isEmpty() const noexcept;

    // *** 关键修改：移除 collectRetired 和 drainAll，这些职责已移交组织器 ***

private:
    // *** 关键修改：移除私有辅助函数 retireNode ***

    std::atomic<node_type*> head_{nullptr};

    // *** 关键修改：成员变量现在只有一个组织器的引用 ***
    hp_organizer_type& hp_organizer_;
};

// 在头文件末尾包含实现，实现 Header-Only
#include "LockFreeStack_impl.hpp"