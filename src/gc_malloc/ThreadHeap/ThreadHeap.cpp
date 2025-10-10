#include <new>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <cassert>

#include "gc_malloc/CentralHeap/CentralHeap.hpp"
#include "gc_malloc/ThreadHeap/MemSubPool.hpp"
#include "gc_malloc/ThreadHeap/ThreadHeap.hpp"

// -------------------- 对外公共接口 --------------------

void* ThreadHeap::allocate(std::size_t nbytes) noexcept {
    ThreadHeap& th = local();

    // 大对象：直接走 CentralHeap（整块 chunk）
    if (nbytes > SizeClassConfig::kMaxSmallAlloc) {
        return CentralHeap::GetInstance().acquireChunk(SizeClassConfig::kChunkSizeBytes);
    }

    // 小对象：映射到 size-class
    const std::size_t class_idx = sizeToClass_(nbytes);
    void* block_ptr = at(th.managers_storage_[class_idx]).allocateBlock();
    if (!block_ptr) return nullptr;

    auto* hdr = static_cast<BlockHeader*>(block_ptr);
    th.attachUsed(hdr);
    return block_ptr;
}

void ThreadHeap::deallocate(void* ptr) noexcept {
    if (!ptr) return;
    static_cast<BlockHeader*>(ptr)->storeFree();  // 跨线程释放只改状态
}

std::size_t ThreadHeap::garbageCollect(std::size_t max_scan) noexcept {
    return local().reclaimBatch(max_scan);
}

// -------------------- 内部实现（TLS / 构造 / 回调桥） --------------------

ThreadHeap& ThreadHeap::local() noexcept {
    static thread_local ThreadHeap tls_instance;
    return tls_instance;
}

ThreadHeap::ThreadHeap() noexcept {
    for (std::size_t i = 0; i < k_class_count; ++i) {
        const std::size_t bs = SizeClassConfig::ClassToSize(i);
        void* slot = static_cast<void*>(&managers_storage_[i]);
        new (slot) SizeClassPoolManager(bs);

        // 回调 ctx 传回自身存储地址，回调里用 at(*ptr) 还原引用
        at(managers_storage_[i]).setRefillCallback(&ThreadHeap::refillFromCentral_cb, /*ctx=*/&managers_storage_[i]);
        at(managers_storage_[i]).setReturnCallback(&ThreadHeap::returnToCentral_cb,   /*ctx=*/&managers_storage_[i]);
    }
}

ThreadHeap::~ThreadHeap() {
    for (std::size_t i = 0; i < k_class_count; ++i) {
        at(managers_storage_[i]).~SizeClassPoolManager();
    }
}

std::size_t ThreadHeap::sizeToClass_(std::size_t nbytes) noexcept {
    return SizeClassConfig::SizeToClass(nbytes);
}

// ---- 与 SizeClassPoolManager 的回调桥 ----

MemSubPool* ThreadHeap::refillFromCentral_cb(void* ctx) noexcept {
    auto* storage_ptr = static_cast<ManagerStorage*>(ctx);
    SizeClassPoolManager& mgr = at(*storage_ptr);
    const std::size_t block_size = mgr.getBlockSize();

    void* raw = CentralHeap::GetInstance().acquireChunk(SizeClassConfig::kChunkSizeBytes);
    if (!raw) return nullptr;

    return new (raw) MemSubPool(block_size);
}

void ThreadHeap::returnToCentral_cb(void* /*ctx*/, MemSubPool* p) noexcept {
    if (!p) return;
    p->~MemSubPool();
    CentralHeap::GetInstance().releaseChunk(static_cast<void*>(p), SizeClassConfig::kChunkSizeBytes);
}

// -------------------- 小工具 --------------------

void ThreadHeap::attachUsed(BlockHeader* blk) noexcept {
    if (!blk) return;
    managed_list_.appendUsed(blk);
}

std::size_t ThreadHeap::reclaimBatch(std::size_t max_scan) noexcept {
    std::size_t reclaimed = 0;
    std::size_t scanned   = 0;

    managed_list_.resetCursor();

    while (scanned < max_scan) {
        BlockHeader* freed = managed_list_.reclaimNextFree();
        if (!freed) break;
        ++scanned;

        void* user_ptr = static_cast<void*>(freed);

        bool released = false;
        for (std::size_t i = 0; i < k_class_count; ++i) {
            if (at(managers_storage_[i]).releaseBlock(user_ptr)) {
                released = true;
                break;
            }
        }
        assert(released && "reclaimBatch: block not owned by any SizeClassPoolManager");

        if (released) ++reclaimed;
    }

    return reclaimed;
}
