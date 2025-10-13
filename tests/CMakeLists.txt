# tests/CMakeLists.txt

# --- 主要的测试可执行文件 ---
add_executable(run_tests
    CentralHeap_gtest.cpp
    CentralHeap_mproc_test.cpp
    CentralHeap_mproc_stress.cpp
    TheadHeap_SmallBlocks_mproc_2proc2th.cpp
    HpSlotManager_test.cpp
    HpRetiredManager_test.cpp
    # LockFreeStack_test.cpp
    ShmMutexLock_gtest.cpp
)

target_link_libraries(run_tests PRIVATE
    mylib
    gtest_main
    pthread
    rt
)
target_include_directories(run_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

include(GoogleTest)
gtest_discover_tests(run_tests)


# --- 独立的工作者程序 ---
add_executable(central_heap_worker
    central_heap_worker.cpp
)

target_link_libraries(central_heap_worker PRIVATE
    mylib
    pthread
    rt
)

target_include_directories(central_heap_worker PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)


# ===================== 这就是解决问题的关键 =====================
# 将 worker 可执行文件的完整路径作为一个宏定义 (-D) 传递给 run_tests 的编译器。
# $<TARGET_FILE:central_heap_worker> 是一个 CMake "生成器表达式",
# 它会在生成构建文件时被替换为 central_heap_worker 可执行文件的确切路径。
# 这样，C++ 代码就能在任何环境下都找到 worker 程序。
target_compile_definitions(run_tests PRIVATE
    WORKER_EXECUTABLE_PATH="$<TARGET_FILE:central_heap_worker>"
)
# ====================================================================