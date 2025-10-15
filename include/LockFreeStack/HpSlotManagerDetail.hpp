#pragma once
#include "LockFreeStack/HpSlot.hpp"
#include <functional>

namespace HazardPointerDetail {

template<class Node, std::size_t MaxPointers>
struct SlotNode
{
    HpSlot<Node, MaxPointers>* slot{nullptr};  // 槽位指针
    SlotNode*     next{nullptr};  // 单链表 next
};

class ThreadExitHandler {
public:
    ~ThreadExitHandler() {
        if (on_exit) {
            on_exit();
        }
    }
    
    // 公开的回调成员
    std::function<void()> on_exit;
};
}