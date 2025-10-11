// tests/CentralHeap_mproc_stress.cpp
#include <gtest/gtest.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <vector>
#include <unordered_set>
#include <cerrno>
#include <cstring>
#include <csignal>
#include <cstdint>
#include <chrono>
#include <thread>

#include "SharedMemoryTestFixture.hpp"
#include "gc_malloc/CentralHeap/CentralHeap.hpp"
#include "gc_malloc/ThreadHeap/ProcessAllocatorContext.hpp"
#include "ShareMemory/ShareMemoryRegion.hpp"

namespace {
constexpr std::size_t kRegionBytes = SharedMemoryTestFixture::kRegionBytes;
inline std::uint64_t offset_in_region(void* base, void* p) {
    auto b = reinterpret_cast<std::uintptr_t>(base);
    auto x = reinterpret_cast<std::uintptr_t>(p);
    return static_cast<std::uint64_t>(x - b);
}
}

// 父进程创建段并完成一次性初始化
class CentralHeapMPStressFixture : public SharedMemoryTestFixture {
protected:
    void SetUp() override {
        SharedMemoryTestFixture::SetUp();
        ProcessAllocatorContext::Setup(base, kRegionBytes);
    }
};

TEST_F(CentralHeapMPStressFixture, ForkedProcesses_StressRounds_NoConcurrentDuplicate) {
    signal(SIGPIPE, SIG_IGN); // 避免写端被 SIGPIPE 杀进程

    const std::size_t cap = kRegionBytes / CentralHeap::kChunkSize; // 16
    ASSERT_GE(cap, 8u);

    const int N = 8;          // 子进程数（不超过容量）
    const int ROUNDS = 100;   // 每进程重复回合数

    struct Pipes { int p2c_go[2]; int c2p_off[2]; };
    std::vector<Pipes> pipes(N);
    std::vector<pid_t> pids(N, -1);

    for (int i = 0; i < N; ++i) {
        ASSERT_EQ(pipe(pipes[i].p2c_go), 0);
        ASSERT_EQ(pipe(pipes[i].c2p_off), 0);

        pid_t pid = fork();
        ASSERT_NE(pid, -1) << "fork failed: " << strerror(errno);

        if (pid == 0) {
            // ---- child ----
            close(pipes[i].p2c_go[1]); // read GO
            close(pipes[i].c2p_off[0]); // write OFF

            // 等待 GO
            char c;
            if (read(pipes[i].p2c_go[0], &c, 1) != 1) _exit(200);

            ShareMemoryRegion shm(SharedMemoryTestFixture::kShmName, kRegionBytes, /*create=*/false);
            void* base2 = shm.getMappedAddress();
            auto& ch = CentralHeap::GetInstance(base2, kRegionBytes);

            for (int r = 0; r < ROUNDS; ++r) {
                void* p = ch.acquireChunk(CentralHeap::kChunkSize);
                if (!p) _exit(201);
                std::uint64_t off = offset_in_region(base2, p);
                if (write(pipes[i].c2p_off[1], &off, sizeof(off)) != (ssize_t)sizeof(off)) _exit(202);
                // 放大“同时期”窗口
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                ch.releaseChunk(p, CentralHeap::kChunkSize);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            _exit(0);
        } else {
            // ---- parent ----
            pids[i] = pid;
            close(pipes[i].p2c_go[0]);  // keep write end
            close(pipes[i].c2p_off[1]); // keep read end
        }
    }

    // 发 GO
    for (int i = 0; i < N; ++i) {
        const char go = 'G';
        ssize_t wn = write(pipes[i].p2c_go[1], &go, 1);
        ASSERT_EQ(wn, 1) << "write GO failed: errno=" << errno << " " << strerror(errno);
        close(pipes[i].p2c_go[1]);
    }

    // ROUNDS 轮的并发唯一性检查（允许历史复用）
    for (int r = 0; r < ROUNDS; ++r) {
        std::unordered_set<std::uint64_t> round_offs;
        round_offs.reserve(N);
        for (int i = 0; i < N; ++i) {
            std::uint64_t off = ~0ull;
            ssize_t n = read(pipes[i].c2p_off[0], &off, sizeof(off));
            ASSERT_EQ(n, (ssize_t)sizeof(off)) << "round " << r << ", child " << i;
            bool ok = round_offs.insert(off).second;
            EXPECT_TRUE(ok) << "concurrent duplicate in round " << r << ", off=" << off;
        }
        EXPECT_LE(round_offs.size(), cap);
    }

    // 回收
    for (int i = 0; i < N; ++i) {
        int st = 0;
        ASSERT_EQ(waitpid(pids[i], &st, 0), pids[i]);
        EXPECT_TRUE(WIFEXITED(st));
        if (WIFEXITED(st)) EXPECT_EQ(WEXITSTATUS(st), 0);
        close(pipes[i].c2p_off[0]);
    }
}
