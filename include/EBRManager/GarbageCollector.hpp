#pragma once

#include <mutex>

#include "Tool/ShmMutexLock.hpp"
#include "EBRManager/GarbageNode.hpp"
#include "gc_malloc/ThreadHeap/ThreadHeap.hpp"

/**
 * @class GarbageCollector
 * @brief 负责安全地回收和释放由 LockFreeSingleLinkedList 提供的垃圾节点链表。
 *
 * 该类提供了一个线程安全的 `collect` 方法，它接收一个垃圾链表的头节点，
 * 然后在互斥锁的保护下，遍历整个链表，为每个节点调用析构函数并使用
 * ThreadHeap 释放其内存。
 */
class GarbageCollector {
public:
    using Node = GarbageNode;

    GarbageCollector() = default;

    ~GarbageCollector() = default;

    GarbageCollector(const GarbageCollector&) = delete;
    GarbageCollector& operator=(const GarbageCollector&) = delete;
    GarbageCollector(GarbageCollector&&) = delete;
    GarbageCollector& operator=(GarbageCollector&&) = delete;

    void collect(Node* garbage_list_head);

private:
    mutable ShmMutexLock lock_;
};


inline void GarbageCollector::collect(Node* garbage_list_head) {
    // 如果传入的是空链表，直接返回，无需加锁。
    if (!garbage_list_head) {
        return;
    }

    std::lock_guard<ShmMutexLock> lock(lock_);

    Node* current = garbage_list_head;
    while (current != nullptr) {
        Node* next = current->next; // 提前保存下一个节点

        // 步骤 1: 显式调用析构函数，清理对象状态
        current->~Node();

        // 步骤 2: 使用 ThreadHeap 释放原始内存
        ThreadHeap::deallocate(current);

        current = next; // 移动到下一个节点
    }
}