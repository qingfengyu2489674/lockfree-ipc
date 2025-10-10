#pragma once

#include <cstddef>
#include "ShmFreeChunkList.hpp"
#include "ShmChunkAllocator.hpp"
#include "ShareMemory/ShmHeader.hpp"

class ShmChunkAllocator;
class ShmFreeChunkList;

class CentralHeap {
public:
    static CentralHeap& GetInstance(void* shm_base, size_t total_bytes);

    void* acquireChunk(size_t size);
    void releaseChunk(void* chunk, size_t size);

    CentralHeap(const CentralHeap&) = delete;
    CentralHeap& operator=(const CentralHeap&) = delete;
    CentralHeap(CentralHeap&&) = delete;
    CentralHeap& operator=(CentralHeap&&) = delete;

public:
    static constexpr size_t kChunkSize = 2 * 1024 *1024;

private:
    CentralHeap(void* shm_base, size_t region_bytes);
    ~CentralHeap() = delete; 

    bool refillCache(); 
    void setSelfOffset(size_t off);

    ShmChunkAllocator shm_alloc_;
    ShmFreeChunkList shm_free_list_;

    size_t self_off_{0}; // 记录自身在共享内存上的偏移

    static constexpr size_t kTargetWatermarkInChunks = 8;
};

