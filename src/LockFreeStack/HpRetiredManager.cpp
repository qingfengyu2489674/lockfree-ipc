// LockFreeStack/HpRetiredManager.cpp
#include "LockFreeStack/HpRetiredManager.hpp"

#include <cassert>

// ========== 公有区 ==========

template <class Node>
HpRetiredManager<Node>::HpRetiredManager() noexcept = default;

template <class Node>
HpRetiredManager<Node>::~HpRetiredManager() noexcept = default;

template <class Node>
void HpRetiredManager<Node>::appendRetiredNodeToList(Node* n) noexcept {
    if (!n) return;
    std::lock_guard<ShmMutexLock> g(lock_);
    // 单节点头插：n -> global_head_
    n->next = global_head_;
    global_head_ = n;
    approx_count_.fetch_add(1, std::memory_order_relaxed);
}

template <class Node>
void HpRetiredManager<Node>::appendRetiredListToList(Node* head) noexcept {
    if (!head) return;
    std::lock_guard<ShmMutexLock> g(lock_);
    (void)appendListLocked_(head);
}

template <class Node>
std::size_t HpRetiredManager<Node>::collect(std::size_t      quota,
                                            const void*      hazard_ctx,
                                            HazardPredicate  hazard_pred,
                                            Reclaimer        reclaimer) noexcept {
    if (quota == 0 || !hazard_pred || !reclaimer) return 0;
    std::lock_guard<ShmMutexLock> g(lock_);
    return scanAndReclaimUpLocked_(quota, hazard_ctx, hazard_pred, reclaimer);
}

template <class Node>
std::size_t HpRetiredManager<Node>::drainAll(Reclaimer reclaimer) noexcept {
    if (!reclaimer) return 0;
    std::lock_guard<ShmMutexLock> g(lock_);
    return drainAllLocked_(reclaimer);
}

template <class Node>
std::size_t HpRetiredManager<Node>::getRetiredCount() const noexcept {
    return approx_count_.load(std::memory_order_relaxed);
}

// ========== 私有区（已持锁版本） ==========

template <class Node>
std::size_t HpRetiredManager<Node>::appendListLocked_(Node* head) noexcept {
    // 前置条件：调用方已持有 lock_ 且 head 非空
    // 把整段 [head ... tail] 头插到 global_head_ 前
    std::size_t merged = 0;

    // 找尾并统计
    Node* tail = head;
    merged = 1;
    while (tail->next) {
        tail = tail->next;
        ++merged;
    }

    tail->next = global_head_;
    global_head_ = head;

    approx_count_.fetch_add(merged, std::memory_order_relaxed);
    return merged;
}

template <class Node>
std::size_t HpRetiredManager<Node>::scanAndReclaimUpLocked_(std::size_t      quota,
                                                            const void*      hazard_ctx,
                                                            HazardPredicate  hazard_pred,
                                                            Reclaimer        reclaimer) noexcept {
    if (!global_head_) return 0;

    // 使用哑元节点简化“删除当前结点”的操作
    Node dummy{};
    dummy.next = global_head_;

    Node* prev = &dummy;
    Node* cur  = dummy.next;

    std::size_t freed = 0;

    while (cur && freed < quota) {
        Node* next = cur->next;

        // 被保护（true） → 不可回收；未被保护（false） → 可回收
        const bool protected_now = hazard_pred(hazard_ctx, cur);
        if (!protected_now) {
            // 从链上摘除并回收
            prev->next = next;
            reclaimer(cur);
            cur = next;
            ++freed;
        } else {
            prev = cur;
            cur  = next;
        }
    }

    // 提交链表头与计数
    global_head_ = dummy.next;
    if (freed != 0) {
        approx_count_.fetch_sub(freed, std::memory_order_relaxed);
    }

    return freed;
}

template <class Node>
std::size_t HpRetiredManager<Node>::drainAllLocked_(Reclaimer reclaimer) noexcept {
    if (!global_head_) return 0;

    // 取走整段
    Node* head = global_head_;
    global_head_ = nullptr;

    // 统计并释放
    std::size_t freed = 0;
    while (head) {
        Node* nxt = head->next;
        reclaimer(head);
        head = nxt;
        ++freed;
    }

    if (freed != 0) {
        approx_count_.fetch_sub(freed, std::memory_order_relaxed);
    }
    return freed;
}

// ======== 显式实例化（可按需添加/删除） ========
// 若此 cpp 被多个 Node 类型共用且以头文件形式包含，则可移除这些实例化，
// 改为在使用处包含本 cpp 或者转为 header-only。
// 这里保留一个空的“参考”块，实际项目中用你的 Node 类型显式实例化：
// template class HpRetiredManager<YourNodeType>;

#include "LockFreeStack/StackNode.hpp"
template class HpRetiredManager<StackNode<int>>;