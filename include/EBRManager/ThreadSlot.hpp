#pragma once

#include <atomic>
#include <cstdint>

/**
 * @brief 代表一个线程在EBR管理器中的专属槽位。
 *
 * ... (文档注释保持不变) ...
 */
class ThreadSlot {
public:
    // --- 侵入式设计所需 ---
    ThreadSlot* next;

    // --- 构造/析构 ---
    ThreadSlot() noexcept;
    ~ThreadSlot() = default;

    // 禁止拷贝和移动
    ThreadSlot(const ThreadSlot&) = delete;
    ThreadSlot& operator=(const ThreadSlot&) = delete;
    ThreadSlot(ThreadSlot&&) = delete;
    ThreadSlot& operator=(ThreadSlot&&) = delete;
    
    // 线程生命周期管理
    bool tryRegister(uint64_t initialEpoch) noexcept;
    void unregister() noexcept;

    // EBR临界区管理
    void enter(uint64_t current_epoch) noexcept; 
    void leave() noexcept;
    
    // 纪元更新
    void setEpoch(uint64_t newEpoch) noexcept;

    // EBR扫描器接口
    uint64_t loadState() const noexcept;

    // --- 静态辅助函数 ---
    static uint64_t unpackEpoch(uint64_t state) noexcept;
    static bool isActive(uint64_t state) noexcept;
    static bool isRegistered(uint64_t state) noexcept;

private:
    // --- 位布局与实现细节 ---
    static constexpr uint64_t kActiveBit     = 1ULL << 0;
    static constexpr uint64_t kRegisteredBit = 1ULL << 1;
    static constexpr int      kEpochShift    = 2;

    // 私有辅助方法，添加下划线后缀
    static uint64_t pack_(uint64_t epoch, bool active, bool registered) noexcept;

    std::atomic<uint64_t> state_;
};