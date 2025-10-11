// tests/SmallBlocks_mt_stable.cpp
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <unordered_set>
#include <atomic>
#include <random>
#include <mutex>
#include <chrono>

#include "SharedMemoryTestFixture.hpp"
#include "gc_malloc/ThreadHeap/ThreadHeap.hpp"
#include "gc_malloc/ThreadHeap/CentralHeapBootstrap.hpp"
#include "gc_malloc/CentralHeap/CentralHeap.hpp"

class SmallBlocksMTFixture : public SharedMemoryTestFixture {
protected:
    void SetUp() override {
        SharedMemoryTestFixture::SetUp();
        // 用夹具的统一容量
        SetupCentral(base, SharedMemoryTestFixture::kRegionBytes);
        (void)CentralHeap::GetInstance(base, SharedMemoryTestFixture::kRegionBytes);
    }
};

TEST_F(SmallBlocksMTFixture, SmallBlocks_Threaded_Stress_NoConcurrentDuplicate) {
    // 只测两个常用 size-class，避免过多 slab 冷启动
    static constexpr std::size_t kSizes[] = {64, 128};

    const int threads = 8;                     // 压力适中
    const int iters_per_thread = 10'000;      // 每线程迭代次数
    const int max_inflight_per_thread = 128;  // 限制在途对象上限

    std::atomic<size_t> total_alloc{0};
    std::atomic<size_t> total_free{0};

    auto worker = [&](int tid) {
        std::mt19937_64 rng(0xC0FFEE ^ (uint64_t)tid);
        std::vector<void*> bag;
        bag.reserve(max_inflight_per_thread);

        // 预热：每个 size 先 alloc/free 32 次，降低冷启动抖动
        for (std::size_t sz : kSizes) {
            for (int i = 0; i < 32; ++i) {
                void* p = ThreadHeap::allocate(sz);
                if (p) ThreadHeap::deallocate(p);
            }
        }

        for (int i = 0; i < iters_per_thread; ++i) {
            std::size_t sz = kSizes[rng() & 1];

            // 轻量重试窗口，缓和瞬时供给不上
            void* p = ThreadHeap::allocate(sz);
            if (!p) {
                for (int r = 0; r < 5 && !p; ++r) {
                    std::this_thread::yield();
                    p = ThreadHeap::allocate(sz);
                }
            }
            ASSERT_NE(p, nullptr) << "allocation failed under reasonable pressure";

            bag.push_back(p);
            total_alloc.fetch_add(1, std::memory_order_relaxed);

            // 控制在途数量；到达上限就释放一半
            if ((int)bag.size() >= max_inflight_per_thread) {
                for (int k = 0; k < max_inflight_per_thread / 2; ++k) {
                    void* q = bag.back(); bag.pop_back();
                    ThreadHeap::deallocate(q);
                    total_free.fetch_add(1, std::memory_order_relaxed);
                }
            }

            // 偶尔随机释放一些
            if ((rng() & 0x3FF) == 0 && !bag.empty()) {
                void* q = bag.back(); bag.pop_back();
                ThreadHeap::deallocate(q);
                total_free.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // 清空剩余
        while (!bag.empty()) {
            void* q = bag.back(); bag.pop_back();
            ThreadHeap::deallocate(q);
            total_free.fetch_add(1, std::memory_order_relaxed);
        }
    };

    std::vector<std::thread> ths;
    ths.reserve(threads);
    for (int t = 0; t < threads; ++t) ths.emplace_back(worker, t);
    for (auto& th : ths) th.join();

    // 所有分配应被释放
    EXPECT_EQ(total_alloc.load(), total_free.load());
}
