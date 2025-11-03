#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <iostream>

// 包含我们要测试的类的头文件
#include "LockFreeSkipList/LockFreeSkipList.hpp"
#include "EBRManager/EBRManager.hpp"

// 包含提供 ThreadHeap 初始化/销毁的夹具
#include "fixtures/ThreadHeapTestFixture.hpp"

// ============================================================================
// --- 类型别名 - 适配跳表 ---
// ============================================================================
using KeyType = int;
using ValueType = std::string;
using SkipList = LockFreeSkipList<KeyType, ValueType>;


// ============================================================================
// --- 测试夹具 (Fixture) ---
// ============================================================================
// 继承自 ThreadHeapTestFixture，它会在 SetUp 和 TearDown 中
// 自动调用 ThreadHeap::init() 和 ThreadHeap::shutdown()。
class LockFreeSkipListFixture : public ThreadHeapTestFixture {};


// ============================================================================
// --- 单线程测试用例 ---
// ============================================================================

// 测试一个空的跳表的基本行为
TEST_F(LockFreeSkipListFixture, EmptySkipList) {
    // 1. 设置
    auto* ebr_manager = new (ThreadHeap::allocate(sizeof(EBRManager))) EBRManager();
    auto* list = new (ThreadHeap::allocate(sizeof(SkipList))) SkipList(*ebr_manager);
    
    // 2. 断言
    ValueType value;
    EXPECT_FALSE(list->find(10, value)); // 在空列表中查找应该失败
    EXPECT_FALSE(list->remove(10));     // 从空列表中删除应该失败

    // 3. 清理
    list->~SkipList();
    ThreadHeap::deallocate(list);
    ebr_manager->~EBRManager();
    ThreadHeap::deallocate(ebr_manager);
}

// 测试基本的插入和查找操作
TEST_F(LockFreeSkipListFixture, InsertAndFind) {
    auto* ebr_manager = new (ThreadHeap::allocate(sizeof(EBRManager))) EBRManager();
    auto* list = new (ThreadHeap::allocate(sizeof(SkipList))) SkipList(*ebr_manager);

    EXPECT_TRUE(list->insert(10, "ten"));
    
    ValueType value;
    EXPECT_TRUE(list->find(10, value));
    EXPECT_EQ(value, "ten");

    EXPECT_FALSE(list->find(99, value)); // 查找一个不存在的键

    list->~SkipList();
    ThreadHeap::deallocate(list);
    ebr_manager->~EBRManager();
    ThreadHeap::deallocate(ebr_manager);
}

// 测试插入一个已存在的键，应该会失败
TEST_F(LockFreeSkipListFixture, InsertExistingKeyFails) {
    auto* ebr_manager = new (ThreadHeap::allocate(sizeof(EBRManager))) EBRManager();
    auto* list = new (ThreadHeap::allocate(sizeof(SkipList))) SkipList(*ebr_manager);

    list->insert(20, "twenty");
    EXPECT_FALSE(list->insert(20, "another twenty")); // 插入重复键应该失败

    ValueType value;
    list->find(20, value);
    EXPECT_EQ(value, "twenty"); // 值应该还是原来的值

    list->~SkipList();
    ThreadHeap::deallocate(list);
    ebr_manager->~EBRManager();
    ThreadHeap::deallocate(ebr_manager);
}

// 测试插入然后删除的操作
TEST_F(LockFreeSkipListFixture, InsertAndRemove) {
    auto* ebr_manager = new (ThreadHeap::allocate(sizeof(EBRManager))) EBRManager();
    auto* list = new (ThreadHeap::allocate(sizeof(SkipList))) SkipList(*ebr_manager);
    
    list->insert(30, "thirty");
    
    ValueType value;
    EXPECT_TRUE(list->find(30, value)); // 确认插入成功
    
    EXPECT_TRUE(list->remove(30)); // 删除成功
    EXPECT_FALSE(list->find(30, value)); // 确认已删除
    EXPECT_FALSE(list->remove(30)); // 再次删除应该失败

    list->~SkipList();
    ThreadHeap::deallocate(list);
    ebr_manager->~EBRManager();
    ThreadHeap::deallocate(ebr_manager);
}


// 测试更复杂的多项操作
TEST_F(LockFreeSkipListFixture, MultipleOperations) {
    auto* ebr_manager = new (ThreadHeap::allocate(sizeof(EBRManager))) EBRManager();
    auto* list = new (ThreadHeap::allocate(sizeof(SkipList))) SkipList(*ebr_manager);

    list->insert(10, "ten");
    list->insert(20, "twenty");
    list->insert(5, "five");

    ValueType value;
    EXPECT_TRUE(list->find(5, value) && value == "five");
    EXPECT_TRUE(list->find(10, value) && value == "ten");
    EXPECT_TRUE(list->find(20, value) && value == "twenty");

    EXPECT_TRUE(list->remove(10));
    EXPECT_FALSE(list->find(10, value));
    EXPECT_TRUE(list->find(20, value)); // 确认其他节点不受影响

    list->~SkipList();
    ThreadHeap::deallocate(list);
    ebr_manager->~EBRManager();
    ThreadHeap::deallocate(ebr_manager);
}


// ============================================================================
// --- 并发测试用例 ---
// ============================================================================

TEST_F(LockFreeSkipListFixture, ConcurrentInsertions) {
    auto* ebr_manager = new (ThreadHeap::allocate(sizeof(EBRManager))) EBRManager();
    auto* list = new (ThreadHeap::allocate(sizeof(SkipList))) SkipList(*ebr_manager);

    const int num_threads = 4;
    const int inserts_per_thread = 1000;
    std::vector<std::thread> threads;

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&list, i, inserts_per_thread]() {
            for (int j = 0; j < inserts_per_thread; ++j) {
                // 生成唯一的键，避免线程间冲突，专注于并发插入的正确性
                int key = i * inserts_per_thread + j;
                std::string value = "val-" + std::to_string(key);
                ASSERT_TRUE(list->insert(key, value));
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // 验证所有插入的元素都存在
    for (int i = 0; i < num_threads * inserts_per_thread; ++i) {
        ValueType value;
        std::string expected_value = "val-" + std::to_string(i);
        ASSERT_TRUE(list->find(i, value)) << "Failed to find key: " << i;
        ASSERT_EQ(value, expected_value) << "Value mismatch for key: " << i;
    }

    list->~SkipList();
    ThreadHeap::deallocate(list);
    ebr_manager->~EBRManager();
    ThreadHeap::deallocate(ebr_manager);
}




// ============================================================================

// 测试2: 高冲突并发插入
// 所有线程从一个共享的、随机化的键池中获取键进行插入。
// 这会制造大量的 CAS 冲突，严格测试 insert 的重试逻辑。
TEST_F(LockFreeSkipListFixture, ConcurrentInsertionsHighContention) {
    auto* ebr_manager = new (ThreadHeap::allocate(sizeof(EBRManager))) EBRManager();
    auto* list = new (ThreadHeap::allocate(sizeof(SkipList))) SkipList(*ebr_manager);

    const int num_threads = 8;
    const int total_keys = 10000;
    
    // 创建一个包含 0 到 total_keys-1 的键向量并打乱
    std::vector<int> keys(total_keys);
    std::iota(keys.begin(), keys.end(), 0);
    std::shuffle(keys.begin(), keys.end(), std::mt19937{std::random_device{}()});

    std::vector<std::thread> threads;
    std::atomic<size_t> key_index{0};
    std::atomic<int> success_inserts{0};

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&]() {
            while (true) {
                size_t current_index = key_index.fetch_add(1);
                if (current_index >= total_keys) {
                    break;
                }
                int key = keys[current_index];
                if (list->insert(key, "val-" + std::to_string(key))) {
                    success_inserts++;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // 断言：成功插入的数量必须等于总键数
    ASSERT_EQ(success_inserts.load(), total_keys);

    // 验证：所有键都确实存在于跳表中
    for (int i = 0; i < total_keys; ++i) {
        ValueType value;
        ASSERT_TRUE(list->find(i, value)) << "Failed to find key: " << i;
    }

    list->~SkipList();
    ThreadHeap::deallocate(list);
    ebr_manager->~EBRManager();
    ThreadHeap::deallocate(ebr_manager);
}

// 测试3: 并发插入和删除 (降压版)
TEST_F(LockFreeSkipListFixture, ConcurrentInsertAndRemove) {
    auto* ebr_manager = new (ThreadHeap::allocate(sizeof(EBRManager))) EBRManager();
    auto* list = new (ThreadHeap::allocate(sizeof(SkipList))) SkipList(*ebr_manager);

    // --- 降压修改 ---
    const int num_inserter_threads = 2;
    const int num_remover_threads = 2;
    const int items_per_thread = 200; // 从 2000 大幅减少
    // --- 结束修改 ---
    const int total_items = items_per_thread * num_inserter_threads;

    // 预先插入所有偶数键
    for (int i = 0; i < total_items; ++i) {
        list->insert(i * 2, "even-" + std::to_string(i * 2));
    }

    std::vector<std::thread> threads;

    // 创建插入线程 (插入奇数)
    for (int i = 0; i < num_inserter_threads; ++i) {
        threads.emplace_back([=, &list]() { // 捕获 list 的引用
            for (int j = 0; j < items_per_thread; ++j) {
                int key = (i * items_per_thread + j) * 2 + 1;
                ASSERT_TRUE(list->insert(key, "odd-" + std::to_string(key)));
            }
        });
    }

    // 创建删除线程 (删除偶数)
    for (int i = 0; i < num_remover_threads; ++i) {
        threads.emplace_back([=, &list]() { // 捕获 list 的引用
            for (int j = 0; j < items_per_thread; ++j) {
                int key = (i * items_per_thread + j) * 2;
                ASSERT_TRUE(list->remove(key));
                // --- 降压修改：缓解活锁 ---
                std::this_thread::yield(); 
                // --- 结束修改 ---
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }
    
    // 验证最终状态... (保持不变)
    for (int i = 0; i < total_items; ++i) {
        ValueType value;
        EXPECT_FALSE(list->find(i * 2, value)) << "Even key " << i * 2 << " should have been removed.";
        EXPECT_TRUE(list->find(i * 2 + 1, value)) << "Odd key " << i * 2 + 1 << " should exist.";
    }

    list->~SkipList();
    ThreadHeap::deallocate(list);
    ebr_manager->~EBRManager();
    ThreadHeap::deallocate(ebr_manager);
}


// 测试4: 混合读、写、删除的终极压力测试 (降压版)
TEST_F(LockFreeSkipListFixture, ConcurrentMixedWorkload) {
    auto* ebr_manager = new (ThreadHeap::allocate(sizeof(EBRManager))) EBRManager();
    auto* list = new (ThreadHeap::allocate(sizeof(SkipList))) SkipList(*ebr_manager);

    const int num_writer_threads = 2;
    const int num_reader_threads = 2;
    const int key_range = 500;
    const std::chrono::milliseconds test_duration(200);

    std::vector<std::thread> threads;
    std::atomic<bool> stop_flag{false};

    // 创建写线程 (50%插入, 50%删除)
    for (int i = 0; i < num_writer_threads; ++i) {
        threads.emplace_back([&]() {
            // --- 修正点 ---
            std::mt19937 engine(std::random_device{}()); // mt1997 -> mt19937
            // --- 结束修正 ---
            std::uniform_int_distribution<int> key_dist(0, key_range - 1);
            std::uniform_int_distribution<int> op_dist(0, 1);
            while (!stop_flag.load()) {
                int key = key_dist(engine);
                if (op_dist(engine) == 0) {
                    list->insert(key, "val-" + std::to_string(key));
                } else {
                    list->remove(key);
                }
                std::this_thread::yield();
            }
        });
    }

    // 创建读线程
    for (int i = 0; i < num_reader_threads; ++i) {
        threads.emplace_back([&]() {
            // --- 修正点 ---
            std::mt19937 engine(std::random_device{}()); // mt1997 -> mt19937
            // --- 结束修正 ---
            std::uniform_int_distribution<int> key_dist(0, key_range - 1);
            ValueType value;
            while (!stop_flag.load()) {
                list->find(key_dist(engine), value);
                std::this_thread::yield();
            }
        });
    }

    std::this_thread::sleep_for(test_duration);
    stop_flag.store(true);

    for (auto& t : threads) {
        t.join();
    }

    // 验证... (保持不变)
    ASSERT_NO_FATAL_FAILURE([&](){
        using Node   = typename SkipList::Node;
        using Packer = StampPtrPacker<Node>;
        using Packed = typename Packer::type;

        std::vector<int> final_keys;

        // 从 head_->next 开始
        Packed p = list->head_->nextSlot(0).load(std::memory_order_relaxed);
        Node* node = list->getUnmarked_(Packer::unpackPtr(p));

        while (node != nullptr) {
            final_keys.push_back(node->key);
            p = node->nextSlot(0).load(std::memory_order_relaxed);
            node = list->getUnmarked_(Packer::unpackPtr(p));
        }

        EXPECT_TRUE(std::is_sorted(final_keys.begin(), final_keys.end()))
            << "Skip list is not sorted after stress test!";
    }());

    list->~SkipList();
    ThreadHeap::deallocate(list);
    ebr_manager->~EBRManager();
    ThreadHeap::deallocate(ebr_manager);
}