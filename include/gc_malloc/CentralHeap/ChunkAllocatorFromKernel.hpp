#pragma once

#include <cstddef>

class ChunkAllocatorFromKernel {
public:
    virtual void* allocate(size_t size) = 0;
    virtual void deallocate(void* ptr,size_t size) = 0;

    virtual ~ChunkAllocatorFromKernel() = default;

    ChunkAllocatorFromKernel(const ChunkAllocatorFromKernel&) = delete;
    ChunkAllocatorFromKernel& operator=(const ChunkAllocatorFromKernel&) = delete;
    ChunkAllocatorFromKernel(ChunkAllocatorFromKernel&&) = delete;
    ChunkAllocatorFromKernel& operator=(ChunkAllocatorFromKernel&&) = delete;

protected:
    ChunkAllocatorFromKernel() = default;
};
