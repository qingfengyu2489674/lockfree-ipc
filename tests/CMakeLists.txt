# 主要的测试可执行文件
add_executable(run_tests
    # CentralHeap_gtest.cpp
    # CentralHeap_mproc_test.cpp
    # TheadHeap_SmallBlocks_mproc_2proc2th.cpp
    # HpSlotManager_test.cpp
    # HpRetiredManager_test.cpp
    # ShmMutexLock_gtest.cpp
    # CentralHeap_mproc_stress.cpp
    LockFreeStack_test.cpp
    # LockFreeQueue_test.cpp
    # LockFreeLinkedList_test.cpp
    # PolicyAllocator_Integration_test.cpp
    # ThreadSlot_test.cpp
    # lock_free_reuse_stack_interface_test.cpp
    # thread_slot_manager_test.cpp
    # EBRManager_test.cpp
    # LockFreeSkipList_test.cpp
    # LockFreeChain_test.cpp
    # LockFreeHashMap_test.cpp

    # skiplist_node_tests.cpp
)

# 链接所需的库
target_link_libraries(run_tests PRIVATE
    mylib
    gtest_main
    pthread
    rt
)

# 包含头文件目录
target_include_directories(run_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# 使用 GoogleTest 自动发现测试
include(GoogleTest)
gtest_discover_tests(run_tests)
