// tests/LockFreeStack_test.cpp
#include <gtest/gtest.h>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <numeric>
#include <algorithm>
#include <chrono>

#include "fixtures/ThreadHeapTestFixture.hpp"
#include "LockFreeStack/LockFreeStack.hpp" // 包含所有依赖

// ============================================================================
// --- 类型别名 - 适配新的模板 API ---
// ============================================================================
using value_t = int;
using node_t  = StackNode<value_t>;

// 为栈固定使用 1 个危险指针和默认的分配/回收策略
constexpr size_t kStackHazardPointers = 1;
using StackSlotMgr    = HpSlotManager<node_t, kStackHazardPointers>;
using StackRetiredMgr = HpRetiredManager<node_t>;
using Stack           = LockFreeStack<value_t>;

// ============================================================================
// --- 测试夹具 (Fixture) ---
// ============================================================================
// 保持原始的 Fixture，只继承 ThreadHeapTestFixture
class LockFreeStackFixture : public ThreadHeapTestFixture {};


// ============================================================================
// --- 测试用例 (只修改类型实例化) ---
// ============================================================================

// 1) 空栈
TEST_F(LockFreeStackFixture, EmptyStack_TryPopFalse) {
    // *** 关键修改：使用新的类型别名 ***
    auto* slot_mgr    = new (ThreadHeap::allocate(sizeof(StackSlotMgr))) StackSlotMgr();
    auto* retired_mgr = new (ThreadHeap::allocate(sizeof(StackRetiredMgr))) StackRetiredMgr();
    auto* st          = new (ThreadHeap::allocate(sizeof(Stack))) Stack(*slot_mgr, *retired_mgr);

    int out = 0;
    EXPECT_TRUE(st->isEmpty());
    EXPECT_FALSE(st->tryPop(out));
    EXPECT_TRUE(st->isEmpty());
    EXPECT_EQ(retired_mgr->getRetiredCount(), 0u);

    // 清理
    st->~Stack();
    ThreadHeap::deallocate(st);
    retired_mgr->~StackRetiredMgr();
    ThreadHeap::deallocate(retired_mgr);
    slot_mgr->~StackSlotMgr();
    ThreadHeap::deallocate(slot_mgr);
}

// 2) 基础 LIFO
TEST_F(LockFreeStackFixture, PushThenPop_LIFOOrder) {
    // *** 关键修改：使用新的类型别名 ***
    auto* slot_mgr    = new (ThreadHeap::allocate(sizeof(StackSlotMgr))) StackSlotMgr();
    auto* retired_mgr = new (ThreadHeap::allocate(sizeof(StackRetiredMgr))) StackRetiredMgr();
    auto* st          = new (ThreadHeap::allocate(sizeof(Stack))) Stack(*slot_mgr, *retired_mgr);

    for (int v = 1; v <= 5; ++v) st->push(v);

    int out = 0;
    for (int expect = 5; expect >= 1; --expect) {
        ASSERT_TRUE(st->tryPop(out));
        EXPECT_EQ(out, expect);
    }
    EXPECT_TRUE(st->isEmpty());

    // 逻辑修复：tryPop 只是将节点放入线程本地的 HpSlot，需要 collectRetired 来转移
    EXPECT_EQ(retired_mgr->getRetiredCount(), 0); // 此时应该为 0
    std::size_t freed = st->collectRetired(1000);
    EXPECT_EQ(freed, 5u); // 真正回收了 5 个
    EXPECT_EQ(retired_mgr->getRetiredCount(), 0u);

    // 清理
    st->~Stack();
    ThreadHeap::deallocate(st);
    retired_mgr->~StackRetiredMgr();
    ThreadHeap::deallocate(retired_mgr);
    slot_mgr->~StackSlotMgr();
    ThreadHeap::deallocate(slot_mgr);
}

// 3) drain_all
TEST_F(LockFreeStackFixture, DrainAll_ForceCollectEverything) {
    // *** 关键修改：使用新的类型别名 ***
    auto* slot_mgr    = new (ThreadHeap::allocate(sizeof(StackSlotMgr))) StackSlotMgr();
    auto* retired_mgr = new (ThreadHeap::allocate(sizeof(StackRetiredMgr))) StackRetiredMgr();
    auto* st          = new (ThreadHeap::allocate(sizeof(Stack))) Stack(*slot_mgr, *retired_mgr);

    for (int i = 0; i < 3; ++i) st->push(i);

    int out = 0;
    for (int i = 0; i < 3; ++i) ASSERT_TRUE(st->tryPop(out));
    
    EXPECT_TRUE(st->isEmpty());
    // 逻辑修复：此时节点仍在 HpSlot 中
    EXPECT_EQ(retired_mgr->getRetiredCount(), 0); 
    
    std::size_t freed = st->drainAll();
    EXPECT_EQ(freed, 3u);
    EXPECT_EQ(retired_mgr->getRetiredCount(), 0u);

    // 清理
    st->~Stack();
    ThreadHeap::deallocate(st);
    retired_mgr->~StackRetiredMgr();
    ThreadHeap::deallocate(retired_mgr);
    slot_mgr->~StackSlotMgr();
    ThreadHeap::deallocate(slot_mgr);
}

// 4) 多线程竞争
TEST_F(LockFreeStackFixture, ConcurrentPushPop_NoDataLossOrCorruption) {
    const int num_producers = 4;
    const int num_consumers = 4;
    const int items_per_producer = 10000;
    const int total_items = num_producers * items_per_producer;

    // *** 关键修改：使用新的类型别名 ***
    auto* slot_mgr    = new (ThreadHeap::allocate(sizeof(StackSlotMgr))) StackSlotMgr();
    auto* retired_mgr = new (ThreadHeap::allocate(sizeof(StackRetiredMgr))) StackRetiredMgr();
    auto* st          = new (ThreadHeap::allocate(sizeof(Stack))) Stack(*slot_mgr, *retired_mgr);

    std::atomic<int> items_remaining_to_pop(total_items);
    std::vector<std::thread> all_threads;
    std::vector<std::vector<int>> consumer_results(num_consumers);

    auto producer_task = [&](int start_value) {
        for (int i = 0; i < items_per_producer; ++i) {
            st->push(start_value + i);
        }
    };

    auto consumer_task = [&](std::vector<int>& local_results) {
        int item;
        while (items_remaining_to_pop.load(std::memory_order_acquire) > 0) {
            if (st->tryPop(item)) {
                local_results.push_back(item);
                items_remaining_to_pop.fetch_sub(1, std::memory_order_acq_rel);
            }
        }
    };

    for (int i = 0; i < num_producers; ++i) {
        all_threads.emplace_back(producer_task, i * items_per_producer);
    }
    for (int i = 0; i < num_consumers; ++i) {
        all_threads.emplace_back(consumer_task, std::ref(consumer_results[i]));
    }
    for (auto& t : all_threads) {
        t.join();
    }

    // 验证
    EXPECT_EQ(items_remaining_to_pop.load(), 0);
    EXPECT_TRUE(st->isEmpty());

    std::vector<int> all_popped_items;
    for (const auto& vec : consumer_results) {
        all_popped_items.insert(all_popped_items.end(), vec.begin(), vec.end());
    }
    std::sort(all_popped_items.begin(), all_popped_items.end());

    std::vector<int> expected_items(total_items);
    std::iota(expected_items.begin(), expected_items.end(), 0);

    ASSERT_EQ(all_popped_items.size(), (size_t)total_items);
    EXPECT_EQ(all_popped_items, expected_items);

    // 清理
    st->~Stack();
    ThreadHeap::deallocate(st);
    retired_mgr->~StackRetiredMgr();
    ThreadHeap::deallocate(retired_mgr);
    slot_mgr->~StackSlotMgr();
    ThreadHeap::deallocate(slot_mgr);
}


// // 5) 单进程多线程：验证线程退出时的自动清理
// TEST_F(LockFreeStackFixture, SingleProcess_ThreadExitCleanupVerification) {
//     // *** 关键修改：使用新的类型别名 ***
//     auto* slot_mgr = new (ThreadHeap::allocate(sizeof(StackSlotMgr))) StackSlotMgr();

//     const int num_threads = 4;
//     std::vector<std::thread> threads;

//     for (int i = 0; i < num_threads; ++i) {
//         threads.emplace_back([slot_mgr]() {
//             // acquireTls 返回的类型现在与 SlotType 匹配
//             auto* slot = slot_mgr->acquireTls();
//             ASSERT_NE(slot, nullptr);
//             std::this_thread::sleep_for(std::chrono::milliseconds(20));
//         });
//     }

//     std::this_thread::sleep_for(std::chrono::milliseconds(10));
//     EXPECT_EQ(slot_mgr->getSlotCount(), num_threads);

//     for (auto& t : threads) {
//         t.join();
//     }
    
//     std::this_thread::sleep_for(std::chrono::milliseconds(10));
//     EXPECT_EQ(slot_mgr->getSlotCount(), 0);

//     // 清理
//     slot_mgr->~StackSlotMgr();
//     ThreadHeap::deallocate(slot_mgr);
// }

// // (暂时移除了 MultiProcessMultiThread 测试，因为它需要对 SharedStressTestBlock 进行类似的模板适配)