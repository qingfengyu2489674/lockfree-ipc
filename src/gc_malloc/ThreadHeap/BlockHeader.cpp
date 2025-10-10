// BlockHeader.cpp
#include "gc_malloc/ThreadHeap/BlockHeader.hpp"

// 默认构造：next=nullptr，state=Free
BlockHeader::BlockHeader() noexcept
    : next(nullptr),
      state(static_cast<std::uint64_t>(BlockState::Free)) {}

// 显式构造：next=nullptr，state=指定值
BlockHeader::BlockHeader(BlockState s) noexcept
    : next(nullptr),
      state(static_cast<std::uint64_t>(s)) {}

BlockState BlockHeader::loadState() const noexcept {
    // Acquire 确保读取到与状态写入配套的数据初始化/清理
    const auto v = state.load(std::memory_order_acquire);
    return static_cast<BlockState>(v);
}

void BlockHeader::storeFree() noexcept {
    // Release 发布在释放前对块内容的写入
    state.store(static_cast<std::uint64_t>(BlockState::Free),
                std::memory_order_release);
}

void BlockHeader::storeUsed() noexcept {
    // Release 发布在占用前对块内容的初始化
    state.store(static_cast<std::uint64_t>(BlockState::Used),
                std::memory_order_release);
}
