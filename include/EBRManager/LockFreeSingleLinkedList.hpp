#pragma once

#include <atomic>
#include "Tool/StampPtrPacker.hpp"
#include "EBRManager/GarbageNode.hpp"

class LockFreeSingleLinkedList {
public:
    using Node = GarbageNode;

private:
    using Packer = StampPtrPacker<Node>;
    typename Packer::atomic_type head_;

public:
    LockFreeSingleLinkedList();
    ~LockFreeSingleLinkedList() = default;

    LockFreeSingleLinkedList(const LockFreeSingleLinkedList&) = delete;
    LockFreeSingleLinkedList& operator=(const LockFreeSingleLinkedList&) = delete;
    LockFreeSingleLinkedList(LockFreeSingleLinkedList&&) = delete;
    LockFreeSingleLinkedList& operator=(LockFreeSingleLinkedList&&) = delete;

    void pushNode(Node* new_node);
    Node* stealList() noexcept;
};
