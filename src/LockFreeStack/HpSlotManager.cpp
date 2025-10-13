// LockFreeStack/HpSlotManager.cpp
#include "LockFreeStack/HpSlotManager.hpp"
#include "LockFreeStack/HpSlot.hpp"
#include "gc_malloc/ThreadHeap/ThreadHeap.hpp"
#include <new>

// ============ 小工具：用 ThreadHeap 构造/销毁对象 ============
namespace {
template <class T, class... Args>
T* thNew(Args&&... args) {
    void* mem = ThreadHeap::allocate(sizeof(T));
    // 如果你的分配器需要更强对齐，可改为按 alignof(T) 分配
    return ::new (mem) T(std::forward<Args>(args)...);
}

template <class T>
void thDelete(T* p) noexcept {
    if (!p) return;
    p->~T();
    ThreadHeap::deallocate(p);
}
} // namespace

// ====================== 模板成员实现 ======================

template <class Node>
HpSlotManager<Node>::~HpSlotManager() {
    std::lock_guard<ShmMutexLock> lk(shm_mutx_);
    SlotNode* cur = head_;
    head_ = nullptr;

    while (cur) {
        SlotNode* next = cur->next;
        // 注意：若 slot->retired_head 还有残留，应该在上层 GC 先 drainAll() 再销毁
        thDelete(cur->slot);
        thDelete(cur);
        cur = next;
    }
    // TLS 的 tls_slot_ 是每线程变量，这里不访问/清空
}

template <class Node>
HpSlot<Node>* HpSlotManager<Node>::acquireTls() {
    if (tls_slot_ != nullptr) return tls_slot_;

    // 用 ThreadHeap 分配并构造
    auto* slot = thNew<HpSlot<Node>>();
    auto* node = thNew<typename HpSlotManager<Node>::SlotNode>();
    node->slot = slot;
    node->next = nullptr;

    {
        std::lock_guard<ShmMutexLock> lk(shm_mutx_);
        linkHead_(node);
    }

    tls_slot_ = slot;
    return tls_slot_;
}

template <class Node>
void HpSlotManager<Node>::unregisterTls() {
    HpSlot<Node>* s = tls_slot_;
    if (!s) return;

    // 从全局链表摘除对应的 SlotNode，并释放它
    {
        std::lock_guard<ShmMutexLock> lk(shm_mutx_);
        unlinkUnlocked_(s);
    }

    // 释放槽位对象本身
    thDelete(s);
    tls_slot_ = nullptr;
}

template <class Node>
std::size_t HpSlotManager<Node>::getShotCount() const {
    std::lock_guard<ShmMutexLock> lk(shm_mutx_);
    std::size_t n = 0;
    for (SlotNode* p = head_; p; p = p->next) ++n;
    return n;
}

// 在 HpSlotManager.cpp 中补充：snapshotHazardpoints 的模板实现
template <class Node>
void HpSlotManager<Node>::snapshotHazardpoints(std::vector<const Node*>& out) const {
    std::lock_guard<ShmMutexLock> lk(shm_mutx_);
    for (SlotNode* p = head_; p; p = p->next) {
        HpSlot<Node>* slot = p->slot;
        if (!slot) continue;
        Node* h = slot->hazard_ptr.load(std::memory_order_acquire);
        if (h) out.push_back(h);
    }
}

// ====================== 新增：flushAllRetiredTo ======================
template <class Node>
std::size_t HpSlotManager<Node>::flushAllRetiredTo(std::atomic<Node*>& dst_head) noexcept {
    // 1) 锁内快照槽位指针列表
    std::vector<HpSlot<Node>*> slots;
    {
        std::lock_guard<ShmMutexLock> lk(shm_mutx_);
        slots.reserve(128);
        for (SlotNode* p = head_; p; p = p->next) {
            if (p->slot) slots.push_back(p->slot);
        }
    }

    std::size_t total = 0;

    // 2) 锁外逐槽位“原子摘取”退休链，并段插到 dst_head
    for (HpSlot<Node>* s : slots) {
        if (!s) continue;

        // 假设 HpSlot<Node> 暴露了 retired_head（std::atomic<Node*>）
        Node* segHead = s->retired_head.exchange(nullptr, std::memory_order_acq_rel);
        if (!segHead) continue;

        // 统计段长 & 找到段尾
        std::size_t cnt = 0;
        Node* segTail = segHead;
        ++cnt;
        while (segTail->next) {
            segTail = segTail->next;
            ++cnt;
        }

        // 段插：segTail->next = oldHead; CAS(dst_head, oldHead, segHead)
        Node* oldHead = dst_head.load(std::memory_order_acquire);
        do {
            segTail->next = oldHead;
        } while (!dst_head.compare_exchange_weak(
                     oldHead, segHead,
                     std::memory_order_release,
                     std::memory_order_acquire));

        total += cnt;
    }

    return total;
}


template <class Node>
void HpSlotManager<Node>::retire(Node* n) noexcept {
    if (!n) return;
    HpSlot<Node>* s = acquireTls();   // 确保拿到本线程的槽位
    s->pushRetired(n);                // CAS 头插到 retired_head
}

// 一次性退休一段链（head 的 next 字段会被复用，不保序）
template <class Node>
void HpSlotManager<Node>::retireList(Node* head) noexcept {
    if (!head) return;
    HpSlot<Node>* s = acquireTls();
    Node* p = head;
    while (p) {
        Node* nxt = p->next;
        s->pushRetired(p);
        p = nxt;
    }
}

template <class Node>
bool HpSlotManager<Node>::unlinkUnlocked_(HpSlot<Node>* s) noexcept {
    SlotNode** cur = &head_;
    while (*cur) {
        if ((*cur)->slot == s) {
            SlotNode* del = *cur;
            *cur = del->next;
            thDelete(del);          // 用 ThreadHeap 释放链表节点
            return true;
        }
        cur = &((*cur)->next);
    }
    return false;
}

// ====================== 显式实例化 ======================
// 模板实现放在 .cpp，需要为会用到的 Node 类型做实例化。
// 例：你的节点是 `StackNode<int>`：
#include "LockFreeStack/StackNode.hpp"
template class HpSlotManager<StackNode<int>>;
// 如需更多类型，继续补：
// template class HpSlotManager<StackNode<uint64_t>>;
// template class HpSlotManager<StackNode<MyPayload>>;
