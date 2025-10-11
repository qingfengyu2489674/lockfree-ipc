// tests/CentralHeap_mproc_smoke.cpp
#include <gtest/gtest.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <vector>
#include <cerrno>
#include <cstring>
#include <csignal>
#include <cstdint>

#include "SharedMemoryTestFixture.hpp"
#include "gc_malloc/CentralHeap/CentralHeap.hpp"
#include "gc_malloc/ThreadHeap/CentralHeapBootstrap.hpp"
#include "ShareMemory/ShareMemoryRegion.hpp"

namespace {
constexpr std::size_t kRegionBytes = SharedMemoryTestFixture::kRegionBytes;
inline bool is_aligned_2mb(void* p) {
    constexpr std::size_t k = CentralHeap::kChunkSize; // 2MiB
    return (reinterpret_cast<std::uintptr_t>(p) % k) == 0;
}
inline bool in_region(void* base, std::size_t bytes, void* p) {
    auto b = reinterpret_cast<std::uintptr_t>(base);
    auto e = b + bytes;
    auto x = reinterpret_cast<std::uintptr_t>(p);
    return (x >= b) && (x + CentralHeap::kChunkSize <= e);
}
}

// 复用你的共享内存夹具（父进程里创建段并初始化 CentralHeap）
class CentralHeapMPFixture : public SharedMemoryTestFixture {
protected:
    void SetUp() override {
        SharedMemoryTestFixture::SetUp();
        // 父进程一次性初始化（子进程只需附着）
        SetupCentral(base, kRegionBytes);
        // (void)CentralHeap::GetInstance(base, kRegionBytes);
    }
};

// 子进程：附着共享段 -> GetInstance -> acquire(2MiB) -> 简单写入 -> 释放 -> 退出
static int child_smoke(const char* shm_name,
                       std::size_t region_bytes,
                       int read_go_fd) // 父->子：同步开始
{
    char c;
    if (read(read_go_fd, &c, 1) != 1) _exit(100); // 没收到 GO

    ShareMemoryRegion shm(shm_name, region_bytes, /*create=*/false);
    void* base = shm.getMappedAddress();

    auto& ch = CentralHeap::GetInstance(base, region_bytes);

    void* p = ch.acquireChunk(CentralHeap::kChunkSize);
    if (!p) _exit(101); // 申请失败

    // 可选：写入自己的 pid 做个“可视化标记”
    pid_t pid = getpid();
    std::memcpy(p, &pid, sizeof(pid));

    // 略等片刻（放大并发窗口），然后释放
    usleep(5 * 1000);
    ch.releaseChunk(p, CentralHeap::kChunkSize);

    _exit(0);
}

// 多进程冒烟：只验证“可附着、可申请并释放”，不做唯一性/压力检查
TEST_F(CentralHeapMPFixture, MultiProcessSmoke_AttachAcquireRelease) {
    // 避免管道写端无读者导致 SIGPIPE 杀掉父进程
    signal(SIGPIPE, SIG_IGN);

    const std::size_t cap = kRegionBytes / CentralHeap::kChunkSize; // 16
    ASSERT_GE(cap, 4u);

    // 起 N 个子进程（不超过容量，避免因容量耗尽而失败）
    const int N = 8;

    struct PipePair { int p2c[2]; };
    std::vector<PipePair> pipes(N);
    std::vector<pid_t> pids(N, -1);

    // 建管道并 fork
    for (int i = 0; i < N; ++i) {
        ASSERT_EQ(pipe(pipes[i].p2c), 0) << "pipe failed: " << strerror(errno);
        pid_t pid = fork();
        ASSERT_NE(pid, -1) << "fork failed: " << strerror(errno);

        if (pid == 0) {
            // 子进程：读端接收 GO；关闭不必要的端
            close(pipes[i].p2c[1]);
            // 附着与申请释放逻辑
            child_smoke(SharedMemoryTestFixture::kShmName, kRegionBytes,
                        pipes[i].p2c[0]);
            // child_smoke 会 _exit，不会返回
        } else {
            // 父进程：保留写端用于发 GO
            pids[i] = pid;
            close(pipes[i].p2c[0]);
        }
    }

    // 所有子进程就绪后，统一发 GO
    for (int i = 0; i < N; ++i) {
        const char go = 'G';
        ssize_t wn = write(pipes[i].p2c[1], &go, 1);
        ASSERT_EQ(wn, 1) << "write GO failed: errno=" << errno << " " << strerror(errno);
        close(pipes[i].p2c[1]);
    }

    // 等待子进程退出并检查返回码
    for (int i = 0; i < N; ++i) {
        int st = 0;
        ASSERT_EQ(waitpid(pids[i], &st, 0), pids[i]);
        if (WIFEXITED(st)) {
            // 0=成功；100/101 是我们在子进程里定义的错误码
            EXPECT_EQ(WEXITSTATUS(st), 0) << "child exited with " << WEXITSTATUS(st);
        } else {
            FAIL() << "child terminated abnormally";
        }
    }

    // 父进程自己再做一次 acquire/release，确认仍可用
    auto& ch = CentralHeap::GetInstance(base, kRegionBytes);
    void* p = ch.acquireChunk(CentralHeap::kChunkSize);
    ASSERT_NE(p, nullptr);
    EXPECT_TRUE(is_aligned_2mb(p));
    EXPECT_TRUE(in_region(base, kRegionBytes, p));
    ch.releaseChunk(p, CentralHeap::kChunkSize);
}
