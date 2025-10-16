#pragma once

#include <limits>

template <class T, class AllocPolicy>
LockFreeLinkedList<T, AllocPolicy>::LockFreeLinkedList(hp_organizer_type& hp_organizer) noexcept
    : hp_organizer_(hp_organizer) {}

template <class T, class AllocPolicy>
LockFreeLinkedList<T, AllocPolicy>::~LockFreeLinkedList() noexcept {
    // 析构时假定没有并发访问，因此可以直接遍历并释放节点
    node_ptr curr = head_sentinel_.next.load(std::memory_order_relaxed);
    while (curr) {
        node_ptr next = node_type::get_unmarked(curr->next.load(std::memory_order_relaxed));
        AllocPolicy::template deallocate<node_type>(curr);
        curr = next;
    }
}

template <class T, class AllocPolicy>
void LockFreeLinkedList<T, AllocPolicy>::find(
    const value_type& value,
    node_ptr& prev,
    node_ptr& curr,
    decltype(std::declval<hp_organizer_type>().acquireTlsSlot()) slot) {
retry:
    // 1. 初始化：总是从绝对安全的哨兵节点开始
    prev = &head_sentinel_;
    curr = prev->next.load(std::memory_order_acquire);

    while (true) {
        // 2. 保护和验证：在解引用 curr 之前，必须保护并验证
        if (slot) slot->protect(0, curr);

        // 验证 prev->next 是否在我们读取后被修改
        if (prev->next.load(std::memory_order_acquire) != curr) {
            goto retry; // 状态不一致，必须从头开始
        }
        
        if (curr == nullptr) {
            // 到达链表末尾，prev 是最后一个节点。这是有效状态。
            // 保护 prev 后返回 (find 调用者可能需要操作它)
            if (slot) slot->protect(1, prev);
            return;
        }

        // 3. 安全访问：现在可以安全地访问 curr
        node_ptr next = curr->next.load(std::memory_order_acquire);

        // 4. 助人机制：检查 curr 是否被标记 (逻辑删除)
        if (node_type::is_marked(next)) {
            node_ptr unmarked_next = node_type::get_unmarked(next);
            // 尝试帮助物理删除 curr。prev 是安全的，因为它在上一次循环中是 curr，
            // 或者在初始时是哨兵节点。我们必须保护它才能进行 CAS。
            if (slot) slot->protect(1, prev);

            if (prev->next.compare_exchange_strong(curr, unmarked_next, std::memory_order_release, std::memory_order_relaxed)) {
                // 物理删除成功，将 curr 退休
                if (slot) slot->clear(0); // 清除对 curr 的保护
                hp_organizer_.retire(curr);
            }
            // 无论 CAS 是否成功，链表结构都已改变，必须从头重试以获得最新视图
            goto retry;
        }

        // 5. 查找逻辑：如果节点有效，比较值
        if (curr->value >= value) {
            // 找到了插入/删除点。保护 prev 和 curr 后返回。
            if (slot) slot->protect(1, prev);
            return;
        }

        // 6. 前进：移动到下一个节点
        prev = curr;
        curr = next;
    }
}

template <class T, class AllocPolicy>
bool LockFreeLinkedList<T, AllocPolicy>::insert(const value_type& value) {
    node_ptr new_node = nullptr;
    auto* slot = hp_organizer_.acquireTlsSlot();

    for (;;) {
        node_ptr prev = nullptr, curr = nullptr;
        find(value, prev, curr, slot);
        // find 返回时，prev 受 HP[1] 保护, curr 受 HP[0] 保护

        // 检查值是否已存在
        if (curr && curr->value == value) {
            if (new_node) {
                AllocPolicy::template deallocate<node_type>(new_node);
            }
            if (slot) slot->clearAll();
            return false;
        }
        
        // 延迟分配新节点
        if (!new_node) {
             new_node = AllocPolicy::template allocate<node_type>(value);
        }
        new_node->next.store(curr, std::memory_order_relaxed);
        
        // prev 受保护，可以安全地对其执行 CAS
        if (prev->next.compare_exchange_strong(curr, new_node, std::memory_order_release, std::memory_order_relaxed)) {
            if (slot) slot->clearAll();
            return true; // 插入成功
        }
        // CAS 失败意味着链表在 find 和 CAS 之间被修改，循环重试
    }
}

template <class T, class AllocPolicy>
bool LockFreeLinkedList<T, AllocPolicy>::remove(const value_type& value) {
    auto* slot = hp_organizer_.acquireTlsSlot();
    for (;;) {
        node_ptr prev = nullptr, curr = nullptr;
        find(value, prev, curr, slot);
        // find 返回时，prev 受 HP[1] 保护, curr 受 HP[0] 保护

        // 如果没找到或值不匹配
        if (!curr || curr->value != value) {
            if (slot) slot->clearAll();
            return false;
        }

        // --- 阶段 1: 逻辑删除 (标记) ---
        // curr 受保护，可以安全地访问其 next 指针
        node_ptr next = curr->next.load(std::memory_order_acquire);
        
        // find 的助人机制应该已经清理了大部分标记节点，
        // 但为防止ABA问题和竞态，仍需检查
        if (node_type::is_marked(next)) {
            continue; // 节点已被其他人标记，重试
        }
        
        node_ptr marked_next = node_type::get_marked(next);
        if (curr->next.compare_exchange_strong(next, marked_next, std::memory_order_release, std::memory_order_relaxed)) {
            // 标记成功！这是删除操作的线性化点。
            // 清除对 curr 的保护，并将其退休
            if (slot) slot->clear(0);
            hp_organizer_.retire(curr);

            // --- 阶段 2: 物理删除 (助人) ---
            // prev 受保护，可以安全地对其执行 CAS
            prev->next.compare_exchange_strong(curr, next, std::memory_order_release, std::memory_order_relaxed);
            
            if (slot) slot->clearAll();
            return true;
        }
        // 标记失败，说明在 find 和 CAS 之间链表被修改，循环重试
    }
}

template <class T, class AllocPolicy>
bool LockFreeLinkedList<T, AllocPolicy>::contains(const value_type& value) noexcept {
    auto* slot = hp_organizer_.acquireTlsSlot();
    node_ptr prev = nullptr, curr = nullptr;
    
    find(value, prev, curr, slot);
    
    // find 返回时，curr 受 HP[0] 保护，可以安全访问
    bool found = (curr && curr->value == value);
    
    if (slot) slot->clearAll();
    
    return found;
}

template <class T, class AllocPolicy>
bool LockFreeLinkedList<T, AllocPolicy>::isEmpty() const noexcept {
    node_ptr head_next = head_sentinel_.next.load(std::memory_order_acquire);
    return head_next == nullptr;
}