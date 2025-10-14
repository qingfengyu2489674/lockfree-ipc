#include "gc_malloc/CentralHeap/ShmFreeChunkList.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream> 

// ========== 构造 / 析构 ==========

ShmFreeChunkList::ShmFreeChunkList()
    : head_(nullptr),
      chunk_count_(0) {
}

ShmFreeChunkList::~ShmFreeChunkList() {
    head_ = nullptr;
    chunk_count_ = 0;
}

// ========== 业务接口 ==========

void* ShmFreeChunkList::acquire() {
    if (head_ == nullptr) {
        return nullptr; // 没有可用的 chunk
    }

    FreeNode* node = head_;
    head_ = head_->next;  // 更新头部为下一个节点
    --chunk_count_;  // 减少 chunk 数量
    node->next = nullptr;

    return static_cast<void*>(node);  // 返回分配的 chunk
}

void ShmFreeChunkList::deposit(void* chunk) {
    if(!chunk) return;
    auto* node = reinterpret_cast<FreeNode*>(chunk);
    
    node->next = head_;
    head_ = node;
    ++chunk_count_;
}

size_t ShmFreeChunkList::getCacheCount() const {
    return chunk_count_;
}

