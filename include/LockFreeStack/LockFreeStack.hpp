// LockFreeStack/LockFreeStack.hpp
#pragma once
#include <atomic>
#include <cstddef>
#include <vector>

// 包含完整的依赖定义，而不是前向声明
#include "LockFreeStack/StackNode.hpp"
#include "LockFreeStack/HpSlotManager.hpp"
#include "LockFreeStack/HpRetiredManager.hpp"

// LockFreeStack 现在也接受一个 AllocPolicy 模板参数
template <
    class T, 
    class AllocPolicy = DefaultHeapPolicy // 使用与管理器一致的默认策略
>
class LockFreeStack {
public:
    using value_type = T;
    using node_type  = StackNode<T>;
    using size_type  = std::size_t;

    // --- 关键修改：定义所使用的管理器具体类型 ---
    // 对于栈，危险指针数量固定为 1
    static constexpr std::size_t kHazardPointers = 1;
    using slot_manager_type    = HpSlotManager<node_type, kHazardPointers, AllocPolicy>;
    using retired_manager_type = HpRetiredManager<node_type, AllocPolicy>;

public:
    // 构造函数现在接收具体类型的管理器引用
    explicit LockFreeStack(slot_manager_type& slot_mgr,
                           retired_manager_type& retired_mgr) noexcept;
    
    ~LockFreeStack() noexcept;

    // ... (其他接口声明保持不变) ...
    void push(const value_type& v); // noexcept is removed as allocation can fail
    void push(value_type&& v);
    bool tryPop(value_type& out) noexcept;
    bool isEmpty() const noexcept;
    size_type collectRetired(size_type quota) noexcept;
    size_type drainAll() noexcept;

private:
    void retireNode(node_type* n) noexcept;

    std::atomic<node_type*> head_{nullptr};

    // 成员变量类型更新为具体类型
    slot_manager_type&    slot_mgr_;
    retired_manager_type& retired_mgr_;
};

// 在头文件末尾包含实现，实现 Header-Only
#include "LockFreeStack_impl.hpp"