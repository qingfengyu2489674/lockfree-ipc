// tests/skiplist_node_tests.cpp
#include <gtest/gtest.h>
#include <cstdint>
#include <string>

#include "fixtures/ThreadHeapTestFixture.hpp"
#include "LockFreeSkipList/LockFreeSkipListNode.hpp"
#include "Tool/StampPtrPacker.hpp"

using K = int;
using V = int;
using Node   = LockFreeSkipListNode<K, V>;
using Packer = StampPtrPacker<Node>;
using Packed = typename Packer::type;

// 测试夹具：确保 ThreadHeap 进程/线程上下文就绪
class SkipListNodeFixture : public ThreadHeapTestFixture {
protected:
    void SetUp() override { ThreadHeapTestFixture::SetUp(); }
    void TearDown() override { ThreadHeapTestFixture::TearDown(); }
};

// -----------------------------------------------------------------------------
// 1) 创建与初始化：forward_ 全为 nullptr、戳为 0
// -----------------------------------------------------------------------------
TEST_F(SkipListNodeFixture, CreateAndInitForward) {
    int h = 4;
    Node* n = Node::create(123, 456, h);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(n->key, 123);
    EXPECT_EQ(n->value, 456);
    EXPECT_EQ(n->height, h);

    for (int i = 0; i < h; ++i) {
        Packed p = n->nextSlot(i).load(std::memory_order_relaxed);
        EXPECT_EQ(Packer::unpackPtr(p), nullptr);
        EXPECT_EQ(Packer::unpackStamp(p), 0);
    }
    Node::destroy(n);
}

// -----------------------------------------------------------------------------
// 2) CAS 成功应自增戳：null -> b（level 0）
// -----------------------------------------------------------------------------
TEST_F(SkipListNodeFixture, CasNextBumpsStamp) {
    Node* a = Node::create(1, 10, /*h*/2);
    Node* b = Node::create(2, 20, /*h*/2);

    auto& slot = a->nextSlot(0);

    // 初始 expected
    auto exp = slot.load(std::memory_order_acquire);
    ASSERT_EQ(Packer::unpackPtr(exp), nullptr);
    auto old_stamp = Packer::unpackStamp(exp);

    // 让 b 的 next[0] = 当前 expected.ptr（这里为 nullptr）
    b->nextSlot(0).store(Packer::pack(Packer::unpackPtr(exp), 0), std::memory_order_relaxed);

    // CAS：a->next[0] 从 exp 改为 b，内部会用 exp.stamp+1 作为新戳
    bool ok = Packer::casBump(slot, exp, b,
                              std::memory_order_release,
                              std::memory_order_acquire);
    ASSERT_TRUE(ok);

    auto cur = slot.load(std::memory_order_acquire);
    EXPECT_EQ(Packer::unpackPtr(cur), b);
    EXPECT_EQ(Packer::unpackStamp(cur), static_cast<uint16_t>(old_stamp + 1));

    Node::destroy(b);
    Node::destroy(a);
}

// -----------------------------------------------------------------------------
// 3) CAS 失败 -> expected 被刷新 -> 重算 desired 再次 CAS
// -----------------------------------------------------------------------------
TEST_F(SkipListNodeFixture, CasNextFailsThenRetries) {
    Node* a   = Node::create(1, 10, 1);
    Node* mid = Node::create(2, 20, 1);
    Node* b   = Node::create(3, 30, 1);

    auto& slot = a->nextSlot(0);

    // 先把 a->next = mid
    {
        auto exp0 = slot.load(std::memory_order_acquire);
        mid->nextSlot(0).store(Packer::pack(Packer::unpackPtr(exp0), 0),
                               std::memory_order_relaxed);
        bool ok0 = Packer::casBump(slot, exp0, mid,
                                   std::memory_order_release,
                                   std::memory_order_acquire);
        ASSERT_TRUE(ok0);
    }

    // 这里模拟“过期 expected”：我们以为 a->next 还是 nullptr（其实已经是 mid）
    auto exp = Packer::pack(nullptr, 0);
    b->nextSlot(0).store(Packer::pack(nullptr, 0), std::memory_order_relaxed);

    // 按过期视图去 CAS，应该失败，并且 exp 会被刷新成当前真实值（即指向 mid）
    bool ok = Packer::casBump(slot, exp, b,
                              std::memory_order_release,
                              std::memory_order_acquire);
    EXPECT_FALSE(ok);

    // 用刷新后的 exp 重新构造 b->next，然后再 CAS
    b->nextSlot(0).store(Packer::pack(Packer::unpackPtr(exp), 0),
                         std::memory_order_relaxed);
    ok = Packer::casBump(slot, exp, b,
                         std::memory_order_release,
                         std::memory_order_acquire);
    EXPECT_TRUE(ok);

    auto cur = slot.load(std::memory_order_acquire);
    EXPECT_EQ(Packer::unpackPtr(cur), b);

    Node::destroy(b);
    Node::destroy(mid);
    Node::destroy(a);
}
