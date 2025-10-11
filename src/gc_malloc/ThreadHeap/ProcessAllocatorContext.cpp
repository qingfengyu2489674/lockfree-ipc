// gc_malloc/Process/ProcessAllocatorContext.cpp
#include "gc_malloc/ThreadHeap/ProcessAllocatorContext.hpp"
#include "gc_malloc/CentralHeap/CentralHeap.hpp"
#include <cassert>
#include <mutex>   // std::once_flag, std::call_once

namespace {
    // 单例指针与一次性构造哨兵
    std::atomic<ProcessAllocatorContext*> g_ctx{nullptr};
    std::once_flag g_ctx_once;
}

void ProcessAllocatorContext::Setup(void* shm_base, std::size_t bytes) {
    std::call_once(g_ctx_once, [=] {
        // 进程生命周期内常驻；无须析构
        auto* ctx = new ProcessAllocatorContext(shm_base, bytes);
        g_ctx.store(ctx, std::memory_order_release);
    });
}

CentralHeap* ProcessAllocatorContext::getCentralHeap() {
    auto* ctx = g_ctx.load(std::memory_order_acquire);
    assert(ctx && "Call ProcessAllocatorContext::Setup(...) before use");
    CentralHeap* p = ctx->g_central_.load(std::memory_order_acquire);
    assert(p && "CentralHeap not published");
    return p;
}

ProcessAllocatorContext::ProcessAllocatorContext(void* shm_base, std::size_t bytes) {
    // 由共享头状态机保证“唯一初始化者”
    CentralHeap& ch = CentralHeap::GetInstance(shm_base, bytes);
    g_central_.store(&ch, std::memory_order_release);
}
