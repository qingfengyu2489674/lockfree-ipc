// tests/HpRetiredManager_test.cpp

#include <gtest/gtest.h>
#include <vector>
#include <unordered_set>
#include <algorithm>
#include <thread>
#include <initializer_list> // For std::initializer_list

// 包含所有依赖的头文件
#include "LockFreeStack/StackNode.hpp"
#include "LockFreeStack/HpRetiredManager.hpp"

// ============================================================================
// --- 类型别名和辅助函数 ---
// ============================================================================

using TestNode = StackNode<int>;
// *** 关键修改：使用 StandardAllocPolicy，因为测试代码用了 new/delete ***
using RetiredMgr = HpRetiredManager<TestNode, StandardAllocPolicy>;

// 辅助函数现在也使用 new，与 StandardAllocPolicy 匹配
static TestNode* build_list_lifo(std::initializer_list<int> vals) {
    TestNode* head = nullptr;
    for (int v : vals) {
        auto* n = new TestNode(v);
        n->next = head;
        head = n;
    }
    return head;
}

class HpRetiredManagerTest : public ::testing::Test {}; // 使用简单的 Test Fixture


// ============================================================================
// --- 测试用例 (已全部适配新的 API) ---
// ============================================================================

// 1) 单节点并入 & 统计 & collect 全量回收
TEST_F(HpRetiredManagerTest, AppendSingleNodes_Then_CollectAll) {
    RetiredMgr mgr;
    mgr.appendRetiredNode(new TestNode(1));
    mgr.appendRetiredNode(new TestNode(2));
    mgr.appendRetiredNode(new TestNode(3));

    EXPECT_EQ(mgr.getRetiredCount(), 3u);

    std::vector<const TestNode*> none;
    // *** API 修改：不再需要传递 &reclaim_delete ***
    std::size_t freed = mgr.collectRetired(1000, none);
    EXPECT_EQ(freed, 3u);
    EXPECT_EQ(mgr.getRetiredCount(), 0u);
}

// 2) 整段并入 & 配额回收（quota）
TEST_F(HpRetiredManagerTest, AppendList_Then_CollectWithQuota) {
    RetiredMgr mgr;
    TestNode* head = build_list_lifo({1,2,3,4,5});
    mgr.appendRetiredList(head);
    EXPECT_EQ(mgr.getRetiredCount(), 5u);

    std::vector<const TestNode*> none;
    // *** API 修改：不再需要传递 &reclaim_delete ***
    std::size_t freed = mgr.collectRetired(2, none);
    EXPECT_EQ(freed, 2u);
    EXPECT_EQ(mgr.getRetiredCount(), 3u);

    freed = mgr.collectRetired(1000, none);
    EXPECT_EQ(freed, 3u);
    EXPECT_EQ(mgr.getRetiredCount(), 0u);
}

// 3) Hazard 保护：受保护节点应被保留
TEST_F(HpRetiredManagerTest, HazardProtected_DeferThenCollect) {
    RetiredMgr mgr;
    TestNode* list = build_list_lifo({1,2,3,4,5});
    TestNode* protected_node = nullptr;
    for (TestNode* p = list; p; p = p->next) if (p->value == 3) { protected_node = p; break; }
    ASSERT_NE(protected_node, nullptr);

    mgr.appendRetiredList(list);
    EXPECT_EQ(mgr.getRetiredCount(), 5u);

    std::vector<const TestNode*> snap = {protected_node};

    // *** API 修改：不再需要传递 &reclaim_delete ***
    std::size_t freed = mgr.collectRetired(1000, snap);
    EXPECT_EQ(freed, 4u);
    EXPECT_EQ(mgr.getRetiredCount(), 1u);

    std::vector<const TestNode*> none;
    freed = mgr.collectRetired(1000, none);
    EXPECT_EQ(freed, 1u);
    EXPECT_EQ(mgr.getRetiredCount(), 0u);
}

// 4) drainAll：强制回收全部
TEST_F(HpRetiredManagerTest, DrainAll_ReclaimEverything) {
    RetiredMgr mgr;
    for (int i = 0; i < 8; ++i) mgr.appendRetiredNode(new TestNode(i));
    EXPECT_EQ(mgr.getRetiredCount(), 8u);

    // *** API 修改：不再需要传递 &reclaim_delete ***
    std::size_t freed = mgr.drainAll();
    EXPECT_EQ(freed, 8u);
    EXPECT_EQ(mgr.getRetiredCount(), 0u);
}

// 5) 并发冒烟测试
TEST_F(HpRetiredManagerTest, MultiThreads_SmokeAppend_ThenCollectAll) {
    RetiredMgr mgr;

    constexpr int kThreads = 4;
    constexpr int kPerThreadNodes = 16;

    std::vector<std::thread> ths;
    for (int t = 0; t < kThreads; ++t) {
        ths.emplace_back([&mgr, t] {
            TestNode* head = build_list_lifo({t*10+1, t*10+2});
            mgr.appendRetiredList(head);
            for (int i = 0; i < kPerThreadNodes - 2; ++i) mgr.appendRetiredNode(new TestNode(-(t*100+i)));
        });
    }
    for (auto& th : ths) th.join();

    const std::size_t expect = static_cast<std::size_t>(kThreads * kPerThreadNodes);
    EXPECT_EQ(mgr.getRetiredCount(), expect);

    std::vector<const TestNode*> none;
    std::size_t total = 0;
    while (mgr.getRetiredCount() > 0) {
        // *** API 修改：不再需要传递 &reclaim_delete ***
        total += mgr.collectRetired(256, none);
    }
    EXPECT_EQ(total, expect);
    EXPECT_EQ(mgr.getRetiredCount(), 0u);
}