#pragma once

#include <atomic>
#include "Tool/StampPtrPacker.hpp"


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