#pragma once

#include <atomic>
#include <cstdint>
#include <utility>

template <typename K, typename V>
struct LockFreeHashMapNode {
    const K key;
    V value;
    std::atomic<LockFreeHashMapNode<K, V>*> next;

    using NodePtr = LockFreeHashMapNode<K, V>*;
    using AtomicNodePtr = std::atomic<NodePtr>;

public:
    template <typename KeyType, typename ValueType>
    LockFreeHashMapNode(KeyType&& k, ValueType&& v);

    LockFreeHashMapNode(const LockFreeHashMapNode&) = delete;
    LockFreeHashMapNode& operator=(const LockFreeHashMapNode&) = delete;

    LockFreeHashMapNode(LockFreeHashMapNode&&) = delete;
    LockFreeHashMapNode& operator=(LockFreeHashMapNode&&) = delete;

public:
    // --- 指针标记工具集 ---
    static constexpr uintptr_t MARK_BIT = 1;

    static NodePtr getPointer(const AtomicNodePtr& atomic_ptr);
    static bool isMarked(const AtomicNodePtr& atomic_ptr);
    
    static NodePtr getMarked(NodePtr ptr);
    static NodePtr getUnmarked(NodePtr ptr);   
};


// --- 生命周期管理 (实现) ---

template <typename K, typename V>
template <typename KeyType, typename ValueType>
LockFreeHashMapNode<K, V>::LockFreeHashMapNode(KeyType&& k, ValueType&& v)
    : key(std::forward<KeyType>(k)),
      value(std::forward<ValueType>(v)),
      next(nullptr) {}


// --- 指针标记工具集 (实现) ---

template <typename K, typename V>
typename LockFreeHashMapNode<K, V>::NodePtr
LockFreeHashMapNode<K, V>::getPointer(const AtomicNodePtr& atomic_ptr) {

    return reinterpret_cast<NodePtr>(
        reinterpret_cast<uintptr_t>(atomic_ptr.load(std::memory_order_acquire)) & ~MARK_BIT
    );
}

template <typename K, typename V>
bool LockFreeHashMapNode<K, V>::isMarked(const AtomicNodePtr& atomic_ptr) {
    return (reinterpret_cast<uintptr_t>(atomic_ptr.load(std::memory_order_acquire)) & MARK_BIT) != 0;
}

template <typename K, typename V>
typename LockFreeHashMapNode<K, V>::NodePtr
LockFreeHashMapNode<K, V>::getMarked(NodePtr ptr) {
    return reinterpret_cast<NodePtr>(
        reinterpret_cast<uintptr_t>(ptr) | MARK_BIT
    );
}

template <typename K, typename V>
typename LockFreeHashMapNode<K, V>::NodePtr
LockFreeHashMapNode<K, V>::getUnmarked(NodePtr ptr) {
    return reinterpret_cast<NodePtr>(
        reinterpret_cast<uintptr_t>(ptr) & ~MARK_BIT
    );
}
