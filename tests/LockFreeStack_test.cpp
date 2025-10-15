// tests/LockFreeStack_test.cpp
#include <gtest/gtest.h>
#include <iostream>
#include <string>

#include "fixtures/ThreadHeapTestFixture.hpp"
#include "LockFreeStack/StackNode.hpp"
#include "LockFreeStack/HpSlotManager.hpp"
#include "LockFreeStack/HpRetiredManager.hpp"
#include "LockFreeStack/LockFreeStack.hpp"
#include "gc_malloc/ThreadHeap/ThreadHeap.hpp"

using value_t    = int;
using node_t     = StackNode<value_t>;
using SlotMgr    = HpSlotManager<node_t>;
using RetiredMgr = HpRetiredManager<node_t>;
using Stack      = LockFreeStack<value_t>;

class LockFreeStackFixture : public ThreadHeapTestFixture {};

// ============================================================================
// 1) 空栈
// ============================================================================ 
TEST_F(LockFreeStackFixture, EmptyStack_TryPopFalse) {
    // 使用线程堆分配管理器
    SlotMgr* slot_mgr = new (ThreadHeap::allocate(sizeof(SlotMgr))) SlotMgr();
    RetiredMgr* retired_mgr = new (ThreadHeap::allocate(sizeof(RetiredMgr))) RetiredMgr();
    
    // 确保 LockFreeStack 也使用共享内存进行分配
    Stack* st = new (ThreadHeap::allocate(sizeof(Stack))) Stack(*slot_mgr, *retired_mgr);

    std::cout << "\n[EmptyStack] init: " << "[Not Outputting debug_to_string()]" << std::endl;

    int out = 0;
    EXPECT_TRUE(st->isEmpty());
    EXPECT_FALSE(st->tryPop(out));
    EXPECT_TRUE(st->isEmpty());
    EXPECT_EQ(retired_mgr->getRetiredCount(), 0u);

    std::cout << "[EmptyStack] final: " << "[Not Outputting debug_to_string()]" << std::endl;

    // 清理
    ThreadHeap::deallocate(slot_mgr);
    ThreadHeap::deallocate(retired_mgr);
    ThreadHeap::deallocate(st); // 清理 Stack 对象
}

// ============================================================================
// 2) 基础 LIFO
// ============================================================================ 
TEST_F(LockFreeStackFixture, PushThenPop_LIFOOrder) {
    // 使用线程堆分配管理器
    SlotMgr* slot_mgr = new (ThreadHeap::allocate(sizeof(SlotMgr))) SlotMgr();
    RetiredMgr* retired_mgr = new (ThreadHeap::allocate(sizeof(RetiredMgr))) RetiredMgr();
    
    // 确保 LockFreeStack 也使用共享内存进行分配
    Stack* st = new (ThreadHeap::allocate(sizeof(Stack))) Stack(*slot_mgr, *retired_mgr);

    for (int v = 1; v <= 5; ++v) st->push(v);
    std::cout << "\n[LIFO] after push 1..5: " << "[Not Outputting debug_to_string()]" << std::endl;

    int out = 0;
    for (int expect = 5; expect >= 1; --expect) {
        ASSERT_TRUE(st->tryPop(out));
        EXPECT_EQ(out, expect);
    }

    EXPECT_TRUE(st->isEmpty());
    std::cout << "[LIFO] after pop all: " << "[Not Outputting debug_to_string()]" << std::endl;

    EXPECT_EQ(retired_mgr->getRetiredCount(), 5u);
    std::size_t freed = st->collectRetired(1000);
    EXPECT_EQ(freed, 5u);
    EXPECT_EQ(retired_mgr->getRetiredCount(), 0u);

    // 清理
    ThreadHeap::deallocate(slot_mgr);
    ThreadHeap::deallocate(retired_mgr);
    ThreadHeap::deallocate(st); // 清理 Stack 对象
}

// ============================================================================
// 3) drain_all：停机/析构场景，全量回收
// ============================================================================ 
TEST_F(LockFreeStackFixture, DrainAll_ForceCollectEverything) {
    // 使用线程堆分配管理器
    SlotMgr* slot_mgr = new (ThreadHeap::allocate(sizeof(SlotMgr))) SlotMgr();
    RetiredMgr* retired_mgr = new (ThreadHeap::allocate(sizeof(RetiredMgr))) RetiredMgr();
    
    // 确保 LockFreeStack 也使用共享内存进行分配
    Stack* st = new (ThreadHeap::allocate(sizeof(Stack))) Stack(*slot_mgr, *retired_mgr);

    for (int i = 0; i < 3; ++i) st->push(i);
    std::cout << "\n[DrainAll] after push 3: " << "[Not Outputting debug_to_string()]" << std::endl;

    int out = 0;
    for (int i = 0; i < 3; ++i) ASSERT_TRUE(st->tryPop(out));
    EXPECT_TRUE(st->isEmpty());
    EXPECT_EQ(retired_mgr->getRetiredCount(), 3u);

    std::size_t freed = st->drainAll();
    EXPECT_EQ(freed, 3u);
    EXPECT_EQ(retired_mgr->getRetiredCount(), 0u);

    std::cout << "[DrainAll] after drain_all: " << "[Not Outputting debug_to_string()]" << std::endl;

    // 清理
    ThreadHeap::deallocate(slot_mgr);
    ThreadHeap::deallocate(retired_mgr);
    ThreadHeap::deallocate(st); // 清理 Stack 对象
}

#include <thread>
#include <vector>
#include <atomic>
#include <numeric>   // C++17, for std::iota
#include <algorithm> // for std::sort

// ============================================================================
// 4) 多线程竞争 (Producer-Consumer)
// ============================================================================ 

TEST_F(LockFreeStackFixture, ConcurrentPushPop_NoDataLossOrCorruption) {
    // 1. 测试参数配置
    // 可以调整这些参数来改变测试的压力
    const int num_producers = 4;
    const int num_consumers = 4;
    const int items_per_producer = 25000;
    const int total_items = num_producers * items_per_producer;

    // 2. 初始化共享资源 (遵循你的测试模式)
    SlotMgr* slot_mgr = new (ThreadHeap::allocate(sizeof(SlotMgr))) SlotMgr();
    RetiredMgr* retired_mgr = new (ThreadHeap::allocate(sizeof(RetiredMgr))) RetiredMgr();
    Stack* st = new (ThreadHeap::allocate(sizeof(Stack))) Stack(*slot_mgr, *retired_mgr);

    // 原子计数器，用于协调消费者何时停止
    std::atomic<int> items_remaining_to_pop(total_items);

    // 3. 定义生产者和消费者任务 (使用 lambda 表达式)
    
    // 生产者任务：每个生产者负责推入一个不重叠的整数区间
    auto producer_task = [&](int start_value) {
        for (int i = 0; i < items_per_producer; ++i) {
            st->push(start_value + i);
        }
    };

    // 消费者任务：持续弹出，直到所有物品都被弹出为止
    auto consumer_task = [&](std::vector<int>& local_popped_items) {
        // 预分配足够空间，避免在循环中频繁重新分配内存
        local_popped_items.reserve(items_per_producer * 2); 
        int item;

        // 只要还有物品需要被弹出，就继续尝试
        while (items_remaining_to_pop.load(std::memory_order_acquire) > 0) {
            if (st->tryPop(item)) {
                local_popped_items.push_back(item);
                // 只有在成功弹出一个物品后，才减少总计数器
                items_remaining_to_pop.fetch_sub(1, std::memory_order_acq_rel);
            }
            // 如果 pop 返回 false (栈暂时为空)，则循环继续，不做任何操作
        }
    };

    // 4. 创建并启动所有线程
    std::vector<std::thread> all_threads;
    all_threads.reserve(num_producers + num_consumers);

    // 为每个消费者准备一个独立的 vector 来存储结果，避免竞争
    std::vector<std::vector<int>> consumer_results(num_consumers);

    // 启动所有生产者线程
    for (int i = 0; i < num_producers; ++i) {
        all_threads.emplace_back(producer_task, i * items_per_producer);
    }

    // 启动所有消费者线程
    for (int i = 0; i < num_consumers; ++i) {
        // 使用 std::ref 来传递 vector 的引用
        all_threads.emplace_back(consumer_task, std::ref(consumer_results[i]));
    }

    // 5. 等待所有线程执行完毕
    for (auto& t : all_threads) {
        t.join();
    }

    // 6. 验证结果的正确性
    
    // 6.1 验证最终状态：计数器归零，栈为空
    EXPECT_EQ(items_remaining_to_pop.load(), 0);
    EXPECT_TRUE(st->isEmpty());

    // 6.2 将所有消费者弹出的数据汇总到一个 vector 中
    std::vector<int> all_popped_items;
    all_popped_items.reserve(total_items);
    for (const auto& vec : consumer_results) {
        all_popped_items.insert(all_popped_items.end(), vec.begin(), vec.end());
    }

    // 6.3 创建一个包含所有期望值的、已排序的 vector
    std::vector<int> expected_items(total_items);
    std::iota(expected_items.begin(), expected_items.end(), 0); // 填充 0, 1, 2, ...

    // 6.4 对弹出的结果进行排序，以便与期望值进行比较
    std::sort(all_popped_items.begin(), all_popped_items.end());

    // 6.5 最终断言：大小相等，内容也完全相同
    ASSERT_EQ(all_popped_items.size(), (size_t)total_items);
    EXPECT_EQ(all_popped_items, expected_items) << "Pushed and popped items do not match!";
    
    // 7. 验证内存回收机制
    EXPECT_EQ(retired_mgr->getRetiredCount(), (unsigned)total_items);
    std::size_t freed = st->collectRetired(total_items * 2); // 使用一个足够大的配额
    EXPECT_EQ(freed, (unsigned)total_items);
    EXPECT_EQ(retired_mgr->getRetiredCount(), 0u);

    // 8. 清理 (遵循你的测试模式)
    ThreadHeap::deallocate(slot_mgr);
    ThreadHeap::deallocate(retired_mgr);
    ThreadHeap::deallocate(st);
}


#include <thread>
#include <vector>
#include <atomic>
#include <numeric>
#include <algorithm>
#include <stdexcept>

// POSIX headers for multi-process support
#include <unistd.h>     // For fork()
#include <sys/wait.h>   // For waitpid()
#include <pthread.h>    // For process-shared barriers

// ============================================================================
// 5) 终极压力测试：多进程 & 多线程
// ============================================================================ 


#include <thread>
#include <vector>
#include <atomic>
#include <numeric>
#include <algorithm>
#include <stdexcept>
#include <chrono> // For std::this_thread::sleep_for

// POSIX headers for multi-process support
#include <unistd.h>
#include <sys/wait.h>
#include <pthread.h>

// (SharedStressTestBlock struct a
// (SharedStressTestBlock struct as before)
struct SharedStressTestBlock {
    // The stack and its managers
    SlotMgr slot_mgr;
    RetiredMgr retired_mgr;
    Stack stack;

    // Synchronization primitives
    pthread_barrier_t start_barrier;

    // Coordination and results
    std::atomic<int> items_to_pop_count;
    std::atomic<int> result_write_index; 
    
    // For intermediate state validation
    std::atomic<bool> observed_non_empty_stack{false};
    std::atomic<bool> observed_retired_nodes{false};
    std::atomic<uint64_t> total_collected_nodes{0};

    int* results_array;

    SharedStressTestBlock(int total_threads, int total_items)
        : slot_mgr(),
          retired_mgr(),
          stack(slot_mgr, retired_mgr),
          items_to_pop_count(total_items),
          result_write_index(0)
    {
        pthread_barrierattr_t attr;
        pthread_barrierattr_init(&attr);
        pthread_barrierattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
        pthread_barrier_init(&start_barrier, &attr, total_threads);
        pthread_barrierattr_destroy(&attr);
        results_array = static_cast<int*>(ThreadHeap::allocate(sizeof(int) * total_items));
    }

    ~SharedStressTestBlock() {
        pthread_barrier_destroy(&start_barrier);
        ThreadHeap::deallocate(results_array);
    }
};


TEST_F(LockFreeStackFixture, MultiProcessMultiThread_WithIntermediateValidation) {
    // 1. 测试参数
    const int num_processes = 4;
    const int producers_per_process = 4;
    const int consumers_per_process = 4;
    // (新增) 审计线程只在父进程中运行
    const int auditors_in_parent = 1; 
    const int items_per_producer = 5000;

    const int total_producers = (num_processes + 1) * producers_per_process;
    // (修改) 总线程数需要加上审计线程
    const int total_threads = total_producers + 
                              (num_processes + 1) * consumers_per_process + 
                              auditors_in_parent;
    const int total_items = total_producers * items_per_producer;

    // 2. 在共享内存中设置控制块
    SharedStressTestBlock* control_block = new (ThreadHeap::allocate(sizeof(SharedStressTestBlock)))
        SharedStressTestBlock(total_threads, total_items);

    // 3. 定义工作任务
    auto worker_task = [&](int process_id, bool is_parent) {
        std::vector<std::thread> threads;
        int num_threads_in_proc = producers_per_process + consumers_per_process;
        if (is_parent) num_threads_in_proc += auditors_in_parent;
        threads.reserve(num_threads_in_proc);

        // Producer task (same as before)
        auto producer_fn = [&](int thread_id) {
            int producer_global_id = process_id * producers_per_process + thread_id;
            int start_value = producer_global_id * items_per_producer;
            pthread_barrier_wait(&control_block->start_barrier);
            for (int i = 0; i < items_per_producer; ++i) {
                control_block->stack.push(start_value + i);
            }
        };

        // Consumer task (same as before)
        auto consumer_fn = [&]() {
            int item;
            pthread_barrier_wait(&control_block->start_barrier);
            while (control_block->items_to_pop_count.load(std::memory_order_acquire) > 0) {
                if (control_block->stack.tryPop(item)) {
                    int write_idx = control_block->result_write_index.fetch_add(1, std::memory_order_acq_rel);
                    if (write_idx < total_items) {
                        control_block->results_array[write_idx] = item;
                    }
                    control_block->items_to_pop_count.fetch_sub(1, std::memory_order_acq_rel);
                }
            }
        };

        // *** 新增：审计线程任务 ***
        auto auditor_fn = [&]() {
            pthread_barrier_wait(&control_block->start_barrier);
            
            while (control_block->items_to_pop_count.load(std::memory_order_acquire) > 0) {
                // 检查栈是否非空
                if (!control_block->stack.isEmpty()) {
                    control_block->observed_non_empty_stack.store(true, std::memory_order_relaxed);
                }

                // 检查是否有退休节点
                if (control_block->retired_mgr.getRetiredCount() > 0) {
                    control_block->observed_retired_nodes.store(true, std::memory_order_relaxed);
                }

                // 模拟后台GC，定期回收
                size_t freed = control_block->stack.collectRetired(100); // 每次尝试回收100个
                if (freed > 0) {
                    control_block->total_collected_nodes.fetch_add(freed, std::memory_order_relaxed);
                }

                // 短暂休眠，避免过度消耗CPU
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        };

        // Spawn threads for this process
        for(int i = 0; i < producers_per_process; ++i) threads.emplace_back(producer_fn, i);
        for(int i = 0; i < consumers_per_process; ++i) threads.emplace_back(consumer_fn);
        if (is_parent) {
            for(int i = 0; i < auditors_in_parent; ++i) threads.emplace_back(auditor_fn);
        }

        for(auto& t : threads) t.join();
    };

    // 4. 创建子进程
    std::vector<pid_t> child_pids;
    for (int i = 0; i < num_processes; ++i) {
        pid_t pid = fork();
        if (pid == -1) {
            FAIL() << "Failed to fork process";
        } else if (pid == 0) { // Child process
            worker_task(i + 1, false); // is_parent = false
            exit(0);
        } else { // Parent process
            child_pids.push_back(pid);
        }
    }

    // 5. 父进程也参与工作 (并启动审计线程)
    worker_task(0, true); // is_parent = true

    // 6. 等待所有子进程结束 (same as before)
    for (pid_t pid : child_pids) {
        int status;
        ASSERT_NE(waitpid(pid, &status, 0), -1);
        ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0) << "Child process crashed or exited with an error.";
    }

    // --- 7. 验证结果 ---
    std::cout << "\n[StressTest] All processes and threads finished. Starting validation." << std::endl;

    // *** 新增：验证中间状态 ***
    EXPECT_TRUE(control_block->observed_non_empty_stack.load())
        << "Auditor thread never observed a non-empty stack, which is highly unlikely.";
    EXPECT_TRUE(control_block->observed_retired_nodes.load())
        << "Auditor thread never observed any retired nodes.";
    
    // 7.1 验证最终状态 (same as before)
    EXPECT_EQ(control_block->items_to_pop_count.load(), 0);
    EXPECT_TRUE(control_block->stack.isEmpty());
    ASSERT_EQ(control_block->result_write_index.load(), total_items);

    // 7.2 验证数据完整性 (same as before)
    std::vector<int> all_popped_items(control_block->results_array, control_block->results_array + total_items);
    std::vector<int> expected_items(total_items);
    std::iota(expected_items.begin(), expected_items.end(), 0);
    std::sort(all_popped_items.begin(), all_popped_items.end());
    EXPECT_EQ(all_popped_items, expected_items);

    // 7.3 验证内存回收 (逻辑稍作调整)
    // 计算剩余未回收的节点
    uint64_t collected_during_run = control_block->total_collected_nodes.load();
    uint64_t remaining_to_collect = total_items - collected_during_run;
    
    EXPECT_EQ(control_block->retired_mgr.getRetiredCount(), remaining_to_collect);
    
    // 进行最终的回收
    std::size_t final_freed = control_block->stack.drainAll(); // 使用 drain_all 更彻底
    EXPECT_EQ(final_freed, remaining_to_collect);
    EXPECT_EQ(control_block->retired_mgr.getRetiredCount(), 0u);

    // 8. 清理 (same as before)
    control_block->~SharedStressTestBlock();
    ThreadHeap::deallocate(control_block);
}