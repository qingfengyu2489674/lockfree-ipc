#include "gc_malloc/ThreadHeap/MemSubPool.hpp"
#include <stdexcept>        // 用于 std::runtime_error 等
#include <cstddef>          // 用于 offsetof


// === 1. 所有辅助工具都安全地隐藏在匿名命名空间中 ===
namespace {

    inline size_t align_up(size_t value, size_t alignment) {
        return (value + alignment - 1) & ~(alignment - 1);
    }
}

size_t MemSubPool::calculateDataOffset() {
    const size_t start_of_data_area =
        offsetof(MemSubPool, bitmap_) + sizeof(Bitmap);
        
    return align_up(
        start_of_data_area,
        alignof(std::max_align_t)
    );
}

size_t MemSubPool::calculateTotalBlockCount(size_t block_size, size_t data_offset) {
    const size_t data_area_size = MemSubPool::kPoolTotalSize - data_offset;
    return data_area_size / block_size;
}


MemSubPool::MemSubPool(size_t block_size):
    magic_(kPoolMagic),
    lock_(),
    block_size_(block_size),
    data_offset_(calculateDataOffset()),
    total_block_count_(calculateTotalBlockCount(block_size, data_offset_)),
    used_block_count_(0),
    next_free_block_hint_(0),
    bitmap_buffer_{0},
    bitmap_(total_block_count_, bitmap_buffer_, kBitMapLength)
{
    if (total_block_count_ > kBitMapLength * 8) {
        throw std::logic_error("Calculated total block count exceeds bitmap capacity.");
    }
}

// --- 析构函数 ---
// 在这里执行清理工作。
MemSubPool::~MemSubPool() {
}


// --- 公共接口实现 ---

void* MemSubPool::allocate() {
    std::lock_guard<std::mutex> guard(lock_);

    // 如果已知池已满，可以直接返回。
    if (used_block_count_.load(std::memory_order_relaxed) >= total_block_count_) {
        return nullptr;
    }


    size_t free_block_index = bitmap_.findFirstFree(next_free_block_hint_);
    
    if (free_block_index == Bitmap::k_not_found && next_free_block_hint_ > 0) {
        free_block_index = bitmap_.findFirstFree(0);
    }

    if (free_block_index != Bitmap::k_not_found) {
        bitmap_.markAsUsed(free_block_index);

        used_block_count_.fetch_add(1, std::memory_order_relaxed);
        
        next_free_block_hint_ = free_block_index + 1;

        char* data_start = reinterpret_cast<char*>(this) + data_offset_;
        void* block_ptr = data_start + (free_block_index * block_size_);

        return block_ptr;
    }

    return nullptr;
}


void MemSubPool::release(void* block_ptr) {
    if (block_ptr == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> guard(lock_);

    char* data_start = reinterpret_cast<char*>(this) + data_offset_;
    char* data_end = reinterpret_cast<char*>(this) + kPoolTotalSize;
    char* p = static_cast<char*>(block_ptr);

    // 检查指针是否落在本内存池的数据区内。
    if (p < data_start || p >= data_end) {
        fprintf(stderr, "Error: Attempted to release a pointer outside of this sub-pool's memory range.\n");
        return;
    }
    
    const ptrdiff_t offset = p - data_start;

    // 检查偏移是否是块大小的整数倍。
    if (offset % block_size_ != 0) {
        fprintf(stderr, "Error: Attempted to release a misaligned pointer.\n");
        return;
    }

    const size_t block_index = offset / block_size_;
    
    // 检查位图中对应的位是否已经是“空闲”，如果是，则为重复释放错误。
    if (!bitmap_.isUsed(block_index)) {
        fprintf(stderr, "Error: Double-free detected on block index %zu.\n", block_index);
        return;
    }

    bitmap_.markAsFree(block_index);
    used_block_count_.fetch_sub(1, std::memory_order_relaxed);

}


bool MemSubPool::isFull() const {
    return used_block_count_.load(std::memory_order_relaxed) >= total_block_count_;
}


bool MemSubPool::isEmpty() const {
    return used_block_count_.load(std::memory_order_relaxed) == 0;
}


size_t MemSubPool::getBlockSize() const {
    return block_size_;
}