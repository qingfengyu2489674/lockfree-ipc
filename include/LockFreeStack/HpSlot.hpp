// hazard/hp_slot.hpp
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

/**
 * @tparam Node 你的 Treiber 栈节点类型（例如 stack::StackNode<T>）
 * 约束与并发模型：
 * - hazard_ptr：本线程单写、收集器多读 → atomic<void*>
 * - retired_head：本线程头插，收集器用 exchange(nullptr) 整段摘走 → atomic<Node*>
 * - 数据面无锁，回收阶段由收集器统一处理
 */
template<class Node>
class HpSlot {
public:
    std::atomic<Node*> hazard_ptr{nullptr};   // 保护指针（Treiber pop 仅需 1 个）
    std::atomic<Node*> retired_head{nullptr}; // 本线程退休链表头（直接 Node*）

public:
    void protect(Node* p) noexcept;
    void clear() noexcept;
    void pushRetired(Node* n) noexcept;
    Node* drainAll() noexcept;
};
