#include "gc_malloc/CentralHeap/CentralHeap.hpp"

#include "gc_malloc/CentralHeap/ShmChunkAllocator.hpp"
#include "gc_malloc/CentralHeap/ShmFreeChunkList.hpp"

#include <new>
#include <type_traits>
#include <cassert>
#include <iostream>

// -----------------------------------------------------------------------------
// 1. 在编译时，在 .bss 段为我们的核心组件预留静态内存。
//    这些全局静态变量保证了它们的生命周期与程序相同，并且不依赖于堆分配。
// -----------------------------------------------------------------------------

static std::aligned_storage<sizeof(ShmChunkAllocator),
                            alignof(ShmChunkAllocator)>::type g_kernel_allocator_buffer;

static std::aligned_storage<sizeof(ShmFreeChunkList),
                            alignof(ShmFreeChunkList)>::type g_chunk_cache_buffer;


// -----------------------------------------------------------------------------
// 2. CentralHeap 的构造/析构函数和单例实现
// -----------------------------------------------------------------------------

CentralHeap& CentralHeap::GetInstance() {
    // Meyers' Singleton: C++11 保证了函数内静态变量的线程安全初始化。
    // instance 本身也会被存放在 BSS/DATA 段。
    static CentralHeap instance;
    return instance;
}


CentralHeap::CentralHeap(void* shm_base, size_t region_bytes)
      : shm_alloc_(shm_base, region_bytes), shm_free_list_{} {}


// -----------------------------------------------------------------------------
// 3. CentralHeap 的核心逻辑实现
// -----------------------------------------------------------------------------

void* CentralHeap::acquireChunk(size_t size) {
    assert(size == kChunkSize);

    void* chunk = FreeChunkManager_ptr->acquire();
    if(chunk != nullptr) { 
        return chunk;
    }

    bool refill_ok  = refillCache();
    if(!refill_ok ) {
        std::cerr << "[CentralHeap::AcquireChunk] WARNING: Failed to refill cache. "
            << "System might be out of memory." << std::endl;
    }

    chunk = FreeChunkManager_ptr->acquire();
    if(chunk != nullptr) { 
        return chunk;
    }

    return nullptr;
}

bool CentralHeap::refillCache() {
    if (FreeChunkManager_ptr->getCacheCount() > 0) {
        return true;
    }

    while(FreeChunkManager_ptr->getCacheCount() <= CentralHeap::kTargetWatermarkInChunks) {
        void* chunk = ChunkAllocatorFromKernel_ptr->allocate(kChunkSize);
        if(!chunk)
            return false;
        FreeChunkManager_ptr->deposit(chunk);
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

    FreeChunkManager_ptr->deposit(chunk);
}

