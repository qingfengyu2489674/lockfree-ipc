// hazard/hp_retired_manager.hpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <vector>

#include "Tool/ShmMutexLock.hpp"
#include "AllocatorPolicies.hpp"

template <class Node, class AllocPolicy = DefaultHeapPolicy>
class HpRetiredManager {
public:
    using Reclaimer       = void(*)(Node*) noexcept;

public:
    HpRetiredManager() noexcept;
    ~HpRetiredManager() noexcept;

    HpRetiredManager(const HpRetiredManager&)            = delete;
    HpRetiredManager& operator=(const HpRetiredManager&) = delete;

    void appendRetiredNode(Node* n) noexcept;
    void appendRetiredList(Node* head) noexcept;

                        
    std::size_t collectRetired(std::size_t                 quota,
                               const std::vector<const Node*>&   hazard_snapshot) noexcept;

    std::size_t drainAll() noexcept;
    std::size_t getRetiredCount() const noexcept;

private:
    std::size_t appendListLocked_(Node* head) noexcept;
    std::size_t scanAndReclaimLocked_(std::size_t      quota,
                                      const std::vector<const Node*>& hazard_snapshot) noexcept;

private:
    mutable ShmMutexLock        lock_;
    Node*                       global_head_{nullptr};
    std::atomic<std::size_t>    approx_count_{0};

};


#include "HpRetiredManager_impl.hpp"
