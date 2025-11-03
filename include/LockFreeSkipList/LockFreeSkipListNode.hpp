#pragma once

#include <atomic>
#include <cstddef>
#include <new>
#include <cassert>

#include "gc_malloc/ThreadHeap/ThreadHeap.hpp"
#include "Tool/StampPtrPacker.hpp"


template<typename Key, typename Value>
class LockFreeSkipListNode {
public:
    using Node         = LockFreeSkipListNode<Key, Value>;
    using Packer       = StampPtrPacker<Node>;
    using Packed       = typename Packer::type;
    using AtomicPacked = typename Packer::atomic_type;

    const Key key;
    Value value;
    const int height;

    AtomicPacked forward_[1];

public:
    ~LockFreeSkipListNode() = default;
    LockFreeSkipListNode(const LockFreeSkipListNode&) = delete;
    LockFreeSkipListNode& operator=(const LockFreeSkipListNode&) = delete;
    LockFreeSkipListNode(LockFreeSkipListNode&&) = delete;
    LockFreeSkipListNode& operator=(LockFreeSkipListNode&&) = delete;

    static Node* create(const Key& key, const Value& value, int height);
    static Node* createHead(const Key& min_key, int height);
    static void  destroy(Node* node);

    inline AtomicPacked&       nextSlot(int lvl)       noexcept { return forward_[lvl]; }
    inline const AtomicPacked& nextSlot(int lvl) const noexcept { return forward_[lvl]; }

private:
    LockFreeSkipListNode(const Key& key, const Value& value, int height);
    LockFreeSkipListNode(const Key& sentinel_key, int height);
};


template<typename Node>
static inline void* allocateNodeMemory(int height) {
    using AtomicPacked = typename Node::AtomicPacked;
    size_t total_size = offsetof(Node, forward_) + sizeof(AtomicPacked) * height;
    return ThreadHeap::allocate(total_size);
}

template<typename Key, typename Value>
LockFreeSkipListNode<Key, Value>* 
LockFreeSkipListNode<Key, Value>::create(const Key& key, const Value& value, int height) {
    assert(height >= 1);
    using Node = LockFreeSkipListNode<Key, Value>;
    void* raw_memory = allocateNodeMemory<Node>(height);
    return new (raw_memory) Node(key, value, height);
}

template<typename Key, typename Value>
LockFreeSkipListNode<Key, Value>* 
LockFreeSkipListNode<Key, Value>::createHead(const Key& min_key, int height) {
    assert(height >= 1);
    using Node = LockFreeSkipListNode<Key, Value>;
    void* raw_memory = allocateNodeMemory<Node>(height);
    return new (raw_memory) Node(min_key, height);
}

template<typename Key, typename Value>
void LockFreeSkipListNode<Key, Value>::destroy(LockFreeSkipListNode* node) {
    if(!node) {
        return;
    }

    node->~LockFreeSkipListNode();
    ThreadHeap::deallocate(node);
}

template<typename Key, typename Value>
LockFreeSkipListNode<Key, Value>::LockFreeSkipListNode(const Key& key, const Value& value, int height)
    : key(key), value(value), height(height) {
    for (int i = 0; i < height; ++i) {
        forward_[i].store(Packer::pack(nullptr, 0), std::memory_order_relaxed);
    }
}


template<typename Key, typename Value>
LockFreeSkipListNode<Key, Value>::LockFreeSkipListNode(const Key& sentinel_key, int height)
    : key(sentinel_key), value(), height(height) {
    for (int i = 0; i < height; ++i) {
        forward_[i].store(Packer::pack(nullptr, 0), std::memory_order_relaxed);
    }
}