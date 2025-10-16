#include <gtest/gtest.h>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <numeric>
#include <algorithm>
#include <chrono>

#include "fixtures/ThreadHeapTestFixture.hpp"
#include "LockFreeQueue/LockFreeQueue.hpp"
#include "Hazard/HazardPointerOrganizer.hpp"

// ============================================================================
// --- 类型别名 - 适配队列和新的 Organizer API ---
// ============================================================================
using value_t = int;
using node_t  = QueueNode<value_t>;
using Queue   = LockFreeQueue<value_t>;

// 为队列定义 Organizer 类型
constexpr size_t kQueueHazardPointers = Queue::kHazardPointers;
using QueueHpOrganizer = HazardPointerOrganizer<node_t, kQueueHazardPointers>;


// ============================================================================
// --- 测试夹具 (Fixture) ---
// ============================================================================
class LockFreeQueueFixture : public ThreadHeapTestFixture {};


// ============================================================================
// --- 测试用例 (已适配 Organizer) ---
// ============================================================================

// 1) 空队列
TEST_F(LockFreeQueueFixture, EmptyQueue_TryPopFalse) {
    // 创建 Organizer 和 Queue
    auto* hp_organizer = new (ThreadHeap::allocate(sizeof(QueueHpOrganizer))) QueueHpOrganizer();
    auto* q            = new (ThreadHeap::allocate(sizeof(Queue))) Queue(*hp_organizer);

    int out = 0;
    EXPECT_TRUE(q->isEmpty());
    EXPECT_FALSE(q->tryPop(out));
    EXPECT_TRUE(q->isEmpty());
    
    // 验证：通过 Organizer 的 collect 方法检查，除了初始的哨兵节点外不应有回收
    EXPECT_EQ(hp_organizer->collect(), 0u);

    // 清理
    q->~Queue();
    ThreadHeap::deallocate(q);
    hp_organizer->~QueueHpOrganizer();
    ThreadHeap::deallocate(hp_organizer);
}

// 2) 基础 FIFO (First-In, First-Out)
TEST_F(LockFreeQueueFixture, PushThenPop_FIFOOrder) {
    // 创建 Organizer 和 Queue
    auto* hp_organizer = new (ThreadHeap::allocate(sizeof(QueueHpOrganizer))) QueueHpOrganizer();
    auto* q            = new (ThreadHeap::allocate(sizeof(Queue))) Queue(*hp_organizer);

    for (int v = 1; v <= 5; ++v) {
        q->push(v);
    }

    int out = 0;
    // ** 关键验证：检查是否为 FIFO 顺序 **
    for (int expect = 1; expect <= 5; ++expect) {
        ASSERT_TRUE(q->tryPop(out));
        EXPECT_EQ(out, expect);
    }
    EXPECT_TRUE(q->isEmpty());

    // 验证：调用 Organizer 的 collect 方法来触发回收
    // 5个数据节点 + 5个被替换的哨兵节点 = 10个节点被回收
    std::size_t freed = hp_organizer->collect(1000);
    EXPECT_EQ(freed, 5u); // 5 个旧的 head 节点 (哨兵)
    EXPECT_EQ(hp_organizer->collect(), 0u); // 再次收集应该没有东西了

    // 清理
    q->~Queue();
    ThreadHeap::deallocate(q);
    hp_organizer->~QueueHpOrganizer();
    ThreadHeap::deallocate(hp_organizer);
}

// 3) drainAll
TEST_F(LockFreeQueueFixture, DrainAll_ForceCollectEverything) {
    // 创建 Organizer 和 Queue
    auto* hp_organizer = new (ThreadHeap::allocate(sizeof(QueueHpOrganizer))) QueueHpOrganizer();
    auto* q            = new (ThreadHeap::allocate(sizeof(Queue))) Queue(*hp_organizer);

    for (int i = 0; i < 3; ++i) {
        q->push(i);
    }

    int out = 0;
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(q->tryPop(out));
    }
    
    EXPECT_TRUE(q->isEmpty());
    // 此时3个旧的head节点已被退休，collect() 应该能回收它们
    EXPECT_EQ(hp_organizer->collect(), 3u); 
    
    // 调用 Organizer 的 drainAll 方法，将线程本地列表（如果还有）移到全局
    std::size_t moved = hp_organizer->drainAllRetired();
    EXPECT_EQ(moved, 0u); // 因为 collect() 已经处理过了

    // 清理
    q->~Queue();
    ThreadHeap::deallocate(q);
    hp_organizer->~QueueHpOrganizer();
    ThreadHeap::deallocate(hp_organizer);
}

// 4) 多线程竞争
TEST_F(LockFreeQueueFixture, ConcurrentPushPop_NoDataLossOrCorruption) {
    const int num_producers = 4;
    const int num_consumers = 4;
    const int items_per_producer = 10000;
    const int total_items = num_producers * items_per_producer;

    // 创建 Organizer 和 Queue
    auto* hp_organizer = new (ThreadHeap::allocate(sizeof(QueueHpOrganizer))) QueueHpOrganizer();
    auto* q            = new (ThreadHeap::allocate(sizeof(Queue))) Queue(*hp_organizer);

    std::atomic<int> items_pushed(0);
    std::atomic<int> items_popped(0);
    std::vector<std::thread> all_threads;
    std::vector<std::vector<int>> consumer_results(num_consumers);

    auto producer_task = [&](int start_value) {
        for (int i = 0; i < items_per_producer; ++i) {
            q->push(start_value + i);
            items_pushed.fetch_add(1, std::memory_order_release);
        }
    };

    auto consumer_task = [&](std::vector<int>& local_results) {
        int item;
        while (items_popped.load(std::memory_order_acquire) < total_items) {
            if (q->tryPop(item)) {
                local_results.push_back(item);
                items_popped.fetch_add(1, std::memory_order_acq_rel);
            } else {
                // 如果队列暂时为空，但任务还没结束，就让出CPU，避免空转
                if (items_pushed.load(std::memory_order_acquire) < total_items) {
                    std::this_thread::yield();
                }
            }
        }
    };

    for (int i = 0; i < num_producers; ++i) {
        all_threads.emplace_back(producer_task, i * items_per_producer);
    }
    for (int i = 0; i < num_consumers; ++i) {
        all_threads.emplace_back(consumer_task, std::ref(consumer_results[i]));
    }
    for (auto& t : all_threads) {
        t.join();
    }

    // 验证
    EXPECT_EQ(items_popped.load(), total_items);
    EXPECT_TRUE(q->isEmpty());

    std::vector<int> all_popped_items;
    for (const auto& vec : consumer_results) {
        all_popped_items.insert(all_popped_items.end(), vec.begin(), vec.end());
    }
    // ** 关键验证：多线程下，我们不关心出队顺序，只关心所有元素是否都被取出了 **
    std::sort(all_popped_items.begin(), all_popped_items.end());

    std::vector<int> expected_items(total_items);
    std::iota(expected_items.begin(), expected_items.end(), 0);

    ASSERT_EQ(all_popped_items.size(), (size_t)total_items);
    EXPECT_EQ(all_popped_items, expected_items);
    
    // 清理前，做一次完全回收
    hp_organizer->collect(total_items + 10);

    // 清理
    q->~Queue();
    ThreadHeap::deallocate(q);
    hp_organizer->~QueueHpOrganizer();
    ThreadHeap::deallocate(hp_organizer);
}


// ============================================================================
// --- 多进程多线程竞争测试 (基于现有夹具) ---
// ============================================================================

// --- POSIX Headers for IPC ---
#include <sys/wait.h>   /* For waitpid */
#include <unistd.h>     /* For fork, _exit */
#include <semaphore.h>  /* For semaphores */

// --- 测试参数 ---
constexpr int kNumProcesses = 2;
constexpr int kProducersPerProcess = 2;
constexpr int kConsumersPerProcess = 2;
constexpr int kItemsPerProducer = 10000;

constexpr int kTotalProducers = kNumProcesses * kProducersPerProcess;
constexpr int kTotalItems = kTotalProducers * kItemsPerProducer;

// --- 共享状态结构体 ---
// 这个结构体的所有内容都将位于由夹具管理的共享内存中
struct SharedState {
    QueueHpOrganizer organizer;
    Queue queue;

    std::atomic<int> items_pushed;
    std::atomic<int> items_popped;
    std::atomic<int> ready_processes;
    std::atomic<bool> test_failed;
    std::atomic<char> popped_flags[kTotalItems];
    sem_t start_gate;

    // 构造函数接收一个引用，这是C++的要求
    // 但我们会使用 placement new，所以这个构造函数不会被常规调用
    SharedState() : queue(organizer) {} 
};

// 子进程的主体逻辑
void child_process_main(SharedState* state, int process_idx) {
    std::vector<std::thread> threads;

    // 生产者任务
    auto producer_task = [&](int producer_id) {
        int start_value = producer_id * kItemsPerProducer;
        for (int i = 0; i < kItemsPerProducer; ++i) {
            state->queue.push(start_value + i);
        }
        state->items_pushed.fetch_add(kItemsPerProducer, std::memory_order_release);
    };

    // 消费者任务
    auto consumer_task = [&]() {
        int item;
        while (state->items_popped.load(std::memory_order_acquire) < kTotalItems) {
            if (state->queue.tryPop(item)) {
                if (item < 0 || item >= kTotalItems) {
                    state->test_failed.store(true);
                } else {
                    char expected = 0;
                    if (!state->popped_flags[item].compare_exchange_strong(expected, 1)) {
                        state->test_failed.store(true); // 重复消费
                    }
                }
                state->items_popped.fetch_add(1, std::memory_order_acq_rel);
            } else if (state->items_pushed.load(std::memory_order_acquire) < kTotalItems) {
                std::this_thread::yield();
            }
        }
    };

    // 在子进程内创建线程
    for (int i = 0; i < kProducersPerProcess; ++i) {
        int global_producer_id = process_idx * kProducersPerProcess + i;
        threads.emplace_back(producer_task, global_producer_id);
    }
    for (int i = 0; i < kConsumersPerProcess; ++i) {
        threads.emplace_back(consumer_task);
    }

    // 同步与执行
    state->ready_processes.fetch_add(1);
    sem_wait(&state->start_gate);

    for (auto& t : threads) {
        t.join();
    }
    _exit(0); // 子进程正常退出
}

// --- 测试用例 ---
// !!! 警告: 假设 ThreadHeapTestFixture 和 HazardPointerOrganizer 支持多进程 !!!
TEST_F(LockFreeQueueFixture, MultiProcess_ConcurrentPushPop_NoDataLossOrCorruption) {
    // 1. 使用夹具的共享内存分配器分配 SharedState
    void* shm_ptr = ThreadHeap::allocate(sizeof(SharedState));
    ASSERT_NE(shm_ptr, nullptr);
    auto* shared_state = new (shm_ptr) SharedState();

    // 2. 初始化共享状态
    shared_state->items_pushed = 0;
    shared_state->items_popped = 0;
    shared_state->ready_processes = 0;
    shared_state->test_failed = false;
    for (int i = 0; i < kTotalItems; ++i) {
        shared_state->popped_flags[i].store(0);
    }
    // 初始化用于进程间同步的信号量
    ASSERT_EQ(sem_init(&shared_state->start_gate, 1, 0), 0);

    // 3. 创建子进程
    std::vector<pid_t> children_pids;
    for (int i = 0; i < kNumProcesses; ++i) {
        pid_t pid = fork();
        ASSERT_NE(pid, -1) << "fork() failed";
        if (pid == 0) { // 子进程
            child_process_main(shared_state, i);
        }
        children_pids.push_back(pid); // 父进程
    }

    // --- 父进程协调和验证 ---
    // 4. 等待所有子进程就绪
    auto start_time = std::chrono::steady_clock::now();
    while (shared_state->ready_processes.load() < kNumProcesses) {
        ASSERT_LT(std::chrono::steady_clock::now() - start_time, std::chrono::seconds(5)) << "Timeout waiting for children to get ready.";
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // 5. 发出开始信号，释放所有子进程
    for (int i = 0; i < kNumProcesses; ++i) {
        sem_post(&shared_state->start_gate);
    }

    // 6. 等待所有子进程结束
    for (pid_t pid : children_pids) {
        int status;
        waitpid(pid, &status, 0);
        ASSERT_TRUE(WIFEXITED(status)) << "Child process " << pid << " terminated abnormally.";
        ASSERT_EQ(WEXITSTATUS(status), 0) << "Child process " << pid << " exited with a non-zero status.";
    }

    // --- 最终验证 ---
    ASSERT_FALSE(shared_state->test_failed.load()) << "Test failed flag was set by a child process.";
    EXPECT_EQ(shared_state->items_pushed.load(), kTotalItems);
    EXPECT_EQ(shared_state->items_popped.load(), kTotalItems);
    EXPECT_TRUE(shared_state->queue.isEmpty());

    int popped_count = 0;
    for (int i = 0; i < kTotalItems; ++i) {
        if (shared_state->popped_flags[i].load() == 1) {
            popped_count++;
        }
    }
    EXPECT_EQ(popped_count, kTotalItems) << "Not all items were popped exactly once.";

    // 7. 清理资源
    sem_destroy(&shared_state->start_gate);
    shared_state->~SharedState();
    ThreadHeap::deallocate(shared_state);
}