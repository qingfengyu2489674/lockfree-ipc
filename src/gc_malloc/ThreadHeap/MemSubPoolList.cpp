// MemSubPoolList.cpp
// 精简版实现：仅支持头插、弹头、按节点摘除；全部 O(1)。

#include "gc_malloc/ThreadHeap/MemSubPoolList.hpp"
#include "gc_malloc/ThreadHeap/MemSubPool.hpp"
#include <cassert>

// 内部小工具：重置节点的侵入式指针
namespace {
inline void resetLinks(MemSubPool* node) noexcept {
    if (!node) return;
    node->list_prev = nullptr;
    node->list_next = nullptr;
}
} // namespace

// ===== 构造 =====
MemSubPoolList::MemSubPoolList() noexcept
    : head_(nullptr), tail_(nullptr), size_(0) {}

// ===== 基本查询 =====
bool MemSubPoolList::empty() const noexcept {
    // 若为空，头尾应同时为 nullptr
    assert((size_ == 0) ? (head_ == nullptr && tail_ == nullptr) : true);
    return size_ == 0;
}

std::size_t MemSubPoolList::size() const noexcept {
    return size_;
}

MemSubPool* MemSubPoolList::front() const noexcept {
    return head_;
}

// ===== 修改操作 =====
void MemSubPoolList::pusFront(MemSubPool* node) noexcept {
    assert(node && "push_front: node 不能为 nullptr");
    // 要求：节点当前未在任一链表中（调用方应保证）
    assert(node->list_prev == nullptr && node->list_next == nullptr);

    if (empty()) {
        head_ = tail_ = node;
        // 显式置空，便于调试
        node->list_prev = nullptr;
        node->list_next = nullptr;
    } else {
        node->list_prev = nullptr;
        node->list_next = head_;
        head_->list_prev = node;
        head_ = node;
    }
    ++size_;
}

MemSubPool* MemSubPoolList::popFront() noexcept {
    if (empty()) return nullptr;

    MemSubPool* old = head_;
    head_ = old->list_next;

    if (head_) {
        head_->list_prev = nullptr;
    } else {
        // 弹出后为空
        tail_ = nullptr;
    }

    resetLinks(old);
    --size_;
    return old;
}

MemSubPool* MemSubPoolList::remove(MemSubPool* node) noexcept {
    // 调用方保证 node 属于本链表；这里做最小防御。
    if (!node) return nullptr;

    // 四种情况分别处理
    if (node == head_ && node == tail_) {
        // 单节点
        head_ = tail_ = nullptr;
    } else if (node == head_) {
        // 头结点（至少有两个节点）
        head_ = node->list_next;
        assert(head_ && "逻辑应保证存在新头结点");
        head_->list_prev = nullptr;
    } else if (node == tail_) {
        // 尾结点（至少有两个节点）
        tail_ = node->list_prev;
        assert(tail_ && "逻辑应保证存在新尾结点");
        tail_->list_next = nullptr;
    } else {
        // 中间节点
        MemSubPool* p = node->list_prev;
        MemSubPool* n = node->list_next;
        assert(p && n);
        p->list_next = n;
        n->list_prev = p;
    }

    resetLinks(node);
    assert(size_ > 0);
    --size_;
    return node;
}
