// hazard/hp_slot.hpp
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>


template<class Node>
class HpSlot {
public:
    void protect(Node* p) noexcept;
    void clear() noexcept;
    void pushRetired(Node* n) noexcept;
    Node* drainAll() noexcept;
    
public:
    std::atomic<Node*> hazard_ptr{nullptr};   // 保护指针（Treiber pop 仅需 1 个）
    std::atomic<Node*> retired_head{nullptr}; // 本线程退休链表头（直接 Node*）
};
