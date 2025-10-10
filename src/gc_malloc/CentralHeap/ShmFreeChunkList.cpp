#include "gc_malloc/CentralHeap/ShmFreeChunkList.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>

// ========== 构造 / 析构 ==========

ShmFreeChunkList::ShmFreeChunkList()
    : head_(nullptr),
      chunk_count_(0),
      shm_mutex_() {
}

ShmFreeChunkList::~ShmFreeChunkList() {
    std::lock_guard<ShmMutexLock> guard(shm_mutex_);
    head_ = nullptr;
    chunk_count_ = 0;
}

// ========== 业务接口 ==========

void* ShmFreeChunkList::acquire() {
    std::lock_guard<ShmMutexLock> guard(shm_mutex_);
    if (head_ == nullptr) {
        return nullptr; // 没有可用的 chunk
    }

    FreeNode* node = head_;
    head_ = head_->next;
    --chunk_count_;
    node->next = nullptr;
    return static_cast<void*>(node);
}

void ShmFreeChunkList::deposit(void* chunk) {
    if(!chunk) return;
    auto* node = reinterpret_cast<FreeNode*>(chunk);
    
    std::lock_guard<ShmMutexLock> guard(shm_mutex_);
    node->next = head_;
    head_ = node;
    ++chunk_count_;
}

size_t ShmFreeChunkList::getCacheCount() const {
    std::lock_guard<ShmMutexLock> guard(shm_mutex_);
    return chunk_count_;
}

