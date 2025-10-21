#pragma once

#include <atomic>
#include "Tool/StampPtrPacker.hpp" // 引入您已经实现的 StampedPtrPacker

/**
 * @brief 一个侵入式的无锁栈。
 * 
 * "侵入式" 意味着模板参数 Node 类型本身必须提供一个 'Node* next' 成员
 * 用于链接。这个栈不拥有它所管理的节点，也不负责节点的分配和销毁。
 * 它只负责以无锁的方式管理这些节点的链接关系。
 * 
 * 由于它管理的是长生命周期、可复用的对象（而不是需要被销毁的对象），
 * 它天然地避免了在 pop 操作中常见的 Use-After-Free (UAF) 问题。
 * 
 * @tparam Node 要被链接的节点类型。必须包含 'Node* next;' 成员。
 */
template <typename Node>
class LockFreeReuseStack {
private:
    using Packer = StampPtrPacker<Node>;
    typename Packer::atomic_type head_;

public:
    LockFreeReuseStack();
    ~LockFreeReuseStack() = default;

    LockFreeReuseStack(const LockFreeReuseStack&) = delete;
    LockFreeReuseStack& operator=(const LockFreeReuseStack&) = delete;
    LockFreeReuseStack(LockFreeReuseStack&&) = delete;
    LockFreeReuseStack& operator=(LockFreeReuseStack&&) = delete;

    void push(Node* new_node);
    Node* pop() noexcept;
};

// --- 实现 ---

template <typename Node>
LockFreeReuseStack<Node>::LockFreeReuseStack() {
    head_.store(Packer::pack(nullptr, 0), std::memory_order_relaxed);
}

template <typename Node>
void LockFreeReuseStack<Node>::push(Node* new_node) {
    for (;;) {
        uint64_t old_packed = head_.load(std::memory_order_relaxed);
        new_node->next = Packer::unpackPtr(old_packed);
        
        uint16_t old_stamp = Packer::unpackStamp(old_packed);
        uint64_t new_packed = Packer::pack(new_node, old_stamp + 1);

        if (head_.compare_exchange_weak(old_packed, new_packed,
                                         std::memory_order_release,
                                         std::memory_order_relaxed)) {
            return;
        }
    }
}

template <typename Node>
Node* LockFreeReuseStack<Node>::pop() noexcept {
    for (;;) {
        uint64_t old_packed = head_.load(std::memory_order_acquire);
        Node* old_head = Packer::unpackPtr(old_packed);

        if (old_head == nullptr) {
            return nullptr;
        }

        Node* new_head = old_head->next;
        uint16_t old_stamp = Packer::unpackStamp(old_packed);
        uint64_t new_packed = Packer::pack(new_head, old_stamp + 1);

        if (head_.compare_exchange_weak(old_packed, new_packed,
                                         std::memory_order_acq_rel,
                                         std::memory_order_acquire)) {
            return old_head;
        }
    }
}