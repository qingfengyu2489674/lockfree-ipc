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

CentralHeap& CentralHeap::GetInstance(void* shm_base, std::size_t total_bytes) {
    auto* base = reinterpret_cast<unsigned char*>(shm_base);
    auto* shm_header  = reinterpret_cast<ShmHeader*>(base);

    if (shm_header->state.load(std::memory_order_acquire) == ShmState::kUninit) {
        // 布局：| ShmHeader | CentralHeap | 数据区 ... |
        const std::size_t off_heap = align_up(sizeof(ShmHeader), alignof(CentralHeap));
        const std::size_t off_data = align_up(off_heap + sizeof(CentralHeap), 64);
        assert(total_bytes > off_data && "shared region too small");

        shm_header->heap_off     = static_cast<std::uint64_t>(off_heap);
        shm_header->data_off     = static_cast<std::uint64_t>(off_data);
        shm_header->region_bytes = total_bytes - off_data;

        void* heap_addr = base + shm_header->heap_off;
        void* data_base = base + shm_header->data_off;

        // 在共享内存中原地构造 CentralHeap 本体
        new (heap_addr) CentralHeap(
            data_base,
            shm_header->region_bytes
        );

        // 记录自身在共享内存中的偏移（直接写成员即可，处于类作用域）
        reinterpret_cast<CentralHeap*>(heap_addr)->self_off_ = off_heap;

        shm_header->state.store(ShmState::kReady, std::memory_order_release);
    }

    // 直接从偏移还原出 CentralHeap*
    auto* heap = reinterpret_cast<CentralHeap*>(base + shm_header->heap_off);
    return *heap;
}

CentralHeap::CentralHeap(void* shm_base, std::size_t region_bytes)
    : shm_alloc_(shm_base, region_bytes),
      shm_free_list_() {
}


// -----------------------------------------------------------------------------
// 2. CentralHeap 的核心逻辑实现
// -----------------------------------------------------------------------------

void* CentralHeap::acquireChunk(size_t size) {
    assert(size == kChunkSize);

    void* chunk = shm_free_list_.acquire();
    if(chunk != nullptr) { 
        return chunk;
    }

    bool refill_ok  = refillCache();
    if(!refill_ok ) {
        std::cerr << "[CentralHeap::AcquireChunk] WARNING: Failed to refill cache. "
            << "System might be out of memory." << std::endl;
    }

    chunk = shm_free_list_.acquire();
    if(chunk != nullptr) { 
        return chunk;
    }

    return nullptr;
}

bool CentralHeap::refillCache() {
    if (shm_free_list_.getCacheCount() > 0) {
        return true;
    }

    while(shm_free_list_.getCacheCount() <= kTargetWatermarkInChunks) {
        void* chunk = shm_alloc_.allocate(kChunkSize);
        if(!chunk)
            return false;
        shm_free_list_.deposit(chunk);
    }

    return true;
}

void CentralHeap::releaseChunk(void* chunk, size_t size) {
    assert(size == kChunkSize);

    // if(FreeChunkManager_ptr->getCacheCount() < kMaxWatermarkInChunks) {
    //     FreeChunkManager_ptr->deposit(chunk);
    // } else {
    //     ChunkAllocatorFromKernel_ptr->deallocate(chunk, kChunkSize);
    // }

    shm_free_list_.deposit(chunk);
}

