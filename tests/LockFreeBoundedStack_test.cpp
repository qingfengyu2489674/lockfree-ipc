#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <random>
#include <chrono>
#include <numeric>
#include <algorithm>
#include <cstdint>

#include "LockFreeStack/HazardPointers/LockFreeIndexStack.hpp"

// ---- 便捷别名（按需调整容量做不同用例）----
using StackSmall = LockFreeBoundedStack<std::uint32_t, 32>;
using StackMid   = LockFreeBoundedStack<std::uint32_t, 4096>;
using StackBig   = LockFreeBoundedStack<std::uint32_t, 65536>;

// ========== 基础：单线程 push/pop 行为 ==========
TEST(LockFreeBoundedStack, SingleThread_LIFO_Basic) {
    StackSmall s;

    // 空栈 pop 失败
    std::uint32_t out = 0;
    EXPECT_FALSE(s.tryPop(out));

    // 逐个 push
    for (std::uint32_t i = 1; i <= 10; ++i) {
        EXPECT_TRUE(s.tryPush(i));
    }

    // LIFO 检查
    for (std::uint32_t i = 10; i >= 1; --i) {
        EXPECT_TRUE(s.tryPop(out));
        EXPECT_EQ(out, i);
        if (i == 1) break;
    }

    // 清空后 pop 失败
    EXPECT_FALSE(s.tryPop(out));
}

// ========== 边界：满/空 边界行为 ==========
TEST(LockFreeBoundedStack, Boundary_FillAndDrain) {
    StackSmall s;

    // 填满
    for (std::uint32_t i = 0; i < 32; ++i) {
        EXPECT_TRUE(s.tryPush(i));
    }
    // 再次 push 失败（满）
    EXPECT_FALSE(s.tryPush(999u));

    // 逐个弹出并验证
    for (int i = 31; i >= 0; --i) {
        std::uint32_t out{};
        EXPECT_TRUE(s.tryPop(out));
        EXPECT_EQ(out, static_cast<std::uint32_t>(i));
    }
    // 再次 pop 失败（空）
    std::uint32_t out{};
    EXPECT_FALSE(s.tryPop(out));
}

// ========== 并发：多生产者 -> 多消费者（分两阶段，堆满后再消费） ==========
TEST(LockFreeBoundedStack, MPMC_ProducersThenConsumers_NoLossNoDup) {
    constexpr std::size_t producers = 8;
    constexpr std::size_t per_prod  = 512;     // 每个生产者的元素数
    constexpr std::size_t total     = producers * per_prod;

    static_assert(total <= 4096, "total must fit StackMid capacity");
    StackMid s;

    // 生产阶段（并发 push，直到所有元素入栈为止）
    std::vector<std::thread> ths;
    ths.reserve(producers);
    for (std::size_t p = 0; p < producers; ++p) {
        ths.emplace_back([&, p] {
            const std::uint32_t base = static_cast<std::uint32_t>(p * per_prod);
            for (std::uint32_t i = 0; i < per_prod; ++i) {
                const std::uint32_t v = base + i;
                while (!s.tryPush(v)) {
                    std::this_thread::yield(); // 退让以减少竞争
                }
            }
        });
    }
    for (auto& t : ths) t.join();
    ths.clear();

    // 消费阶段（并发 pop，直到拿完 total 个元素）
    std::atomic<std::size_t> popped{0};
    std::vector<std::atomic<bool>> seen(total);
    for (auto& f : seen) f.store(false, std::memory_order_relaxed);

    constexpr std::size_t consumers = 8;
    ths.reserve(consumers);
    for (std::size_t c = 0; c < consumers; ++c) {
        ths.emplace_back([&] {
            std::uint32_t v{};
            while (popped.load(std::memory_order_relaxed) < total) {
                if (s.tryPop(v)) {
                    auto prev = popped.fetch_add(1, std::memory_order_relaxed);
                    (void)prev;
                    ASSERT_LT(v, total) << "popped value out of expected range";
                    // 检查是否重复
                    bool expected = false;
                    bool ok = seen[v].compare_exchange_strong(expected, true, std::memory_order_relaxed);
                    ASSERT_TRUE(ok) << "duplicate pop: " << v;
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }
    for (auto& t : ths) t.join();

    EXPECT_EQ(popped.load(), total);

    // 所有元素都应被标记为 seen
    for (std::size_t i = 0; i < total; ++i) {
        EXPECT_TRUE(seen[i].load()) << "missing value: " << i;
    }
}

// ========== 并发交错：生产与消费同时进行 ==========
TEST(LockFreeBoundedStack, MPMC_Interleaved_Stress) {
    StackBig s;

    constexpr std::size_t producers = 8;
    constexpr std::size_t consumers = 8;
    constexpr std::size_t per_prod  = 4000;     // 每个生产者投放数量
    constexpr std::size_t total     = producers * per_prod;

    std::atomic<std::size_t> produced{0};
    std::atomic<std::size_t> consumed{0};

    std::vector<std::atomic<bool>> seen(total);
    for (auto& f : seen) f.store(false, std::memory_order_relaxed);

    std::vector<std::thread> ths;
    ths.reserve(producers + consumers);

    // 生产者
    for (std::size_t p = 0; p < producers; ++p) {
        ths.emplace_back([&, p] {
            const std::uint32_t base = static_cast<std::uint32_t>(p * per_prod);
            for (std::uint32_t i = 0; i < per_prod; ++i) {
                const std::uint32_t v = base + i;
                while (!s.tryPush(v)) {
                    std::this_thread::yield();
                }
                produced.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // 消费者
    for (std::size_t c = 0; c < consumers; ++c) {
        ths.emplace_back([&] {
            std::uint32_t v{};
            // 消费直到检测到所有生产完成且栈已被基本抽空
            for (;;) {
                if (s.tryPop(v)) {
                    auto n = consumed.fetch_add(1, std::memory_order_relaxed) + 1;
                    ASSERT_LT(v, total) << "popped value out of expected range";
                    bool expected = false;
                    bool ok = seen[v].compare_exchange_strong(expected, true, std::memory_order_relaxed);
                    ASSERT_TRUE(ok) << "duplicate pop: " << v;

                    // 快速结束条件：全部消费完成
                    if (n == total) break;
                } else {
                    // 若生产已全部结束且再也取不到元素，退出
                    if (produced.load(std::memory_order_relaxed) == total) {
                        // 再试几次以排空发布时间差
                        int spins = 0;
                        std::uint32_t tmp{};
                        while (spins++ < 1000 && s.tryPop(tmp)) {
                            auto n = consumed.fetch_add(1, std::memory_order_relaxed) + 1;
                            ASSERT_LT(tmp, total);
                            bool expected = false;
                            bool ok = seen[tmp].compare_exchange_strong(expected, true, std::memory_order_relaxed);
                            ASSERT_TRUE(ok);
                            if (n == total) break;
                        }
                        if (consumed.load() >= produced.load()) break;
                    }
                    std::this_thread::yield();
                }
            }
        });
    }

    for (auto& t : ths) t.join();

    EXPECT_EQ(produced.load(), total);
    EXPECT_EQ(consumed.load(), total);

    for (std::size_t i = 0; i < total; ++i) {
        EXPECT_TRUE(seen[i].load()) << "missing value: " << i;
    }
}

// ========== 轻量压力（时间片段内随机交错）==========
TEST(LockFreeBoundedStack, TimedRandom_Interleave) {
    StackMid s;
    constexpr int threads = 8;
    constexpr int push_ratio = 60; // 0..99 中小于该阈值则执行 push（其余 pop）
    std::atomic<std::size_t> produced{0}, consumed{0};
    std::vector<std::thread> ths;
    ths.reserve(threads);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);

    for (int t = 0; t < threads; ++t) {
        ths.emplace_back([&, t] {
            std::mt19937 rng(static_cast<unsigned>(t + 1234567));
            std::uniform_int_distribution<int> pick(0, 99);
            std::uint32_t ticket = static_cast<std::uint32_t>(t) << 24; // 线程域编码

            while (std::chrono::steady_clock::now() < deadline) {
                if (pick(rng) < push_ratio) {
                    // push
                    std::uint32_t v = ticket++;
                    if (s.tryPush(v)) {
                        produced.fetch_add(1, std::memory_order_relaxed);
                    } else {
                        std::this_thread::yield();
                    }
                } else {
                    // pop
                    std::uint32_t out{};
                    if (s.tryPop(out)) {
                        consumed.fetch_add(1, std::memory_order_relaxed);
                    } else {
                        std::this_thread::yield();
                    }
                }
            }

            // 额外 drain 一下，避免少量滞留
            std::uint32_t out{};
            while (s.tryPop(out)) {
                consumed.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& th : ths) th.join();

    // 在此压力模型下，可能 produced >= consumed，栈内可能留存元素（允许）
    // 只要不出现“多弹/丢元素”的问题即可：我们检查不变量
    // （严格内容校验在上面的 MPMC 测试里完成）
    EXPECT_GE(produced.load(), consumed.load());
}
