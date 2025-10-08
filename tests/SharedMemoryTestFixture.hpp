#pragma once
#include <gtest/gtest.h>
#include "SharedMemoryManager.hpp"

struct SharedMemoryTestFixture : public ::testing::Test {
    static inline SharedMemoryManager* shm = nullptr;
    static inline void* base = nullptr;
    static constexpr const char* kShmName = "/lf_ipc_test";

    static void SetUpTestSuite() {
        shm = new SharedMemoryManager(kShmName, 1 << 20, true); // 1MB
        base = shm->getMappedAddress();
    }

    static void TearDownTestSuite() {
        delete shm;
        SharedMemoryManager::unlinkSegment(kShmName);
    }
};
