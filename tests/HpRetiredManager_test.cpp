// tests/HpRetiredManager_test.cpp
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <unordered_set>
#include <atomic>

#include "fixtures/ThreadHeapTestFixture.hpp"

#include "LockFreeStack/StackNode.hpp"
#include "LockFreeStack/HpRetiredManager.hpp"


using TestNode = StackNode<int>;
using RetiredMgr = HpRetiredManager<TestNode>;

// ---- helpers ---------------------------------------------------------------

static TestNode* build_list_lifo(const std::vector<int>& vals) {
    TestNode* head = nullptr;
    for (int v : vals) {
        auto* n = new TestNode(v);
        n->next = head;
        head = n;
    }
    return head;
}

static void split_detach(TestNode* head, std::vector<TestNode*>& out) {
    for (TestNode* p = head; p; p = p->next) out.push_back(p);
    for (TestNode* p : out) p->next = nullptr;
}

static void reclaim_delete(TestNode* n) noexcept { delete n; }

static bool in_const_set(const void* ctx, const TestNode* p) noexcept {
    const auto* s = static_cast<const std::unordered_set<const TestNode*>*>(ctx);
    return s && (s->find(p) != s->end());
}

class HpRetiredManagerFixture : public ThreadHeapTestFixture {};

// ============================================================================
// 1) 基础：append 单节点 & 统计 & 全量回收
// ============================================================================
TEST_F(HpRetiredManagerFixture, AppendSingleNodes_Then_CollectAll) {
    RetiredMgr mgr;

    // append: 1,2,3
    mgr.appendRetiredNodeToList(new TestNode(1));
    mgr.appendRetiredNodeToList(new TestNode(2));
    mgr.appendRetiredNodeToList(new TestNode(3));

    EXPECT_EQ(mgr.getRetiredCount(), 3u);

    // 无 hazard，全量回收
    const std::unordered_set<const TestNode*> empty{};
    std::size_t freed = mgr.collect(/*quota*/ 1000, &empty, &in_const_set, &reclaim_delete);
    EXPECT_EQ(freed, 3u);
    EXPECT_EQ(mgr.getRetiredCount(), 0u);
}

// ============================================================================
// 2) 基础：append 整段链表 & 配额回收（quota）
// ============================================================================
TEST_F(HpRetiredManagerFixture, AppendList_Then_CollectWithQuota) {
    RetiredMgr mgr;

    // 构建 1..5 的链并整段并入
    TestNode* head = build_list_lifo({1,2,3,4,5}); // 5->4->3->2->1
    mgr.appendRetiredListToList(head);

    EXPECT_EQ(mgr.getRetiredCount(), 5u);

    const std::unordered_set<const TestNode*> none{};
    // 限额回收 2 个
    std::size_t freed = mgr.collect(/*quota*/ 2, &none, &in_const_set, &reclaim_delete);
    EXPECT_EQ(freed, 2u);
    EXPECT_EQ(mgr.getRetiredCount(), 3u);

    // 再回收剩余
    freed = mgr.collect(/*quota*/ 1000, &none, &in_const_set, &reclaim_delete);
    EXPECT_EQ(freed, 3u);
    EXPECT_EQ(mgr.getRetiredCount(), 0u);
}

// ============================================================================
// 3) Hazard 判定：受保护节点应被保留，取消保护后可回收
// ============================================================================
TEST_F(HpRetiredManagerFixture, HazardProtectedNode_IsDeferred_UntilUnprotected) {
    RetiredMgr mgr;

    // 构建 1..5
    TestNode* list = build_list_lifo({1,2,3,4,5}); // 5..1
    // 找到 value==3 的节点
    TestNode* protected_node = nullptr;
    for (TestNode* p = list; p; p = p->next) {
        if (p->value == 3) { protected_node = p; break; }
    }
    ASSERT_NE(protected_node, nullptr);

    // 并入
    mgr.appendRetiredListToList(list);
    EXPECT_EQ(mgr.getRetiredCount(), 5u);

    // hazard 集：保护“3”
    std::unordered_set<const TestNode*> hz;
    hz.insert(protected_node);

    // 第一次回收：应释放 4 个，仅保留受保护的那个
    std::size_t freed = mgr.collect(/*quota*/ 1000, &hz, &in_const_set, &reclaim_delete);
    EXPECT_EQ(freed, 4u);
    EXPECT_EQ(mgr.getRetiredCount(), 1u);

    // 取消保护
    hz.clear();

    // 第二次回收：释放剩下的 1 个
    freed = mgr.collect(/*quota*/ 1000, &hz, &in_const_set, &reclaim_delete);
    EXPECT_EQ(freed, 1u);
    EXPECT_EQ(mgr.getRetiredCount(), 0u);
}

// ============================================================================
// 4) drainAll：无条件回收全部（停机/析构场景）
// ============================================================================
TEST_F(HpRetiredManagerFixture, DrainAll_ForceReclaimEverything) {
    RetiredMgr mgr;

    // append 多个
    for (int i = 0; i < 10; ++i) mgr.appendRetiredNodeToList(new TestNode(i));
    EXPECT_EQ(mgr.getRetiredCount(), 10u);

    // 强制回收全部
    std::size_t freed = mgr.drainAll(&reclaim_delete);
    EXPECT_EQ(freed, 10u);
    EXPECT_EQ(mgr.getRetiredCount(), 0u);
}

// ============================================================================
// 5) 并发：多线程并发 append，然后一次性回收
// ============================================================================
TEST_F(HpRetiredManagerFixture, MultiThreads_Append_Then_Collect) {
    RetiredMgr mgr;

    const int kThreads = std::max(4u, std::thread::hardware_concurrency());
    const int kPerThreadNodes = 64;

    std::vector<std::thread> ths;
    ths.reserve(kThreads);

    for (int t = 0; t < kThreads; ++t) {
        ths.emplace_back([&mgr, kPerThreadNodes, t] {
            // 构建一段链并整段并入
            std::vector<int> vals;
            vals.reserve(kPerThreadNodes);
            for (int i = 0; i < kPerThreadNodes; ++i) vals.push_back(t * 100000 + i);
            TestNode* head = build_list_lifo(vals);
            mgr.appendRetiredListToList(head);

            // 也混入若干单节点 append
            for (int i = 0; i < 8; ++i) mgr.appendRetiredNodeToList(new TestNode(-(t * 1000 + i)));
        });
    }
    for (auto& th : ths) th.join();

    const std::size_t expect = static_cast<std::size_t>(kThreads) * (kPerThreadNodes + 8);
    EXPECT_EQ(mgr.getRetiredCount(), expect);

    // 无 hazard，全量回收
    const std::unordered_set<const TestNode*> none{};
    std::size_t freed = 0;
    while (mgr.getRetiredCount() > 0) {
        freed += mgr.collect(/*quota*/ 2048, &none, &in_const_set, &reclaim_delete);
    }

    EXPECT_EQ(freed, expect);
    EXPECT_EQ(mgr.getRetiredCount(), 0u);
}
