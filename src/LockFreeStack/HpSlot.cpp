#include "LockFreeStack/HpSlot.hpp"


template<class Node>
void HpSlot<Node>::protect(Node* p) noexcept {
    hazard_ptr.store(p, std::memory_order_release);
}

template<class Node>
void HpSlot<Node>::clear() noexcept {
    hazard_ptr.store(nullptr, std::memory_order_release);
}

template<class Node>
void HpSlot<Node>::pushRetired(Node* n) noexcept {
    // 要求：Node 拥有 `Node* next`，且此处仅本线程写入 n->next
    Node* old = retired_head.load(std::memory_order_relaxed);
    do {
        n->next = old;  // 仅当前线程写，尚未发布给其他线程
    } while (!retired_head.compare_exchange_weak(
        old, n,
        std::memory_order_release,   // 成功：发布整节点（含 next 等）
        std::memory_order_acquire)); // 失败：获得新的 old（含同步）
}

template<class Node>
Node* HpSlot<Node>::drainAll() noexcept {
    // 原子性摘走整段退休链；调用方随后可遍历/筛选/释放
    return retired_head.exchange(nullptr, std::memory_order_acq_rel);
}

#include "LockFreeStack/StackNode.hpp"
template class HpSlot<StackNode<int>>;

