#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>

#include "ChunkAllocatorFromKernel.hpp"

class ShmChunkAllocator : public ChunkAllocatorFromKernel {
public:
    void* allocate(size_t size) override;
    void deallocate(void* ptr, size_t size) override;

    explicit ShmChunkAllocator(void* shm_base,
                                size_t region_bytes);

    ~ShmChunkAllocator() override = default;

    // 便捷查询（仅声明）
    void*        getShmBase() const noexcept;
    size_t  getRegionBytes() const noexcept;
    size_t  getTotalChunks() const noexcept;
    size_t  getUsedChunks() const noexcept;


    ShmChunkAllocator(const ShmChunkAllocator&) = delete;
    ShmChunkAllocator& operator=(const ShmChunkAllocator&) = delete;
    ShmChunkAllocator(ShmChunkAllocator&&) = delete;
    ShmChunkAllocator& operator=(ShmChunkAllocator&&) = delete;
private:
    static constexpr size_t kAlignmentSize = 2 * 1024 * 1024; // 2MB

    // --- 关键字段 ---
    alignas(64) std::atomic<std::uint64_t> next_chunk_idx_{0}; // “下一块”的序号（index），CAS/fetch_add 推进
    unsigned char* shm_base_   = nullptr;   // 本进程视角的映射基址（注意：跨进程请用偏移传递）
    size_t    region_bytes_ = 0;       // 整个映射区域大小（字节）
    size_t    total_chunks_ = 0;       // 可用 2MB 块总数 = (region_bytes - header_bytes)/kAlignmentSize
};
