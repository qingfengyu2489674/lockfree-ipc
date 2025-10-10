#include "gc_malloc/CentralHeap/FreeChunkListCache.hpp"
#include <cassert>

void* FreeChunkListCache::acquire() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (head_ == nullptr) {
        assert(chunk_count_ == 0);
        return nullptr;
    }
    
    FreeNode* node_to_return = head_;
    
    head_ = head_->next;

    chunk_count_--;

    return static_cast<void*>(node_to_return);
}

void FreeChunkListCache::deposit(void* chunk) {
    if (chunk == nullptr) {
        return;
    }

    FreeNode* new_node = static_cast<FreeNode*>(chunk);

    std::lock_guard<std::mutex> lock(mutex_);

    new_node->next = head_;
    head_ = new_node;

    chunk_count_++;
}


size_t FreeChunkListCache::getCacheCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return chunk_count_;
}
