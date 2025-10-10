#pragma once

#include <cstdint>
#include <atomic>

enum class BlockState : std::uint64_t {
    Free = 0,
    Used = 1
};

// 块头部：前 16 字节 = [8B 链表指针][8B 状态位]
class alignas(16) BlockHeader {
public:
    BlockHeader* next;                  // 8B：单链表指针
    std::atomic<std::uint64_t> state;   // 8B：状态位（原子或 volatile）

    // 构造与析构
    BlockHeader() noexcept;
    explicit BlockHeader(BlockState s) noexcept;
    ~BlockHeader() = default;

    BlockState loadState() const noexcept;
    void storeFree() noexcept;
    void storeUsed() noexcept;
};
