// src/gc_malloc/ThreadHeap/CentralHeapBootstrap.cpp
#include <atomic>
#include <cassert>
#include "gc_malloc/CentralHeap/CentralHeap.hpp"     // 有 GetInstance 声明
#include "gc_malloc/ThreadHeap/CentralHeapBootstrap.hpp"

std::atomic<CentralHeap*> g_central{nullptr};
thread_local CentralHeap* t_central = nullptr;

void SetupCentral(void* shm_base, std::size_t bytes) {
    CentralHeap& ch = CentralHeap::GetInstance(shm_base, bytes);
    g_central.store(&ch, std::memory_order_release);
}

CentralHeap* getCentral() {
    if (t_central) return t_central;
    CentralHeap* p = g_central.load(std::memory_order_acquire);
    assert(p && "Call SetupCentral(...) before using ThreadHeap");
    t_central = p;
    return p;
}
