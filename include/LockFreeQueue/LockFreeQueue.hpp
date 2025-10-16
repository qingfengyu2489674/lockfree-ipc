#pragma once
#include <atomic>
#include <cstddef>

#include "LockFreeQueue/QueueNode.hpp"
#include "Hazard/HazardPointerOrganizer.hpp"

// 前向声明 AllocPolicy 的默认类型
class DefaultHeapPolicy;

template <class T, class AllocPolicy = DefaultHeapPolicy>
class LockFreeQueue {
public:
    using value_type = T;
    using node_type  = QueueNode<T>;
    using size_type  = std::size_t;

    // *** 关键修改：现在需要2个危险指针 ***
    // HP[0] 用于保护 head
    // HP[1] 用于保护 head->next (first_node)
    static constexpr std::size_t kHazardPointers = 2; 
    using hp_organizer_type = HazardPointerOrganizer<node_type, kHazardPointers, AllocPolicy>;

public:
    explicit LockFreeQueue(hp_organizer_type& hp_organizer) noexcept;
    ~LockFreeQueue() noexcept;

    // 接口保持不变
    void push(const value_type& v);
    void push(value_type&& v);
    bool tryPop(value_type& out) noexcept;
    bool isEmpty() const noexcept;

private:
    template<typename U>
    void pushImpl(U&& v);

    // head_ 和 tail_ 加上一些填充以防止伪共享（可选优化）
    alignas(64) std::atomic<node_type*> head_;
    alignas(64) std::atomic<node_type*> tail_;

    hp_organizer_type& hp_organizer_;
};

#include "LockFreeQueue_impl.hpp"