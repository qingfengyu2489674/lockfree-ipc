#pragma once

#include <atomic>
#include <cstdint>
#include <utility>
#include "Tool/StampPtrPacker.hpp"

template <typename K, typename V>
struct LockFreeHashMapNode {
    using Node          = LockFreeHashMapNode<K, V>;
    using NodePtr       = Node*;

    using Packer        = StampPtrPacker<Node>;
    using Packed        = typename Packer::type;         // uint64_t
    using AtomicPacked  = typename Packer::atomic_type;  // std::atomic<uint64_t>
    using AtomicNodePtr = AtomicPacked;                  // 兼容旧名：表示“版本指针槽位”

    static constexpr uintptr_t MARK_BIT = 1;             // LSB 逻辑删除标记（打在原始指针上）

    const K key;
    V value;
    AtomicNodePtr next;                                  // 版本指针槽位（packed: ptr+stamp）

    // 构造
    template <typename KeyType, typename ValueType>
    LockFreeHashMapNode(KeyType&& k, ValueType&& v);

    LockFreeHashMapNode(const Node&)            = delete;
    Node& operator=(const Node&)                = delete;
    LockFreeHashMapNode(Node&&)                 = delete;
    Node& operator=(Node&&)                     = delete;

    // ---- 仅保留的原始接口（四个） ----
    static NodePtr getPointer(const AtomicNodePtr& atomic_slot);
    static bool    isMarked (const AtomicNodePtr& atomic_slot);

    static NodePtr getMarked  (NodePtr ptr);
    static NodePtr getUnmarked(NodePtr ptr);
};

// ---------- 实现 ----------

template <typename K, typename V>
template <typename KeyType, typename ValueType>
LockFreeHashMapNode<K, V>::LockFreeHashMapNode(KeyType&& k, ValueType&& v)
    : key(std::forward<KeyType>(k))
    , value(std::forward<ValueType>(v))
    , next(Packer::pack(nullptr, 0)) // 初始为空，stamp = 0
{}

// 从槽位读出 packed -> 解包出指针 -> 去标记（返回 raw Node*）
template <typename K, typename V>
typename LockFreeHashMapNode<K, V>::NodePtr
LockFreeHashMapNode<K, V>::getPointer(const AtomicNodePtr& atomic_slot) {
    auto packed = atomic_slot.load(std::memory_order_acquire);
    auto* p     = Packer::unpackPtr(packed);
    return getUnmarked(p);
}

// 读取槽位并判断“当前指针是否带标记位”
template <typename K, typename V>
bool LockFreeHashMapNode<K, V>::isMarked(const AtomicNodePtr& atomic_slot) {
    auto packed = atomic_slot.load(std::memory_order_acquire);
    auto* p     = Packer::unpackPtr(packed);
    return (reinterpret_cast<uintptr_t>(p) & MARK_BIT) != 0;
}

// 在 raw 指针上打/去标记（与槽位无关）
template <typename K, typename V>
typename LockFreeHashMapNode<K, V>::NodePtr
LockFreeHashMapNode<K, V>::getMarked(NodePtr ptr) {
    return reinterpret_cast<Node*>(
        reinterpret_cast<uintptr_t>(ptr) | MARK_BIT
    );
}

template <typename K, typename V>
typename LockFreeHashMapNode<K, V>::NodePtr
LockFreeHashMapNode<K, V>::getUnmarked(NodePtr ptr) {
    return reinterpret_cast<Node*>(
        reinterpret_cast<uintptr_t>(ptr) & ~MARK_BIT
    );
}
