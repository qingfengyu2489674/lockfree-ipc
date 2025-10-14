#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <iostream>
#include <atomic>
#include <sys/mman.h> 
#include <iostream>
#include <unordered_set>
#include <csignal>
#include <cstdlib>
#include "gc_malloc/CentralHeap/CentralHeap.hpp"
#include "ShareMemory/ShareMemoryRegion.hpp"

class CentralHeapTest : public ::testing::Test {
protected:
    void* base = nullptr;
    CentralHeap* heap = nullptr;
    const std::string kShmName = "/shared_memory_test";
    const size_t kRegionBytes = 1024 * 1024 * 256;  // 256 MB

    // 信号处理函数，用于捕获段错误并清理共享内存
    static void handle_signal(int signal) {
        if (signal == SIGSEGV) {
            std::cerr << "Received SIGSEGV, cleaning up shared memory..." << std::endl;
            shm_unlink("/shared_memory_test");  // 删除共享内存
            std::cerr << "Shared memory cleaned up." << std::endl;
        }
        exit(signal);  // 退出程序
    }

    void SetUp() override {
        // 注册信号处理器捕获 SIGSEGV
        signal(SIGSEGV, handle_signal);

        // 创建共享内存区域
        ShareMemoryRegion shm(kShmName, kRegionBytes, true);
        base = shm.getMappedAddress();

        std::cout << "Mapped base address: " << base << std::endl;

        // 确保共享内存映射成功
        ASSERT_NE(base, nullptr) << "Mapped address is null. Shared memory setup failed.";
        ASSERT_NE(base, MAP_FAILED) << "mmap failed, returned MAP_FAILED.";

        // 创建 CentralHeap 实例
        heap = &CentralHeap::GetInstance(base, kRegionBytes);

        // 检查 heap 是否初始化成功
        ASSERT_NE(heap, nullptr) << "CentralHeap instance initialization failed.";
    }

    // TearDown 是在每个测试用例运行之后执行的函数
    void TearDown() override {
        // 在程序正常退出时清理共享内存
        shm_unlink(kShmName.c_str());
        std::cout << "Shared memory cleaned up on normal exit." << std::endl;
    }
};

/**
 * @brief 测试 CentralHeap 的并发分配和释放，确保每个线程分配的内存块不重复
 */
TEST_F(CentralHeapTest, AcquireReleaseConcurrency_LockValidation) {
    std::vector<void*> acquired_chunks;
    std::unordered_set<void*> unique_chunks;

    const int num_threads = 10;
    const size_t chunk_size = CentralHeap::kChunkSize;

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.push_back(std::thread([this, &acquired_chunks, &unique_chunks, chunk_size] {
            void* chunk = heap->acquireChunk(chunk_size);
            ASSERT_NE(chunk, nullptr);

            std::cout << "Thread " << std::this_thread::get_id() << " allocated chunk: " << chunk << std::endl;

            bool inserted = unique_chunks.insert(chunk).second;
            ASSERT_TRUE(inserted) << "Duplicate chunk detected!";

            acquired_chunks.push_back(chunk);
            heap->releaseChunk(chunk, chunk_size);
        }));
    }

    for (auto& t : threads) {
        t.join();
    }

    for (auto& chunk : acquired_chunks) {
        ASSERT_NE(chunk, nullptr);
    }

    std::cout << "Test passed: Multiple threads successfully acquire and release unique chunks." << std::endl;
}

int main(int argc, char** argv) {
    // 注册退出时清理共享内存
    atexit([]() {
        shm_unlink("/shared_memory_test");
        std::cout << "Shared memory cleaned up on exit." << std::endl;
    });

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();  // 运行所有测试用例
}
