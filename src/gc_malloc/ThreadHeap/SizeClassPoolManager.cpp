// SizeClassPoolManager.cpp
// 水位策略子池管理器：不直接分配/回收 2MB，改由回调处理；本类仅做三链迁移与水位维护。

#include "gc_malloc/ThreadHeap/SizeClassPoolManager.hpp"
#include "gc_malloc/ThreadHeap/MemSubPool.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>

// ===================== 构造 / 析构 =====================

SizeClassPoolManager::SizeClassPoolManager(std::size_t block_size) noexcept
    : block_size_(block_size)
{}

SizeClassPoolManager::~SizeClassPoolManager()
{}

// ===================== 回调设置 =====================

void SizeClassPoolManager::setRefillCallback(RefillCallback cb, void* ctx) noexcept {
    refill_cb_  = cb;
    refill_ctx_ = ctx;
}

void SizeClassPoolManager::setReturnCallback(ReturnCallback cb, void* ctx) noexcept {
    return_cb_  = cb;
    return_ctx_ = ctx;
}



// ===================== 分配 / 释放 =====================

void* SizeClassPoolManager::allocateBlock() noexcept {
    // 若 partial 与 empty 都空，先尝试按水位补齐空闲
    if (partial_.empty() && empty_.empty()) {
        refillEmptyPools();
    }

    // 选取可用子池（会从对应链表 pop 出来）
    MemSubPool* pool = acquireUsablePool();
    if (!pool) return nullptr;

    void* block = pool->allocate();
    if (!block) {
        // 理论上不应发生（我们刚从 empty/partial 取出），稳妥起见放回合适链表
        if (pool->isEmpty())       
            empty_.pusFront(pool);
        else if (pool->isFull())   
            full_.pusFront(pool);
        else                       
            partial_.pusFront(pool);
        return nullptr;
    }

    // 分配后迁移：满 -> full；否则 -> partial
    if (pool->isFull()) 
        full_.pusFront(pool);
    else                
        partial_.pusFront(pool);

    return block;
}

bool SizeClassPoolManager::releaseBlock(void* ptr) noexcept {
    if (!ptr) return true;

    MemSubPool* pool = ptrToOwnerPool(ptr);
    // 简易校验：块大小是否匹配（不能完全证明属于本管理器，但可过滤大部分误用）
    if (!pool || pool->getBlockSize() != block_size_) {
        return false;
    }

    // 释放前记录是否处于 full（据此确定其原所在链，无需额外“位置标签”）
    const bool was_full = pool->isFull();

    // 执行释放
    pool->release(ptr);

    // 从旧链摘除并插入新链（按照释放后的状态）
    if (was_full) {
        // 必然在 full_ 链
        MemSubPool* removed = full_.remove(pool);
        (void)removed; // 仅用于调试期校验
        assert(removed == pool);
    } else {
        // 必然在 partial_ 链（empty_ 不可能持有在用块）
        MemSubPool* removed = partial_.remove(pool);
        (void)removed;
        assert(removed == pool);
    }

    if (pool->isEmpty()) {
        empty_.pusFront(pool);
        // 空闲增加后，若超高水位则回落至目标水位
        trimEmptyPools();
    } else {
        partial_.pusFront(pool);
    }

    return true;
}

// ===================== 统计 / 查询 =====================

std::size_t SizeClassPoolManager::getBlockSize() const noexcept {
    return block_size_;
}

std::size_t SizeClassPoolManager::getPoolCountEmpty() const noexcept {
    return empty_.size();
}

std::size_t SizeClassPoolManager::getPoolCountPartial() const noexcept {
    return partial_.size();
}

std::size_t SizeClassPoolManager::getPoolCountFull() const noexcept {
    return full_.size();
}

bool SizeClassPoolManager::ownsPointer(const void* ptr) const noexcept {
    // 简化策略：根据 2MB 对齐找到“可能的”所属子池，并校验块大小。
    // 这不能 100% 保证属于“本管理器”，但在不引入额外索引结构的前提下足够实用。
    const MemSubPool* p = ptrToOwnerPool(ptr);
    return p && (p->getBlockSize() == block_size_);
}

// ===================== 内部辅助 =====================

inline bool SizeClassPoolManager::poolIsEmpty(const MemSubPool* p) noexcept {
    return p->isEmpty();
}

inline bool SizeClassPoolManager::poolIsFull(const MemSubPool* p) noexcept {
    return p->isFull();
}

MemSubPool* SizeClassPoolManager::ptrToOwnerPool(const void* block_ptr) noexcept {
    if (!block_ptr) return nullptr;
    auto addr = reinterpret_cast<std::uintptr_t>(block_ptr);
    const std::uintptr_t mask = static_cast<std::uintptr_t>(MemSubPool::kPoolTotalSize) - 1;
    auto base = addr & ~mask;
    return reinterpret_cast<MemSubPool*>(base);
}

// —— 水位控制 ——

void SizeClassPoolManager::refillEmptyPools() noexcept {
    if (!empty_.empty()) return;
    if (!refill_cb_)     return;

    while (empty_.size() < kTargetEmptyWatermark) {
        MemSubPool* p = refill_cb_(refill_ctx_);
        if (!p) break;

        // 要求：回调返回的子池应当是“未挂链且为空闲”的
        assert(p->list_prev == nullptr && p->list_next == nullptr);
        // assert(p->IsEmpty());
        // assert(p->GetBlockSize() == block_size_);

        empty_.pusFront(p);
    }
}

void SizeClassPoolManager::trimEmptyPools() noexcept {
    if (!return_cb_) return;

    while (empty_.size() > kHighEmptyWatermark) {
        MemSubPool* p = empty_.popFront();
        if (!p) break; // 理论上不会发生
        return_cb_(return_ctx_, p);
    }
}

// —— 选择可用子池 ——

MemSubPool* SizeClassPoolManager::acquireUsablePool() noexcept {
    if (!partial_.empty()) {
        return partial_.popFront();
    }

    if (empty_.empty()) {
        refillEmptyPools();
    }
    if (!empty_.empty()) {
        return empty_.popFront();
    }

    return nullptr;
}
