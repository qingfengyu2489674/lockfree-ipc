#include <gtest/gtest.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <vector>
#include <string>
#include <stdexcept>
#include <cstdio>
#include <cerrno>
#include <cstring>

#include "fixtures/ShmTestFixture.hpp"
#include "gc_malloc/CentralHeap/CentralHeap.hpp"
#include "gc_malloc/ThreadHeap/ProcessAllocatorContext.hpp"
#include "ShareMemory/ShareMemoryRegion.hpp"

// 这两个宏用于将 CMake 传入的宏定义 (WORKER_EXECUTABLE_PATH) 转换成一个字符串字面量
#define XSTR(s) STR(s)
#define STR(s) #s

// 测试主进程负责创建和初始化共享内存
class CentralHeapMPStressFixture : public SharedMemoryTestFixture {
protected:
    static void SetUpTestSuite() {
        ShareMemoryRegion::unlinkSegment(kShmName);
        SharedMemoryTestFixture::SetUpTestSuite();
        ProcessAllocatorContext::Setup(base, kRegionBytes);
    }
    
    static void TearDownTestSuite() {
        SharedMemoryTestFixture::TearDownTestSuite();
    }
};

TEST_F(CentralHeapMPStressFixture, ForkExec_HighContention_NoCrashesOrLeaks) {
    const int N = 8;
    const int ROUNDS_PER_PROCESS = 500;

    std::vector<pid_t> pids;
    
    // 使用由 CMake 在编译时注入的、包含完整路径的宏
    const char* worker_path = XSTR(WORKER_EXECUTABLE_PATH);
    
    std::string region_bytes_str = std::to_string(kRegionBytes);
    std::string rounds_str = std::to_string(ROUNDS_PER_PROCESS);

    for (int i = 0; i < N; ++i) {
        pid_t pid = fork();
        ASSERT_NE(pid, -1) << "fork failed: " << strerror(errno);

        if (pid == 0) {
            // 现在 worker_path 是一个绝对路径，execlp 保证能找到它
            execlp(worker_path,
                   worker_path,
                   kShmName,
                   region_bytes_str.c_str(),
                   rounds_str.c_str(),
                   (char*)NULL);
            
            // 如果 execlp 返回，说明它执行失败了
            perror("execlp failed");
            _exit(127);
        }
        pids.push_back(pid);
    }
    
    bool all_children_succeeded = true;
    for (pid_t pid : pids) {
        int status = 0;
        ASSERT_EQ(waitpid(pid, &status, 0), pid);
        
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            all_children_succeeded = false;
            ADD_FAILURE() << "Worker process " << pid << " failed. "
                          << "A crash, deadlock (timeout), or allocation error likely occurred in CentralHeap.\n"
                          << "  ExitStatus=" << (WIFEXITED(status) ? WEXITSTATUS(status) : -1)
                          << ", TermSignal=" << (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
        }
    }
    
    ASSERT_TRUE(all_children_succeeded) << "One or more worker processes failed the high-contention test.";

    // 进行最终的健全性检查 (Sanity Check)
    auto& ch = CentralHeap::GetInstance(base, kRegionBytes);
    const std::size_t theoretical_cap = kRegionBytes / CentralHeap::kChunkSize;
    std::vector<void*> final_chunks;
    
    for (size_t i = 0; i < theoretical_cap; ++i) {
        void* p = ch.acquireChunk(CentralHeap::kChunkSize);
        if (!p) {
            ASSERT_GE(i, theoretical_cap - 2) << "Failed to acquire chunk #" << (i + 1)
                << ". A significant memory leak likely occurred.";
            break;
        }
        final_chunks.push_back(p);
    }
    
    for (void* p : final_chunks) {
        ch.releaseChunk(p, CentralHeap::kChunkSize);
    }
}