// LockFreeStack_impl.hpp
#pragma once

// *** 关键修改：构造函数实现更新 ***
template <class T, class AllocPolicy>
LockFreeStack<T, AllocPolicy>::LockFreeStack(hp_organizer_type& hp_organizer) noexcept
    : hp_organizer_(hp_organizer), head_{nullptr} {}

template <class T, class AllocPolicy>
LockFreeStack<T, AllocPolicy>::~LockFreeStack() noexcept {
    // 析构函数可以简化。栈本身不负责强制清理所有节点的内存，
    // 这个职责属于 HazardPointerOrganizer 的所有者。
    // 我们只需确保栈不再持有任何节点的引用。
    head_.store(nullptr, std::memory_order_relaxed);
}

// push 方法不需要改变，因为它依赖的是 AllocPolicy，而不是HP机制
template <class T, class AllocPolicy>
void LockFreeStack<T, AllocPolicy>::push(const value_type& v) {
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
    for (;;) {
        node_type* old_head = head_.load(std::memory_order_acquire);

        if (!old_head) {
            return false;
        }

        auto* slot = hp_organizer_.acquireTlsSlot();
        
        if (slot) {
            slot->protect(0, old_head);
        }

        if (old_head != head_.load(std::memory_order_acquire)) {
            if (slot) {
                slot->clear(0);
            }
            continue;
        }

        node_type* next = old_head->next;

        if (head_.compare_exchange_strong(
                old_head, next,
                std::memory_order_acq_rel,
                std::memory_order_relaxed)) {
            
            out = std::move(old_head->value);

            if (slot) {
                slot->clear(0);
            }
            
            hp_organizer_.retire(old_head);
            
            return true;
        }

        if (slot) {
            slot->clear(0);
        }
    }
}

template <class T, class AllocPolicy>
bool LockFreeStack<T, AllocPolicy>::isEmpty() const noexcept {
    return head_.load(std::memory_order_acquire) == nullptr;
}

// *** 关键修改：删除了 retireNode, collectRetired, 和 drainAll 的实现 ***
// *** 这些方法现在由 HazardPointerOrganizer 负责 ***