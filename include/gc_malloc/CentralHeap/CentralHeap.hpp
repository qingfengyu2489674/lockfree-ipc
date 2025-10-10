#pragma once

#include <cstddef>

class ChunkAllocatorFromKernel;
class FreeChunkCache;

class CentralHeap {
public:
    static CentralHeap& GetInstance();

    void* acquireChunk(size_t size);
    void releaseChunk(void* chunk, size_t size);

    CentralHeap(const CentralHeap&) = delete;
    CentralHeap& operator=(const CentralHeap&) = delete;
    CentralHeap(CentralHeap&&) = delete;
    CentralHeap& operator=(CentralHeap&&) = delete;

public:
    static constexpr size_t kChunkSize = 2 * 1024 *1024;

private:
    CentralHeap();
    virtual ~CentralHeap(); 

    bool refillCache(); 

    ChunkAllocatorFromKernel* ChunkAllocatorFromKernel_ptr = nullptr;
    FreeChunkCache* FreeChunkCache_ptr = nullptr;

    static constexpr size_t kMaxWatermarkInChunks = 16;
    static constexpr size_t kTargetWatermarkInChunks = 8;
};

