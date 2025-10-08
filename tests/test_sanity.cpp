#include "SharedMemoryTestFixture.hpp"
#include <cstring>

TEST_F(SharedMemoryTestFixture, SharedMemoryWorks) {
    const char* msg = "hello lockfree-ipc";
    std::strcpy(static_cast<char*>(base), msg);

    // 验证写入与读取一致
    EXPECT_STREQ(static_cast<char*>(base), msg);
}
