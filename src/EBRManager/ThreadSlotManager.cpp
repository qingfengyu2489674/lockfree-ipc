#include "EBRManager/ThreadSlotManager.hpp"
#include <utility>
#include <new>
#include <mutex>
#include <cstring>

ThreadSlotManager::ThreadSlotManager()
    : capacity_(0) {}

ThreadSlotManager::~ThreadSlotManager() {}


ThreadSlot* ThreadSlotManager::getLocalSlot() {

    thread_local LocalSlotProxy g_local_slot_proxy;

    if(!g_local_slot_proxy.hasSlot()) {
        ThreadSlot* new_slot = acquireSlot_();
        if(!new_slot) {
            return nullptr;
        }
        g_local_slot_proxy.acquire(this, new_slot);
    }

    return g_local_slot_proxy.get();
}

void ThreadSlotManager::releaseSlot_(ThreadSlot* slot) noexcept{
    free_slots_.push(slot);
}

ThreadSlot* ThreadSlotManager::acquireSlot_() {

    ThreadSlot* slot = free_slots_.pop();
    if(slot) {
        return slot;
    }

    return expandAndAcquire();
}

ThreadSlot* ThreadSlotManager::expandAndAcquire() {
    std::lock_guard<ShmMutexLock> lock(resize_lock_);

    ThreadSlot* slot = free_slots_.pop();
    if(slot) {
        return slot;
    }

    const size_t current_capacity = capacity_.load(std::memory_order_relaxed);
    const size_t new_slots_to_add = (current_capacity == 0) ? kInitialCapacity : current_capacity;

    void* raw_mem = ThreadHeap::allocate(sizeof(ThreadSlot) * new_slots_to_add);
    if(!raw_mem) {
        return nullptr;
    }

    std::memset(raw_mem, 0, sizeof(ThreadSlot) * new_slots_to_add);

    ThreadSlot* new_slots_array = static_cast<ThreadSlot*>(raw_mem);
    for(size_t i = 0; i < new_slots_to_add; ++i) {
        new(&new_slots_array[i]) ThreadSlot();
    }

    detail::ThreadHeapArrayDeleter<ThreadSlot> deleter{new_slots_to_add};
    Segment new_segment(new_slots_array, deleter);

    for(size_t i = 0; i < new_slots_to_add - 1; ++i) {
        free_slots_.push(&new_slots_array[i]);
    }

    segments_.push_back(std::move(new_segment));
    capacity_.fetch_add(new_slots_to_add, std::memory_order_relaxed);

    return &new_slots_array[new_slots_to_add -1];
}

ThreadSlotManager::LocalSlotProxy::~LocalSlotProxy() {
    if(manager_ && slot_) {
        manager_->releaseSlot_(slot_);
    }
}