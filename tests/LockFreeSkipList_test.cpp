#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <iostream>
#include <random>
#include <algorithm>
#include <numeric>

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
class LockFreeSkipListFixture : public ThreadHeapTestFixture {
protected:
    // ========================================================================
    // --- 核心修复函数：在隔离线程中运行测试 ---
    // ========================================================================
    // 这个函数解决了 TSan 的 SEGV 问题。
    // 原理：EBRManager 必须比所有注册过 TLS 的线程活得更久。
    // 我们在主线程创建 Manager，在子线程创建 List 和运行测试。
    // 子线程结束后，其 TLS 析构时 Manager 依然存活，从而安全退出。
    // ========================================================================
    template <typename Func>
    void RunInIsolatedThread(Func&& func) {
        // 1. [主线程] 分配 Manager (它将活得最久)
        auto* ebr_manager = new (ThreadHeap::allocate(sizeof(EBRManager))) EBRManager();
        
        // 2. [主线程] 启动一个子线程来承载测试逻辑
        std::thread worker([ebr_manager, func]() {
            // 3. [子线程] 创建 SkipList (这会注册子线程的 TLS 到 Manager)
            auto* list = new (ThreadHeap::allocate(sizeof(SkipList))) SkipList(*ebr_manager);
            
            // 4. [子线程] 执行具体的测试断言
            func(list);

            // 5. [子线程] 清理 List
            list->~SkipList();
            ThreadHeap::deallocate(list);

            // 6. [子线程] 线程即将结束，TLS 析构函数被调用。
            //    此时 ebr_manager 依然有效，releaseSlot 操作安全。
        });

        // 7. [主线程] 等待子线程彻底销毁 (包括其 TLS 清理完毕)
        worker.join();

        // 8. [主线程] 现在可以安全地销毁 Manager 了
        ebr_manager->~EBRManager();
        ThreadHeap::deallocate(ebr_manager);
    }
};


// ============================================================================
// --- 单线程测试用例 ---
// ============================================================================

TEST_F(LockFreeSkipListFixture, EmptySkipList) {
    RunInIsolatedThread([](SkipList* list) {
        ValueType value;
        EXPECT_FALSE(list->find(10, value)); // 在空列表中查找应该失败
        EXPECT_FALSE(list->remove(10));     // 从空列表中删除应该失败
    });
}

TEST_F(LockFreeSkipListFixture, InsertAndFind) {
    RunInIsolatedThread([](SkipList* list) {
        EXPECT_TRUE(list->insert(10, "ten"));
        
        ValueType value;
        EXPECT_TRUE(list->find(10, value));
        EXPECT_EQ(value, "ten");

        EXPECT_FALSE(list->find(99, value)); // 查找一个不存在的键
    });
}

TEST_F(LockFreeSkipListFixture, InsertExistingKeyFails) {
    RunInIsolatedThread([](SkipList* list) {
        list->insert(20, "twenty");
        EXPECT_FALSE(list->insert(20, "another twenty")); // 插入重复键应该失败

        ValueType value;
        list->find(20, value);
        EXPECT_EQ(value, "twenty"); // 值应该还是原来的值
    });
}

TEST_F(LockFreeSkipListFixture, InsertAndRemove) {
    RunInIsolatedThread([](SkipList* list) {
        list->insert(30, "thirty");
        
        ValueType value;
        EXPECT_TRUE(list->find(30, value)); // 确认插入成功
        
        EXPECT_TRUE(list->remove(30)); // 删除成功
        EXPECT_FALSE(list->find(30, value)); // 确认已删除
        EXPECT_FALSE(list->remove(30)); // 再次删除应该失败
    });
}

TEST_F(LockFreeSkipListFixture, MultipleOperations) {
    RunInIsolatedThread([](SkipList* list) {
        list->insert(10, "ten");
        list->insert(20, "twenty");
        list->insert(5, "five");

        ValueType value;
        EXPECT_TRUE(list->find(5, value) && value == "five");
        EXPECT_TRUE(list->find(10, value) && value == "ten");
        EXPECT_TRUE(list->find(20, value) && value == "twenty");

        EXPECT_TRUE(list->remove(10));
        EXPECT_FALSE(list->find(10, value));
        EXPECT_TRUE(list->find(20, value)); 
    });
}


// ============================================================================
// --- 并发测试用例 ---
// ============================================================================

TEST_F(LockFreeSkipListFixture, ConcurrentInsertions) {
    RunInIsolatedThread([](SkipList* list) {
        const int num_threads = 4;
        const int inserts_per_thread = 1000;
        std::vector<std::thread> threads;

        for (int i = 0; i < num_threads; ++i) {
            // 注意 capture list 指针
            threads.emplace_back([list, i, inserts_per_thread]() {
                for (int j = 0; j < inserts_per_thread; ++j) {
                    int key = i * inserts_per_thread + j;
                    std::string value = "val-" + std::to_string(key);
                    ASSERT_TRUE(list->insert(key, value));
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        for (int i = 0; i < num_threads * inserts_per_thread; ++i) {
            ValueType value;
            std::string expected_value = "val-" + std::to_string(i);
            ASSERT_TRUE(list->find(i, value)) << "Failed to find key: " << i;
            ASSERT_EQ(value, expected_value) << "Value mismatch for key: " << i;
        }
    });
}

TEST_F(LockFreeSkipListFixture, ConcurrentInsertionsHighContention) {
    RunInIsolatedThread([](SkipList* list) {
        const int num_threads = 8;
        const int total_keys = 10000;
        
        std::vector<int> keys(total_keys);
        std::iota(keys.begin(), keys.end(), 0);
        std::shuffle(keys.begin(), keys.end(), std::mt19937{std::random_device{}()});

        std::vector<std::thread> threads;
        std::atomic<size_t> key_index{0};
        std::atomic<int> success_inserts{0};

        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&key_index, &success_inserts, &keys, list, total_keys]() {
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

        ASSERT_EQ(success_inserts.load(), total_keys);

        for (int i = 0; i < total_keys; ++i) {
            ValueType value;
            ASSERT_TRUE(list->find(i, value)) << "Failed to find key: " << i;
        }
    });
}

TEST_F(LockFreeSkipListFixture, ConcurrentInsertAndRemove) {
    RunInIsolatedThread([](SkipList* list) {
        const int num_inserter_threads = 2;
        const int num_remover_threads = 2;
        const int items_per_thread = 200; 
        const int total_items = items_per_thread * num_inserter_threads;

        // 预先插入所有偶数键
        for (int i = 0; i < total_items; ++i) {
            list->insert(i * 2, "even-" + std::to_string(i * 2));
        }

        std::vector<std::thread> threads;

        // 插入线程 (奇数)
        for (int i = 0; i < num_inserter_threads; ++i) {
            threads.emplace_back([list, i, items_per_thread]() {
                for (int j = 0; j < items_per_thread; ++j) {
                    int key = (i * items_per_thread + j) * 2 + 1;
                    ASSERT_TRUE(list->insert(key, "odd-" + std::to_string(key)));
                }
            });
        }

        // 删除线程 (偶数)
        for (int i = 0; i < num_remover_threads; ++i) {
            threads.emplace_back([list, i, items_per_thread]() {
                for (int j = 0; j < items_per_thread; ++j) {
                    int key = (i * items_per_thread + j) * 2;
                    ASSERT_TRUE(list->remove(key));
                    std::this_thread::yield(); 
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }
        
        for (int i = 0; i < total_items; ++i) {
            ValueType value;
            EXPECT_FALSE(list->find(i * 2, value)) << "Even key " << i * 2 << " should have been removed.";
            EXPECT_TRUE(list->find(i * 2 + 1, value)) << "Odd key " << i * 2 + 1 << " should exist.";
        }
    });
}

TEST_F(LockFreeSkipListFixture, ConcurrentMixedWorkload) {
    RunInIsolatedThread([](SkipList* list) {
        const int num_writer_threads = 2;
        const int num_reader_threads = 2;
        const int key_range = 500;
        const std::chrono::milliseconds test_duration(200);

        std::vector<std::thread> threads;
        std::atomic<bool> stop_flag{false};

        // 写线程
        for (int i = 0; i < num_writer_threads; ++i) {
            threads.emplace_back([list, &stop_flag, key_range]() {
                std::mt19937 engine(std::random_device{}()); 
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

        // 读线程
        for (int i = 0; i < num_reader_threads; ++i) {
            threads.emplace_back([list, &stop_flag, key_range]() {
                std::mt19937 engine(std::random_device{}()); 
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

        // 验证链表有序性
        ASSERT_NO_FATAL_FAILURE([list](){
            using Node   = typename SkipList::Node;
            using Packer = StampPtrPacker<Node>;
            using Packed = typename Packer::type;

            std::vector<int> final_keys;

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
    });
}