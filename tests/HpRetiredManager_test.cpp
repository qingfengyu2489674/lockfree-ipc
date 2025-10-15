// tests/HpRetiredManager_test.cpp
#include <gtest/gtest.h>
#include <vector>
#include <unordered_set>
#include <algorithm>
#include <thread>

#include "fixtures/ThreadHeapTestFixture.hpp"
#include "LockFreeStack/StackNode.hpp"
#include "LockFreeStack/HpRetiredManager.hpp"

using TestNode   = StackNode<int>;
using RetiredMgr = HpRetiredManager<TestNode>;

static TestNode* build_list_lifo(std::initializer_list<int> vals) {
    TestNode* head = nullptr;
    for (int v : vals) {
        auto* n = new TestNode(v);
        n->next = head;
        head = n;               // 头插，最终顺序为 last->...->first
    }
    return head;
}
static void reclaim_delete(TestNode* n) noexcept { delete n; }
static std::vector<const TestNode*> snapshot_from_set(const std::unordered_set<const TestNode*>& s) {
    return std::vector<const TestNode*>{s.begin(), s.end()};
}

class HpRetiredManagerFixture : public ThreadHeapTestFixture {};

// ============================================================================
// 1) 单节点并入 & 统计 & collect 全量回收
// ============================================================================
TEST_F(HpRetiredManagerFixture, AppendSingleNodes_Then_CollectAll) {
    RetiredMgr mgr;
    mgr.appendRetiredNode(new TestNode(1));
    mgr.appendRetiredNode(new TestNode(2));
    mgr.appendRetiredNode(new TestNode(3));

    EXPECT_EQ(mgr.getRetiredCount(), 3u);

    std::vector<const TestNode*> none;  // 空 hazard
    std::size_t freed = mgr.collectRetired(/*quota*/ 1000, none, &reclaim_delete);
    EXPECT_EQ(freed, 3u);
    EXPECT_EQ(mgr.getRetiredCount(), 0u);
}

// ============================================================================
// 2) 整段并入 & 配额回收（quota）
// ============================================================================
TEST_F(HpRetiredManagerFixture, AppendList_Then_CollectWithQuota) {
    RetiredMgr mgr;
    TestNode* head = build_list_lifo({1,2,3,4,5});   // 5->4->3->2->1
    mgr.appendRetiredList(head);
    EXPECT_EQ(mgr.getRetiredCount(), 5u);

    std::vector<const TestNode*> none;
    std::size_t freed = mgr.collectRetired(/*quota*/ 2, none, &reclaim_delete);
    EXPECT_EQ(freed, 2u);
    EXPECT_EQ(mgr.getRetiredCount(), 3u);

    freed = mgr.collectRetired(/*quota*/ 1000, none, &reclaim_delete);
    EXPECT_EQ(freed, 3u);
    EXPECT_EQ(mgr.getRetiredCount(), 0u);
}

// ============================================================================
// 3) Hazard 保护：受保护节点应被保留，取消保护后可回收
// ============================================================================
TEST_F(HpRetiredManagerFixture, HazardProtected_DeferThenCollect) {
    RetiredMgr mgr;
    // 构造 1..5，找到值为 3 的节点作为受保护对象
    TestNode* list = build_list_lifo({1,2,3,4,5});
    TestNode* protected_node = nullptr;
    for (TestNode* p = list; p; p = p->next) if (p->value == 3) { protected_node = p; break; }
    ASSERT_NE(protected_node, nullptr);

    mgr.appendRetiredList(list);
    EXPECT_EQ(mgr.getRetiredCount(), 5u);

    std::unordered_set<const TestNode*> hz{protected_node};
    auto snap = snapshot_from_set(hz);

    // 第一次：应保留受保护的 1 个，其余都回收
    std::size_t freed = mgr.collectRetired(/*quota*/ 1000, snap, &reclaim_delete);
    EXPECT_EQ(freed, 4u);
    EXPECT_EQ(mgr.getRetiredCount(), 1u);

    // 取消保护
    hz.clear();
    std::vector<const TestNode*> none;
    freed = mgr.collectRetired(/*quota*/ 1000, none, &reclaim_delete);
    EXPECT_EQ(freed, 1u);
    EXPECT_EQ(mgr.getRetiredCount(), 0u);
}

// ============================================================================
// 4) drainAll：停机/析构场景，强制回收全部
// ============================================================================
TEST_F(HpRetiredManagerFixture, DrainAll_ReclaimEverything) {
    RetiredMgr mgr;
    for (int i = 0; i < 8; ++i) mgr.appendRetiredNode(new TestNode(i));
    EXPECT_EQ(mgr.getRetiredCount(), 8u);

    std::size_t freed = mgr.drainAll(&reclaim_delete);
    EXPECT_EQ(freed, 8u);
    EXPECT_EQ(mgr.getRetiredCount(), 0u);
}

// ============================================================================
// 5) 并发冒烟：多线程 append，然后一次 collect 清空（规模很小，避免不稳定）
// ============================================================================
TEST_F(HpRetiredManagerFixture, MultiThreads_SmokeAppend_ThenCollectAll) {
    RetiredMgr mgr;

    constexpr int kThreads = 4;
    constexpr int kPerThreadNodes = 16;

    std::vector<std::thread> ths;
    ths.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        ths.emplace_back([&mgr, t] {
            // 整段并入
            TestNode* head = build_list_lifo({t*10+1, t*10+2, t*10+3, t*10+4});
            mgr.appendRetiredList(head);
            // 再并入若干单节点
            for (int i = 0; i < kPerThreadNodes - 4; ++i) mgr.appendRetiredNode(new TestNode(-(t*100+i)));
        });
    }
    for (auto& th : ths) th.join();

    const std::size_t expect = static_cast<std::size_t>(kThreads * kPerThreadNodes);
    EXPECT_EQ(mgr.getRetiredCount(), expect);

    std::vector<const TestNode*> none;
    std::size_t total = 0;
    // 分批收割，避免一次配额过小卡住
    while (mgr.getRetiredCount() > 0) {
        total += mgr.collectRetired(/*quota*/ 256, none, &reclaim_delete);
    }
    EXPECT_EQ(total, expect);
    EXPECT_EQ(mgr.getRetiredCount(), 0u);
}
