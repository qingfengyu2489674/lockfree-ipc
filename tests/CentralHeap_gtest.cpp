// tests/CentralHeap_gtest.cpp
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <unordered_set>
#include <mutex>
#include <cstdint>
#include <cstring>

#include "SharedMemoryTestFixture.hpp"
#include "gc_malloc/CentralHeap/CentralHeap.hpp"
#include "gc_malloc/ThreadHeap/ThreadHeap.hpp"
#include "gc_malloc/ThreadHeap/CentralHeapBootstrap.hpp"

// 如果 SetupCentral 没有在可见头文件中声明，请解开下面这行：
// extern void SetupCentral(void* shm_base, size_t bytes);

namespace {
constexpr std::size_t kRegionBytes = SharedMemoryTestFixture::kRegionBytes;
inline bool is_aligned_2mb(void* p) {
    constexpr std::size_t k = CentralHeap::kChunkSize; // 2MB
    return (reinterpret_cast<std::uintptr_t>(p) % k) == 0;
}
inline bool in_region(void* base, std::size_t bytes, void* p) {
    auto b = reinterpret_cast<std::uintptr_t>(base);
    auto e = b + bytes;
    auto x = reinterpret_cast<std::uintptr_t>(p);
    return (x >= b) && (x + CentralHeap::kChunkSize <= e);
}
} // namespace

// 继承共享内存夹具
class CentralHeapFixture : public SharedMemoryTestFixture {
protected:
    void SetUp() override {
        SharedMemoryTestFixture::SetUp();
        // 在任何线程使用前，初始化中心堆（单例）
        SetupCentral(base, kRegionBytes);
        (void)CentralHeap::GetInstance(base, kRegionBytes);
    }
};

// -------------------- 基本可用性与单例 --------------------

TEST_F(CentralHeapFixture, SingletonGetInstanceReturnsSameAddress) {
    auto& ch1 = CentralHeap::GetInstance(base, kRegionBytes);
    auto& ch2 = CentralHeap::GetInstance(base, kRegionBytes);
    EXPECT_EQ(&ch1, &ch2);
}

TEST_F(CentralHeapFixture, AcquireOneChunkAlignedAndInsideRegion) {
    auto& ch = CentralHeap::GetInstance(base, kRegionBytes);
    void* p = ch.acquireChunk(CentralHeap::kChunkSize);
    ASSERT_NE(p, nullptr);
    EXPECT_TRUE(is_aligned_2mb(p));
    EXPECT_TRUE(in_region(base, kRegionBytes, p));
    ch.releaseChunk(p, CentralHeap::kChunkSize);
}

// -------------------- 容量边界：耗尽/释放/再获取 --------------------

TEST_F(CentralHeapFixture, ExhaustAllChunksThenReleaseAndReuse) {
    auto& ch = CentralHeap::GetInstance(base, kRegionBytes);

    // 理论上限（可能因为对齐损耗而取不到这么多）
    const std::size_t theoretical_max = kRegionBytes / CentralHeap::kChunkSize;
    ASSERT_GT(theoretical_max, 0u);

    std::vector<void*> got;
    got.reserve(theoretical_max);

    // 关键：拿到空为止，适配对齐损耗
    for (;;) {
        void* p = ch.acquireChunk(CentralHeap::kChunkSize);
        if (!p) break;
        ASSERT_TRUE(is_aligned_2mb(p));
        ASSERT_TRUE(in_region(base, kRegionBytes, p));
        got.push_back(p);
    }

    ASSERT_GT(got.size(), 0u);
    EXPECT_LE(got.size(), theoretical_max);

    // 释放全部后应可再次获取
    for (void* p : got) ch.releaseChunk(p, CentralHeap::kChunkSize);
    got.clear();

    void* again = ch.acquireChunk(CentralHeap::kChunkSize);
    ASSERT_NE(again, nullptr);
    ch.releaseChunk(again, CentralHeap::kChunkSize);
}

// -------------------- 释放后重用（不强制 LIFO/FIFO，仅验证有效性） --------------------

TEST_F(CentralHeapFixture, ReleaseThenAcquireGetsValidChunk) {
    auto& ch = CentralHeap::GetInstance(base, kRegionBytes);
    void* p1 = ch.acquireChunk(CentralHeap::kChunkSize);
    ASSERT_NE(p1, nullptr);
    ch.releaseChunk(p1, CentralHeap::kChunkSize);

    void* p2 = ch.acquireChunk(CentralHeap::kChunkSize);
    ASSERT_NE(p2, nullptr);
    EXPECT_TRUE(is_aligned_2mb(p2));
    EXPECT_TRUE(in_region(base, kRegionBytes, p2));
    ch.releaseChunk(p2, CentralHeap::kChunkSize);
}

// -------------------- 并发小压力：唯一性与对齐 --------------------
TEST_F(CentralHeapFixture, ConcurrentAcquireUniqueAndAligned) {
    auto& ch = CentralHeap::GetInstance(base, kRegionBytes);
    const size_t cap = kRegionBytes / CentralHeap::kChunkSize;
    ASSERT_GE(cap, 4u);

    const int threads = 8;
    const int per_thread_attempts =
        static_cast<int>((cap + threads - 1) / threads) + 2;

    std::mutex mtx;
    std::unordered_set<void*> in_use;  // 仅跟踪“当前被持有”的地址
    std::atomic<size_t> distinct_seen{0};
    std::unordered_set<void*> distinct_all;

    auto worker = [&]{
        std::vector<void*> mine; mine.reserve(per_thread_attempts);
        for (int i = 0; i < per_thread_attempts; ++i) {
            void* p = ch.acquireChunk(CentralHeap::kChunkSize);
            if (!p) break;
            {
                std::lock_guard<std::mutex> lk(mtx);
                // 如果插入失败，说明“同时”被他人持有，才算真正错误
                bool ok = in_use.insert(p).second;
                EXPECT_TRUE(ok) << "same chunk handed out concurrently";
                // 统计见过的不同地址（可选）
                distinct_all.insert(p);
            }
            mine.push_back(p);
        }
        // 归还
        for (void* p : mine) {
            {
                std::lock_guard<std::mutex> lk(mtx);
                in_use.erase(p);
            }
            ch.releaseChunk(p, CentralHeap::kChunkSize);
        }
    };

    std::vector<std::thread> th(threads);
    for (auto& t : th) t = std::thread(worker);
    for (auto& t : th) t.join();

    // 允许复用时，不再要求总获取 <= 容量；只要“同时期”没有重复即可
    EXPECT_LE(distinct_all.size(), cap);
}

// -------------------- 与共享内存示例一致的 smoke --------------------

TEST_F(CentralHeapFixture, SharedMemoryBasicWriteReadSmoke) {
    const char* msg = "hello lockfree-ipc";
    std::strcpy(static_cast<char*>(base), msg);
    EXPECT_STREQ(static_cast<char*>(base), msg);
}
