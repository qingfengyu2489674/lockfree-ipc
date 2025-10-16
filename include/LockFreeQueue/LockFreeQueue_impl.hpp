#pragma once

#include "LockFreeQueue.hpp"

// 构造函数 (无变化)
template <class T, class AllocPolicy>
LockFreeQueue<T, AllocPolicy>::LockFreeQueue(hp_organizer_type& hp_organizer) noexcept
    : hp_organizer_(hp_organizer) 
{
    auto* dummy_node = AllocPolicy::template allocate<node_type>(); // 假设QueueNode有默认构造
    head_.store(dummy_node, std::memory_order_relaxed);
    tail_.store(dummy_node, std::memory_order_relaxed);
}

// 析构函数 (无变化)
template <class T, class AllocPolicy>
LockFreeQueue<T, AllocPolicy>::~LockFreeQueue() noexcept {
    value_type ignored;
    while (tryPop(ignored));
    node_type* dummy = head_.load(std::memory_order_relaxed);
    AllocPolicy::deallocate(dummy);
}

// push 相关实现 (无变化)
template <class T, class AllocPolicy>
void LockFreeQueue<T, AllocPolicy>::push(const value_type& v) {
    pushImpl(v);
}

template <class T, class AllocPolicy>
void LockFreeQueue<T, AllocPolicy>::push(value_type&& v) {
    pushImpl(std::move(v));
}

template <class T, class AllocPolicy>
template<typename U>
void LockFreeQueue<T, AllocPolicy>::pushImpl(U&& v) {
    auto* new_node = AllocPolicy::template allocate<node_type>(std::forward<U>(v));
    for (;;) {
        node_type* old_tail = tail_.load(std::memory_order_acquire);
        node_type* next = old_tail->next.load(std::memory_order_relaxed);
        if (old_tail != tail_.load(std::memory_order_acquire)) {
            continue;
        }
        if (next != nullptr) {
            tail_.compare_exchange_weak(old_tail, next, std::memory_order_release, std::memory_order_relaxed);
            continue;
        }
        node_type* null_next = nullptr;
        if (old_tail->next.compare_exchange_weak(null_next, new_node, std::memory_order_release, std::memory_order_relaxed)) {
            tail_.compare_exchange_strong(old_tail, new_node, std::memory_order_release, std::memory_order_relaxed);
            return;
        }
    }
}


// *** tryPop 的全新、更安全的实现 ***
template <class T, class AllocPolicy>
bool LockFreeQueue<T, AllocPolicy>::tryPop(value_type& out) noexcept {
    for (;;) {
        auto* slot = hp_organizer_.acquireTlsSlot();
        
        // 1. 读取并保护 head
        node_type* old_head = head_.load(std::memory_order_acquire);
        if (slot) {
            slot->protect(0, old_head);
        }

        // 2. 验证 head 在我们保护它之后没有改变
        if (old_head != head_.load(std::memory_order_acquire)) {
            if (slot) slot->clearAll();
            continue;
        }

        // 3. 读取并保护 first_node (即 head->next)
        // 这是安全的，因为 old_head 已经被HP保护，不会被回收。
        node_type* first_node = old_head->next.load(std::memory_order_acquire);
        if (slot) {
            slot->protect(1, first_node);
        }
        
        // 4. 读取 tail 用于后续判断
        node_type* old_tail = tail_.load(std::memory_order_acquire);
        
        // 5. 最终验证：在所有指针都被读取和保护后，检查状态是否一致
        if (old_head != head_.load(std::memory_order_acquire)) {
            if (slot) slot->clearAll();
            continue;
        }
        // 再次检查 old_head->next 是否在我们保护它之后被改变了
        if (old_head->next.load(std::memory_order_relaxed) != first_node) {
            if (slot) slot->clearAll();
            continue;
        }

        // 6. 处理队列为空或 tail 指针落后的情况
        if (old_head == old_tail) {
            if (first_node == nullptr) {
                if (slot) slot->clearAll();
                return false; // 队列确定为空
            }
            // tail 指针落后，帮助移动它
            tail_.compare_exchange_weak(
                old_tail, first_node, 
                std::memory_order_release, std::memory_order_relaxed);
            
            if (slot) slot->clearAll();
            continue; // 帮助完后重试
        }

        // first_node 不应为空，因为 head != tail
        if (first_node == nullptr) {
             if (slot) slot->clearAll();
             continue; // 状态不一致，重试
        }

        // 7. 尝试原子地移动 head 指针
        if (head_.compare_exchange_strong(
                old_head, first_node, 
                std::memory_order_acq_rel, 
                std::memory_order_relaxed)) 
        {
            // 成功！
            // 读取 value 是安全的，因为 first_node 仍被HP[1]保护
            out = std::move(first_node->value);
            
            // 将旧的哨兵节点（old_head）退休
            // 这是安全的，因为 old_head 仍被HP[0]保护
            hp_organizer_.retire(old_head);
            
            // 清除所有保护并返回
            if (slot) slot->clearAll();
            return true;
        }
        
        // CAS 失败，说明有其他线程抢先了。清除保护并重试。
        if (slot) slot->clearAll();
    }
}

// isEmpty (无变化)
template <class T, class AllocPolicy>
bool LockFreeQueue<T, AllocPolicy>::isEmpty() const noexcept {
    return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
}