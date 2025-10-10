#pragma once
#include <gtest/gtest.h>
#include "ShareMemory/ShareMemoryRegion.hpp"

struct SharedMemoryTestFixture : public ::testing::Test {
    static inline SharedMemoryRegion* shm = nullptr;
    static inline void* base = nullptr;
    static constexpr const char* kShmName = "/lf_ipc_test";

    static void SetUpTestSuite() {
        shm = new SharedMemoryRegion(kShmName, 1 << 20, true); // 1MB
        base = shm->getMappedAddress();
    }

    static void TearDownTestSuite() {
        delete shm;
        SharedMemoryRegion::unlinkSegment(kShmName);
    }
};
