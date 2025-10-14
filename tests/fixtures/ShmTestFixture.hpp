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
    static ShareMemoryRegion* shm;
    static void* base;

    static void SetUpTestSuite();
    static void TearDownTestSuite();
};