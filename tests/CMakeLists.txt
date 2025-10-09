# tests/CMakeLists.txt

# 1. 创建一个测试可执行文件
add_executable(run_tests
    test_sanity.cpp
    LockFreeStack_Shmem_SingleThread.cpp
)

# 2. 链接库：业务库 + GoogleTest
target_link_libraries(run_tests PRIVATE
    mylib       # 这是 src/CMakeLists.txt 里生成的库
    gtest_main  # 来自 third_party/googletest
    pthread
    rt
)

# 3. 确保包含目录可见
target_include_directories(run_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# 4. 启用 GoogleTest 的自动测试发现
include(GoogleTest)
gtest_discover_tests(run_tests)
