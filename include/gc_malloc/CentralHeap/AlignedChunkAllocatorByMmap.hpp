#pragma once

#include <cstddef>

#include "ChunkAllocatorFromKernel.hpp"

class AlignedChunkAllocatorByMmap : public ChunkAllocatorFromKernel {
public:
    void* allocate(size_t size) override;
    void deallocate(void* ptr, size_t size) override;

    AlignedChunkAllocatorByMmap() = default;
    ~AlignedChunkAllocatorByMmap() override = default;

    AlignedChunkAllocatorByMmap(const AlignedChunkAllocatorByMmap&) = delete;
    AlignedChunkAllocatorByMmap& operator=(const AlignedChunkAllocatorByMmap&) = delete;
    AlignedChunkAllocatorByMmap(AlignedChunkAllocatorByMmap&&) = delete;
    AlignedChunkAllocatorByMmap& operator=(AlignedChunkAllocatorByMmap&&) = delete;
private:
    static constexpr size_t kAlignmentSize = 2 * 1024 * 1024; // 2MB
};
