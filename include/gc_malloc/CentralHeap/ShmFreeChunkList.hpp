#pragma once

#include <list>
#include <mutex>
#include "FreeChunkManager.hpp"

struct FreeNode {
    FreeNode* next;
};

class ShmFreeChunkList : public FreeChunkManager
{
public:
    void* acquire() override;
    void deposit(void* chunk) override;

    size_t getCacheCount() const override;
    void printRemainingChunks();

    ShmFreeChunkList();
    ~ShmFreeChunkList() override;

    ShmFreeChunkList(const ShmFreeChunkList&) = delete;
    ShmFreeChunkList& operator=(const ShmFreeChunkList&) = delete;
    ShmFreeChunkList(ShmFreeChunkList&&) = delete;
    ShmFreeChunkList& operator=(ShmFreeChunkList&&) = delete;
private:
    FreeNode* head_ = nullptr;
    size_t chunk_count_ = 0;
    // mutable ShmMutexLock shm_mutex_;
};