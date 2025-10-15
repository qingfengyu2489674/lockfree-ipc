// LockFreeStack/LockFreeStack.hpp
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>
#include <string>
#include <mutex>

// 前置声明
template <class T>    class StackNode;
template <class Node> class HpSlotManager;
template <class Node> class HpRetiredManager;

template <class T>
class LockFreeStack {
public:
    using value_type = T;
    using node_type  = StackNode<T>;
    using size_type  = std::size_t;

public:
    explicit LockFreeStack(HpSlotManager<node_type>& slot_mgr,
                           HpRetiredManager<node_type>& retired_mgr) noexcept;
    ~LockFreeStack() noexcept;

    LockFreeStack(const LockFreeStack&)            = delete;
    LockFreeStack& operator=(const LockFreeStack&) = delete;
    LockFreeStack(LockFreeStack&&)                 = delete;
    LockFreeStack& operator=(LockFreeStack&&)      = delete;

    void push(const value_type& v) noexcept;
    void push(value_type&& v) noexcept;
    bool tryPop(value_type& out) noexcept;

    bool isEmpty() const noexcept;

    void retireNode(node_type* n) noexcept;
    void retireList(node_type* head) noexcept;

    size_type collectRetired(size_type quota) noexcept;
    size_type drainAll() noexcept;

private:
    size_type collectFromSnapshot_(size_type quota,
                                std::vector<const node_type*>& hazard_snapshot) noexcept;

    std::atomic<node_type*> head_{nullptr};

    HpSlotManager<node_type>&    slot_mgr_;
    HpRetiredManager<node_type>& retired_mgr_;

};


extern template class LockFreeStack<int>;