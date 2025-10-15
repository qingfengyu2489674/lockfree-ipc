#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <unordered_set>
#include <atomic>

#include "fixtures/ThreadHeapTestFixture.hpp"
#include "LockFreeStack/StackNode.hpp"
#include "LockFreeStack/HpSlot.hpp"
#include "LockFreeStack/HpSlotManager.hpp"

using TestNode = StackNode<int>;
using Manager  = HpSlotManager<TestNode>;

// 便捷：构建一条链（vector 的后端为栈顶顺序）
static TestNode* build_list_lifo(const std::vector<int>& vals) {
    TestNode* head = nullptr;
    for (int v : vals) {
        auto* n = new TestNode(v);
        n->next = head;
        head = n;
    }
    return head;
}

// 便捷：回收整条链
static void delete_list(TestNode* head) {
    while (head) {
        TestNode* nx = head->next;
        delete head;
        head = nx;
    }
}

class HpSlotManagerFixture : public ThreadHeapTestFixture {};

// ---------- 1) 基础：protect / clear 可见性 ----------
TEST_F(HpSlotManagerFixture, HpSlot_ProtectClear_BasicVisibility) {
    HpSlot<TestNode> slot;

    // 初始为空
    EXPECT_EQ(slot.hazard_ptr.load(std::memory_order_acquire), nullptr);

    // 设置保护指针
    TestNode a(1);
    slot.protect(&a);
    EXPECT_EQ(slot.hazard_ptr.load(std::memory_order_acquire), &a);

    // 清空
    slot.clear();
    EXPECT_EQ(slot.hazard_ptr.load(std::memory_order_acquire), nullptr);
}

// ---------- 2) 基础：pushRetired / drainAll 顺序与清空 ----------
TEST_F(HpSlotManagerFixture, HpSlot_PushRetired_Then_DrainAll_LIFOAndClear) {
    HpSlot<TestNode> slot;

    // 依次 pushRetired: 1, 2, 3 => 链应为 3->2->1
    auto* n1 = new TestNode(1);
    auto* n2 = new TestNode(2);
    auto* n3 = new TestNode(3);

    slot.pushRetired(n1);
    slot.pushRetired(n2);
    slot.pushRetired(n3);

    // drainAll 一次拿空
    TestNode* head = slot.drainAll();
    EXPECT_NE(head, nullptr);
    EXPECT_EQ(slot.retired_head.load(std::memory_order_acquire), nullptr);

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

// ---------- 3) 收集者尊重危险指针的流程模拟 ----------
TEST_F(HpSlotManagerFixture, Collector_Respects_HazardPointers) {
    HpSlot<TestNode> slotA;
    HpSlot<TestNode> slotB;

    // 在 B 中压入若干退休节点：1..5（头插后链为 5->4->3->2->1）
    std::vector<int> vals = {1, 2, 3, 4, 5};
    for (int v : vals) slotB.pushRetired(new TestNode(v));

    // 模拟：A 正在保护“节点 3”（我们得先拿到指针）
    TestNode* all = slotB.drainAll();
    ASSERT_NE(all, nullptr);

    TestNode* protected_node = nullptr;
    for (TestNode* p = all; p; p = p->next) {
        if (p->value == 3) { protected_node = p; break; }
    }
    ASSERT_NE(protected_node, nullptr);

    // 把整段挂回去
    std::vector<TestNode*> nodes;
    for (TestNode* p = all; p; p = p->next) nodes.push_back(p);
    for (TestNode* p : nodes) { p->next = nullptr; slotB.pushRetired(p); }
    all = nullptr;

    // 设置危险指针：slotA 保护“3”
    slotA.protect(protected_node);

    // ---- 收集者流程：----
    std::unordered_set<TestNode*> live;
    live.insert(static_cast<TestNode*>(slotA.hazard_ptr.load(std::memory_order_acquire)));

    TestNode* drained = slotB.drainAll();
    ASSERT_NE(drained, nullptr);

    std::size_t freed = 0;
    std::vector<TestNode*> survivors;
    for (TestNode* p = drained; p; ) {
        TestNode* nx = p->next;
        if (live.count(p) == 0) {
            delete p; ++freed;
        } else {
            survivors.push_back(p); // 暂存：下轮再试
        }
        p = nx;
    }

    // 目前应只留下“3”一个存活
    ASSERT_EQ(survivors.size(), 1u);
    EXPECT_EQ(survivors[0], protected_node);

    // 取消保护，再做第二轮收集
    slotA.clear();
    live.clear();

    for (TestNode* p : survivors) { p->next = nullptr; slotB.pushRetired(p); }
    survivors.clear();

    TestNode* drained2 = slotB.drainAll();
    std::size_t freed2 = 0;
    for (TestNode* p = drained2; p; ) {
        TestNode* nx = p->next;
        delete p; ++freed2;
        p = nx;
    }

    EXPECT_GT(freed, 0u);
    EXPECT_EQ(freed2, 1u);  // 第二轮释放了被保护的那个
}

// ---------- 4) Manager：同批线程注册 → 主线程检查 → 同批线程清理 ----------
TEST_F(HpSlotManagerFixture, Manager_Register_Count_Then_Cleanup_InSameThreads) {
    Manager mgr;

    const int kThreads = std::max(4u, std::thread::hardware_concurrency());
    std::vector<std::thread> workers;
    workers.reserve(kThreads);

    std::atomic<int> registered{0};
    std::atomic<bool> release_to_cleanup{false};

    // 同一批线程：先注册并等待主线程检查计数，再各自注销后退出
    for (int i = 0; i < kThreads; ++i) {
        workers.emplace_back([&] {
            auto* slot = mgr.acquireTls();
            ASSERT_NE(slot, nullptr);
            registered.fetch_add(1, std::memory_order_relaxed);

            // 等待主线程检查计数
            while (!release_to_cleanup.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            mgr.unregisterTls();
        });
    }

    // 等待所有线程都完成注册
    while (registered.load(std::memory_order_acquire) != kThreads) {
        std::this_thread::yield();
    }

    // 验证计数
    EXPECT_EQ(mgr.getSlotCount(), static_cast<std::size_t>(kThreads));

    // 允许线程清理
    release_to_cleanup.store(true, std::memory_order_release);
    for (auto& th : workers) th.join();

    EXPECT_EQ(mgr.getSlotCount(), 0u);
}
