#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <mutex>
#include "LockFreeStack/HpSlot.hpp"
#include "Tool/ShmMutexLock.hpp"
#include "LockFreeStack/HpSlotManagerDetail.hpp"
#include "AllocatorPolicies.hpp"


template<class Node, std::size_t MaxPointers, class AllocPolicy = DefaultHeapPolicy>
class HpSlotManager {
public:
    using SlotType = HpSlot<Node, MaxPointers>;
    using SlotNode = HazardPointerDetail::SlotNode<Node, MaxPointers>;

    HpSlotManager() = default;
    ~HpSlotManager();

    HpSlotManager(const HpSlotManager&) = delete;
    HpSlotManager& operator=(const HpSlotManager&) = delete;

    SlotType* acquireTls();
    std::size_t getSlotCount() const;
    void snapshotHazardpoints(std::vector<const Node*>& out) const;
    std::size_t flushAllRetiredTo(std::atomic<Node*>& dst_head) noexcept;


    void retireNode(Node* n) noexcept;
    void retireList(Node* head) noexcept;

private:
    void unlinkUnlocked_(SlotType* s, SlotNode*& out_node_to_delete) noexcept;
    void unregisterTls_();
    SlotNode* head_{nullptr};

    // 保护链表结构的互斥（跨进程鲁棒）
    mutable ShmMutexLock shm_mutx_;

    inline static thread_local SlotType* tls_slot_ = nullptr;
    // 仍然使用 Handler 来触发 onThreadExit
    inline static thread_local HazardPointerDetail::ThreadExitHandler tls_exit_handler_{};

    void linkHead_(SlotNode* n) noexcept {
        n->next = head_;
        head_   = n;
    }
};


#include "HpSlotManager_impl.hpp"
