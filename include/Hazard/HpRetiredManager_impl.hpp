// LockFreeStack/HpRetiredManager.cpp
#pragma once

#include <cassert>
#include <unordered_set>

// ========== 修复点 1：改成模板函数 ==========
// 这样编译器就能推导出 Node 类型，而不需要在类外部预先定义 Node
template <typename T>
static T* getGcNext(T* n) {
    // 假设 T 继承自 GCHook
    return static_cast<T*>(n->gc_next);
}

template <typename T>
static void setGcNext(T* n, T* next) {
    n->gc_next = next;
}
// ==========================================

template <class Node, class AllocPolicy>
HpRetiredManager<Node, AllocPolicy>::HpRetiredManager() noexcept = default;

template <class Node, class AllocPolicy>
HpRetiredManager<Node, AllocPolicy>::~HpRetiredManager() noexcept = default;


template <class Node, class AllocPolicy>
void HpRetiredManager<Node, AllocPolicy>::appendRetiredNode(Node* n) noexcept {
    if (!n) return;
    std::lock_guard<ShmMutexLock> guard(lock_);
    n->gc_next = global_head_; 
    global_head_ = n;
    approx_count_.fetch_add(1, std::memory_order_relaxed);
}

template <class Node, class AllocPolicy>
void HpRetiredManager<Node, AllocPolicy>::appendRetiredList(Node* head) noexcept {
    if (!head) return;
    std::lock_guard<ShmMutexLock> guard(lock_);
    appendListLocked_(head);
}

template <class Node, class AllocPolicy>
std::size_t HpRetiredManager<Node, AllocPolicy>::collectRetired(std::size_t quota, const std::vector<const Node*>& hazard_snapshot) noexcept {
    if (quota == 0) {
        quota = static_cast<std::size_t>(-1); 
    }
    std::lock_guard<ShmMutexLock> guard(lock_);
    return scanAndReclaimLocked_(quota, hazard_snapshot);
}


template <class Node, class AllocPolicy>
std::size_t HpRetiredManager<Node, AllocPolicy>::drainAll() noexcept {
    Node* list_to_drain = nullptr;
    {
        std::lock_guard<ShmMutexLock> guard(lock_);
        list_to_drain = std::exchange(global_head_, nullptr);
    }

    if (!list_to_drain) return 0;

    std::size_t freed_count = 0;
    Node* current = list_to_drain;
    while (current) {
        Node* next = static_cast<Node*>(current->gc_next);
        // *** 使用与 HpSlotManager 一致的 AllocPolicy 进行回收 ***
        AllocPolicy::deallocate(current);
        current = next;
        freed_count++;
    }

    if (freed_count > 0) {
        approx_count_.fetch_sub(freed_count, std::memory_order_relaxed);
    }
    return freed_count;
}



template <class Node, class AllocPolicy>
std::size_t HpRetiredManager<Node, AllocPolicy>::getRetiredCount() const noexcept {
    return approx_count_.load(std::memory_order_relaxed);
}

// ========== 私有区（已持锁版本） ==========

template <class Node, class AllocPolicy>
size_t HpRetiredManager<Node, AllocPolicy>::appendListLocked_(Node* head) noexcept {
    Node* tail = head;
    std::size_t count = 1;
    while (tail && tail->gc_next) {
        tail = static_cast<Node*>(tail->gc_next);
        count++;
    }
    if (tail) {
        tail->gc_next = global_head_;
        global_head_ = head;
        approx_count_.fetch_add(count, std::memory_order_relaxed);
    }

    return count;
}


template <class Node, class AllocPolicy>
std::size_t HpRetiredManager<Node, AllocPolicy>::scanAndReclaimLocked_(
    std::size_t quota, const std::vector<const Node*>& hazard_snapshot) noexcept 
{
    if (!global_head_) return 0;

    std::unordered_set<const Node*> hazard_set(hazard_snapshot.begin(), hazard_snapshot.end());
    
    Node dummy_head{};
    dummy_head.gc_next = global_head_;
    Node* prev = &dummy_head;
    Node* current = global_head_;
    std::size_t freed_count = 0;

    while (current && freed_count < quota) {
        Node* next = static_cast<Node*>(current->gc_next);
        if (hazard_set.find(current) == hazard_set.end()) {
            prev->gc_next = next;
            // *** 使用与 HpSlotManager 一致的 AllocPolicy 进行回收 ***
            AllocPolicy::deallocate(current);
            freed_count++;
            current = next;
        } else {
            prev = current;
            current = next;
        }
    }

    global_head_ = static_cast<Node*>(dummy_head.gc_next);
    if (freed_count > 0) {
        approx_count_.fetch_sub(freed_count, std::memory_order_relaxed);
    }
    return freed_count;
}

