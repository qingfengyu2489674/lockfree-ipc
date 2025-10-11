#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <assert.h>

#include "gc_malloc/CentralHeap/ChunkAllocatorFromKernel.hpp"
#include "gc_malloc/CentralHeap/ShmChunkAllocator.hpp"



ShmChunkAllocator::ShmChunkAllocator(void* shm_base, size_t region_bytes)
    : shm_base_(static_cast<unsigned char*>(shm_base)),
      region_bytes_(region_bytes)
{
    assert(shm_base != nullptr && "shm_base must not be null !");

    const uintptr_t base_u    = reinterpret_cast<uintptr_t>(shm_base_);
    const uintptr_t aligned_u = (base_u + (kAlignmentSize - 1)) & ~(uintptr_t)(kAlignmentSize - 1);
    const size_t    lead      = static_cast<size_t>(aligned_u - base_u);

    if (region_bytes_ <= lead) {
        // 对齐后没有空间了
        base_aligned_  = nullptr;
        bytes_aligned_ = 0;
        total_chunks_  = 0;
        next_chunk_idx_.store(0, std::memory_order_relaxed);
        return;
    }

    size_t remain = region_bytes_ - lead;
    // 向下取整到 2MB 的整数倍
    remain &= ~(kAlignmentSize - 1);

    base_aligned_  = reinterpret_cast<unsigned char*>(aligned_u);
    bytes_aligned_ = remain;
    total_chunks_  = bytes_aligned_ / kAlignmentSize;
    next_chunk_idx_.store(0, std::memory_order_relaxed);

    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                  "atomic<uint64_t> must be lock-free on this platform");
}



void* ShmChunkAllocator::allocate(size_t size) {
    if (size == 0 || total_chunks_ == 0 || bytes_aligned_ == 0) {
        return nullptr;
    }
    // 需要的 2MB 块数
    const size_t need_chunks = (size + kAlignmentSize - 1) / kAlignmentSize;
    if (need_chunks == 0) return nullptr; // 防御，理论上进不到

    uint64_t old_index = next_chunk_idx_.load(std::memory_order_acquire);
    while (true) {
        if (old_index + need_chunks > total_chunks_) {
            return nullptr; // 容量不足
        }
        if (next_chunk_idx_.compare_exchange_weak(
                old_index, old_index + need_chunks,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {

            const size_t byte_offset = static_cast<size_t>(old_index) * kAlignmentSize;
            return static_cast<void*>(base_aligned_ + byte_offset);
        }
    }
}


void ShmChunkAllocator::deallocate(void* ptr, size_t size) {
    // bump-only：不做局部回收
    // 复用策略放在上层 CentralHeap 的自由链表中
}

// 便捷查询
void* ShmChunkAllocator::getShmBase() const noexcept {
    return static_cast<void*>(shm_base_);
}

std::size_t ShmChunkAllocator::getRegionBytes() const noexcept {
    return region_bytes_;
}

std::size_t ShmChunkAllocator::getTotalChunks() const noexcept {
    return total_chunks_;
}

std::size_t ShmChunkAllocator::getUsedChunks() const noexcept {
    return static_cast<std::size_t>(
        next_chunk_idx_.load(std::memory_order_acquire));
}