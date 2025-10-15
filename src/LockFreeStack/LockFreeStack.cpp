// LockFreeStack/LockFreeStack.cpp
#include "LockFreeStack/LockFreeStack.hpp"
#include "LockFreeStack/StackNode.hpp"
#include "LockFreeStack/HpSlotManager.hpp"
#include "LockFreeStack/HpRetiredManager.hpp"
#include "LockFreeStack/HpSlot.hpp"
#include "gc_malloc/ThreadHeap/ThreadHeap.hpp"

#include <new>
#include <vector>
#include <utility>

// ====================== 小工具：用 ThreadHeap 构造节点 ======================
namespace {
template <class Node, class... Args>
inline Node* thNewNode(Args&&... args) {
    void* mem = ThreadHeap::allocate(sizeof(Node));
    // 如需更强对齐，可替换为按 alignof(Node) 的分配接口
    return ::new (mem) Node(std::forward<Args>(args)...);
}
} // namespace

// ====================== 模板成员实现 ======================

template <class T>
LockFreeStack<T>::LockFreeStack(HpSlotManager<node_type>& slot_mgr,
                                HpRetiredManager<node_type>& retired_mgr) noexcept
    : head_(nullptr)
    , slot_mgr_(slot_mgr)
    , retired_mgr_(retired_mgr) {}

template <class T>
LockFreeStack<T>::~LockFreeStack() noexcept = default;


// ----- push (const&) -----
template <class T>
void LockFreeStack<T>::push(const value_type& v) noexcept {
    // 原: node_type* n = thNewNode<node_type>(v);
    void* mem = ThreadHeap::allocate(sizeof(node_type));
    node_type* n = ::new (mem) node_type();  // 先默认构造整个节点
    n->value = v;                             // 显式写入 value
    node_type* old = head_.load(std::memory_order_relaxed);
    do {
        n->next = old;
    } while (!head_.compare_exchange_weak(
        old, n,
        std::memory_order_release,
        std::memory_order_relaxed));
}

// ----- push (rvalue) -----
template <class T>
void LockFreeStack<T>::push(value_type&& v) noexcept {
    // 原: node_type* n = thNewNode<node_type>(std::move(v));
    void* mem = ThreadHeap::allocate(sizeof(node_type));
    node_type* n = ::new (mem) node_type();  // 先默认构造整个节点
    n->value = std::move(v);                 // 显式写入 value（移动）
    node_type* old = head_.load(std::memory_order_relaxed);
    do {
        n->next = old;
    } while (!head_.compare_exchange_weak(
        old, n,
        std::memory_order_release,
        std::memory_order_relaxed));
}


template <class T>
bool LockFreeStack<T>::tryPop(value_type& out) noexcept {
    for (;;) {
        node_type* h = head_.load(std::memory_order_acquire);
        if (!h) {
            // 空栈：不要获取 HP 槽，不要做任何分配/锁
            return false;
        }

        // 只有确实“看到了一个节点”时，才拿 TLS 槽并保护
        auto* slot = slot_mgr_.acquireTls();   // 允许返回 nullptr；必须是无阻塞/无分配的
        if (slot) slot->protect(h);

        // 再次确认 head 仍是 h
        if (h != head_.load(std::memory_order_acquire)) {
            if (slot) slot->clear();
            continue;
        }

        node_type* next = h->next;
        if (head_.compare_exchange_strong(
                h, next,
                std::memory_order_acq_rel,     // 成功：获取节点与其内容
                std::memory_order_relaxed)) {  // 失败：h 已被更新
            out = std::move(h->value);
            if (slot) slot->clear();
            retired_mgr_.appendRetiredNode(h);
            return true;
        }

        if (slot) slot->clear(); // CAS 失败：清保护，重试
    }
}



// ====================== empty（用 acquire） ======================
template <class T>
bool LockFreeStack<T>::isEmpty() const noexcept {
    return head_.load(std::memory_order_acquire) == nullptr;
}

// ----- retire helpers -----
template <class T>
void LockFreeStack<T>::retireNode(node_type* n) noexcept {
    slot_mgr_.retireNode(n);
}

template <class T>
void LockFreeStack<T>::retireList(node_type* head) noexcept {
    slot_mgr_.retireList(head);
}

// ----- 顶层回收：先汇总 per-slot 退休链，再拍快照 + collect -----
template <class T>
typename LockFreeStack<T>::size_type
LockFreeStack<T>::collectRetired(size_type quota) noexcept {
    if (quota == 0) return 0;

    // 1) 汇总：把所有 HpSlot 的本地退休链并入一个临时原子头
    std::atomic<node_type*> agg_head{nullptr};
    slot_mgr_.flushAllRetiredTo(agg_head);

    // 2) 将聚合链整段并入 RetiredManager 的全局退休链（内部加锁，线程安全）
    if (node_type* seg = agg_head.exchange(nullptr, std::memory_order_acq_rel)) {
        retired_mgr_.appendRetiredList(seg);
    }

    // 3) 拍 Hazard 快照
    std::vector<const node_type*> snapshot;
    snapshot.reserve(256);
    slot_mgr_.snapshotHazardpoints(snapshot);

    // 4) 回收（仍然走你已有的匿名回收器与 collect_from_snapshot_）
    return collectFromSnapshot_(quota, snapshot);
}

template <class T>
typename LockFreeStack<T>::size_type
LockFreeStack<T>::collectFromSnapshot_(size_type quota,
                                         std::vector<const node_type*>& snapshot) noexcept {
    // .cpp 内匿名回收器（函数指针签名：Node*，noexcept）
    auto reclaimer = +[](node_type* p) noexcept {
        if (!p) return;
        p->~node_type();          // 析构节点（含 value）
        ThreadHeap::deallocate(p); // 线程堆释放
    };

    return static_cast<size_type>(retired_mgr_.collectRetired(quota, snapshot, reclaimer));
}

// ----- 停机/析构：全量回收（匿名回收器：线程堆释放） -----
template <class T>
typename LockFreeStack<T>::size_type
LockFreeStack<T>::drainAll() noexcept {
    auto reclaimer = +[](node_type* p) noexcept {
        if (!p) return;
        p->~node_type();
        ThreadHeap::deallocate(p);
    };
    return static_cast<size_type>(retired_mgr_.drainAll(reclaimer));
}


// ====================== 显式实例化 ======================
// 如需支持更多 T，请在此添加显式实例化
template class LockFreeStack<int>;
