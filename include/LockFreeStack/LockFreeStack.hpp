// LockFreeStack/LockFreeStack.hpp
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>
#include <string>
#include <mutex>

// 前置声明
template <class T>    class StackNode;
template <class Node> class HpSlotManager;
template <class Node> class HpRetiredManager;

/**
 * @brief Treiber 无锁栈（仅声明，无实现）
 * - 使用 HpSlotManager 进行 Hazard 指针发布与线程本地退休；
 * - 回收时由顶层在内部（.cpp）拍快照并调用 HpRetiredManager::collect；
 * - 实际释放在 HpRetiredManager 的 .cpp 中通过 ThreadHeap::deallocate 完成。
 */
template <class T>
class LockFreeStack {
public:
    using value_type = T;
    using node_type  = StackNode<T>;
    using size_type  = std::size_t;

public:
    // 依赖注入：外部提供 HpSlotManager / HpRetiredManager 的实例
    explicit LockFreeStack(HpSlotManager<node_type>& slot_mgr,
                           HpRetiredManager<node_type>& retired_mgr) noexcept;
    ~LockFreeStack() noexcept;

    LockFreeStack(const LockFreeStack&)            = delete;
    LockFreeStack& operator=(const LockFreeStack&) = delete;
    LockFreeStack(LockFreeStack&&)                 = delete;
    LockFreeStack& operator=(LockFreeStack&&)      = delete;

    // 基本操作（Treiber）
    void push(const value_type& v) noexcept;
    void push(value_type&& v) noexcept;

    // 非阻塞尝试弹出：成功返回 true，并把值写入 out
    bool try_pop(value_type& out) noexcept;

    // 近似判空（仅检查 head_ 是否为 nullptr）
    bool empty() const noexcept;

    // 将单个节点退休（包装 HpSlotManager::retire）
    void retire_node(node_type* n) noexcept;

    // 将一段链退休（包装 HpSlotManager::retireList）
    void retire_list(node_type* head) noexcept;

    /**
     * @brief 顶层回收：内部（.cpp）在“持有退休管理器锁”的情况下，
     *        用 slot_mgr_ 拍摄 hazard 快照并传给 retired_mgr_.collect。
     */
    size_type collect(size_type quota) noexcept;

    /**
     * @brief 已有快照时的便捷回收：把快照直接转交给退休管理器。
     */

    // 停机/析构阶段：全量回收
    size_type drain_all() noexcept;

        // 调试：返回从栈顶到栈底的值（仅用于单元测试/调试）
    std::vector<value_type> debug_dump_top_to_bottom() const noexcept;

    // 调试：把栈格式化为字符串 "top -> v1 -> v2 -> ... -> null"
    std::string debug_to_string() const;

private:
    size_type collect_from_snapshot_(size_type quota,
                                std::vector<const node_type*>& hazard_snapshot) noexcept;

    // 栈顶（Treiber 头指针）
    std::atomic<node_type*> head_{nullptr};

    // 引用外部管理器
    HpSlotManager<node_type>&    slot_mgr_;
    HpRetiredManager<node_type>& retired_mgr_;

    mutable std::mutex dbg_mtx_; 

};


extern template class LockFreeStack<int>;