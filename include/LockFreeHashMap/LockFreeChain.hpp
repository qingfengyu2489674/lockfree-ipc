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
    using Node = LockFreeHashMapNode<K, V>;
    
    // 公共类型别名
    using NodePtr = typename Node::NodePtr;
    using AtomicNodePtr = typename Node::AtomicNodePtr;

    LockFreeChain();
    ~LockFreeChain();

    // 禁止拷贝和移动
    LockFreeChain(const LockFreeChain&) = delete;
    LockFreeChain& operator=(const LockFreeChain&) = delete;
    LockFreeChain(LockFreeChain&&) = delete;
    LockFreeChain& operator=(LockFreeChain&&) = delete;

    std::optional<V> find(const K& key, EBRManager& manager) const;

    template <typename KeyType, typename ValueType>
    bool insert(KeyType&& key, ValueType&& value, EBRManager& manager);
    bool remove(const K& key, EBRManager& manager);
    NodePtr getHead() const;

private:
    struct SearchResult {
        AtomicNodePtr* prevNext_; // 指向前一个节点的next指针的地址
        NodePtr curr_;            // 指向当前找到的节点
    };

    SearchResult search_(const K& key, EBRManager& manager) const;

private:
    // 链表的头指针。声明为mutable以允许const成员函数在辅助清理时修改链表物理结构。
    mutable AtomicNodePtr head_;
    
    // 键比较函数对象
    KeyEqual keyEqual_;
};


#include "LockFreeChain.impl.hpp"