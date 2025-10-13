// tests/ShmMutexLock_gtest.cpp (Robust, Standalone Version)
#include <gtest/gtest.h>
#include <atomic>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>
#include <new>

// 包含你的头文件
#include "Tool/ShmMutexLock.hpp"
#include "ShareMemory/ShareMemoryRegion.hpp"

// --- 测试基础设施 ---
// 定义一个包含锁和受保护数据的共享结构体
struct SharedData {
    ShmMutexLock mutex;
    alignas(64) std::atomic<int> counter;
};

// GTest 测试套件 Fixture (无需任何 fork() 特殊处理)
struct ShmMutexLockTestFixture : public ::testing::Test {
    static inline ShareMemoryRegion* shm = nullptr;
    static inline void* base = nullptr;
    static inline SharedData* shared_data = nullptr;

    static constexpr const char* kShmName = "/robust_shm_mutex_test";
    static constexpr std::size_t kRegionBytes = 4096; // 4KB 足够了

    static void SetUpTestSuite() {
        // 如果之前的测试异常退出，先尝试清理
        ShareMemoryRegion::unlinkSegment(kShmName);
        
        shm = new ShareMemoryRegion(kShmName, kRegionBytes, /*create=*/true);
        base = shm->getMappedAddress();
        shared_data = new (base) SharedData();
    }

    static void TearDownTestSuite() {
        if (shared_data) {
            shared_data->~SharedData(); // 显式调用析构函数
        }
        delete shm;
        ShareMemoryRegion::unlinkSegment(kShmName);
    }

    // 每个测试用例开始前，都重置共享内存中的计数器
    void SetUp() override {
        if (shared_data) {
            shared_data->counter.store(0, std::memory_order_relaxed);
        }
    }
};


// ============ 测试 1: 并发互斥性 ============
TEST_F(ShmMutexLockTestFixture, ConcurrentIncrement_IsCorrect) {
    const int kNumProcesses = 8;
    const int kIncrementsPerProcess = 10000;

    std::vector<pid_t> pids;
    for (int i = 0; i < kNumProcesses; ++i) {
        pid_t pid = fork();
        ASSERT_NE(pid, -1);

        if (pid == 0) { // 子进程逻辑：只干活，不验证
            for (int j = 0; j < kIncrementsPerProcess; ++j) {
                std::lock_guard<ShmMutexLock> lock(shared_data->mutex);
                int current = shared_data->counter.load(std::memory_order_relaxed);
                shared_data->counter.store(current + 1, std::memory_order_relaxed);
            }
            _exit(0); // 任务完成，成功退出
        }
        pids.push_back(pid);
    }

    // 父进程逻辑：回收所有子进程并验证它们都成功了
    for (pid_t pid : pids) {
        int status = 0;
        ASSERT_EQ(waitpid(pid, &status, 0), pid);
        ASSERT_TRUE(WIFEXITED(status)) << "Child process " << pid << " terminated abnormally.";
        ASSERT_EQ(WEXITSTATUS(status), 0) << "Child process " << pid << " did not exit with status 0.";
    }

    // 父进程逻辑：验证最终共享数据的结果
    const int expected_value = kNumProcesses * kIncrementsPerProcess;
    EXPECT_EQ(shared_data->counter.load(), expected_value);
}


// ============ 测试 2: try_lock 语义 ============
TEST_F(ShmMutexLockTestFixture, TryLock_BehavesAsExpected) {
    // 1. 父进程先持有锁
    shared_data->mutex.lock();

    pid_t pid = fork();
    ASSERT_NE(pid, -1);

    if (pid == 0) { // 子进程逻辑：执行检查并通过退出码报告
        // 第一次尝试: 此时锁被父进程持有, try_lock 应该失败
        if (shared_data->mutex.try_lock()) {
            _exit(1); // 失败点 1: 不应该能锁住
        }

        // 等待一段时间让父进程释放锁 (简单 sleep 替代复杂管道)
        usleep(100 * 1000); // 100ms

        // 第二次尝试: 此时父进程应该已经释放了锁, try_lock 应该成功
        if (!shared_data->mutex.try_lock()) {
            _exit(2); // 失败点 2: 应该能锁住
        }

        // 成功锁住后, 释放并正常退出
        shared_data->mutex.unlock();
        _exit(0); // 所有检查都通过
    }

    // 父进程逻辑: 等待一小段时间, 确保子进程有机会先运行, 然后释放锁
    usleep(50 * 1000); // 50ms
    shared_data->mutex.unlock();

    // 父进程逻辑：等待子进程完成，并断言它成功通过了所有检查
    int status = 0;
    ASSERT_EQ(waitpid(pid, &status, 0), pid);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0) << "Child process failed a try_lock check. Exit code: " << WEXITSTATUS(status);
}


// ============ 测试 3: 鲁棒性恢复 ============
TEST_F(ShmMutexLockTestFixture, RecoversFromOwnerCrash) {
    pid_t pid = fork();
    ASSERT_NE(pid, -1);

    if (pid == 0) { // 子进程逻辑：模拟崩溃
        shared_data->mutex.lock();
        shared_data->counter.store(999); // 留下一个"半成品"数据
        _exit(0); // 持有锁直接退出
    }

    // 父进程等待 "崩溃的" 子进程结束
    int status = 0;
    ASSERT_EQ(waitpid(pid, &status, 0), pid);
    ASSERT_TRUE(WIFEXITED(status));

    // 父进程逻辑：验证锁的恢复能力
    // ShmMutexLock 内部的 EOWNERDEAD 处理逻辑应该能让这里成功, 而不是死锁
    ASSERT_NO_THROW({
        std::lock_guard<ShmMutexLock> lock(shared_data->mutex);
        // 成功获取锁, 表明恢复机制工作
        
        // 验证并修复数据
        ASSERT_EQ(shared_data->counter.load(), 999);
        shared_data->counter.store(1000);
    });

    // 验证数据已被修复
    EXPECT_EQ(shared_data->counter.load(), 1000);
}