// gc_malloc/Process/ProcessAllocatorContext.hpp
#pragma once
#include <cstddef>
#include <atomic>

class CentralHeap;

class ProcessAllocatorContext {
public:
    static void Setup(void* shm_base, std::size_t bytes);
    static CentralHeap* getCentralHeap();

private:
    ProcessAllocatorContext(void* shm_base, std::size_t bytes);
    std::atomic<CentralHeap*> g_central_{nullptr};
};

