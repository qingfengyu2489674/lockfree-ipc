#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

#include "fixtures/ThreadHeapTestFixture.hpp" // 您的内存分配器夹具
#include "EBRManager/EBRManager.hpp"         // 被测试的类

// ============================================================================
// --- 测试辅助工具 ---
// ============================================================================

// 用于跟踪对象是否被析构的辅助结构体。
struct TrackableObject {
    // 指向一个外部的原子计数器，析构时会增加它。
    std::atomic<size_t>* destruction_counter;

    // 构造函数
    TrackableObject(std::atomic<size_t>* counter) : destruction_counter(counter) {}

    // 析构函数：在被销毁时，原子地增加计数器。
    ~TrackableObject() {
        if (destruction_counter) {
            destruction_counter->fetch_add(1, std::memory_order_relaxed);
        }
    }
};


// ============================================================================
// --- 测试夹具 (Fixture) ---
// ============================================================================

class EBRManagerTest : public ThreadHeapTestFixture {
protected:
    // 每个测试用例都会有一个全新的 EBRManager 实例。
    EBRManager ebr_manager_;
};


// ============================================================================
// --- 测试用例 ---
// ============================================================================

/**
 * @test Lifecycle
 * @brief 测试 EBRManager 能否被成功创建和销毁。
 *
 * 这是一个基本的健全性检查。如果构造和析构（包含内部的垃圾回收循环）
 * 能够无误执行，测试就通过。
 */
TEST_F(EBRManagerTest, Lifecycle) {
    // 夹具的创建和销毁本身就构成了测试。
    // 如果没有崩溃或断言失败，就意味着基本生命周期管理是正常的。
    SUCCEED();
}

/**
 * @test SingleThreadSimpleRetireAndReclaim
 * @brief 在单线程环境下，测试一个对象能否被成功回收。
 *
 * 流程：
 * 1. 进入临界区。
 * 2. 废弃一个对象。
 * 3. 离开临界区。
 * 4. 循环进入/离开以推进纪元，直到触发回收。
 * 5. 验证对象最终被析构。
 */
TEST_F(EBRManagerTest, SingleThreadSimpleRetireAndReclaim) {
    std::atomic<size_t> counter = 0;

    // 1. 分配一个受跟踪的对象
    void* mem = ThreadHeap::allocate(sizeof(TrackableObject));
    TrackableObject* obj = new(mem) TrackableObject(&counter);

    // 2. 进入临界区，废弃对象，然后离开
    ebr_manager_.enter();
    ebr_manager_.retire(obj);
    ebr_manager_.leave();

    // 此时，对象刚被放入垃圾列表，纪元可能推进到1，但对象绝不应该被回收
    ASSERT_EQ(counter.load(), 0);

    // 3. 循环推进纪元以触发回收
    // 第一次 leave 后，纪元可能推进到 1。回收的是纪元 -2，即 -1 (无效)。
    // 第二次 leave 后，纪元可能推进到 2。回收的是纪元 0 的垃圾。
    // 第三次 leave 后，纪元可能推进到 3。回收的是纪元 1 的垃圾。
    // 我们循环三次确保覆盖所有纪元槽。
    for (size_t i = 0; i < EBRManager::kNumEpochLists; ++i) {
        ebr_manager_.enter();
        ebr_manager_.leave();
    }

    // 4. 验证对象最终被析构
    ASSERT_EQ(counter.load(), 1);
}

/**
 * @test MultiThreadStressTest
 * @brief 在多线程环境下进行压力测试。
 *
 * 流程：
 * 1. 创建多个线程。
 * 2. 每个线程循环执行 enter-retire-leave 操作。
 * 3. 等待所有线程完成。
 * 4. 在主线程中“冲刷”EBR系统，确保所有剩余垃圾都被回收。
 * 5. 验证所有被废弃的对象都被成功析构。
 */
TEST_F(EBRManagerTest, MultiThreadStressTest) {
    constexpr size_t kNumThreads = 8;
    constexpr size_t kObjectsPerThread = 1000;
    constexpr size_t kTotalObjects = kNumThreads * kObjectsPerThread;

    std::atomic<size_t> destruction_counter = 0;
    std::vector<std::thread> threads;

    for (size_t i = 0; i < kNumThreads; ++i) {
        threads.emplace_back([&]() {
            for (size_t j = 0; j < kObjectsPerThread; ++j) {
                // 进入临界区
                ebr_manager_.enter();

                // 分配并废弃一个对象
                void* mem = ThreadHeap::allocate(sizeof(TrackableObject));
                TrackableObject* obj = new(mem) TrackableObject(&destruction_counter);
                ebr_manager_.retire(obj);
                
                // 离开临界区，可能会触发纪元推进和回收
                ebr_manager_.leave();
                
                // 添加微小的随机延迟以增加线程交错的可能性
                std::this_thread::sleep_for(std::chrono::microseconds(rand() % 5));
            }
        });
    }

    // 等待所有工作线程完成
    for (auto& t : threads) {
        t.join();
    }

    // 主线程需要“冲刷”EBR系统，因为可能有些垃圾还留在列表中等待回收
    // 确保所有纪元槽都能被检查和回收
    for (size_t i = 0; i < EBRManager::kNumEpochLists; ++i) {
        ebr_manager_.enter();
        ebr_manager_.leave();
    }
    
    // 最终验证，所有创建的对象都应该被销毁了
    EXPECT_EQ(destruction_counter.load(), kTotalObjects);
}