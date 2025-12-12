// HpSlotManager_impl.hpp
#pragma once
#include <iostream>
#include <vector>
#include <thread>
#include <new>


// ====================== 模板成员实现 ======================

template<class Node, std::size_t MaxPointers, class AllocPolicy>
HpSlotManager<Node, MaxPointers, AllocPolicy>::~HpSlotManager() {
    SlotNode* current = head_;
    while (current) {
        SlotNode* next = current->next;
        AllocPolicy::deallocate(current->slot);
        AllocPolicy::deallocate(current);
        current = next;
    }

    if (tls_slot_ != nullptr) {
        // 通过直接清空 on_exit，我们阻止了在程序退出后期对悬垂指针的访问。
        tls_exit_handler_.on_exit = nullptr;
        tls_slot_ = nullptr; // 逻辑上也清空槽位指针
    }
}



template<class Node, std::size_t MaxPointers, class AllocPolicy>
auto HpSlotManager<Node, MaxPointers, AllocPolicy>::acquireTls() -> SlotType* {
    if (tls_slot_ == nullptr) {
        tls_exit_handler_.on_exit = [this]() { this->unregisterTls_(); };

        auto* slot = AllocPolicy::template allocate<SlotType>();
        auto* node = AllocPolicy::template allocate<SlotNode>();
        node->slot = slot;

        {
            std::lock_guard<ShmMutexLock> lock(shm_mutx_);
            node->next = head_;
            head_ = node;
        }
        tls_slot_ = slot;
    }
    return tls_slot_;
}


template<class Node, std::size_t MaxPointers, class AllocPolicy>
void HpSlotManager<Node, MaxPointers, AllocPolicy>::unregisterTls_() {
    SlotType* slot_to_delete = tls_slot_;
    if (!slot_to_delete) return;

    SlotNode* node_to_delete = nullptr;
    {
        std::lock_guard<ShmMutexLock> lock(shm_mutx_);
        unlinkUnlocked_(slot_to_delete, node_to_delete);
    }
    
    // 在锁外执行释放
    AllocPolicy::deallocate(node_to_delete);
    AllocPolicy::deallocate(slot_to_delete);
    
    tls_slot_ = nullptr;
}


template<class Node, std::size_t MaxPointers, class AllocPolicy>
std::size_t HpSlotManager<Node, MaxPointers, AllocPolicy>::getSlotCount() const {
    std::size_t n = 0;
    std::lock_guard<ShmMutexLock> lk(shm_mutx_);
    for (SlotNode* p = head_; p; p = p->next) ++n;
    return n;
}


template<class Node, std::size_t MaxPointers, class AllocPolicy>
void HpSlotManager<Node, MaxPointers, AllocPolicy>::snapshotHazardpoints(std::vector<const Node*>& out) const {
    out.clear();
    std::lock_guard<ShmMutexLock> lock(shm_mutx_);
    for (SlotNode* p = head_; p; p = p->next) {
        SlotType* slot = p->slot;
        // 使用 HpSlot 的公共接口进行扫描
        for (std::size_t i = 0; i < slot->getHazardPointerCount(); ++i) {
            const auto& hp_atomic = slot->getHazardPointerAt(i);
            const Node* ptr = hp_atomic.load(std::memory_order_acquire);
            if (ptr) {
                out.push_back(ptr);
            }
        }
    }
}


// ====================== 新增：flushAllRetiredTo ======================
template<class Node, std::size_t MaxPointers, class AllocPolicy>
std::size_t HpSlotManager<Node, MaxPointers, AllocPolicy>::flushAllRetiredTo(std::atomic<Node*>& dst_head) noexcept {
    std::vector<SlotType*> slots;
    {
        std::lock_guard<ShmMutexLock> lock(shm_mutx_);
        for (SlotNode* p = head_; p; p = p->next) {
            slots.push_back(p->slot);
        }
    }

    size_t total_flushed = 0;
    for (SlotType* slot : slots) {
        // [修改 1] getRetiredListHead() 现在返回 Atomic<GCHook*>，所以取出的是 GCHook*
        GCHook* hook_ptr = slot->getRetiredListHead().exchange(nullptr, std::memory_order_acq_rel);
        
        if (!hook_ptr) continue;
        
        // [修改 2] 将 GCHook* 安全转回 Node* (前提是 Node 继承自 GCHook)
        Node* retired_list = static_cast<Node*>(hook_ptr);
        
        Node* tail = retired_list;
        size_t count = 1;

        // [修改 3] 遍历链表寻找尾部时，必须走 gc_next，绝对不能碰 next (那是栈逻辑用的)
        while (tail->gc_next) {
            // gc_next 是 GCHook* 类型，转回 Node* 继续遍历
            tail = static_cast<Node*>(tail->gc_next);
            count++;
        }

        Node* old_head = dst_head.load(std::memory_order_relaxed);
        do {
            // [修改 4] 链接旧链表头时，修改的是 gc_next
            // old_head 是 Node*，赋值给 gc_next (GCHook*) 是安全的隐式转换
            tail->gc_next = old_head;
            
        } while (!dst_head.compare_exchange_weak(old_head, retired_list, std::memory_order_release, std::memory_order_relaxed));
        
        total_flushed += count;
    }
    return total_flushed;
}


template<class Node, std::size_t MaxPointers, class AllocPolicy>
void HpSlotManager<Node, MaxPointers, AllocPolicy>::retireNode(Node* n) noexcept {
    if (n) {
        acquireTls()->pushRetired(n);
    }
}

template<class Node, std::size_t MaxPointers, class AllocPolicy>
void HpSlotManager<Node, MaxPointers, AllocPolicy>::retireList(Node* head) noexcept {
    if (!head) return;
    SlotType* s = acquireTls();
    Node* current = head;
    while (current) {
        Node* next = current->next;
        s->pushRetired(current);
        current = next;
    }
}

template<class Node, std::size_t MaxPointers, class AllocPolicy>
void HpSlotManager<Node, MaxPointers, AllocPolicy>::unlinkUnlocked_(SlotType* s, SlotNode*& out_node_to_delete) noexcept {
    SlotNode** current_ptr = &head_;
    while (*current_ptr) {
        if ((*current_ptr)->slot == s) {
            out_node_to_delete = *current_ptr;
            *current_ptr = (*current_ptr)->next;
            return;
        }
        current_ptr = &((*current_ptr)->next);
    }
}

