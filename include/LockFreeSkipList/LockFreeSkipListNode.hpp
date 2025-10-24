#pragma once

#include <atomic>
#include <cstddef>
#include <new>
#include <cassert>

#include "gc_malloc/ThreadHeap/ThreadHeap.hpp"


template<typename Key, typename Value>
class LockFreeSkipListNode {
public:
    const Key key;
    Value value;
    const int height;

    std::atomic<LockFreeSkipListNode*> forward_[1];

public:
    ~LockFreeSkipListNode() = default;
    LockFreeSkipListNode(const LockFreeSkipListNode&) = delete;
    LockFreeSkipListNode& operator=(const LockFreeSkipListNode&) = delete;
    LockFreeSkipListNode(LockFreeSkipListNode&&) = delete;
    LockFreeSkipListNode& operator=(LockFreeSkipListNode&&) = delete;

    static LockFreeSkipListNode* create(const Key& key, const Value& value, int height);
    static LockFreeSkipListNode* createHead(const Key& min_key, int height);
    static void destroy(LockFreeSkipListNode* node);

private:
    LockFreeSkipListNode(const Key& key, const Value& value, int height);
    LockFreeSkipListNode(const Key& sentinel_key, int height);
};


template<typename Key, typename Value>
void* allocateNodeMemory(int height) {
    size_t total_size = sizeof(LockFreeSkipListNode<Key, Value>) + (height -1) * sizeof(std::atomic<LockFreeSkipListNode<Key, Value>*>);
    return ThreadHeap::allocate(total_size);
}

template<typename Key, typename Value>
LockFreeSkipListNode<Key, Value>* LockFreeSkipListNode<Key, Value>::create(const Key& key, const Value& value, int height) {
    assert(height >= 1 && "Node height must be at least 1.");
    void* raw_memory = allocateNodeMemory<Key, Value>(height);
    return new (raw_memory) LockFreeSkipListNode(key, value, height);
}

template<typename Key, typename Value>
LockFreeSkipListNode<Key, Value>* LockFreeSkipListNode<Key, Value>::createHead(const Key& min_key, int height) {
    assert(height >= 1 && "Head node height must be at least 1.");
    void* raw_memory = allocateNodeMemory<Key, Value>(height);
    return new (raw_memory) LockFreeSkipListNode(min_key, height);
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

        for(int i = 0; i < height; ++i) {
            forward_[i].store(nullptr, std::memory_order_relaxed);
        }
}


template<typename Key, typename Value>
LockFreeSkipListNode<Key, Value>::LockFreeSkipListNode(const Key& sentinel_key, int height)
    : key(sentinel_key), value(), height(height) {
    for (int i = 0; i < height; ++i) {
        forward_[i].store(nullptr, std::memory_order_relaxed);
    }
}