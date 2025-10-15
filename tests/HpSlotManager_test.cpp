// tests/LegacyHp_gtest.cpp

#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <unordered_set>
#include <atomic>
#include <algorithm> // for std::max

// 包含所有依赖的头文件
#include "fixtures/ThreadHeapTestFixture.hpp"
#include "LockFreeStack/StackNode.hpp"
#include "LockFreeStack/HpSlot.hpp"
#include "LockFreeStack/HpSlotManager.hpp"

// ============================================================================
// --- 类型别名和辅助函数，适配新的模板 ---
// ============================================================================

using TestNode = StackNode<int>;

// 为这个测试套件定义需要 2 个危险指针的组件，以覆盖多指针场景
constexpr size_t kNumTestPointers = 2;
using TestSlot = HpSlot<TestNode, kNumTestPointers>;
using TestManager = HpSlotManager<TestNode, kNumTestPointers>;

// 便捷：构建一条链
static TestNode* build_list_lifo(const std::vector<int>& vals) {
    TestNode* head = nullptr;
    for (int v : vals) {
        auto* n = new (ThreadHeap::allocate(sizeof(TestNode))) TestNode(v);
        n->next = head;
        head = n;
    }
    return head;
}

// 便捷：回收整条链
static void delete_list(TestNode* head) {
    while (head) {
        TestNode* nx = head->next;
        head->~TestNode();
        ThreadHeap::deallocate(head);
        head = nx;
    }
}

// ============================================================================
// --- 测试夹具 ---
// ============================================================================
class HpSlotManagerTest : public ThreadHeapTestFixture {};


// ============================================================================
// --- 测试用例 (已全部适配) ---
// ============================================================================

// ---------- 1) 基础：protect / clear 可见性 ----------
TEST_F(HpSlotManagerTest, HpSlot_ProtectClear_BasicVisibility) {
    TestSlot slot; // <-- 使用新的模板类型

    // 初始为空
    EXPECT_EQ(slot.getHazardPointerAt(0).load(std::memory_order_acquire), nullptr);

    // 设置保护指针
    TestNode a(1);
    slot.protect(0, &a); // <-- 使用带索引的 protect
    EXPECT_EQ(slot.getHazardPointerAt(0).load(std::memory_order_acquire), &a);

    // 清空
    slot.clear(0); // <-- 使用带索引的 clear
    EXPECT_EQ(slot.getHazardPointerAt(0).load(std::memory_order_acquire), nullptr);
}

// ---------- 2) 基础：pushRetired / drainAllRetired 顺序与清空 ----------
TEST_F(HpSlotManagerTest, HpSlot_PushRetired_Then_DrainAll_LIFOAndClear) {
    TestSlot slot; // <-- 使用新的模板类型

    // 依次 pushRetired: 1, 2, 3 => 链应为 3->2->1
    auto* n1 = new (ThreadHeap::allocate(sizeof(TestNode))) TestNode(1);
    auto* n2 = new (ThreadHeap::allocate(sizeof(TestNode))) TestNode(2);
    auto* n3 = new (ThreadHeap::allocate(sizeof(TestNode))) TestNode(3);

    slot.pushRetired(n1);
    slot.pushRetired(n2);
    slot.pushRetired(n3);

    // drainAllRetired 一次拿空
    TestNode* head = slot.drainAllRetired();
    EXPECT_NE(head, nullptr);
    EXPECT_EQ(slot.getRetiredListHead().load(std::memory_order_acquire), nullptr); // <-- 使用新的 getter

    // 校验 LIFO 顺序：3, 2, 1
    ASSERT_NE(head, nullptr);
    EXPECT_EQ(head->value, 3);
    ASSERT_NE(head->next, nullptr);
    EXPECT_EQ(head->next->value, 2);
    ASSERT_NE(head->next->next, nullptr);
    EXPECT_EQ(head->next->next->value, 1);
    EXPECT_EQ(head->next->next->next, nullptr);

    delete_list(head); // 回收
}

// ---------- 3) Manager：线程注册、计数和自动清理 ----------
TEST_F(HpSlotManagerTest, Manager_RegisterCountAndAutoCleanup) {
    // 在共享内存中创建管理器
    auto* mgr = new (ThreadHeap::allocate(sizeof(TestManager))) TestManager();

    const int kThreads = std::max(4u, std::thread::hardware_concurrency());
    std::vector<std::thread> workers;
    workers.reserve(kThreads);

    // 启动线程，每个线程获取自己的槽位
    for (int i = 0; i < kThreads; ++i) {
        workers.emplace_back([mgr] {
            auto* slot = mgr->acquireTls();
            ASSERT_NE(slot, nullptr);
            // 保持线程存活一小段时间
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            // 线程退出时，析构函数应自动调用 unregisterTls
        });
    }

    // 在线程运行期间检查计数
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(mgr->getSlotCount(), static_cast<std::size_t>(kThreads));

    // 等待所有线程结束
    for (auto& th : workers) th.join();

    // 所有线程退出后，槽位应被自动清理
    // 等待一小会儿，以防清理有延迟
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(mgr->getSlotCount(), 0u);

    // 手动清理管理器本身
    mgr->~TestManager();
    ThreadHeap::deallocate(mgr);
}