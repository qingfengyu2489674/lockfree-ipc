#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <mutex>                  // 仅用于 std::lock_guard 语义；真正加锁用 ShmMutexLock
#include "LockFreeStack/HpSlot.hpp"
#include "Tool/ShmMutexLock.hpp"

/**
 * @brief HP 槽位管理器（链表 + TLS）
 * - 每个线程注册并持有一个 HpSlot<Node>*（缓存于 TLS）
 * - 槽位集合用单链表维护；增删/遍历均由 ShmMutexLock 保护
 * - snapshot() 在锁内拷贝当前槽位指针列表，供锁外进行 GC 判定
 */
template <class Node>
class HpSlotManager {
public:
    HpSlotManager() = default;
    ~HpSlotManager();

    HpSlotManager(const HpSlotManager&) = delete;
    HpSlotManager& operator=(const HpSlotManager&) = delete;

    HpSlot<Node>* acquireTls();
    void unregisterTls();
    std::size_t getShotCount() const;
    void snapshotHazardpoints(std::vector<const Node*>& out) const;
    std::size_t flushAllRetiredTo(std::atomic<Node*>& dst_head) noexcept;


    void retire(Node* n) noexcept;
    void retireList(Node* head) noexcept;

private:
    struct SlotNode {
        HpSlot<Node>* slot{nullptr};  // 槽位指针
        SlotNode*     next{nullptr};  // 单链表 next
    };

    // 仅在持锁下访问的链表表头
    SlotNode* head_{nullptr};

    // 保护链表结构的互斥（跨进程鲁棒）
    mutable ShmMutexLock shm_mutx_;

    // 每线程缓存：本线程的槽位指针（C++17 inline TLS，避免 ODR/链接问题）
    inline static thread_local HpSlot<Node>* tls_slot_ = nullptr;

    void linkHead_(SlotNode* n) noexcept {
        n->next = head_;
        head_   = n;
    }

    bool unlinkUnlocked_(HpSlot<Node>* s) noexcept;
};
