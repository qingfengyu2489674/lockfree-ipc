// hazard/hp_retired_manager.hpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <atomic>
#include <mutex>

#include "Tool/ShmMutexLock.hpp"

/**
 * @tparam Node 需为 intrusive 单链表节点，包含 `Node* next;`
 *
 * 职责（与 HpSlotManager 平行）：
 * - 维护受锁保护的全局退休链表。
 * - 提供并入（单节点/整段）与受配额的回收接口。
 * - Hazard 判定与摘链由上层组织器负责。
 */
template <class Node>
class HpRetiredManager {
public:
    using Reclaimer       = void(*)(Node*) noexcept;                           // 节点释放
    using HazardPredicate = bool(*)(const void* ctx, const Node* p) noexcept;  // true=被保护

public:
    HpRetiredManager() noexcept;
    ~HpRetiredManager() noexcept;

    HpRetiredManager(const HpRetiredManager&)            = delete;
    HpRetiredManager& operator=(const HpRetiredManager&) = delete;

    // 并入单个退休节点（线程安全，头插）
    void appendRetiredNodeToList(Node* n) noexcept;

    // 并入一段退休链（线程安全，整段头插）
    void appendRetiredListToList(Node* head) noexcept;

    // 在外部提供的判定/回收下，回收最多 quota 个未被保护的节点
    std::size_t collect(std::size_t      quota,
                        const void*      hazard_ctx,
                        HazardPredicate  hazard_pred,
                        Reclaimer        reclaimer) noexcept;

    // 无条件回收全部已退休节点（用于停机/析构）
    std::size_t drainAll(Reclaimer reclaimer) noexcept;

    // 粗略统计：当前已退休节点数量
    std::size_t getRetiredCount() const noexcept;

private:
    // 仅在内部持锁下访问
    mutable ShmMutexLock        lock_;
    Node*                       global_head_{nullptr};
    std::atomic<std::size_t>    approx_count_{0};

private:
    // 下列函数要求调用前已持有 lock_

    // 头插整段 [head]，返回合并数量
    std::size_t appendListLocked_(Node* head) noexcept;

    // 扫描并回收至多 quota 个未被保护节点
    std::size_t scanAndReclaimUpLocked_(std::size_t      quota,
                                        const void*      hazard_ctx,
                                        HazardPredicate  hazard_pred,
                                        Reclaimer        reclaimer) noexcept;

    // 无条件回收整个链表
    std::size_t drainAllLocked_(Reclaimer reclaimer) noexcept;
};
