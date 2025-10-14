#include "ShmTestFixture.hpp"


// --- 定义静态成员变量并提供初始值 ---
// 这里的 `SharedMemoryTestFixture::` 是必需的，用于指定作用域
ShareMemoryRegion* SharedMemoryTestFixture::shm = nullptr;
void* SharedMemoryTestFixture::base = nullptr;

void SharedMemoryTestFixture::SetUpTestSuite() {
    try {
        // 尝试创建共享内存区域
        shm = new ShareMemoryRegion(kShmName, kRegionBytes, true);
        ASSERT_NE(shm, nullptr) << "Failed to create ShareMemoryRegion object.";

        // 获取映射的地址
        base = shm->getMappedAddress();
        std::cout << "Shared memory base address: " << base << std::endl;
        
        // 检查映射地址是否有效
        ASSERT_NE(base, nullptr) << "Mapped address is null. Shared memory setup failed.";

    } catch (const std::exception& e) {
        // 捕获异常并输出错误信息
        std::cerr << "Exception occurred: " << e.what() << std::endl;
        FAIL() << "Failed to initialize shared memory: " << e.what();
    }
}

void SharedMemoryTestFixture::TearDownTestSuite() {
    delete shm;
    shm = nullptr; // 删除后将指针置空是一个好习惯
    base = nullptr;
    ShareMemoryRegion::unlinkSegment(kShmName);
}