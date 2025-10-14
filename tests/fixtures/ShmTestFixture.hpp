#pragma once
#include <gtest/gtest.h>
#include <sys/mman.h> 
#include <iostream>
#include "ShareMemory/ShareMemoryRegion.hpp"

class ShareMemoryRegion;

struct SharedMemoryTestFixture : public ::testing::Test {
public: // <--- 为常量和类型定义设置 public区域
    static constexpr const char* kShmName = "/lf_ipc_test";
    static constexpr std::size_t kRegionBytes = 128u << 20; // 128MB

protected: // <--- 为实现细节保留 protected 区域
    inline static ShareMemoryRegion* shm = nullptr;
    inline static void* base = nullptr;

    static void SetUpTestSuite() {
        ShareMemoryRegion::unlinkSegment(kShmName);
        std::cout << "Pre-emptively unlinked any stale shared memory segment." << std::endl;
        try {
            shm = new ShareMemoryRegion(kShmName, kRegionBytes, true);
            ASSERT_NE(shm, nullptr) << "Failed to create ShareMemoryRegion object.";

            base = shm->getMappedAddress();
            std::cout << "Shared memory base address: " << base << std::endl;
            
            ASSERT_NE(base, nullptr) << "Mapped address is null. Shared memory setup failed.";

        } catch (const std::exception& e) {
            std::cerr << "Exception during SetUpTestSuite: " << e.what() << std::endl;
            FAIL() << "Failed to initialize shared memory due to an exception: " << e.what();
        }
    }

    static void TearDownTestSuite() {
        delete shm;
        shm = nullptr;
        base = nullptr;

        ShareMemoryRegion::unlinkSegment(kShmName);
        std::cout << "Shared memory segment '" << kShmName << "' unlinked." << std::endl;
    }
};