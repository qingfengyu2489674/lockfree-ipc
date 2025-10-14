// tests/LockFreeStack_test.cpp
#include <gtest/gtest.h>
#include <iostream>
#include <string>

#include "fixtures/ThreadHeapTestFixture.hpp"
#include "LockFreeStack/StackNode.hpp"
#include "LockFreeStack/HpSlotManager.hpp"
#include "LockFreeStack/HpRetiredManager.hpp"
#include "LockFreeStack/LockFreeStack.hpp"
#include "gc_malloc/ThreadHeap/ThreadHeap.hpp"

using value_t    = int;
using node_t     = StackNode<value_t>;
using SlotMgr    = HpSlotManager<node_t>;
using RetiredMgr = HpRetiredManager<node_t>;
using Stack      = LockFreeStack<value_t>;

class LockFreeStackFixture : public ThreadHeapTestFixture {};

// ============================================================================
// 1) 空栈
// ============================================================================ 
TEST_F(LockFreeStackFixture, EmptyStack_TryPopFalse) {
    // 使用线程堆分配管理器
    SlotMgr* slot_mgr = new (ThreadHeap::allocate(sizeof(SlotMgr))) SlotMgr();
    RetiredMgr* retired_mgr = new (ThreadHeap::allocate(sizeof(RetiredMgr))) RetiredMgr();
    
    // 确保 LockFreeStack 也使用共享内存进行分配
    Stack* st = new (ThreadHeap::allocate(sizeof(Stack))) Stack(*slot_mgr, *retired_mgr);

    std::cout << "\n[EmptyStack] init: " << "[Not Outputting debug_to_string()]" << std::endl;

    int out = 0;
    EXPECT_TRUE(st->empty());
    EXPECT_FALSE(st->try_pop(out));
    EXPECT_TRUE(st->empty());
    EXPECT_EQ(retired_mgr->getRetiredCount(), 0u);

    std::cout << "[EmptyStack] final: " << "[Not Outputting debug_to_string()]" << std::endl;

    // 清理
    ThreadHeap::deallocate(slot_mgr);
    ThreadHeap::deallocate(retired_mgr);
    ThreadHeap::deallocate(st); // 清理 Stack 对象
}

// ============================================================================
// 2) 基础 LIFO
// ============================================================================ 
TEST_F(LockFreeStackFixture, PushThenPop_LIFOOrder) {
    // 使用线程堆分配管理器
    SlotMgr* slot_mgr = new (ThreadHeap::allocate(sizeof(SlotMgr))) SlotMgr();
    RetiredMgr* retired_mgr = new (ThreadHeap::allocate(sizeof(RetiredMgr))) RetiredMgr();
    
    // 确保 LockFreeStack 也使用共享内存进行分配
    Stack* st = new (ThreadHeap::allocate(sizeof(Stack))) Stack(*slot_mgr, *retired_mgr);

    for (int v = 1; v <= 5; ++v) st->push(v);
    std::cout << "\n[LIFO] after push 1..5: " << "[Not Outputting debug_to_string()]" << std::endl;

    int out = 0;
    for (int expect = 5; expect >= 1; --expect) {
        ASSERT_TRUE(st->try_pop(out));
        EXPECT_EQ(out, expect);
    }

    EXPECT_TRUE(st->empty());
    std::cout << "[LIFO] after pop all: " << "[Not Outputting debug_to_string()]" << std::endl;

    EXPECT_EQ(retired_mgr->getRetiredCount(), 5u);
    std::size_t freed = st->collect(1000);
    EXPECT_EQ(freed, 5u);
    EXPECT_EQ(retired_mgr->getRetiredCount(), 0u);

    // 清理
    ThreadHeap::deallocate(slot_mgr);
    ThreadHeap::deallocate(retired_mgr);
    ThreadHeap::deallocate(st); // 清理 Stack 对象
}

// // ============================================================================
// // 3) drain_all：停机/析构场景，全量回收
// // ============================================================================ 
// TEST_F(LockFreeStackFixture, DrainAll_ForceCollectEverything) {
//     // 使用线程堆分配管理器
//     SlotMgr* slot_mgr = new (ThreadHeap::allocate(sizeof(SlotMgr))) SlotMgr();
//     RetiredMgr* retired_mgr = new (ThreadHeap::allocate(sizeof(RetiredMgr))) RetiredMgr();
    
//     // 确保 LockFreeStack 也使用共享内存进行分配
//     Stack* st = new (ThreadHeap::allocate(sizeof(Stack))) Stack(*slot_mgr, *retired_mgr);

//     for (int i = 0; i < 3; ++i) st->push(i);
//     std::cout << "\n[DrainAll] after push 3: " << "[Not Outputting debug_to_string()]" << std::endl;

//     int out = 0;
//     for (int i = 0; i < 3; ++i) ASSERT_TRUE(st->try_pop(out));
//     EXPECT_TRUE(st->empty());
//     EXPECT_EQ(retired_mgr->getRetiredCount(), 3u);

//     std::size_t freed = st->drain_all();
//     EXPECT_EQ(freed, 3u);
//     EXPECT_EQ(retired_mgr->getRetiredCount(), 0u);

//     std::cout << "[DrainAll] after drain_all: " << "[Not Outputting debug_to_string()]" << std::endl;

//     // 清理
//     ThreadHeap::deallocate(slot_mgr);
//     ThreadHeap::deallocate(retired_mgr);
//     ThreadHeap::deallocate(st); // 清理 Stack 对象
// }
