#pragma once
#include <gtest/gtest.h>

// 你的工程已有的头
#include "ShmTestFixture.hpp"
#include "gc_malloc/ThreadHeap/ProcessAllocatorContext.hpp"
#include "gc_malloc/CentralHeap/CentralHeap.hpp"

/**
 * @brief 统一的分配器测试夹具
 * - 继承 SharedMemoryTestFixture（提供 base/kRegionBytes）
 * - 在 SetUp() 里完成 ProcessAllocatorContext::Setup(...)
 * - 如有需要，可在 TearDown() 做收尾
 *
 * 用法：
 *   #include "tests/fixtures/ThreadHeapTestFixture.hpp"
 *   class MyFixture : public ThreadHeapTestFixture {};
 *   TEST_F(MyFixture, Case) { ... }
 */
class ThreadHeapTestFixture : public SharedMemoryTestFixture {
protected:
    void SetUp() override {
        SharedMemoryTestFixture::SetUp();
        // 必须在任何线程/分配发生前初始化分配器上下文
        ProcessAllocatorContext::Setup(base, kRegionBytes);
    }

    void TearDown() override {
        // 如果你的分配器需要显式收尾（比如 Shutdown/Finalize），在这里调用：
        // ProcessAllocatorContext::Shutdown();
        SharedMemoryTestFixture::TearDown();
    }
};
