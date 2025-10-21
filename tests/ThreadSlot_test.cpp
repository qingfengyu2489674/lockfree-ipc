#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <numeric>

// 包含我们需要测试的类
#include "EBRManager/ThreadSlot.hpp"

// ====================================================================
//                       测试夹具 (Test Fixture)
// ====================================================================
// 为每个测试提供一个干净的 ThreadSlot 实例

class ThreadSlotTest : public ::testing::Test {
protected:
    // 每个测试用例都会有一个全新的 ThreadSlot 实例
    ThreadSlot* slot;

    void SetUp() override {
        // 在每个测试开始前，创建一个新的槽位
        slot = new ThreadSlot();
    }

    void TearDown() override {
        // 在每个测试结束后，清理内存
        delete slot;
    }
};

// ====================================================================
//                     单线程逻辑测试用例
// ====================================================================

// 测试：一个新创建的槽位是否处于正确的初始状态
TEST_F(ThreadSlotTest, InitialStateIsCorrect) {
    uint64_t state = slot->loadState();

    // 初始状态应该是：未注册、不活跃、纪元为0
    ASSERT_FALSE(ThreadSlot::isRegistered(state));
    ASSERT_FALSE(ThreadSlot::isActive(state));
    ASSERT_EQ(ThreadSlot::unpackEpoch(state), 0);

    // 验证初始状态的打包值是否为全零
    ASSERT_EQ(state, 0);
}

// 测试：注册/注销的生命周期
TEST_F(ThreadSlotTest, RegisterAndUnregisterCycle) {
    const uint64_t initial_epoch = 5;

    // 1. 尝试在一个未注册的槽位上注册，应该成功
    ASSERT_TRUE(slot->tryRegister(initial_epoch));

    // 验证注册后的状态
    uint64_t registered_state = slot->loadState();
    EXPECT_TRUE(ThreadSlot::isRegistered(registered_state));
    EXPECT_TRUE(ThreadSlot::isActive(registered_state)); // 注册后默认为活跃
    EXPECT_EQ(ThreadSlot::unpackEpoch(registered_state), initial_epoch);

    // 2. 尝试在已经注册的槽位上再次注册，应该失败
    ASSERT_FALSE(slot->tryRegister(initial_epoch + 1));
    // 确认状态没有被错误地修改
    EXPECT_EQ(slot->loadState(), registered_state);

    // 3. 注销该槽位
    slot->unregister();

    // 验证注销后的状态
    uint64_t unregistered_state = slot->loadState();
    EXPECT_FALSE(ThreadSlot::isRegistered(unregistered_state));
    EXPECT_FALSE(ThreadSlot::isActive(unregistered_state));
    // 注销时纪元应该被保留
    EXPECT_EQ(ThreadSlot::unpackEpoch(unregistered_state), initial_epoch);

    // 4. 尝试注销一个已经注销的槽位，应该无害地失败
    slot->unregister();
    EXPECT_EQ(slot->loadState(), unregistered_state);
}

// 测试：进入/离开临界区的状态转换
TEST_F(ThreadSlotTest, EnterAndLeaveCycle) {
    slot->tryRegister(10); // 首先必须注册

    // 初始注册后，状态是活跃的
    ASSERT_TRUE(ThreadSlot::isActive(slot->loadState()));

    // 1. 离开临界区
    slot->leave();
    uint64_t left_state = slot->loadState();
    EXPECT_TRUE(ThreadSlot::isRegistered(left_state));
    EXPECT_FALSE(ThreadSlot::isActive(left_state)); // 应该变为不活跃
    EXPECT_EQ(ThreadSlot::unpackEpoch(left_state), 10);

    // 2. 尝试离开一个已经不活跃的槽位，状态不应改变
    slot->leave();
    EXPECT_EQ(slot->loadState(), left_state);

    // 3. 重新进入临界区
    slot->enter();
    uint64_t entered_state = slot->loadState();
    EXPECT_TRUE(ThreadSlot::isRegistered(entered_state));
    EXPECT_TRUE(ThreadSlot::isActive(entered_state)); // 应该变回活跃
    EXPECT_EQ(ThreadSlot::unpackEpoch(entered_state), 10);

    // 4. 尝试进入一个已经活跃的槽位，状态不应改变
    slot->enter();
    EXPECT_EQ(slot->loadState(), entered_state);
}

// 测试：纪元更新功能
TEST_F(ThreadSlotTest, SetEpoch) {
    slot->tryRegister(100);
    
    // 更新纪元
    const uint64_t new_epoch = 200;
    slot->setEpoch(new_epoch);

    // 验证状态
    uint64_t new_state = slot->loadState();
    EXPECT_TRUE(ThreadSlot::isRegistered(new_state));
    EXPECT_TRUE(ThreadSlot::isActive(new_state)); // 活跃状态不变
    EXPECT_EQ(ThreadSlot::unpackEpoch(new_state), new_epoch); // 纪元已更新

    // 确保在未注册的槽位上设置纪元是无效的
    slot->unregister();
    slot->setEpoch(300); // 尝试更新
    // 纪元应该保持注销前的值
    EXPECT_EQ(ThreadSlot::unpackEpoch(slot->loadState()), new_epoch);
}

// 测试：静态辅助函数的正确性
TEST(ThreadSlotStaticTest, UnpackersWorkCorrectly) {
    // 状态: 纪元=123, 活跃=true, 已注册=true
    uint64_t state1 = (123ULL << 2) | (1ULL << 1) | (1ULL << 0);
    EXPECT_EQ(ThreadSlot::unpackEpoch(state1), 123);
    EXPECT_TRUE(ThreadSlot::isRegistered(state1));
    EXPECT_TRUE(ThreadSlot::isActive(state1));

    // 状态: 纪元=456, 活跃=false, 已注册=true
    uint64_t state2 = (456ULL << 2) | (1ULL << 1) | (0ULL << 0);
    EXPECT_EQ(ThreadSlot::unpackEpoch(state2), 456);
    EXPECT_TRUE(ThreadSlot::isRegistered(state2));
    EXPECT_FALSE(ThreadSlot::isActive(state2));

    // 状态: 纪元=789, 活跃=false, 已注册=false (全零初始状态)
    uint64_t state3 = (789ULL << 2) | (0ULL << 1) | (0ULL << 0);
    EXPECT_EQ(ThreadSlot::unpackEpoch(state3), 789);
    EXPECT_FALSE(ThreadSlot::isRegistered(state3));
    EXPECT_FALSE(ThreadSlot::isActive(state3));
}

// ====================================================================
//                         并发测试用例
// ====================================================================

// 一个简单的并发测试，验证多个线程更新同一个槽的纪元不会导致数据损坏
TEST_F(ThreadSlotTest, ConcurrentEpochUpdate) {
    const int num_threads = 8;
    std::vector<std::thread> threads;

    // 首先，必须注册槽位
    slot->tryRegister(0);

    // 创建多个线程，每个线程都尝试将纪元设置为自己的ID
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([this, i]() {
            // 每个线程更新100次，增加竞争的可能性
            for (int j = 0; j < 100; ++j) {
                this->slot->setEpoch(i);
            }
        });
    }

    // 等待所有线程完成
    for (auto& t : threads) {
        t.join();
    }

    // 验证最终状态
    uint64_t final_state = slot->loadState();
    uint64_t final_epoch = ThreadSlot::unpackEpoch(final_state);

    // 我们不知道最后哪个线程“赢了”，但最终的纪元必须是 [0, num_threads-1] 之间的一个值
    EXPECT_GE(final_epoch, 0);
    EXPECT_LT(final_epoch, num_threads);

    // 状态应该依然是“已注册”和“活跃”
    EXPECT_TRUE(ThreadSlot::isRegistered(final_state));
    EXPECT_TRUE(ThreadSlot::isActive(final_state));
}