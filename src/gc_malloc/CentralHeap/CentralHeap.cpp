#include "gc_malloc/CentralHeap/CentralHeap.hpp"

#include "gc_malloc/CentralHeap/ShmChunkAllocator.hpp"
#include "gc_malloc/CentralHeap/ShmFreeChunkList.hpp"

#include "ShareMemory/ShmHeader.hpp" 

#include <new>
#include <type_traits>
#include <cassert>
#include <iostream>
#include <thread>

static inline std::size_t align_up(std::size_t x, std::size_t a) {
    return (x + (a - 1)) & ~(a - 1);
}

// -----------------------------------------------------------------------------
// 1. CentralHeap 的构造/析构函数和单例实现
// -----------------------------------------------------------------------------

// 假设 ShmHeader 里：std::atomic<int> state;
// enum { kUninit=0, kInit=1, kReady=2 };

CentralHeap& CentralHeap::GetInstance(void* shm_base, size_t total_bytes) {
    auto* base = reinterpret_cast<unsigned char*>(shm_base);
    auto* H = reinterpret_cast<ShmHeader*>(base);

    ShmState  expected = ShmState::kUninit;
    if (H->app_state.compare_exchange_strong(
            expected, ShmState::kInitializing,
            std::memory_order_acq_rel,   // 成功：acq_rel，拿到初始化权
            std::memory_order_acquire))  // 失败：acquire，读取别人发布的结果
    {
        // —— 我是“唯一的初始化者” ——
        uint64_t off_heap = H->heap_offset;
        const size_t off_data = align_up(off_heap + sizeof(CentralHeap), 64);

        assert(H->total_size > off_data);
        size_t region_bytes = H->total_size - off_data;

        void* heap_addr = base + off_heap;
        void* data_base = base + off_data;

        new (heap_addr) CentralHeap(data_base, region_bytes);
        reinterpret_cast<CentralHeap*>(heap_addr)->self_off_ = off_heap;

        // 发布“就绪”
        H->app_state.store(ShmState::kReady, std::memory_order_release);
    } else {
        // —— 我不是初始化者：等待初始化完成 ——
        // 若出现短窗口看到的不是 kReady，就自旋等待；必要时 sleep/backoff
        while (H->app_state.load(std::memory_order_acquire) != ShmState::kReady) {
             // 简单的 yield，生产环境建议加 pause 指令
             std::this_thread::yield();
        }
    }

    return *reinterpret_cast<CentralHeap*>(base + H->heap_offset);
}


CentralHeap::CentralHeap(void* shm_base, std::size_t region_bytes)
    : shm_alloc_(shm_base, region_bytes),
      shm_free_list_() {
}


// -----------------------------------------------------------------------------
// 2. CentralHeap 的核心逻辑实现
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// CentralHeap 核心逻辑实现
// -----------------------------------------------------------------------------

void* CentralHeap::acquireChunk(size_t size) {
    assert(size == kChunkSize);

    // 显式锁定
    std::lock_guard<ShmMutexLock> lock(shm_mutex_);

    void* chunk = shm_free_list_.acquire();
    if (chunk != nullptr) { 
        return chunk;
    }

    bool refill_ok = refillCacheNolock();
    if (!refill_ok) {
        std::cerr << "[CentralHeap::AcquireChunk] WARNING: Failed to refill cache. "
                  << "System might be out of memory." << std::endl;
    }

    chunk = shm_free_list_.acquire();

    return chunk;
}

bool CentralHeap::refillCacheNolock() {
    if (shm_free_list_.getCacheCount() > 0) {
        return true;
    }

    while (shm_free_list_.getCacheCount() <= kTargetWatermarkInChunks) {
        void* chunk = shm_alloc_.allocate(kChunkSize);
        if (!chunk) {
            return false;
        }
        shm_free_list_.deposit(chunk);
    }

    return true;
}

void CentralHeap::releaseChunk(void* chunk, size_t size) {
    assert(size == kChunkSize);

    // 显式锁定
    std::lock_guard<ShmMutexLock> lock(shm_mutex_);

    shm_free_list_.deposit(chunk);
}