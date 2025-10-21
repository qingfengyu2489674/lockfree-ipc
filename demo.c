#pragma once

#include <atomic>
#include <cstdint>

/**
 * @brief 代表一个线程在EBR管理器中的状态槽。
 *
 * 这个类被设计为侵入式的，以便能被无锁栈（如 LockFreeReuseStack）高效管理。
 * 它使用一个单一的64位原子变量来打包所有状态（活跃、过期、纪元），
 * 所有的状态转换都通过无锁的CAS操作完成。
 */
class ThreadState {
public:
    // --- 侵入式设计所需 ---
    // 这个指针专门用于当该对象在空闲列表中时，将其链接起来。
    ThreadState* next_in_freelist;

    // --- 构造/析构 ---

    /**
     * @brief 构造一个新的线程槽。
     * 初始状态为不活跃、已过期，纪元为0。可以被立即获取。
     */
    ThreadState() noexcept;

    // 禁止拷贝和移动，因为每个槽都代表一个唯一的线程状态。
    ThreadState(const ThreadState&) = delete;
    ThreadState& operator=(const ThreadState&) = delete;
    ThreadState(ThreadState&&) = delete;
    ThreadState& operator=(ThreadState&&) = delete;

    // --- 核心API ---

    /**
     * @brief 尝试为一个新线程获取并激活这个槽位。
     * 只有当槽位处于“已过期”状态时才能成功。
     * @param initial_epoch 线程被激活时应处的初始纪元。
     * @return 如果成功获取，返回 true；否则返回 false。
     */
    bool try_acquire(uint64_t initial_epoch) noexcept;

    /**
     * @brief 线程退出时，释放此槽位。
     * 将槽位标记为“不活跃”和“已过期”，使其可被重新分配。
     */
    void release() noexcept;

    /**
     * @brief 线程更新自己所处的纪元。
     * @param new_epoch 新的纪元号。
     */
    void set_epoch(uint64_t new_epoch) noexcept;

    /**
     * @brief EBR管理器用来安全地读取一个槽的完整状态快照。
     * @return 包含所有打包状态的64位整数。
     */
    uint64_t load_state() const noexcept;

    // --- 静态辅助函数 (用于解析状态快照) ---

    static uint64_t unpack_epoch(uint64_t state) noexcept;
    static bool is_active(uint64_t state) noexcept;
    static bool is_expired(uint64_t state) noexcept;

private:
    // --- 位布局与实现细节 ---

    // [ Bits 63..2: Epoch | Bit 1: Expired | Bit 0: Active ]
    static constexpr uint64_t kActiveBit  = 1ULL << 0;
    static constexpr uint64_t kExpiredBit = 1ULL << 1;
    static constexpr int      kEpochShift = 2;

    /**
     * @brief 将各个状态组件打包成一个64位整数。
     */
    static uint64_t pack(uint64_t epoch, bool active, bool expired) noexcept {
        uint64_t state = epoch << kEpochShift;
        if (active)  state |= kActiveBit;
        if (expired) state |= kExpiredBit;
        return state;
    }

    // 核心状态变量
    std::atomic<uint64_t> state_;
};

// --- 内联实现 ---

inline ThreadState::ThreadState() noexcept
    : next_in_freelist(nullptr) {
    // 初始状态：纪元0, 不活跃, 已过期
    state_.store(pack(0, false, true), std::memory_order_relaxed);
}

inline bool ThreadState::try_acquire(uint64_t initial_epoch) noexcept {
    uint64_t old_state = state_.load(std::memory_order_relaxed);
    for (;;) {
        // 只有当槽位是过期状态时，我们才能尝试获取
        if (!is_expired(old_state)) {
            return false;
        }

        // 准备新状态：纪元为当前全局纪元，活跃，未过期
        uint64_t new_state = pack(initial_epoch, true, false);

        // 尝试原子地将槽从“已过期”状态转换为“活跃”状态
        if (state_.compare_exchange_weak(old_state, new_state,
                                          std::memory_order_release,
                                          std::memory_order_relaxed)) {
            return true;
        }
        // CAS失败：说明状态被其他线程改变（理论上不应发生，因为我们
        // 独占了这个从freelist中pop出来的节点），或者伪失败。
        // old_state 已被自动更新，循环将使用新值重试。
    }
}

inline void ThreadState::release() noexcept {
    uint64_t old_state = state_.load(std::memory_order_relaxed);
    for (;;) {
        if (is_expired(old_state)) {
            return; // 已经是过期状态，无需操作
        }

        // 新状态：纪元保持不变，不活跃，已过期
        uint64_t epoch = unpack_epoch(old_state);
        uint64_t new_state = pack(epoch, false, true);

        // 原子地将状态更新为已过期
        if (state_.compare_exchange_weak(old_state, new_state,
                                          std::memory_order_release,
                                          std::memory_order_relaxed)) {
            return;
        }
    }
}

inline void ThreadState::set_epoch(uint64_t new_epoch) noexcept {
    uint64_t old_state = state_.load(std::memory_order_relaxed);
    for (;;) {
        // 不应更新一个不属于我们的（已过期的）槽
        if (is_expired(old_state)) {
            return;
        }

        // 新状态：纪元更新，活跃状态保持不变，未过期
        bool active = is_active(old_state);
        uint64_t new_state = pack(new_epoch, active, false);

        if (state_.compare_exchange_weak(old_state, new_state,
                                          std::memory_order_release,
                                          std::memory_order_relaxed)) {
            return;
        }
    }
}

inline uint64_t ThreadState::load_state() const noexcept {
    // 扫描器必须使用 acquire 来与修改者（线程）的 release 操作同步
    return state_.load(std::memory_order_acquire);
}

inline uint64_t ThreadState::unpack_epoch(uint64_t state) noexcept {
    return state >> kEpochShift;
}

inline bool ThreadState::is_active(uint64_t state) noexcept {
    return (state & kActiveBit) != 0;
}

inline bool ThreadState::is_expired(uint64_t state) noexcept {
    return (state & kExpiredBit) != 0;
}