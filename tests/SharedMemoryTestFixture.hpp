#pragma once
#include <gtest/gtest.h>
#include "ShareMemory/ShareMemoryRegion.hpp"

struct SharedMemoryTestFixture : public ::testing::Test {
    static inline ShareMemoryRegion* shm = nullptr;
    static inline void* base = nullptr;
    static constexpr const char* kShmName = "/lf_ipc_test";
    static constexpr std::size_t   kRegionBytes = 128u << 20;

    static void SetUpTestSuite() {
        shm = new ShareMemoryRegion(kShmName, kRegionBytes, true); // 128MB
        base = shm->getMappedAddress();
    }

    static void TearDownTestSuite() {
        delete shm;
        ShareMemoryRegion::unlinkSegment(kShmName);
    }
};
