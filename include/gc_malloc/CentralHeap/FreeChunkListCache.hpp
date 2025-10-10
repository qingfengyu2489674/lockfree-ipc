#pragma once

#include <list>
#include <mutex>
#include "FreeChunkCache.hpp"

struct FreeNode {
    FreeNode* next;
};

class FreeChunkListCache : public FreeChunkCache
{
public:
    void* acquire() override;
    void deposit(void* chunk) override;

    size_t getCacheCount() const override;

    FreeChunkListCache() = default;
    ~FreeChunkListCache() override = default;

    FreeChunkListCache(const FreeChunkListCache&) = delete;
    FreeChunkListCache& operator=(const FreeChunkListCache&) = delete;
    FreeChunkListCache(FreeChunkListCache&&) = delete;
    FreeChunkListCache& operator=(FreeChunkListCache&&) = delete;
private:
    FreeNode* head_ = nullptr;
    size_t chunk_count_ = 0;
    mutable std::mutex mutex_;
};