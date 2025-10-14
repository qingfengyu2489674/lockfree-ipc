#include "gc_malloc/CentralHeap/CentralHeap.hpp"

#include "gc_malloc/CentralHeap/ShmChunkAllocator.hpp"
#include "gc_malloc/CentralHeap/ShmFreeChunkList.hpp"

#include <new>
#include <type_traits>
#include <cassert>
#include <iostream>

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
    if (H->state.compare_exchange_strong(
            expected, ShmState::kConstructing,
            std::memory_order_acq_rel,   // 成功：acq_rel，拿到初始化权
            std::memory_order_acquire))  // 失败：acquire，读取别人发布的结果
    {
        // —— 我是“唯一的初始化者” ——
        const size_t off_heap = align_up(sizeof(ShmHeader), alignof(CentralHeap));
        const size_t off_data = align_up(off_heap + sizeof(CentralHeap), 64);
        assert(total_bytes > off_data);

        H->heap_off     = static_cast<uint64_t>(off_heap);
        H->data_off     = static_cast<uint64_t>(off_data);
        H->region_bytes = total_bytes - off_data;

        void* heap_addr = base + H->heap_off;
        void* data_base = base + H->data_off;

        // 只会被调用一次
        new (heap_addr) CentralHeap(data_base, H->region_bytes);
        reinterpret_cast<CentralHeap*>(heap_addr)->self_off_ = off_heap;

        // 发布“就绪”
        H->state.store(ShmState::kReady, std::memory_order_release);
    } else {
        // —— 我不是初始化者：等待初始化完成 ——
        // 若出现短窗口看到的不是 kReady，就自旋等待；必要时 sleep/backoff
        while (H->state.load(std::memory_order_acquire) != ShmState::kReady) {
            // 可加 pause/backoff 或 sched_yield
        }
    }

    return *reinterpret_cast<CentralHeap*>(base + H->heap_off);
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
    shm_mutex_.lock();

    void* chunk = shm_free_list_.acquire();
    if (chunk != nullptr) { 
        shm_mutex_.unlock();  // 解锁
        return chunk;
    }

    bool refill_ok = refillCacheNolock();
    if (!refill_ok) {
        std::cerr << "[CentralHeap::AcquireChunk] WARNING: Failed to refill cache. "
                  << "System might be out of memory." << std::endl;
    }

    chunk = shm_free_list_.acquire();
    shm_mutex_.unlock();  // 解锁

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
    shm_mutex_.lock();

    shm_free_list_.deposit(chunk);
    shm_mutex_.unlock();  // 解锁
}