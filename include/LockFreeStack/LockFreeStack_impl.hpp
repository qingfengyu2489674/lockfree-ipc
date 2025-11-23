// LockFreeStack_impl.hpp
#pragma once

// *** 关键修改：构造函数实现更新 ***
template <class T, class AllocPolicy>
LockFreeStack<T, AllocPolicy>::LockFreeStack(hp_organizer_type& hp_organizer) noexcept
    : hp_organizer_(hp_organizer), head_{nullptr} {}


template <class T, class AllocPolicy>
LockFreeStack<T, AllocPolicy>::~LockFreeStack() noexcept {
    // 必须清空栈中剩余元素，否则会造成内存泄漏
    value_type discard_val;
    while (tryPop(discard_val)) {
        // 循环弹出直到为空，tryPop 内部会调用 retire
    }
    // head_ 此时已为 nullptr
}


// push 方法不需要改变，因为它依赖的是 AllocPolicy，而不是HP机制
template <class T, class AllocPolicy>
void LockFreeStack<T, AllocPolicy>::push(const value_type& v) {
    auto* new_node = AllocPolicy::template allocate<node_type>(v);
    new_node->next = head_.load(MemoryOrder::Relaxed);
    while (!head_.compare_exchange_weak(
        new_node->next, new_node,
        MemoryOrder::Release, MemoryOrder::Relaxed));
}

template <class T, class AllocPolicy>
void LockFreeStack<T, AllocPolicy>::push(value_type&& v) {
    auto* new_node = AllocPolicy::template allocate<node_type>(std::move(v));
    new_node->next = head_.load(MemoryOrder::Relaxed);
    while (!head_.compare_exchange_weak(
        new_node->next, new_node,
        MemoryOrder::Release, MemoryOrder::Relaxed));
}

template <class T, class AllocPolicy>
bool LockFreeStack<T, AllocPolicy>::tryPop(value_type& out) noexcept {

    auto* slot = hp_organizer_.acquireTlsSlot();
    if (!slot) return false;

    for (;;) {
        node_type* old_head = head_.load(MemoryOrder::Acquire);

        if (!old_head) {
            slot->clear(0);
            return false;
        }

        slot->protect(0, old_head);
        
        atomic_thread_fence(MemoryOrder::SeqCst);

        if (old_head != head_.load(MemoryOrder::Acquire)) {
            continue;
        }

        node_type* next = old_head->next;

        if (head_.compare_exchange_strong(
                old_head, next,
                MemoryOrder::AcqRel,
                MemoryOrder::Relaxed)) {
            
            out = std::move(old_head->value);
            
            hp_organizer_.retire(old_head);

            slot->clear(0);
            
            return true;
        }

    }
}

template <class T, class AllocPolicy>
bool LockFreeStack<T, AllocPolicy>::isEmpty() const noexcept {
    return head_.load(MemoryOrder::Acquire) == nullptr;
}

// *** 关键修改：删除了 retireNode, collectRetired, 和 drainAll 的实现 ***
// *** 这些方法现在由 HazardPointerOrganizer 负责 ***