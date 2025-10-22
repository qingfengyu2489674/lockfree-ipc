#include "EBRManager/ThreadSlot.hpp"

// --- 构造函数实现 ---
ThreadSlot::ThreadSlot() noexcept
    : next(nullptr) {
    // 初始状态：全零 (纪元0, 不活跃, 未被注册)。
    state_.store(pack_(0, false, false), std::memory_order_relaxed);
}

// --- 核心API实现 ---

bool ThreadSlot::tryRegister(uint64_t initialEpoch) noexcept {
    uint64_t old_state = state_.load(std::memory_order_relaxed);
    for (;;) {
        if (isRegistered(old_state)) {
            return false;
        }
        uint64_t new_state = pack_(initialEpoch, true, true);
        if (state_.compare_exchange_weak(old_state, new_state,
                                          std::memory_order_release,
                                          std::memory_order_relaxed)) {
            return true;
        }
    }
}

void ThreadSlot::unregister() noexcept {
    uint64_t old_state = state_.load(std::memory_order_relaxed);
    for (;;) {
        if (!isRegistered(old_state)) {
            return;
        }
        uint64_t epoch = unpackEpoch(old_state);
        uint64_t new_state = pack_(epoch, false, false);
        if (state_.compare_exchange_weak(old_state, new_state,
                                          std::memory_order_release,
                                          std::memory_order_relaxed)) {
            return;
        }
    }
}

void ThreadSlot::enter(uint64_t current_epoch) noexcept {
    // 这个方法原子地更新纪元并将线程标记为活跃。
    // 我们也在这里处理“注册”的逻辑。
    uint64_t old_state = state_.load(std::memory_order_relaxed);
    for (;;) {
        // 如果已经活跃，则什么都不做
        if (isActive(old_state)) {
            return;
        }

        // 打包新的状态：使用传入的纪元，并标记为 active 和 registered。
        uint64_t new_state = pack_(current_epoch, true, true); 

        if (state_.compare_exchange_weak(old_state, new_state,
                                          std::memory_order_release,
                                          std::memory_order_relaxed)) {
            return;
        }
    }
}

void ThreadSlot::leave() noexcept {
    uint64_t old_state = state_.load(std::memory_order_relaxed);
    for (;;) {
        // 只有已注册且活跃的线程才能离开
        if (!isRegistered(old_state) || !isActive(old_state)) {
            // 如果已经不活跃，或槽位不再属于我们，则不做任何事
            return;
        }

        uint64_t epoch = unpackEpoch(old_state);
        uint64_t new_state = pack_(epoch, false, true);

        if (state_.compare_exchange_weak(old_state, new_state,
                                          std::memory_order_release,
                                          std::memory_order_relaxed)) {
            return;
        }
    }
}
// --- 结束新增 ---

void ThreadSlot::setEpoch(uint64_t newEpoch) noexcept {
    uint64_t old_state = state_.load(std::memory_order_relaxed);
    for (;;) {
        if (!isRegistered(old_state)) {
            return;
        }
        bool active = isActive(old_state);
        uint64_t new_state = pack_(newEpoch, active, true);
        if (state_.compare_exchange_weak(old_state, new_state,
                                          std::memory_order_release,
                                          std::memory_order_relaxed)) {
            return;
        }
    }
}

uint64_t ThreadSlot::loadState() const noexcept {
    return state_.load(std::memory_order_acquire);
}

// --- 静态辅助函数实现 ---
uint64_t ThreadSlot::unpackEpoch(uint64_t state) noexcept {
    return state >> kEpochShift;
}

bool ThreadSlot::isActive(uint64_t state) noexcept {
    return (state & kActiveBit) != 0;
}

bool ThreadSlot::isRegistered(uint64_t state) noexcept {
    return (state & kRegisteredBit) != 0;
}

// --- 私有静态辅助函数实现 ---
uint64_t ThreadSlot::pack_(uint64_t epoch, bool active, bool registered) noexcept {
    uint64_t state = epoch << kEpochShift;
    if (active)     state |= kActiveBit;
    if (registered) state |= kRegisteredBit;
    return state;
}