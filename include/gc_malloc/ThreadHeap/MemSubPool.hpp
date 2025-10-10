#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <new>

#include "gc_malloc/ThreadHeap/Bitmap.hpp"

constexpr size_t CACHE_LINE_SIZE = 64;

class alignas(CACHE_LINE_SIZE) MemSubPool {
public:
    static constexpr size_t kPoolTotalSize = 2 * 1024 * 1024; // 2MB
    static constexpr size_t kPoolAlignment = kPoolTotalSize;
    static constexpr size_t kMinBlockSize = 32; 
    static constexpr size_t kBitMapLength = (kPoolTotalSize / kMinBlockSize + 7) / 8;
    static constexpr uint32_t kPoolMagic = 0xDEADBEEF;

public:
    explicit MemSubPool(size_t block_size);
    virtual ~MemSubPool();

    void* allocate();
    void release(void* block_ptr);

    bool isFull() const;
    bool isEmpty() const;
    size_t getBlockSize() const;

public:
    MemSubPool* list_prev = nullptr;
    MemSubPool* list_next = nullptr;

private:
    static size_t calculateDataOffset();
    static size_t calculateTotalBlockCount(size_t block_size, size_t data_offset);

    MemSubPool(const MemSubPool&) = delete;
    MemSubPool& operator=(const MemSubPool&) = delete;
    MemSubPool(MemSubPool&&) = delete;
    MemSubPool& operator=(MemSubPool&&) = delete;

private:
    const uint32_t magic_;
    std::mutex lock_;

    const size_t block_size_;
    const size_t data_offset_;
    const size_t total_block_count_;
    std::atomic<size_t> used_block_count_;
    size_t next_free_block_hint_;

    unsigned char bitmap_buffer_[kBitMapLength];
    Bitmap bitmap_;
};