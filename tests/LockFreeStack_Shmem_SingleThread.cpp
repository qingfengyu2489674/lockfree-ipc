// tests/LockFreeStack_Shmem_SingleThread.cpp
#include <gtest/gtest.h>
#include <new>                    // placement new
#include "lockfree/LockFreeStack.hpp"
#include "SharedMemoryTestFixture.hpp"   // 你已有的夹具，提供 base 指针等

// 夹具里假设有：void* base; 以及在 SetUp() 里完成映射

TEST_F(SharedMemoryTestFixture, LockFreeStack_InSharedMemory_SingleThread) {
    // 1) 在共享内存基址上构造 LockFreeStack<int>（placement new）
    using Stack = LockFreeStack<int>;
    ASSERT_NE(base, nullptr);

    // 若担心对齐，可用 std::align；多数情况下映射地址是页对齐，够用了
    auto* stack = new (base) Stack(); // placement new 到共享内存

    // 2) 基本功能：push / pop / empty
    EXPECT_TRUE(stack->empty());

    const int N = 10000;
    long long expected = 1LL * (N - 1) * N / 2; // push 0..N-1 的和
    for (int i = 0; i < N; ++i) stack->push(i);

    EXPECT_FALSE(stack->empty());

    long long sum = 0;
    int out = 0;
    int popped = 0;
    while (stack->pop(out)) {
        sum += out;
        ++popped;
    }

    EXPECT_EQ(popped, N);
    EXPECT_EQ(sum, expected);
    EXPECT_TRUE(stack->empty());

    // 3) 手动析构（placement new 的对象不能用 delete）
    stack->~Stack();
}
