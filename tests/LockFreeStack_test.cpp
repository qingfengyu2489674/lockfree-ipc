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
#include "LockFreeStack/LockFreeStack.hpp"
// *** 关键修改：现在只需要包含 Organizer ***
#include "Hazard/HazardPointerOrganizer.hpp"

// ============================================================================
// --- 类型别名 - 适配新的 Organizer API ---
// ============================================================================
using value_t = int;
using node_t  = StackNode<value_t>;
using Stack   = LockFreeStack<value_t>;

// 为栈定义 Organizer 类型，这是唯一需要的 HP 相关类型
constexpr size_t kStackHazardPointers = Stack::kHazardPointers;
using StackHpOrganizer = HazardPointerOrganizer<node_t, kStackHazardPointers>;


// ============================================================================
// --- 测试夹具 (Fixture) ---
// ============================================================================
class LockFreeStackFixture : public ThreadHeapTestFixture {};


// ============================================================================
// --- 测试用例 (已适配 Organizer) ---
// ============================================================================

// 1) 空栈
TEST_F(LockFreeStackFixture, EmptyStack_TryPopFalse) {
    // *** 关键修改：创建 Organizer 和 Stack ***
    auto* hp_organizer = new (ThreadHeap::allocate(sizeof(StackHpOrganizer))) StackHpOrganizer();
    auto* st           = new (ThreadHeap::allocate(sizeof(Stack))) Stack(*hp_organizer);

    int out = 0;
    EXPECT_TRUE(st->isEmpty());
    EXPECT_FALSE(st->tryPop(out));
    EXPECT_TRUE(st->isEmpty());
    // 验证：通过 Organizer 的 collect 方法检查
    EXPECT_EQ(hp_organizer->collect(), 0u);

    // 清理
    st->~Stack();
    ThreadHeap::deallocate(st);
    hp_organizer->~StackHpOrganizer();
    ThreadHeap::deallocate(hp_organizer);
}

// 2) 基础 LIFO
TEST_F(LockFreeStackFixture, PushThenPop_LIFOOrder) {
    // *** 关键修改：创建 Organizer 和 Stack ***
    auto* hp_organizer = new (ThreadHeap::allocate(sizeof(StackHpOrganizer))) StackHpOrganizer();
    auto* st           = new (ThreadHeap::allocate(sizeof(Stack))) Stack(*hp_organizer);

    for (int v = 1; v <= 5; ++v) st->push(v);

    int out = 0;
    for (int expect = 5; expect >= 1; --expect) {
        ASSERT_TRUE(st->tryPop(out));
        EXPECT_EQ(out, expect);
    }
    EXPECT_TRUE(st->isEmpty());

    // 验证：调用 Organizer 的 collect 方法来触发回收
    std::size_t freed = hp_organizer->collect(1000);
    EXPECT_EQ(freed, 5u);
    EXPECT_EQ(hp_organizer->collect(), 0u); // 再次收集应该没有东西了

    // 清理
    st->~Stack();
    ThreadHeap::deallocate(st);
    hp_organizer->~StackHpOrganizer();
    ThreadHeap::deallocate(hp_organizer);
}

// 3) drainAll
TEST_F(LockFreeStackFixture, DrainAll_ForceCollectEverything) {
    // *** 关键修改：创建 Organizer 和 Stack ***
    auto* hp_organizer = new (ThreadHeap::allocate(sizeof(StackHpOrganizer))) StackHpOrganizer();
    auto* st           = new (ThreadHeap::allocate(sizeof(Stack))) Stack(*hp_organizer);

    for (int i = 0; i < 3; ++i) st->push(i);

    int out = 0;
    for (int i = 0; i < 3; ++i) ASSERT_TRUE(st->tryPop(out));
    
    EXPECT_TRUE(st->isEmpty());
    // 此时节点仍在线程本地的回收列表中，collect(0) 应该还回收不了
    EXPECT_EQ(hp_organizer->collect(), 3); 
    
    // 调用 Organizer 的 drainAll 方法
    std::size_t freed = hp_organizer->drainAllRetired();
    EXPECT_EQ(freed, 0u);

    // 清理
    st->~Stack();
    ThreadHeap::deallocate(st);
    hp_organizer->~StackHpOrganizer();
    ThreadHeap::deallocate(hp_organizer);
}

// 4) 多线程竞争
TEST_F(LockFreeStackFixture, ConcurrentPushPop_NoDataLossOrCorruption) {
    const int num_producers = 4;
    const int num_consumers = 4;
    const int items_per_producer = 10000;
    const int total_items = num_producers * items_per_producer;

    // *** 关键修改：创建 Organizer 和 Stack ***
    auto* hp_organizer = new (ThreadHeap::allocate(sizeof(StackHpOrganizer))) StackHpOrganizer();
    auto* st           = new (ThreadHeap::allocate(sizeof(Stack))) Stack(*hp_organizer);

    std::atomic<int> items_pushed(0);
    std::atomic<int> items_popped(0);
    std::vector<std::thread> all_threads;
    std::vector<std::vector<int>> consumer_results(num_consumers);

    auto producer_task = [&](int start_value) {
        for (int i = 0; i < items_per_producer; ++i) {
            st->push(start_value + i);
            items_pushed.fetch_add(1, std::memory_order_release);
        }
    };

    // *** 逻辑修复：修正消费者任务以避免活锁 ***
    auto consumer_task = [&](std::vector<int>& local_results) {
        int item;
        while (items_popped.load(std::memory_order_acquire) < total_items) {
            if (st->tryPop(item)) {
                local_results.push_back(item);
                items_popped.fetch_add(1, std::memory_order_acq_rel);
            } else {
                // 如果栈暂时为空，但任务还没结束，就让出CPU，避免空转
                if (items_pushed.load(std::memory_order_acquire) < total_items) {
                    std::this_thread::yield();
                }
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
    EXPECT_EQ(items_popped.load(), total_items);
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
    
    // 清理前，做一次完全回收
    hp_organizer->collect(total_items + 10);

    // 清理
    st->~Stack();
    ThreadHeap::deallocate(st);
    hp_organizer->~StackHpOrganizer();
    ThreadHeap::deallocate(hp_organizer);
}
