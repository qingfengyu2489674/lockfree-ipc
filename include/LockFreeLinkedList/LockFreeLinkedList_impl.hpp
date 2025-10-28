#pragma once

#include <limits>

template <class T, class AllocPolicy>
LockFreeLinkedList<T, AllocPolicy>::LockFreeLinkedList(hp_organizer_type& hp_organizer) noexcept
    : hp_organizer_(hp_organizer) {
        head_sentinel_.next.store(nullptr, std::memory_order_relaxed);
}

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
    if (slot) slot->protect(0, prev);
    curr = prev->next.load(std::memory_order_acquire);

    while (true) {
        // 2. 保护和验证：在解引用 curr 之前，必须保护并验证
        if (slot) slot->protect(1, curr);

        // 验证 prev->next 是否在我们读取后被修改
        if (prev->next.load(std::memory_order_acquire) != curr) {
            goto retry; // 状态不一致，必须从头开始
        }
        
        if (curr == nullptr) {
            // 到达链表末尾，prev 是最后一个节点。这是有效状态。
            // 保护 prev 后返回 (find 调用者可能需要操作它)
            return;
        }

        // 3. 安全访问：现在可以安全地访问 curr
        node_ptr next = curr->next.load(std::memory_order_acquire);

        // 4. 助人机制：检查 curr 是否被标记 (逻辑删除)
        if (node_type::is_marked(next)) {
            node_ptr unmarked_next = node_type::get_unmarked(next);
            // 尝试帮助物理删除 curr。prev 是安全的，因为它在上一次循环中是 curr，
            // 或者在初始时是哨兵节点。我们必须保护它才能进行 CAS。

            if (prev->next.compare_exchange_strong(curr, unmarked_next, std::memory_order_release, std::memory_order_relaxed)) {
                // 物理删除成功，将 curr 退休
                if (slot) slot->clear(1); // 清除对 curr 的保护
                hp_organizer_.retire(curr);
            }
            // 无论 CAS 是否成功，链表结构都已改变，必须从头重试以获得最新视图
            goto retry;
        }

        // 5. 查找逻辑：如果节点有效，比较值
        if (curr->value >= value) {
            // 找到了插入/删除点。保护 prev 和 curr 后返回。
            return;
        }

        // 6. 前进：移动到下一个节点
        prev = curr;
        slot->protect(0, prev); 
        curr = node_type::get_unmarked(next);
    }
}

template <class T, class AllocPolicy>
bool LockFreeLinkedList<T, AllocPolicy>::insert(const value_type& value) {
    node_ptr new_node = nullptr;
    auto* slot = hp_organizer_.acquireTlsSlot();

    for (;;) {
        node_ptr prev = nullptr, curr = nullptr;
        find(value, prev, curr, slot);
        // find 返回时，prev 受 HP[0] 保护, curr 受 HP[1] 保护

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
        node_ptr prev = nullptr;
        node_ptr curr = nullptr;

        find(value, prev, curr, slot);
        // 约定：find 返回时:
        //   HP[0] 保护 prev
        //   HP[1] 保护 curr

        if (!curr || curr->value != value) {
            if (slot) slot->clearAll();
            return false;
        }

        // curr 被 HP[1] 保护，此时安全读 next
        node_ptr next = curr->next.load(std::memory_order_acquire);
        node_ptr unmarked_next = node_type::get_unmarked(next);

        // 1. 逻辑删除（给 curr 打标记）
        if (!node_type::is_marked(next)) {
            node_ptr marked_next = node_type::get_marked(unmarked_next);
            if (!curr->next.compare_exchange_strong(
                    next,
                    marked_next,
                    std::memory_order_release,
                    std::memory_order_relaxed))
            {
                // 有其他线程同时在动它，重来
                continue;
            }
            // 注意：逻辑删除成功是线性化点
        }

        // 2. 物理删除（把 curr 从 prev->next 链路里跳过）
        if (prev->next.compare_exchange_strong(
                curr,
                unmarked_next,
                std::memory_order_release,
                std::memory_order_relaxed))
        {
            // 我们把 curr 从主链摘掉了
            // 现在 safe retire: 删前先停止保护
            if (slot) slot->clear(1);   // HP[1] 对应 curr
            hp_organizer_.retire(curr);
        }
        // 如果 CAS 失败，说明别的线程已经做过 unlink，
        // 那也没关系：curr 可能已经被别的线程 retire 或马上 retire。

        if (slot) slot->clearAll();
        return true;
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