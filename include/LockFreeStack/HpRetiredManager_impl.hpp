// LockFreeStack/HpRetiredManager.cpp
#pragma once

#include <cassert>
#include <unordered_set>


template <class Node, class AllocPolicy>
void HpRetiredManager<Node, AllocPolicy>::appendRetiredNode(Node* n) noexcept {
    if (!n) return;
    std::lock_guard<ShmMutexLock> guard(lock_);
    n->next = global_head_;
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
    if (quota == 0) return 0;
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
        Node* next = current->next;
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
    while (tail && tail->next) {
        tail = tail->next;
        count++;
    }
    if (tail) {
        tail->next = global_head_;
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
    dummy_head.next = global_head_;
    Node* prev = &dummy_head;
    Node* current = global_head_;
    std::size_t freed_count = 0;

    while (current && freed_count < quota) {
        Node* next = current->next;
        if (hazard_set.find(current) == hazard_set.end()) {
            prev->next = next;
            // *** 使用与 HpSlotManager 一致的 AllocPolicy 进行回收 ***
            AllocPolicy::deallocate(current);
            freed_count++;
            current = next;
        } else {
            prev = current;
            current = next;
        }
    }

    global_head_ = dummy_head.next;
    if (freed_count > 0) {
        approx_count_.fetch_sub(freed_count, std::memory_order_relaxed);
    }
    return freed_count;
}

