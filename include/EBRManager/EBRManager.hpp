//EBRManager/EBRManager.hpp
#pragma once

#include <atomic>
#include <cstdint>
#include "EBRManager/ThreadSlotManager.hpp"
#include "EBRManager/GarbageCollector.hpp"
#include "EBRManager/GarbageNode.hpp"
#include "EBRManager/LockFreeSingleLinkedList.hpp"
#include "gc_malloc/ThreadHeap/ThreadHeap.hpp"

class EBRManager {
public:
    EBRManager();
    ~EBRManager();

    EBRManager(const EBRManager&) = delete;
    EBRManager& operator=(const EBRManager&) = delete;
    EBRManager(const EBRManager&&) = delete;
    EBRManager& operator=(const EBRManager&&) = delete;

    void enter();
    void leave();

    template<typename T>
    void retire(T* ptr);

public:
    static constexpr size_t kNumEpochLists = 3;

private:
    bool tryAdvanceEpoch_();
    void collectGarbage_(uint64_t epoch_to_collect);
    ThreadSlot* getLocalSlot_();

private:
    alignas(64) std::atomic<uint64_t> global_epoch_;
    LockFreeSingleLinkedList garbage_lists_[kNumEpochLists];

    ThreadSlotManager slot_manager_;
    GarbageCollector garbage_collector_;
};


template<typename T>
void EBRManager::retire(T* ptr) {
    if(ptr == nullptr){
        return;
    }
    
    auto deleter = [](void* p) {
        T* typed_p   = static_cast<T*>(p);
        typed_p->~T();
        ThreadHeap::deallocate(typed_p);
    };

    void* gnode_mem = ThreadHeap::allocate(sizeof(GarbageNode));
    GarbageNode* g_node = new(gnode_mem) GarbageNode(ptr, deleter);

    uint64_t current_epoch = global_epoch_.load(std::memory_order_relaxed);
    this->garbage_lists_[current_epoch % kNumEpochLists].pushNode(g_node);
}
