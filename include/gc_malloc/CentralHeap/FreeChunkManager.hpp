#pragma once

#include <cstddef>

class FreeChunkManager
{
public:
    virtual void* acquire() = 0;
    virtual void deposit(void* chunk) = 0;
    virtual size_t getCacheCount() const = 0;

    virtual ~FreeChunkManager() = default;

    FreeChunkManager(const FreeChunkManager&) = delete;
    FreeChunkManager& operator=(const FreeChunkManager&) = delete;
    FreeChunkManager(FreeChunkManager&&) = delete;
    FreeChunkManager& operator=(FreeChunkManager&&) = delete;

protected:
    FreeChunkManager() = default;
};

