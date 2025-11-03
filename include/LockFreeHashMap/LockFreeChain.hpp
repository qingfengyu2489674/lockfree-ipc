#pragma once

#include "LockFreeHashMapNode.hpp"
#include "EBRManager/EBRManager.hpp"

#include <optional>
#include <functional>

template <typename K,
          typename V,
          typename KeyEqual = std::equal_to<K>>
class LockFreeChain {
public:
    using Node          = LockFreeHashMapNode<K, V>;
    using NodePtr       = typename Node::NodePtr;
    using AtomicNodePtr = typename Node::AtomicNodePtr;

    using Packer        = typename Node::Packer;
    using Packed        = typename Packer::type;

    LockFreeChain();
    ~LockFreeChain();

    LockFreeChain(const LockFreeChain&)            = delete;
    LockFreeChain& operator=(const LockFreeChain&) = delete;
    LockFreeChain(LockFreeChain&&)                 = delete;
    LockFreeChain& operator=(LockFreeChain&&)      = delete;

    std::optional<V> find(const K& key, EBRManager& manager) const;

    template <typename KeyType, typename ValueType>
    bool insert(KeyType&& key, ValueType&& value, EBRManager& manager);

    bool     remove(const K& key, EBRManager& manager);
    NodePtr  getHead() const;

private:
    struct SearchResult {
        AtomicNodePtr* prevNext_; // 指向“前驱->next”槽位（atomic packed）
        NodePtr        curr_;     // 当前未标记节点（raw 指针）
    };

    SearchResult search_(const K& key, EBRManager& manager) const;

private:
    mutable AtomicNodePtr head_;  // 头结点槽位（packed: ptr+stamp）
    KeyEqual              keyEqual_;
};

#include "LockFreeChain.impl.hpp"
