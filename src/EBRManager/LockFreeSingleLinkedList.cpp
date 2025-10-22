#include "EBRManager/LockFreeSingleLinkedList.hpp"


LockFreeSingleLinkedList::LockFreeSingleLinkedList() {
    head_.store(Packer::pack(nullptr, 0), std::memory_order_relaxed);
}  

void LockFreeSingleLinkedList::pushNode(Node* new_node) {
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

LockFreeSingleLinkedList::Node* LockFreeSingleLinkedList::stealList() noexcept {
    for(;;) {
        uint64_t old_packed = head_.load(std::memory_order_acquire);
        Node* old_head = Packer::unpackPtr(old_packed);
        
        if (old_head == nullptr) {
            return nullptr; // 链表为空
        }

        uint16_t old_stamp = Packer::unpackStamp(old_packed);
        uint64_t new_packed = Packer::pack(nullptr, old_stamp + 1);

        if (head_.compare_exchange_weak(old_packed, new_packed,
                                         std::memory_order_acq_rel,
                                         std::memory_order_acquire)) {
            return old_head; // 返回被窃取的链表头
        }
    }
}
