#pragma once

#include "LockFreeChain.hpp"
#include "EBRManager/guard.hpp"
#include "gc_malloc/ThreadHeap/ThreadHeap.hpp"  // 析构里需要

#include <utility>

// --- 构造 / 析构 ---

template <typename K, typename V, typename KeyEqual>
LockFreeChain<K, V, KeyEqual>::LockFreeChain()
    : head_(Packer::pack(nullptr, 0)), keyEqual_() {}

template <typename K, typename V, typename KeyEqual>
LockFreeChain<K, V, KeyEqual>::~LockFreeChain() {
    NodePtr curr = Node::getPointer(head_);
    while (curr) {
        NodePtr next = Node::getPointer(curr->next);
        curr->~Node();
        ThreadHeap::deallocate(curr);
        curr = next;
    }
}

template <typename K, typename V, typename KeyEqual>
typename LockFreeChain<K, V, KeyEqual>::NodePtr
LockFreeChain<K, V, KeyEqual>::getHead() const {
    return Node::getPointer(head_);
}

// --- 查找 ---

template <typename K, typename V, typename KeyEqual>
std::optional<V> LockFreeChain<K, V, KeyEqual>::find(const K& key, EBRManager& manager) const {
    SearchResult result = search_(key, manager);
    if (result.curr_ != nullptr && !Node::isMarked(result.curr_->next)) {
        return result.curr_->value;
    }
    return std::nullopt;
}

// --- 插入 ---

template <typename K, typename V, typename KeyEqual>
template <typename KeyType, typename ValueType>
bool LockFreeChain<K, V, KeyEqual>::insert(KeyType&& key, ValueType&& value, EBRManager& manager) {
    void*   raw_mem  = ThreadHeap::allocate(sizeof(Node));
    NodePtr new_node = new (raw_mem) Node(std::forward<KeyType>(key), std::forward<ValueType>(value));

    while (true) {
        SearchResult result = search_(key, manager);

        // 已存在：释放新节点
        if (result.curr_ != nullptr) {
            new_node->~Node();
            ThreadHeap::deallocate(new_node);
            return false;
        }

        // 挂接到 result.curr_ 之前
        new_node->next.store(Node::Packer::pack(result.curr_, 0), std::memory_order_relaxed);

        // 校验 prevNext_ 当前视图仍指向 result.curr_（去标记视图）
        typename Node::Packer::type exp = result.prevNext_->load(std::memory_order_acquire);
        if (Node::getUnmarked(Node::Packer::unpackPtr(exp)) != result.curr_) {
            continue;
        }

        // CAS + 自增戳
        if (Node::Packer::casBump(*result.prevNext_, exp, new_node,
                                  std::memory_order_release,
                                  std::memory_order_acquire)) {
            return true;
        }
        // 否则 exp 已被刷新，继续重试
    }
}

// --- 删除 ---

template <typename K, typename V, typename KeyEqual>
bool LockFreeChain<K, V, KeyEqual>::remove(const K& key, EBRManager& manager) {
    while (true) {
        SearchResult result = search_(key, manager);
        if (result.curr_ == nullptr) {
            return false; // 不存在
        }

        NodePtr node_to_delete = result.curr_;
        NodePtr next_node      = Node::getPointer(node_to_delete->next);

        // 已被标记，交给 search_ 去清理
        if (Node::isMarked(node_to_delete->next)) {
            continue;
        }

        // 逻辑删除（next -> marked(next)），用版本 CAS
        typename Node::Packer::type exp_next = node_to_delete->next.load(std::memory_order_acquire);
        if (Node::getUnmarked(Node::Packer::unpackPtr(exp_next)) != next_node) {
            continue; // 形势变了
        }

        NodePtr marked_next = Node::getMarked(next_node);
        if (Node::Packer::casBump(node_to_delete->next, exp_next, marked_next)) {
            // 物理摘除：prevNext_ 从 node_to_delete -> next_node
            typename Node::Packer::type exp_prev = result.prevNext_->load(std::memory_order_acquire);
            if (Node::getUnmarked(Node::Packer::unpackPtr(exp_prev)) == node_to_delete) {
                Node::Packer::casBump(*result.prevNext_, exp_prev, next_node,
                                      std::memory_order_release,
                                      std::memory_order_relaxed);
            }
            ebr::retire(manager, node_to_delete);
            return true;
        }
        // 否则失败，重试
    }
}

// --- 私有：搜索并按需清理（带 EBR 保护） ---

template <typename K, typename V, typename KeyEqual>
typename LockFreeChain<K, V, KeyEqual>::SearchResult
LockFreeChain<K, V, KeyEqual>::search_(const K& key, EBRManager& manager) const {
retry_search:
    SearchResult result = { const_cast<AtomicNodePtr*>(&head_), Node::getPointer(head_) };

    while (result.curr_ != nullptr) {
        NodePtr curr_unmarked = result.curr_;
        NodePtr next          = Node::getPointer(curr_unmarked->next);

        // 如果当前节点已被标记：尝试物理摘除
        if (Node::isMarked(curr_unmarked->next)) {
            NodePtr next_unmarked = Node::getUnmarked(next);

            // 仅在 prevNext_ 仍指向 curr_unmarked 时尝试修复
            typename Node::Packer::type exp_prev = result.prevNext_->load(std::memory_order_acquire);
            if (Node::getUnmarked(Node::Packer::unpackPtr(exp_prev)) == curr_unmarked) {
                if (Node::Packer::casBump(*result.prevNext_, exp_prev, next_unmarked,
                                          std::memory_order_release,
                                          std::memory_order_relaxed)) {
                    ebr::retire(manager, curr_unmarked);
                }
            }
            goto retry_search; // 结构已变，最安全从头再来
        }

        if (keyEqual_(curr_unmarked->key, key)) {
            result.curr_ = curr_unmarked;
            return result;
        }

        result.prevNext_ = &(curr_unmarked->next);
        result.curr_     = next;
    }

    // 未找到
    return result;
}
