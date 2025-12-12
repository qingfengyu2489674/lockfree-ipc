// include/Hazard/HazardPointerOrganizer.hpp
#pragma once

#include <vector>
#include <cstddef>
#include <atomic>

#include "Hazard/HpSlotManager.hpp"
#include "Hazard/HpRetiredManager.hpp"
#include "gc_malloc/ThreadHeap/ThreadHeap.hpp"
#include "Hazard/HpSlotManagerDetail.hpp"
#include "Hazard/GCHook.hpp"


template <class Node, std::size_t MaxPointers, class AllocPolicy = DefaultHeapPolicy>
class HazardPointerOrganizer {
public:
    using SlotManager    = HpSlotManager<Node, MaxPointers, AllocPolicy>;
    using RetiredManager = HpRetiredManager<Node, AllocPolicy>;
    using SlotType       = typename SlotManager::SlotType;

public:
    HazardPointerOrganizer() = default;

    ~HazardPointerOrganizer() {
        collect();
        drainAllRetired();
    }

    HazardPointerOrganizer(const HazardPointerOrganizer&) = delete;
    HazardPointerOrganizer& operator=(const HazardPointerOrganizer&) = delete;

    void retire(Node* node) noexcept {
        slot_manager_.retireNode(node);
    }
    

    std::size_t collect(std::size_t quota = 0) noexcept {
        std::atomic<Node*> collected_list_head{nullptr};
        slot_manager_.flushAllRetiredTo(collected_list_head);
        
        Node* head_ptr = collected_list_head.load(std::memory_order_relaxed);
        if (head_ptr) {
            retired_manager_.appendRetiredList(head_ptr);
        }
        
        std::vector<const Node*> snapshot;
        slot_manager_.snapshotHazardpoints(snapshot);
        
        return retired_manager_.collectRetired(quota, snapshot);
    }

    std::size_t drainAllRetired() noexcept {
        std::atomic<Node*> collected_list_head{nullptr};
        slot_manager_.flushAllRetiredTo(collected_list_head);
        
        Node* head_ptr = collected_list_head.load(std::memory_order_relaxed);
        if (head_ptr) {
            retired_manager_.appendRetiredList(head_ptr);
        }
        
        return retired_manager_.drainAll();
    }

    SlotType* acquireTlsSlot() {
        return slot_manager_.acquireTls();
    }
    
private:
    SlotManager    slot_manager_{};
    RetiredManager retired_manager_{};
};


