// hazard/hp_slot.hpp
#pragma once
#include <atomic>
#include <cstddef>
#include <array>
#include <cstdint>
#include <cassert> 

template<class Node, std::size_t MaxPointers>
class HpSlot {
public:
    HpSlot() = default;
    ~HpSlot() = default;

    HpSlot(const HpSlot&) = delete;
    HpSlot& operator=(const HpSlot&) = delete;

    void protect(std::size_t index, Node* p) noexcept;
    void clear(std::size_t index) noexcept;
    void clearAll() noexcept;

    void pushRetired(Node* n) noexcept;
    Node* drainAllRetired() noexcept;

    std::size_t getHazardPointerCount() const noexcept;
    
    std::atomic<Node*>& getRetiredListHead() noexcept;
    const std::atomic<Node*>& getHazardPointerAt(std::size_t index) const noexcept;
    
private: 
    std::array<std::atomic<Node*>, MaxPointers> hazard_ptrs_{};
    std::atomic<Node*> retired_head{nullptr}; // 本线程退休链表头（直接 Node*）
};


template<class Node, std::size_t MaxPointers>
std::size_t HpSlot<Node, MaxPointers>::getHazardPointerCount() const noexcept {
    return MaxPointers;
}


template<class Node, std::size_t MaxPointers>
const std::atomic<Node*>& HpSlot<Node, MaxPointers>::getHazardPointerAt(std::size_t index) const noexcept {
    assert(index < MaxPointers && "Hazard pointer index is out of bounds.");
    return hazard_ptrs_[index];
}

template<class Node, std::size_t MaxPointers>
void HpSlot<Node, MaxPointers>::protect(std::size_t index, Node* p) noexcept {
    assert(index < MaxPointers && "Hazard pointer index is out of bounds.");
    // 操作 hazard_ptrs_ 数组的指定索引，而不是单个 hazard_ptr
    hazard_ptrs_[index].store(p, std::memory_order_release);
}


template<class Node, std::size_t MaxPointers>
void HpSlot<Node, MaxPointers>::clear(std::size_t index) noexcept {
    assert(index < MaxPointers && "Hazard pointer index is out of bounds.");
    // 操作 hazard_ptrs_ 数组的指定索引
    hazard_ptrs_[index].store(nullptr, std::memory_order_release);
}

template<class Node, std::size_t MaxPointers>
void HpSlot<Node, MaxPointers>::clearAll() noexcept {
    // 遍历数组，清空所有指针
    for (auto& hp_atomic : hazard_ptrs_) {
        hp_atomic.store(nullptr, std::memory_order_release);
    }
}


// --- 退休链表管理实现 (逻辑不变，仅更新模板签名) ---

template<class Node, std::size_t MaxPointers>
void HpSlot<Node, MaxPointers>::pushRetired(Node* n) noexcept {
    // 这部分逻辑与 retired_head 相关，与危险指针数量无关，因此保持不变。
    // 要求：Node 拥有 `Node* next`。
    Node* old_head = retired_head.load(std::memory_order_relaxed);
    do {
        n->next = old_head;
    } while (!retired_head.compare_exchange_weak(
        old_head, n,
        std::memory_order_release,
        std::memory_order_relaxed)); // 失败时 relaxed 即可，因为 CAS 会加载新值
}

template<class Node, std::size_t MaxPointers>
Node* HpSlot<Node, MaxPointers>::drainAllRetired() noexcept {
    return retired_head.exchange(nullptr, std::memory_order_acq_rel);
}


template<class Node, std::size_t MaxPointers>
inline std::atomic<Node*>& HpSlot<Node, MaxPointers>::getRetiredListHead() noexcept {
    return retired_head;
}