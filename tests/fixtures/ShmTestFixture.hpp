#pragma once
#include <gtest/gtest.h>
#include <sys/mman.h> 
#include <iostream>
#include "ShareMemory/ShmSegment.hpp" 

class ShareMemoryRegion;

struct SharedMemoryTestFixture : public ::testing::Test {
public: // <--- 为常量和类型定义设置 public区域
    static constexpr const char* kShmName = "/lf_ipc_test";
    static constexpr std::size_t kRegionBytes = 256u << 20;

protected: // <--- 为实现细节保留 protected 区域
    inline static std::unique_ptr<ShmSegment> segment;
    inline static void* base = nullptr;

    static void SetUpTestSuite() {
        ShmSegment::unlink(kShmName);
        std::cout << "Pre-emptively unlinked." << std::endl;
        try {
            segment = std::make_unique<ShmSegment>(kShmName, kRegionBytes);
            ASSERT_NE(segment, nullptr);

            base = segment->getBaseAddress();
            std::cout << "Shared memory base address: " << base << std::endl;
            
            ASSERT_NE(base, nullptr);

        } catch (const std::exception& e) {
            std::cerr << "Exception: " << e.what() << std::endl;
            FAIL() << e.what();
        }
    }

    static void TearDownTestSuite() {
        // 销毁对象 (munmap, close)
        segment.reset();
        base = nullptr;

        // 清理文件
        ShmSegment::unlink(kShmName);
        std::cout << "Unlinked." << std::endl;
    }
};