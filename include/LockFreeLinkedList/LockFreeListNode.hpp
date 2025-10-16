#pragma once

#include <atomic>
#include <utility> // for std::move

/**
 * @brief 免锁链表的节点 (Node for a Lock-Free Linked List)
 * 
 * @tparam T 节点存储的数据类型
 * 
 * 这个节点专为免锁链表设计，其核心特性是：
 * 1.  `next` 指针是原子的 (`std::atomic`)，因为它可能被多个线程在链表的任何位置并发修改。
 *     这与免锁栈的节点不同，栈节点只需要栈顶的 head 指针是原子的。
 * 2.  利用 `next` 指针的最低有效位 (LSB) 作为“逻辑删除标记”(mark bit)。
 *     - 如果 LSB 是 0：节点是正常的、有效的。
 *     - 如果 LSB 是 1：节点已被“逻辑删除”，正在等待被物理摘除和内存回收。
 *     链表的操作逻辑（如 find, insert, delete）需要负责处理这个标记位的打包和解包。
 */
template <class T>
class LockFreeListNode {
public:
    using node_ptr = LockFreeListNode<T>*;

    T value;                                  // 存储的值
    std::atomic<node_ptr> next{nullptr};      // 指向下一个节点的原子指针

public:
    LockFreeListNode() = default;
    explicit LockFreeListNode(const T& v) : value(v), next(nullptr) {}
    explicit LockFreeListNode(T&& v) : value(std::move(v)), next(nullptr) {}


    ~LockFreeListNode() = default;

    LockFreeListNode(const LockFreeListNode&) = delete;
    LockFreeListNode& operator=(const LockFreeListNode&) = delete;
    LockFreeListNode(LockFreeListNode&&) = delete;
    LockFreeListNode& operator=(LockFreeListNode&&) = delete;


    // --- 辅助函数（可选但推荐）---
    // 这些静态辅助函数可以简化链表实现中对“标记位”的操作，使代码更清晰。

    /**
     * @brief 获取指针的实际地址（清除标记位）
     */
    static node_ptr get_ptr(node_ptr p) {
        return reinterpret_cast<node_ptr>(
            reinterpret_cast<uintptr_t>(p) & ~1UL
        );
    }

    /**
     * @brief 检查指针是否被标记
     */
    static bool is_marked(node_ptr p) {
        return (reinterpret_cast<uintptr_t>(p) & 1UL) != 0;
    }

    /**
     * @brief 获取一个未标记的指针
     */
    static node_ptr get_unmarked(node_ptr p) {
        return get_ptr(p); // 别名，语义更清晰
    }

    /**
     * @brief 获取一个已标记的指针
     */
    static node_ptr get_marked(node_ptr p) {
        return reinterpret_cast<node_ptr>(
            reinterpret_cast<uintptr_t>(p) | 1UL
        );
    }
};