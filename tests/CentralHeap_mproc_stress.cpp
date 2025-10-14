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
    ShareMemoryRegion* shm = nullptr; 
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
        shm = new ShareMemoryRegion(kShmName, kRegionBytes, true);
        base = shm->getMappedAddress();

        std::cout << "Mapped base address: " << base << std::endl;

        // 确保共享内存映射成功
        ASSERT_NE(base, nullptr) << "Mapped address is null. Shared memory setup failed.";
        ASSERT_NE(base, MAP_FAILED) << "mmap failed, returned MAP_FAILED.";

        // 创建 CentralHeap 实例
        heap = &CentralHeap::GetInstance(base, kRegionBytes);

        // 检查 heap 是否初始化成功
        ASSERT_NE(heap, nullptr) << "CentralHeap instance initialization failed.";
    }

    // void SetUp() override {
    //     // 使用 mmap 映射普通的内存区域
    //     size_t region_size = kRegionBytes;
    //     void* base = mmap(nullptr, region_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        
    //     // 输出映射的内存地址
    //     std::cout << "Mapped base address: " << base << std::endl;

    //     // 确保内存映射成功
    //     ASSERT_NE(base, nullptr) << "Mapped address is null. Memory mapping failed.";
    //     ASSERT_NE(base, MAP_FAILED) << "mmap failed, returned MAP_FAILED.";

    //     // 创建 CentralHeap 实例，使用 mmap 映射的内存作为堆区域
    //     heap = &CentralHeap::GetInstance(base, region_size);

    //     // 检查 heap 是否初始化成功
    //     ASSERT_NE(heap, nullptr) << "CentralHeap instance initialization failed.";
    // }

    // TearDown 是在每个测试用例运行之后执行的函数
    void TearDown() override {
        delete shm;
        shm = nullptr; // 好习惯
        shm_unlink(kShmName.c_str());
        std::cout << "Shared memory cleaned up on normal exit." << std::endl;
    }
};

TEST_F(CentralHeapTest, AcquireReleaseConcurrency_LockValidation) {
    std::vector<void*> acquired_chunks;
    std::unordered_set<void*> unique_chunks;  // 数据结构
    std::mutex unique_chunks_mutex;           // 用来保护 unique_chunks 的互斥锁

    const int num_threads = 10;
    const size_t chunk_size = CentralHeap::kChunkSize;

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.push_back(std::thread([this, &acquired_chunks, &unique_chunks, &unique_chunks_mutex, chunk_size] {
            void* chunk = heap->acquireChunk(chunk_size);
            ASSERT_NE(chunk, nullptr);

            std::cout << "Thread " << std::this_thread::get_id() << " allocated chunk: " << chunk << std::endl;

            // 使用互斥锁保护对 unique_chunks 的访问
            {
                std::lock_guard<std::mutex> lock(unique_chunks_mutex);
                bool inserted = unique_chunks.insert(chunk).second;
                ASSERT_TRUE(inserted) << "Duplicate chunk detected!";
                acquired_chunks.push_back(chunk);
            }

            

            // heap->releaseChunk(chunk, chunk_size);
        }));
    }

    for (auto& t : threads) {
        t.join();
    }

    for (auto& chunk : acquired_chunks) {
        ASSERT_NE(chunk, nullptr);
        heap->releaseChunk(chunk, chunk_size);
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
