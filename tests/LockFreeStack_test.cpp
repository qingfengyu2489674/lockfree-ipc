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
// *** 关键修改：现在只需要包含 Organizer，Stack 不再管理内存回收细节 ***
#include "Hazard/HazardPointerOrganizer.hpp"

// ============================================================================
// --- 类型别名 - 适配新的 Organizer API ---
// ============================================================================
using value_t = int;
// StackNode 定义在 LockFreeStack/StackNode.hpp 中，这里直接使用
using node_t  = StackNode<value_t>;
using Stack   = LockFreeStack<value_t>;

// 为栈定义 Organizer 类型
// kHazardPointers 是 Stack 定义的常量 (通常为 1)
constexpr size_t kStackHazardPointers = Stack::kHazardPointers;
using StackHpOrganizer = HazardPointerOrganizer<node_t, kStackHazardPointers>;

// ============================================================================
// --- 测试夹具 (Fixture) ---
// ============================================================================
class LockFreeStackFixture : public ThreadHeapTestFixture {};

// ============================================================================
// --- 测试用例 ---
// ============================================================================

// 1) 空栈测试
TEST_F(LockFreeStackFixture, EmptyStack_TryPopFalse) {
    // 1. 先分配 Organizer
    auto* hp_organizer = new (ThreadHeap::allocate(sizeof(StackHpOrganizer))) StackHpOrganizer();
    // 2. 再分配 Stack，并注入 Organizer
    auto* st           = new (ThreadHeap::allocate(sizeof(Stack))) Stack(*hp_organizer);

    int out = 0;
    // 初始状态
    EXPECT_TRUE(st->isEmpty());
    
    // 尝试 Pop 应该失败
    EXPECT_FALSE(st->tryPop(out));
    EXPECT_TRUE(st->isEmpty());
    
    // 验证：没有任何东西被 retire，所以回收数量应为 0
    EXPECT_EQ(hp_organizer->collect(), 0u);

    // 清理：注意析构顺序，先 Stack 后 Organizer
    st->~Stack();
    ThreadHeap::deallocate(st);
    
    hp_organizer->~StackHpOrganizer();
    ThreadHeap::deallocate(hp_organizer);
}

// 2) 基础 LIFO (先进后出) 测试
TEST_F(LockFreeStackFixture, PushThenPop_LIFOOrder) {
    auto* hp_organizer = new (ThreadHeap::allocate(sizeof(StackHpOrganizer))) StackHpOrganizer();
    auto* st           = new (ThreadHeap::allocate(sizeof(Stack))) Stack(*hp_organizer);

    // Push 1, 2, 3, 4, 5
    for (int v = 1; v <= 5; ++v) {
        st->push(v);
    }
    EXPECT_FALSE(st->isEmpty());

    // Pop 5, 4, 3, 2, 1
    int out = 0;
    for (int expect = 5; expect >= 1; --expect) {
        ASSERT_TRUE(st->tryPop(out));
        EXPECT_EQ(out, expect);
    }
    EXPECT_TRUE(st->isEmpty());

    // 验证：Organizer 应该能回收这 5 个节点
    // 注意：collect() 是尝试回收，根据策略可能保留一些缓存。
    // 为了测试确定性，我们可以使用 drainAllRetired()，或者在这里假设 collect 会尽力回收
    // 这里演示使用 drainAllRetired 确保全部回收
    std::size_t freed = hp_organizer->drainAllRetired();
    EXPECT_EQ(freed, 5u);

    st->~Stack();
    ThreadHeap::deallocate(st);
    hp_organizer->~StackHpOrganizer();
    ThreadHeap::deallocate(hp_organizer);
}

// 3) 内存回收测试 (DrainAll)
TEST_F(LockFreeStackFixture, DrainAll_ForceCollectEverything) {
    auto* hp_organizer = new (ThreadHeap::allocate(sizeof(StackHpOrganizer))) StackHpOrganizer();
    auto* st           = new (ThreadHeap::allocate(sizeof(Stack))) Stack(*hp_organizer);

    // Push 3 个元素
    for (int i = 0; i < 3; ++i) st->push(i);

    // Pop 3 个元素 -> 这会导致 3 个节点被 retire 到 Organizer 中
    int out = 0;
    for (int i = 0; i < 3; ++i) ASSERT_TRUE(st->tryPop(out));
    
    EXPECT_TRUE(st->isEmpty());
    
    // 此时节点仍在 Organizer 的待回收列表中（可能是线程局部的，也可能是全局的）
    // 调用 drainAllRetired 强制回收所有内容
    std::size_t freed = hp_organizer->drainAllRetired();
    
    // 断言我们回收了正好 3 个节点
    EXPECT_EQ(freed, 3u);
    
    // 再次调用应该没有东西可回收
    EXPECT_EQ(hp_organizer->drainAllRetired(), 0u);

    st->~Stack();
    ThreadHeap::deallocate(st);
    hp_organizer->~StackHpOrganizer();
    ThreadHeap::deallocate(hp_organizer);
}

// 4) 多线程并发竞争测试
TEST_F(LockFreeStackFixture, ConcurrentPushPop_NoDataLossOrCorruption) {
    const int num_producers = 4;
    const int num_consumers = 4;
    const int items_per_producer = 10000;
    const int total_items = num_producers * items_per_producer;

    auto* hp_organizer = new (ThreadHeap::allocate(sizeof(StackHpOrganizer))) StackHpOrganizer();
    auto* st           = new (ThreadHeap::allocate(sizeof(Stack))) Stack(*hp_organizer);

    std::atomic<int> items_pushed(0);
    std::atomic<int> items_popped(0);
    // *** 新增：用于统计所有线程实际释放的内存总数 ***
    std::atomic<size_t> total_freed_count(0);
    
    std::vector<std::thread> all_threads;
    std::vector<std::vector<int>> consumer_results(num_consumers);

    auto producer_task = [&](int start_value) {
        for (int i = 0; i < items_per_producer; ++i) {
            st->push(start_value + i);
            items_pushed.fetch_add(1, std::memory_order_release);
        }
    };

    auto consumer_task = [&](std::vector<int>& local_results) {
        local_results.reserve(items_per_producer * 2);
        int item;
        while (items_popped.load(std::memory_order_acquire) < total_items) {
            if (st->tryPop(item)) {
                local_results.push_back(item);
                items_popped.fetch_add(1, std::memory_order_acq_rel);
            } else {
                if (items_pushed.load(std::memory_order_acquire) < total_items) {
                    std::this_thread::yield();
                }
            }
        }
        
        // *** 关键修复 ***
        // 线程结束前，必须主动清理自己积压的 retired 节点。
        // 因为线程销毁后，主线程可能无法访问该线程的 TLS 缓存。
        // drainAllRetired() 在这里会扫描当前线程的 HP 保护情况并尽可能释放内存。
        size_t local_freed = hp_organizer->drainAllRetired();
        total_freed_count.fetch_add(local_freed, std::memory_order_relaxed);
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

    // 验证逻辑保持不变
    EXPECT_EQ(items_popped.load(), total_items);
    EXPECT_TRUE(st->isEmpty());

    std::vector<int> all_popped_items;
    all_popped_items.reserve(total_items);
    for (const auto& vec : consumer_results) {
        all_popped_items.insert(all_popped_items.end(), vec.begin(), vec.end());
    }
    std::sort(all_popped_items.begin(), all_popped_items.end());

    std::vector<int> expected_items(total_items);
    std::iota(expected_items.begin(), expected_items.end(), 0);

    ASSERT_EQ(all_popped_items.size(), (size_t)total_items);
    EXPECT_EQ(all_popped_items, expected_items);
    
    // *** 关键修复验证 ***
    // 主线程再做最后一次兜底清理（处理可能的残留或全局列表）
    size_t final_sweep = hp_organizer->drainAllRetired();
    total_freed_count.fetch_add(final_sweep, std::memory_order_relaxed);

    // 现在我们断言：所有线程回收的总数 + 主线程回收的总数 == 总节点数
    EXPECT_EQ(total_freed_count.load(), (size_t)total_items);

    // 清理
    st->~Stack();
    ThreadHeap::deallocate(st);
    hp_organizer->~StackHpOrganizer();
    ThreadHeap::deallocate(hp_organizer);
}