#pragma once

#include <cstddef>

class FreeChunkCache
{
public:
    virtual void* acquire() = 0;
    virtual void deposit(void* chunk) = 0;
    virtual size_t getCacheCount() const = 0;

    virtual ~FreeChunkCache() = default;

    FreeChunkCache(const FreeChunkCache&) = delete;
    FreeChunkCache& operator=(const FreeChunkCache&) = delete;
    FreeChunkCache(FreeChunkCache&&) = delete;
    FreeChunkCache& operator=(FreeChunkCache&&) = delete;

protected:
    FreeChunkCache() = default;
};

