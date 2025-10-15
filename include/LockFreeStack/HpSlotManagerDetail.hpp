#pragma once
#include "LockFreeStack/HpSlot.hpp"

namespace HazardPointerDetail {

template <class Node>
struct SlotNode
{
    HpSlot<Node>* slot{nullptr};  // 槽位指针
    SlotNode*     next{nullptr};  // 单链表 next
};

class ThreadExitHandler {
public:
    // 析构函数调用一个静态清理函数
    ~ThreadExitHandler();
};

}