#pragma once

#include "LockFreeChain.hpp"
#include "EBRManager/guard.hpp"

#include <utility>

template <typename K, typename V, typename KeyEqual>
LockFreeChain<K, V, KeyEqual>::LockFreeChain()
    : head_(nullptr), keyEqual_() {}

template <typename K, typename V, typename KeyEqual>
LockFreeChain<K, V, KeyEqual>::~LockFreeChain() {
    // 析构函数在单线程上下文中执行，遍历并销毁所有节点。
    NodePtr curr_unmarked = Node::getUnmarked(head_.load(std::memory_order_relaxed));
    while (curr_unmarked) {
        NodePtr next_unmarked = Node::getUnmarked(curr_unmarked->next.load(std::memory_order_relaxed));
        
        // 1. 显式调用析构函数
        curr_unmarked->~Node();
        // 2. 使用 ThreadHeap 释放内存
        ThreadHeap::deallocate(curr_unmarked);
        
        curr_unmarked = next_unmarked;
    }
}


template <typename K, typename V, typename KeyEqual>
typename LockFreeChain<K, V, KeyEqual>::NodePtr
LockFreeChain<K, V, KeyEqual>::getHead() const {
    return head_.load(std::memory_order_acquire);
}


template <typename K, typename V, typename KeyEqual>
std::optional<V> LockFreeChain<K, V, KeyEqual>::find(const K& key, EBRManager& manager) const {
    SearchResult result = search_(key, manager);
    
    if (result.curr_ != nullptr && !Node::isMarked(result.curr_->next.load(std::memory_order_acquire))) {
        return result.curr_->value;
    }
    
    return std::nullopt;
}



template <typename K, typename V, typename KeyEqual>
template <typename KeyType, typename ValueType>
bool LockFreeChain<K, V, KeyEqual>::insert(KeyType&& key, ValueType&& value, EBRManager& manager) {
    // 1. 使用 ThreadHeap 分配原始内存
    void* raw_mem = ThreadHeap::allocate(sizeof(Node));
    // 2. 使用 placement new 在内存上构造节点
    NodePtr new_node = new (raw_mem) Node(std::forward<KeyType>(key), std::forward<ValueType>(value));
    
    while (true) {
        SearchResult result = search_(key, manager);

        // Case 1: Key已经存在。
        if (result.curr_ != nullptr) {
            // 插入失败，释放新创建的节点。
            new_node->~Node();
            ThreadHeap::deallocate(new_node);
            return false;
        }

        // Case 2: Key不存在，尝试插入。
        new_node->next.store(result.curr_, std::memory_order_relaxed);
        
        NodePtr expected = result.curr_;
        if (result.prevNext_->compare_exchange_strong(expected, new_node, std::memory_order_release, std::memory_order_relaxed)) {
            // 插入成功！
            return true;
        }
        
        // CAS失败，重试。new_node 指针和它指向的已构造对象将被重用于下一次尝试。
    }
}


template <typename K, typename V, typename KeyEqual>
bool LockFreeChain<K, V, KeyEqual>::remove(const K& key, EBRManager& manager) {
    while (true) {
        SearchResult result = search_(key, manager);

        if (result.curr_ == nullptr) {
            return false; // Key不存在
        }

        NodePtr node_to_delete = result.curr_;
        NodePtr next_node = node_to_delete->next.load(std::memory_order_acquire);

        if (Node::isMarked(next_node)) {
            continue; // 节点已被标记，让 search_ 去清理并重试
        }
        
        NodePtr marked_next = Node::getMarked(next_node);
        if (node_to_delete->next.compare_exchange_strong(next_node, marked_next, std::memory_order_acq_rel, std::memory_order_relaxed)) {
            // 逻辑删除成功。
            NodePtr expected = node_to_delete;
            if (result.prevNext_->compare_exchange_strong(expected, next_node, std::memory_order_release, std::memory_order_relaxed)) {
                // 物理删除也成功，将节点交由EBR回收。
                // EBRManager 内部最终会调用析构函数和 ThreadHeap::deallocate。
                ebr::retire(manager, node_to_delete);
            }
            return true;
        }
        // CAS失败，重试。
    }
}


// --- 私有辅助函数 ---

template <typename K, typename V, typename KeyEqual>
typename LockFreeChain<K, V, KeyEqual>::SearchResult
LockFreeChain<K, V, KeyEqual>::search_(const K& key, EBRManager& manager) const {
    retry_search:
    SearchResult result = { const_cast<AtomicNodePtr*>(&head_), head_.load(std::memory_order_acquire) };

    while (result.curr_ != nullptr) {
        NodePtr curr_unmarked = Node::getUnmarked(result.curr_);
        NodePtr next = curr_unmarked->next.load(std::memory_order_acquire);

        if (Node::isMarked(next)) {
            NodePtr next_unmarked = Node::getUnmarked(next);
            NodePtr expected_curr = result.curr_;

            if (result.prevNext_->compare_exchange_strong(expected_curr, next_unmarked, std::memory_order_release, std::memory_order_relaxed)) {
                // 物理摘除成功，将节点交由EBR回收。
                ebr::retire(manager, curr_unmarked);
            }
            
            // 无论CAS成功与否，链表结构都可能已变，从头重试最安全。
            goto retry_search;
        }

        if (keyEqual_(curr_unmarked->key, key)) {
            // 找到匹配且未被标记的节点
            result.curr_ = curr_unmarked;
            return result;
        }
        
        result.prevNext_ = &(curr_unmarked->next);
        result.curr_ = next;
    }

    // 未找到，result.curr_ 为 nullptr
    return result;
}