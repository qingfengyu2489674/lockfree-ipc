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
    // 使用无限循环来处理并发竞争下的重试
    for (;;) {
        // 1. 第一次加载 head 指针。使用 acquire 确保我们能看到其他线程的写入。
        node_type* old_head = head_.load(std::memory_order_acquire);

        // 2. 空栈快速路径检查：如果栈为空，立即返回 false。
        //    这避免了在栈为空时执行无谓的、开销更高的 TLS 槽获取操作。
        if (!old_head) {
            return false;
        }

        // 3. 只有在栈非空时，才获取本线程的危险指针槽。
        //    一个健壮的 HP 管理器在槽位耗尽时可能返回 nullptr。
        auto* slot = slot_mgr_.acquireTls();
        
        // 4. 保护我们认为的栈顶节点。如果槽获取失败，则跳过保护。
        //    (注意：如果 slot 为 nullptr，后续操作可能不安全，取决于 HP 管理器的设计。
        //     一个更安全的设计是在 slot 为空时 continue 重试或抛出异常)。
        //    这里我们假设 acquireTls 总是成功的，或者后续逻辑能处理 slot 为空。
        if (slot) {
            slot->protect(0, old_head); // 假设 protect 接受索引和指针
        }

        // 5. 重新加载 head 并进行二次检查（经典的 Load-Protect-Reload 序列）。
        //    这确保在我们设置危险指针的微小时间窗口内，head 没有被其他线程修改。
        if (old_head != head_.load(std::memory_order_acquire)) {
            // 如果 head 已改变，说明我们保护的节点已不是栈顶。
            // 清理保护并从头开始重试。
            if (slot) {
                slot->clear(0); // 清理对应索引的保护
            }
            continue; // 返回 for 循环的开头
        }

        // 6. 到此为止，我们有一个被 HP 保护的、确认仍然是栈顶的节点 old_head。
        //    现在可以安全地读取它的 next 指针。因为节点被保护，不会被回收，
        //    所以这里的非原子读是安全的。
        node_type* next = old_head->next;

        // 7. 尝试原子地将 head 指针从 old_head 更新为 next。
        if (head_.compare_exchange_strong(
                old_head, next,
                std::memory_order_acq_rel,     // 成功时：这是一个读-改-写操作，需要 acq_rel
                std::memory_order_relaxed)) {  // 失败时：我们不依赖这次失败的读取来同步，所以 relaxed 足够
            
            // CAS 成功！我们拥有了 old_head 节点的所有权。
            out = std::move(old_head->value);

            // 清理危险指针，因为我们不再需要保护这个节点了。
            if (slot) {
                slot->clear(0);
            }
            
            // 将弹出的节点放入待回收列表。
            retireNode(old_head);
            
            return true; // 操作成功完成
        }

        // 8. CAS 失败。这意味着在步骤 5 和 7 之间，有另一个线程抢先完成了 pop 操作。
        //    我们的尝试失败了，但这是正常的并发情况。
        //    清理危险指针，然后循环会自然地进行下一次重试。
        if (slot) {
            slot->clear(0);
        }
    }
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