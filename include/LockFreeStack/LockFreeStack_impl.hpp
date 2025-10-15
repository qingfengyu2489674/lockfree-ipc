// LockFreeStack_impl.hpp
#pragma once

#include <vector>

// ============================================================================
// --- LockFreeStack 模板成员实现 ---
// ============================================================================

template <class T, class AllocPolicy>
LockFreeStack<T, AllocPolicy>::LockFreeStack(
    slot_manager_type& slot_mgr,
    retired_manager_type& retired_mgr) noexcept
    : slot_mgr_(slot_mgr), retired_mgr_(retired_mgr), head_{nullptr} {}

template <class T, class AllocPolicy>
LockFreeStack<T, AllocPolicy>::~LockFreeStack() noexcept {
    // 析构时，将栈中剩余的所有节点都放入回收站
    // 注意：这只是逻辑上的回收，真正的内存释放在管理器销毁时
    node_type* current = head_.exchange(nullptr);
    if (current) {
        // 使用 drainAll 将剩余节点一次性回收
        retired_mgr_.drainAll();
    }
}

template <class T, class AllocPolicy>
void LockFreeStack<T, AllocPolicy>::push(const value_type& v) {
    // 使用注入的策略来分配新节点
    auto* new_node = AllocPolicy::template allocate<node_type>(v);
    new_node->next = head_.load(std::memory_order_relaxed);
    while (!head_.compare_exchange_weak(
        new_node->next, new_node,
        std::memory_order_release, std::memory_order_relaxed));
}

template <class T, class AllocPolicy>
void LockFreeStack<T, AllocPolicy>::push(value_type&& v) {
    auto* new_node = AllocPolicy::template allocate<node_type>(std::move(v));
    new_node->next = head_.load(std::memory_order_relaxed);
    while (!head_.compare_exchange_weak(
        new_node->next, new_node,
        std::memory_order_release, std::memory_order_relaxed));
}

template <class T, class AllocPolicy>
bool LockFreeStack<T, AllocPolicy>::tryPop(value_type& out) noexcept {
    // 获取本线程的槽位，它已经是正确的类型了
    auto* slot = slot_mgr_.acquireTls();
    
    node_type* old_head = head_.load(std::memory_order_relaxed);
    do {
        if (!old_head) {
            return false; // 栈为空
        }
        // 1. 保护可能弹出的节点
        slot->protect(0, old_head);
        // 2. 重新检查 head，防止在保护期间 head 已被其他线程修改
        //    这是经典的 HP load-protect-reload 序列
    } while (old_head != head_.load(std::memory_order_acquire));

    // 尝试弹出
    if (head_.compare_exchange_strong(old_head, old_head->next, std::memory_order_acq_rel)) {
        // 弹出成功
        out = std::move(old_head->value);
        // 将旧节点放入回收站
        retireNode(old_head);
        // 清理危险指针
        slot->clear(0);
        return true;
    }

    // 弹出失败，清理危险指针
    slot->clear(0);
    return false;
}

template <class T, class AllocPolicy>
bool LockFreeStack<T, AllocPolicy>::isEmpty() const noexcept {
    return head_.load(std::memory_order_acquire) == nullptr;
}

template <class T, class AllocPolicy>
void LockFreeStack<T, AllocPolicy>::retireNode(node_type* n) noexcept {
    // 之前这个函数在 HpSlotManager 里，现在应该在 LockFreeStack 内部调用管理器
    if (n) {
        slot_mgr_.retireNode(n);
    }
}

template <class T, class AllocPolicy>
std::size_t LockFreeStack<T, AllocPolicy>::collectRetired(size_type quota) noexcept {
    // 1. 从 SlotManager 获取所有线程的退休节点，并移动到 RetiredManager
    //    这里 flushAllRetiredTo 的目标是 RetiredManager 的内部链表，需要 RetiredManager 暴露接口
    //    为了简化，我们假设 retireNode 已经将节点放到了 HpSlot 中，
    //    现在需要将这些节点从 HpSlot 转移到 HpRetiredManager
    
    std::atomic<node_type*> collected_list_head{nullptr};
    slot_mgr_.flushAllRetiredTo(collected_list_head);
    retired_mgr_.appendRetiredList(collected_list_head.load());

    // 2. 从 SlotManager 获取危险指针快照
    std::vector<const node_type*> snapshot;
    slot_mgr_.snapshotHazardpoints(snapshot);
    
    // 3. 将快照传递给 RetiredManager 进行安全回收
    return retired_mgr_.collectRetired(quota, snapshot);
}

template <class T, class AllocPolicy>
std::size_t LockFreeStack<T, AllocPolicy>::drainAll() noexcept {
    // 同上，先从 Slot 冲刷到 RetiredManager
    std::atomic<node_type*> collected_list_head{nullptr};
    slot_mgr_.flushAllRetiredTo(collected_list_head);
    retired_mgr_.appendRetiredList(collected_list_head.load());
    
    // 然后调用 RetiredManager 的 drainAll
    return retired_mgr_.drainAll();
}