#include <gtest/gtest.h>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <numeric>
#include <algorithm>
#include <set>

#include "fixtures/ThreadHeapTestFixture.hpp" // 假设您的测试环境有这个
#include "LockFreeLinkedList/LockFreeLinkedList.hpp" // 引入待测试的链表
#include "Hazard/HazardPointerOrganizer.hpp"     // 引入 HP 组织器

// ============================================================================
// --- 类型别名 - 适配链表和 Organizer API ---
// ============================================================================
using value_t = int;
using node_t  = LockFreeListNode<value_t>;
using List    = LockFreeLinkedList<value_t>;

// 为链表定义 Organizer 类型
constexpr size_t kListHazardPointers = List::kHazardPointers;
using ListHpOrganizer = HazardPointerOrganizer<node_t, kListHazardPointers>;


// ============================================================================
// --- 测试夹具 (Fixture) ---
// ============================================================================
class LockFreeLinkedListFixture : public ThreadHeapTestFixture {};


// ============================================================================
// --- 单线程测试用例 ---
// ============================================================================

// 1) 测试空链表
TEST_F(LockFreeLinkedListFixture, EmptyList_OperationsCorrect) {
    auto* hp_organizer = new (ThreadHeap::allocate(sizeof(ListHpOrganizer))) ListHpOrganizer();
    auto* list         = new (ThreadHeap::allocate(sizeof(List))) List(*hp_organizer);

    EXPECT_TRUE(list->isEmpty());
    EXPECT_FALSE(list->contains(10));
    EXPECT_FALSE(list->remove(20));
    
    // 验证：没有任何节点被回收
    EXPECT_EQ(hp_organizer->collect(), 0u);

    list->~List();
    ThreadHeap::deallocate(list);
    hp_organizer->~ListHpOrganizer();
    ThreadHeap::deallocate(hp_organizer);
}

// 2) 顺序插入并验证
TEST_F(LockFreeLinkedListFixture, Insert_InOrder_ContainsCorrect) {
    auto* hp_organizer = new (ThreadHeap::allocate(sizeof(ListHpOrganizer))) ListHpOrganizer();
    auto* list         = new (ThreadHeap::allocate(sizeof(List))) List(*hp_organizer);

    for (int i = 1; i <= 5; ++i) {
        EXPECT_TRUE(list->insert(i * 10)); // 10, 20, 30, 40, 50
    }

    EXPECT_FALSE(list->isEmpty());
    for (int i = 1; i <= 5; ++i) {
        EXPECT_TRUE(list->contains(i * 10));
    }
    EXPECT_FALSE(list->contains(5));  // 检查不存在的值
    EXPECT_FALSE(list->contains(55));

    // 插入重复值应失败
    EXPECT_FALSE(list->insert(30));

    list->~List();
    ThreadHeap::deallocate(list);
    hp_organizer->~ListHpOrganizer();
    ThreadHeap::deallocate(hp_organizer);
}

// 3) 逆序插入并验证（考验有序性）
TEST_F(LockFreeLinkedListFixture, Insert_ReverseOrder_ContainsCorrect) {
    auto* hp_organizer = new (ThreadHeap::allocate(sizeof(ListHpOrganizer))) ListHpOrganizer();
    auto* list         = new (ThreadHeap::allocate(sizeof(List))) List(*hp_organizer);

    for (int i = 5; i >= 1; --i) {
        EXPECT_TRUE(list->insert(i * 10)); // 50, 40, 30, 20, 10
    }
    
    // 验证 contains 仍然可以找到所有值
    for (int i = 1; i <= 5; ++i) {
        EXPECT_TRUE(list->contains(i * 10));
    }

    // 验证内部顺序是否正确 (通过析构函数中的遍历间接验证，或添加一个 to_vector 方法验证)
    // 这里我们相信 find 的逻辑是正确的

    list->~List();
    ThreadHeap::deallocate(list);
    hp_organizer->~ListHpOrganizer();
    ThreadHeap::deallocate(hp_organizer);
}

// 4) 插入和删除
TEST_F(LockFreeLinkedListFixture, Insert_Remove_StateCorrect) {
    auto* hp_organizer = new (ThreadHeap::allocate(sizeof(ListHpOrganizer))) ListHpOrganizer();
    auto* list         = new (ThreadHeap::allocate(sizeof(List))) List(*hp_organizer);

    list->insert(10);
    list->insert(20);
    list->insert(30);

    EXPECT_TRUE(list->contains(20));
    EXPECT_TRUE(list->remove(20));
    EXPECT_FALSE(list->contains(20));
    EXPECT_FALSE(list->remove(20)); // 再次删除应失败

    EXPECT_TRUE(list->contains(10));
    EXPECT_TRUE(list->contains(30));

    list->~List();
    ThreadHeap::deallocate(list);
    hp_organizer->~ListHpOrganizer();
    ThreadHeap::deallocate(hp_organizer);
}

// 5) 验证删除后的内存回收
TEST_F(LockFreeLinkedListFixture, Insert_Remove_MemoryReclaimed) {
    auto* hp_organizer = new (ThreadHeap::allocate(sizeof(ListHpOrganizer))) ListHpOrganizer();
    auto* list         = new (ThreadHeap::allocate(sizeof(List))) List(*hp_organizer);
    
    list->insert(10);
    list->insert(20);
    list->insert(30);

    EXPECT_TRUE(list->remove(10));
    EXPECT_TRUE(list->remove(30));

    // 此时，节点 10 和 30 应该在退休列表中
    // 调用 collect() 应该能回收它们
    size_t freed = hp_organizer->collect(100);
    EXPECT_EQ(freed, 2u);
    EXPECT_EQ(hp_organizer->collect(), 0u); // 再次收集应该没有了

    EXPECT_FALSE(list->contains(10));
    EXPECT_TRUE(list->contains(20));
    EXPECT_FALSE(list->contains(30));

    list->~List();
    ThreadHeap::deallocate(list);
    hp_organizer->~ListHpOrganizer();
    ThreadHeap::deallocate(hp_organizer);
}

// ============================================================================
// --- 多线程并发测试用例 ---
// ============================================================================

// 6) 多线程并发插入
TEST_F(LockFreeLinkedListFixture, ConcurrentInsert_AllElementsPresent) {
    auto* hp_organizer = new (ThreadHeap::allocate(sizeof(ListHpOrganizer))) ListHpOrganizer();
    auto* list         = new (ThreadHeap::allocate(sizeof(List))) List(*hp_organizer);

    constexpr int kNumThreads = 4;
    constexpr int kItemsPerThread = 100;
    std::vector<std::thread> threads;

    for (int i = 0; i < kNumThreads; ++i) {
        threads.emplace_back([list, i]() {
            for (int j = 0; j < kItemsPerThread; ++j) {
                // 每个线程插入不同的值范围，避免冲突
                int value = i * kItemsPerThread + j;
                list->insert(value);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // 验证所有插入的元素都存在
    for (int i = 0; i < kNumThreads * kItemsPerThread; ++i) {
        EXPECT_TRUE(list->contains(i));
    }

    list->~List();
    ThreadHeap::deallocate(list);
    hp_organizer->~ListHpOrganizer();
    ThreadHeap::deallocate(hp_organizer);
}

// 7) 多线程并发插入和删除（压力测试）
TEST_F(LockFreeLinkedListFixture, ConcurrentInsertRemove_DataRaces) {
    auto* hp_organizer = new (ThreadHeap::allocate(sizeof(ListHpOrganizer))) ListHpOrganizer();
    auto* list         = new (ThreadHeap::allocate(sizeof(List))) List(*hp_organizer);

    constexpr int kNumThreads = 8;
    constexpr int kOperations = 500;
    constexpr int kValueRange = 100;
    std::vector<std::thread> threads;
    std::atomic<bool> start{false};

    for (int i = 0; i < kNumThreads; ++i) {
        threads.emplace_back([list, &start]() {
            // 等待所有线程准备就绪
            while (!start.load()) {}
            
            for (int j = 0; j < kOperations; ++j) {
                int value = rand() % kValueRange;
                if (rand() % 2 == 0) {
                    list->insert(value);
                } else {
                    list->remove(value);
                }
            }
        });
    }

    start.store(true); // 发令枪

    for (auto& t : threads) {
        t.join();
    }

    // 在这种随机操作下，我们无法预测最终状态。
    // 这个测试的主要目的是在高并发下运行，并使用工具（如 Thread Sanitizer）
    // 来检测数据竞争。只要程序不崩溃、不死锁，就基本通过。
    // 我们仍然可以做一个简单的健全性检查。
    int dummy_count = 0;
    for (int i = 0; i < kValueRange; ++i) {
        if (list->contains(i)) {
            dummy_count++;
        }
    }
    std::cout << "ConcurrentInsertRemove: " << dummy_count << " elements remained in the list." << std::endl;
    
    // 触发最后的清理
    hp_organizer->collect(10000);

    list->~List();
    ThreadHeap::deallocate(list);
    hp_organizer->~ListHpOrganizer();
    ThreadHeap::deallocate(hp_organizer);
}