#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <mutex>
#include "LockFreeStack/HpSlot.hpp"
#include "Tool/ShmMutexLock.hpp"
#include "LockFreeStack/HpSlotManagerDetail.hpp"


template <class Node>
class HpSlotManager {
public:
    HpSlotManager() = default;
    ~HpSlotManager();

    HpSlotManager(const HpSlotManager&) = delete;
    HpSlotManager& operator=(const HpSlotManager&) = delete;

    static void onThreadExit();

    HpSlot<Node>* acquireTls();
    std::size_t getShotCount() const;
    void snapshotHazardpoints(std::vector<const Node*>& out) const;
    std::size_t flushAllRetiredTo(std::atomic<Node*>& dst_head) noexcept;


    void retireNode(Node* n) noexcept;
    void retireList(Node* head) noexcept;

private:
    bool unlinkUnlocked_(HpSlot<Node>* s) noexcept;
    void unregisterTls_();

    using SlotNode = HazardPointerDetail::SlotNode<Node>;
    SlotNode* head_{nullptr};

    // 保护链表结构的互斥（跨进程鲁棒）
    mutable ShmMutexLock shm_mutx_;

    inline static thread_local HpSlotManager<Node>* tls_manager_ = nullptr;
    inline static thread_local HpSlot<Node>* tls_slot_ = nullptr;
    // 仍然使用 Handler 来触发 onThreadExit
    inline static thread_local HazardPointerDetail::ThreadExitHandler tls_exit_handler_{};

    void linkHead_(SlotNode* n) noexcept {
        n->next = head_;
        head_   = n;
    }


};
