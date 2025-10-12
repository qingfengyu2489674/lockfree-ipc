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
