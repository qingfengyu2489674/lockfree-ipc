#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <assert.h>

#include "gc_malloc/CentralHeap/ChunkAllocatorFromKernel.hpp"
#include "gc_malloc/CentralHeap/Shm2MBChunkAllocator.hpp"



Shm2MBChunkAllocator::Shm2MBChunkAllocator(void* shm_base, size_t region_bytes)
    : shm_base_(static_cast<unsigned char*>(shm_base)),
      region_bytes_(region_bytes)
{
    assert(shm_base != nullptr && "shm_base must not be null !");
    assert(reinterpret_cast<uintptr_t>(shm_base) % kAlignmentSize == 0 && "shm_base must be 2MB-aligned !");
    assert(region_bytes % kAlignmentSize == 0 && "region_bytes must be 2MB-aligned !");

    total_chunks_ = region_bytes / kAlignmentSize;
    next_chunk_idx_.store(0, std::memory_order_relaxed);

    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "atomic<uint64_t> must be lock-free on this platform");
}

void* Shm2MBChunkAllocator::allocate(size_t size) {
    if(size == 0 || total_chunks_ == 0){
        return nullptr;
    }

    const size_t need_index = (size + kAlignmentSize - 1) / kAlignmentSize;
    uint64_t old_index = next_chunk_idx_.load(std::memory_order_acquire);

    while(true) {
        if(old_index + need_index > total_chunks_) {
            return nullptr;
        }

        if(next_chunk_idx_.compare_exchange_weak(
            old_index, old_index + need_index,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) 
        {
            const size_t byte_offset = static_cast<size_t>(old_index) * kAlignmentSize;
            return static_cast<void*>(shm_base_ + byte_offset);
        }
    }
}

void Shm2MBChunkAllocator::deallocate(void* ptr, size_t size) {
    // bump-only：不做局部回收
    // 复用策略放在上层 CentralHeap 的自由链表中
}

// 便捷查询
void* Shm2MBChunkAllocator::getShmBase() const noexcept {
    return static_cast<void*>(shm_base_);
}

std::size_t Shm2MBChunkAllocator::getRegionBytes() const noexcept {
    return region_bytes_;
}

std::size_t Shm2MBChunkAllocator::getTotalChunks() const noexcept {
    return total_chunks_;
}

std::size_t Shm2MBChunkAllocator::getUsedChunks() const noexcept {
    return static_cast<std::size_t>(
        next_chunk_idx_.load(std::memory_order_acquire));
}