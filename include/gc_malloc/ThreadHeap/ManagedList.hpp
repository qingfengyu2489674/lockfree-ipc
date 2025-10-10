#pragma once

#include <cstdint>
#include "BlockHeader.hpp"

// 单链表：管理已分配的块
// 特性：单线程遍历回收，无需加锁
class ManagedList {
public:
    ManagedList() noexcept;
    virtual ~ManagedList() = default;

    ManagedList(const ManagedList&) = delete;
    ManagedList& operator=(const ManagedList&) = delete;
    ManagedList(ManagedList&&) = delete;
    ManagedList& operator=(ManagedList&&) = delete;

    // 尾插块
    void appendUsed(BlockHeader* blk) noexcept;

    // 从游标位置开始，摘除并返回下一个空闲块
    BlockHeader* reclaimNextFree() noexcept;

    // 重置游标到链表头
    void resetCursor() noexcept;

    // 状态查询
    bool empty() const noexcept;
    BlockHeader* head() const noexcept;
    BlockHeader* tail() const noexcept;

private:
    BlockHeader* head_ = nullptr;
    BlockHeader* tail_ = nullptr;
    BlockHeader* cursor_prev_ = nullptr;
    BlockHeader* cursor_cur_  = nullptr;
};
