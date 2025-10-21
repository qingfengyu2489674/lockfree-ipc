#include "EBRManager/ThreadSlotManager.hpp"
#include <utility> // for std::move
#include <new>     // for placement new

// --- ThreadSlotManager 实现 ---

ThreadSlotManager::ThreadSlotManager() 
    : capacity_(0) // 初始容量为0，采用懒汉式分配
{
    // segments_ 和 free_slots_ 由其默认构造函数初始化
    // resize_lock_ 也由其默认构造函数初始化
}

ThreadSlotManager::~ThreadSlotManager() {
    // RAII机制会自动处理所有事情：
    // 1. segments_ 的析构函数会被调用。
    // 2. 它会依次调用其中每个 Segment (unique_ptr) 的析构函数。
    // 3. 每个 unique_ptr 会调用其关联的 ThreadHeapArrayDeleter。
    // 4. Deleter 会手动调用每个 ThreadSlot 的析构函数，然后使用 ThreadHeap::deallocate 释放内存。
    // 无需在此处编写任何代码。
}

ThreadSlot* ThreadSlotManager::getLocalSlot() {
    // 每个线程拥有一个独立的 LocalSlotProxy 实例。
    // 其生命周期与线程相同，线程退出时会自动析构。
    thread_local LocalSlotProxy g_local_slot_proxy;

    if (!g_local_slot_proxy.hasSlot()) {
        // 如果当前线程还未分配槽位，则为其分配一个。
        ThreadSlot* new_slot = acquireSlot_();
        if (!new_slot) {
            // 内存分配失败，无法获取槽位。
            return nullptr;
        }
        // 将槽位的所有权信息交给代理对象管理。
        g_local_slot_proxy.acquire(this, new_slot);
    }
    
    return g_local_slot_proxy.get();
}

void ThreadSlotManager::releaseSlot_(ThreadSlot* slot) noexcept {
    // 将不再使用的槽位推入无锁栈，以便快速复用。
    free_slots_.push(slot);
}

ThreadSlot* ThreadSlotManager::acquireSlot_() {
    // 1. 尝试从空闲列表中快速获取一个槽位 (无锁)
    ThreadSlot* slot = free_slots_.pop();
    if (slot) {
        return slot;
    }
    
    // 2. 如果空闲列表为空，则进入慢速路径，进行扩容
    return expandAndAcquire();
}

ThreadSlot* ThreadSlotManager::expandAndAcquire() {
    // 使用互斥锁保护扩容过程，确保线程安全。
    std::lock_guard<ShmMutexLock> lock(resize_lock_);

    // 双重检查锁定：在获取锁后，再次检查空闲列表。
    // 可能在当前线程等待锁的过程中，其他线程已经完成了扩容并释放了槽位。
    ThreadSlot* slot = free_slots_.pop();
    if (slot) {
        return slot;
    }

    // --- 确定扩容大小 ---
    const size_t current_capacity = capacity_.load(std::memory_order_relaxed);
    const size_t new_slots_to_add = (current_capacity == 0) ? kInitialCapacity : current_capacity; // 首次分配或倍增

    // --- 分配并构造 ---
    // 1. 从共享内存堆分配原始内存
    void* raw_mem = ThreadHeap::allocate(sizeof(ThreadSlot) * new_slots_to_add);
    if (!raw_mem) {
        // 共享内存分配失败
        return nullptr;
    }

    // 2. 在原始内存上使用 placement new 构造 ThreadSlot 对象
    ThreadSlot* new_slots_array = static_cast<ThreadSlot*>(raw_mem);
    for (size_t i = 0; i < new_slots_to_add; ++i) {
        new (&new_slots_array[i]) ThreadSlot();
    }

    // 3. 创建一个智能指针 (Segment) 来管理这块内存的生命周期
    //    需要将数组大小传入自定义删除器，以便正确析构和释放。
    detail::ThreadHeapArrayDeleter<ThreadSlot> deleter{new_slots_to_add};
    Segment new_segment(new_slots_array, deleter);
    
    // --- 更新状态 ---
    // 将新创建的槽位（除了要返回的那个）加入空闲列表
    for (size_t i = 0; i < new_slots_to_add - 1; ++i) {
        free_slots_.push(&new_slots_array[i]);
    }

    // 将新的内存段添加到管理器中
    // 注意：这里的拼写遵循了您的代码，但建议改为 segments_
    segments_.push_back(std::move(new_segment));

    // 原子地更新总容量
    // 注意：这里的命名遵循了您的代码，但建议改为 capacity_
    capacity_.fetch_add(new_slots_to_add, std::memory_order_relaxed);

    // 返回最后一个新创建的槽位，直接分配给调用者，避免入栈再出栈的开销
    return &new_slots_array[new_slots_to_add - 1];
}


// --- LocalSlotProxy 实现 ---

ThreadSlotManager::LocalSlotProxy::~LocalSlotProxy() {
    // 这是RAII机制的核心：当线程退出，其 thread_local 的 proxy 对象被析构时，
    // 它会自动将被占用的槽位归还给管理器。
    if (manager_ && slot_) {
        manager_->releaseSlot_(slot_);
    }
}