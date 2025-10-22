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


// ============================================================================
// --- 新增：测试辅助工具 ---
// ============================================================================

// 用于验证对象内存完整性的、更复杂的跟踪对象
struct ComplexTrackableObject {
    std::atomic<size_t>* destruction_counter;
    uint64_t magic_value; // 用于检测内存是否被意外踩踏
    char padding[128];    // 模拟更真实的对象大小

    static constexpr uint64_t kMagicValue = 0xDEADBEEFCAFEBABE;

    ComplexTrackableObject(std::atomic<size_t>* counter) 
        : destruction_counter(counter), magic_value(kMagicValue) {
        // 用特定模式填充，便于调试
        memset(padding, 'A', sizeof(padding));
    }

    ~ComplexTrackableObject() {
        // 在析构时，验证 magic_value 是否仍然完好
        // 如果内存被提前释放或被其他线程错误地修改，这里很可能会失败
        EXPECT_EQ(magic_value, kMagicValue); 
        
        if (destruction_counter) {
            destruction_counter->fetch_add(1, std::memory_order_relaxed);
        }
    }
};



/**
 * @test MultiThreadWithStallingReader
 * @brief 模拟一个线程长时间持有临界区，测试是否会阻塞垃圾回收。
 *
 * 流程：
 * 1. 启动一个“阻塞线程”，它进入临界区后长时间休眠。
 * 2. 在此期间，其他“工作线程”疯狂地创建和废弃对象。
 * 3. 关键验证点1：在“阻塞线程”离开临界区之前，任何垃圾都不应该被回收。
 * 4. “阻塞线程”离开临界区。
 * 5. 主线程冲刷EBR系统。
 * 6. 关键验证点2：一旦阻塞解除，所有之前积压的垃圾都应该被成功回收。
 *
 * 这个测试验证了EBR最核心的安全性：只要有任何一个“读者”存在，
 * 它所见到的数据就必须是安全的。
 */
TEST_F(EBRManagerTest, MultiThreadWithStallingReader) {
    constexpr size_t kNumWorkerThreads = 4;
    constexpr size_t kObjectsPerWorker = 500;
    constexpr size_t kTotalObjects = kNumWorkerThreads * kObjectsPerWorker;

    std::atomic<size_t> destruction_counter = 0;
    
    // *** 新增同步标志 ***
    // staller_can_leave 用于通知阻塞线程何时可以离开临界区
    std::atomic<bool> staller_can_leave(false);
    // staller_is_in_cs 用于确认阻塞线程已经进入临界区
    std::atomic<bool> staller_is_in_cs(false);

    std::vector<std::thread> worker_threads;

    // 1. 启动“阻塞线程”
    std::thread stalling_thread([&]() {
        ebr_manager_.enter();
        staller_is_in_cs.store(true); // 通知主线程，我已进入临界区

        // *** 修改点：不再是固定休眠，而是等待主线程的信号 ***
        while (!staller_can_leave.load()) {
            std::this_thread::yield(); // 等待信号，避免忙等
        }

        ebr_manager_.leave();
    });

    // 等待阻塞线程确实进入了临界区
    while (!staller_is_in_cs.load()) {
        std::this_thread::yield();
    }

    // 2. 启动“工作线程”
    for (size_t i = 0; i < kNumWorkerThreads; ++i) {
        worker_threads.emplace_back([&]() {
            for (size_t j = 0; j < kObjectsPerWorker; ++j) {
                ebr_manager_.enter();
                void* mem = ThreadHeap::allocate(sizeof(TrackableObject));
                TrackableObject* obj = new(mem) TrackableObject(&destruction_counter);
                ebr_manager_.retire(obj);
                ebr_manager_.leave();
            }
        });
    }

    for (auto& t : worker_threads) {
        t.join();
    }

    // 3. 关键验证点1：此时，所有工作线程已结束，但阻塞线程保证仍在临界区内
    ASSERT_EQ(destruction_counter.load(), 0) << "Garbage was collected while a reader was stalling!";

    // 4. *** 新增：通知阻塞线程可以离开了 ***
    staller_can_leave.store(true);
    stalling_thread.join();

    // 5. 主线程冲刷EBR系统，触发回收
    for (size_t i = 0; i < EBRManager::kNumEpochLists + 1; ++i) {
        ebr_manager_.enter();
        ebr_manager_.leave();
    }

    // 6. 关键验证点2：阻塞解除后，所有垃圾都应被回收
    EXPECT_EQ(destruction_counter.load(), kTotalObjects);
}


/**
 * @test MultiThreadMixedWorkload
 * @brief 模拟读多写少的混合负载场景。
 *
 * 流程：
 * 1. 创建大量“读者线程”，它们只执行 enter/leave 操作。
 * 2. 创建少量“写入线程”，它们执行 enter/retire/leave 操作。
 * 3. 验证在大量读操作的干扰下，写入线程产生的垃圾依然能被正确回收。
 *
 * 这个测试验证了 `tryAdvanceEpoch` 在面对不同线程状态组合时的正确性。
 */
TEST_F(EBRManagerTest, MultiThreadMixedWorkload) {
    constexpr size_t kReaderThreads = 8;
    constexpr size_t kWriterThreads = 2;
    constexpr size_t kObjectsPerWriter = 500;
    constexpr size_t kTotalObjects = kWriterThreads * kObjectsPerWriter;
    constexpr int kReaderLoops = 1000;

    std::atomic<size_t> destruction_counter = 0;
    std::vector<std::thread> threads;

    // 1. 创建读者线程
    for (size_t i = 0; i < kReaderThreads; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < kReaderLoops; ++j) {
                ebr_manager_.enter();
                // 模拟读操作
                std::this_thread::sleep_for(std::chrono::microseconds(1));
                ebr_manager_.leave();
            }
        });
    }

    // 2. 创建写入线程
    for (size_t i = 0; i < kWriterThreads; ++i) {
        threads.emplace_back([&]() {
            for (size_t j = 0; j < kObjectsPerWriter; ++j) {
                ebr_manager_.enter();
                void* mem = ThreadHeap::allocate(sizeof(TrackableObject));
                TrackableObject* obj = new(mem) TrackableObject(&destruction_counter);
                ebr_manager_.retire(obj);
                ebr_manager_.leave();
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // 冲刷EBR系统
    for (size_t i = 0; i < EBRManager::kNumEpochLists; ++i) {
        ebr_manager_.enter();
        ebr_manager_.leave();
    }

    // 3. 验证回收正确性
    EXPECT_EQ(destruction_counter.load(), kTotalObjects);
}


/**
 * @test MultiThreadRetireComplexObjects
 * @brief 压力测试回收具有内部状态的复杂对象。
 *
 * 流程：
 * 1. 使用 `ComplexTrackableObject`，它在构造时设置一个“魔法值”。
 * 2. 在析构时，它会检查这个“魔法值”是否仍然存在。
 * 3. 多线程并发地创建和废弃这些复杂对象。
 *
 * 这个测试的目标是捕获 use-after-free 或内存损坏问题。如果EBR系统
 * 错误地提前回收了内存，并且该内存被重新分配和覆盖，那么在旧对象的
 * 析构函数被调用时，“魔法值”就会被破坏，导致断言失败。
 */
TEST_F(EBRManagerTest, MultiThreadRetireComplexObjects) {
    constexpr size_t kNumThreads = 8;
    constexpr size_t kObjectsPerThread = 1000;
    constexpr size_t kTotalObjects = kNumThreads * kObjectsPerThread;

    std::atomic<size_t> destruction_counter = 0;
    std::vector<std::thread> threads;

    for (size_t i = 0; i < kNumThreads; ++i) {
        threads.emplace_back([&]() {
            for (size_t j = 0; j < kObjectsPerThread; ++j) {
                ebr_manager_.enter();
                void* mem = ThreadHeap::allocate(sizeof(ComplexTrackableObject));
                // 使用复杂对象
                ComplexTrackableObject* obj = new(mem) ComplexTrackableObject(&destruction_counter);
                ebr_manager_.retire(obj);
                ebr_manager_.leave();
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // 冲刷EBR系统
    for (size_t i = 0; i < EBRManager::kNumEpochLists; ++i) {
        ebr_manager_.enter();
        ebr_manager_.leave();
    }

    // 验证所有对象都被正确销毁（析构函数中的断言没有失败）
    EXPECT_EQ(destruction_counter.load(), kTotalObjects);
}