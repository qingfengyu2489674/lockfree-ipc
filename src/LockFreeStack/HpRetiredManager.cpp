// LockFreeStack/HpRetiredManager.cpp
#include "LockFreeStack/HpRetiredManager.hpp"

#include <cassert>
#include <unordered_set>

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
std::size_t HpRetiredManager<Node>::collect(std::size_t                 quota,
                                            std::vector<const Node*>&   hazard_snapshot,
                                            Reclaimer                   reclaimer) noexcept
{
    if (quota == 0 || !reclaimer) return 0;
    std::lock_guard<ShmMutexLock> g(lock_);
    return scanAndReclaimUpLocked_(quota, hazard_snapshot, reclaimer);
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

// 替换原先的 scanAndReclaimUpLocked_ 定义为以下版本
template <class Node>
std::size_t HpRetiredManager<Node>::scanAndReclaimUpLocked_(
        std::size_t               quota,
        std::vector<const Node*>& hazard_snapshot,
        Reclaimer                 reclaimer) noexcept
{
    if (!global_head_ || quota == 0) return 0;

    // 在锁内用快照构建哈希集合，去重并加速判定
    std::unordered_set<const Node*> hazard_set;
    hazard_set.reserve(hazard_snapshot.size());
    for (const Node* h : hazard_snapshot) {
        if (h) hazard_set.insert(h);
    }

    // 使用哑元节点简化“删除当前结点”的操作
    Node dummy{};
    dummy.next = global_head_;

    Node* prev = &dummy;
    Node* cur  = dummy.next;

    std::size_t freed = 0;

    while (cur && freed < quota) {
        Node* next = cur->next;

        // O(1) 判定当前节点是否被 HP 保护
        const bool protected_now = (hazard_set.find(cur) != hazard_set.end());

        if (!protected_now) {
            // 从链上摘除并回收（保持与原实现一致：锁内回收）
            prev->next = next;
            reclaimer(cur);
            ++freed;
            cur = next;
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