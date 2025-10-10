// ManagedList.cpp
#include "gc_malloc/ThreadHeap/ManagedList.hpp"

ManagedList::ManagedList() noexcept
    : head_(nullptr),
      tail_(nullptr),
      cursor_prev_(nullptr),
      cursor_cur_(nullptr) {}

void ManagedList::appendUsed(BlockHeader* blk) noexcept {
    if (!blk) return;
    blk->storeUsed();
    blk->next = nullptr;

    if (!head_) {
        head_ = tail_ = blk;
        // 若游标尚未初始化，保持为空；由 reset_cursor() 显式开启一轮遍历
        return;
    }

    tail_->next = blk;
    tail_ = blk;
}

BlockHeader* ManagedList::reclaimNextFree() noexcept {
    // 如果游标未设置，认为没有开启遍历
    if (!cursor_cur_) return nullptr;

    while (cursor_cur_) {
        BlockHeader* cur = cursor_cur_;
        BlockHeader* nxt = cur->next;

        if (cur->loadState() == BlockState::Free) {
            // 从链表中摘除 cur
            if (cursor_prev_) {
                cursor_prev_->next = nxt;
            } else {
                // 移除的是头结点
                head_ = nxt;
            }

            if (cur == tail_) {
                // 更新尾指针
                tail_ = cursor_prev_;
            }

            // 断开 cur->next，返回给上层回收处理
            cur->next = nullptr;

            // 游标前驱不变（仍指向摘除前的前驱）
            cursor_cur_ = nxt;
            return cur;
        }

        // 未释放：向前推进游标
        cursor_prev_ = cur;
        cursor_cur_  = nxt;
    }

    // 遍历到末尾未发现可回收块
    return nullptr;
}

void ManagedList::resetCursor() noexcept {
    cursor_prev_ = nullptr;
    cursor_cur_  = head_;
}

bool ManagedList::empty() const noexcept {
    return head_ == nullptr;
}

BlockHeader* ManagedList::head() const noexcept {
    return head_;
}

BlockHeader* ManagedList::tail() const noexcept {
    return tail_;
}
