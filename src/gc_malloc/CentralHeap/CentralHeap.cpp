#include "gc_malloc/CentralHeap/CentralHeap.hpp"

#include "gc_malloc/CentralHeap/AlignedChunkAllocatorByMmap.hpp"
#include "gc_malloc/CentralHeap/FreeChunkListCache.hpp"

#include <new>
#include <type_traits>
#include <cassert>
#include <iostream>

// -----------------------------------------------------------------------------
// 1. 在编译时，在 .bss 段为我们的核心组件预留静态内存。
//    这些全局静态变量保证了它们的生命周期与程序相同，并且不依赖于堆分配。
// -----------------------------------------------------------------------------

static std::aligned_storage<sizeof(AlignedChunkAllocatorByMmap),
                            alignof(AlignedChunkAllocatorByMmap)>::type g_kernel_allocator_buffer;

static std::aligned_storage<sizeof(FreeChunkListCache),
                            alignof(FreeChunkListCache)>::type g_chunk_cache_buffer;


// -----------------------------------------------------------------------------
// 2. CentralHeap 的构造/析构函数和单例实现
// -----------------------------------------------------------------------------

CentralHeap& CentralHeap::GetInstance() {
    // Meyers' Singleton: C++11 保证了函数内静态变量的线程安全初始化。
    // instance 本身也会被存放在 BSS/DATA 段。
    static CentralHeap instance;
    return instance;
}

CentralHeap::CentralHeap() {
    // 使用 placement new 在预留的静态内存上构造组件。
    // 这不会调用全局 malloc/new。
    ChunkAllocatorFromKernel_ptr = new (&g_kernel_allocator_buffer) AlignedChunkAllocatorByMmap();
    FreeChunkCache_ptr = new (&g_chunk_cache_buffer) FreeChunkListCache();
}



CentralHeap::~CentralHeap() {
    // 必须手动、显式地调用【派生类】的析构函数。
    // 我们使用 static_cast，因为我们确切地知道指针背后的真实对象类型。
    if (FreeChunkCache_ptr) {
        static_cast<FreeChunkListCache*>(FreeChunkCache_ptr)->~FreeChunkListCache();
    }
    if (ChunkAllocatorFromKernel_ptr) {
        static_cast<AlignedChunkAllocatorByMmap*>(ChunkAllocatorFromKernel_ptr)->~AlignedChunkAllocatorByMmap();
    }
}


// -----------------------------------------------------------------------------
// 3. CentralHeap 的核心逻辑实现
// -----------------------------------------------------------------------------

void* CentralHeap::acquireChunk(size_t size) {
    assert(size == kChunkSize);

    void* chunk = FreeChunkCache_ptr->acquire();
    if(chunk != nullptr) { 
        return chunk;
    }

    bool refill_ok  = refillCache();
    if(!refill_ok ) {
        std::cerr << "[CentralHeap::AcquireChunk] WARNING: Failed to refill cache. "
            << "System might be out of memory." << std::endl;
    }

    chunk = FreeChunkCache_ptr->acquire();
    if(chunk != nullptr) { 
        return chunk;
    }

    return nullptr;
}

bool CentralHeap::refillCache() {
    if (FreeChunkCache_ptr->getCacheCount() > 0) {
        return true;
    }

    while(FreeChunkCache_ptr->getCacheCount() <= CentralHeap::kTargetWatermarkInChunks) {
        void* chunk = ChunkAllocatorFromKernel_ptr->allocate(kChunkSize);
        if(!chunk)
            return false;
        FreeChunkCache_ptr->deposit(chunk);
    }

    return true;
}

void CentralHeap::releaseChunk(void* chunk, size_t size) {
    assert(size == kChunkSize);

    // if(FreeChunkCache_ptr->getCacheCount() < kMaxWatermarkInChunks) {
    //     FreeChunkCache_ptr->deposit(chunk);
    // } else {
    //     ChunkAllocatorFromKernel_ptr->deallocate(chunk, kChunkSize);
    // }

    FreeChunkCache_ptr->deposit(chunk);
}

