// EBRManager.cpp
#include "EBRManager/EBRManager.hpp"
#include "EBRManager/ThreadSlot.hpp"

EBRManager::EBRManager() {
    // 初始化全局纪元为0
    global_epoch_.store(0, std::memory_order_relaxed);
}

EBRManager::~EBRManager() {
    for (size_t list_index = 0; list_index < kNumEpochLists; ++list_index) {
        collectGarbage_(list_index);
    }
}

ThreadSlot* EBRManager::getLocalSlot_() {
    return slot_manager_.getLocalSlot();
}

void EBRManager::enter() {
    ThreadSlot* slot = getLocalSlot_();
    if (slot) {
        uint64_t current_epoch = global_epoch_.load(std::memory_order_relaxed);
        // 调用新的、单一的、原子化的方法
        slot->enter(current_epoch);
    }
}


void EBRManager::leave() {
    ThreadSlot* slot = getLocalSlot_();
    if (slot) {
        // 标记线程离开临界区（变为非活跃状态）
        slot->leave();

        if (tryAdvanceEpoch_()) {
            uint64_t current_global_epoch = global_epoch_.load(std::memory_order_relaxed);

            if (current_global_epoch >= 2) {
                uint64_t epoch_to_collect = current_global_epoch - 2;
                collectGarbage_(epoch_to_collect);
            }
        }
    }
}

bool EBRManager::tryAdvanceEpoch_() {
    // 使用 acquire 内存序加载，确保我们能看到其他线程 leave 操作释放的最新状态
    uint64_t current_epoch = global_epoch_.load(std::memory_order_acquire);
    
    bool can_advance = true;

    // 遍历所有已注册的线程槽，检查是否有“掉队者”
    slot_manager_.forEachSlot([&](const ThreadSlot& slot) {
        if (!can_advance) return; // 如果已发现不能推进，提前退出

        uint64_t slot_state = slot.loadState();

        if (ThreadSlot::isActive(slot_state) && 
            ThreadSlot::unpackEpoch(slot_state) < current_epoch) {
            can_advance = false;
        }
    });

    if (!can_advance) {
        return false; // 发现掉队者，无法推进
    }

    // 如果没有掉队者，尝试原子地将全局纪元加一
    return global_epoch_.compare_exchange_strong(
        current_epoch, 
        current_epoch + 1,
        std::memory_order_acq_rel,
        std::memory_order_relaxed
    );
}

void EBRManager::collectGarbage_(uint64_t epoch_to_collect) {
    size_t list_index = epoch_to_collect % kNumEpochLists;

    GarbageNode* garbage_head = garbage_lists_[list_index].stealList();

    if (garbage_head) {
        garbage_collector_.collect(garbage_head);
    }
}