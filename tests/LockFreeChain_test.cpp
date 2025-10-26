#include <gtest/gtest.h>
#include <string>
#include <optional>

// 包含测试所需的基础设施
#include "fixtures/ThreadHeapTestFixture.hpp"
#include "EBRManager/EBRManager.hpp"
#include "EBRManager/guard.hpp"
#include "LockFreeHashMap/LockFreeChain.hpp"

// ============================================================================
// --- 类型别名 ---
// ============================================================================
using KeyType   = int;
using ValueType = std::string;
using Chain     = LockFreeChain<KeyType, ValueType>;

// ============================================================================
// --- 测试夹具 (Fixture) ---
// ============================================================================
class LockFreeChainFixture : public ThreadHeapTestFixture {
protected:
    void SetUp() override {
        // 调用基类的 SetUp
        ThreadHeapTestFixture::SetUp();

        // 使用 ThreadHeap 分配和构造 EBRManager 和 LockFreeChain
        manager_ = new (ThreadHeap::allocate(sizeof(EBRManager))) EBRManager();
        chain_ = new (ThreadHeap::allocate(sizeof(Chain))) Chain();
    }

    void TearDown() override {
        // 按照构造的逆序进行析构和释放
        chain_->~Chain();
        ThreadHeap::deallocate(chain_);

        manager_->~EBRManager();
        ThreadHeap::deallocate(manager_);
        
        // 调用基类的 TearDown
        ThreadHeapTestFixture::TearDown();
    }

    EBRManager* manager_ = nullptr;
    Chain* chain_        = nullptr;
};

// ============================================================================
// --- 单线程基础功能测试用例 ---
// ============================================================================

// 测试: 在空链表上查找，应返回空
TEST_F(LockFreeChainFixture, FindOnEmpty) {
    ebr::Guard guard(*manager_); // 所有操作都需要 Guard
    auto result = chain_->find(100, *manager_);
    EXPECT_FALSE(result.has_value());
}

// 测试: 成功插入一个新节点，并能成功找到它
TEST_F(LockFreeChainFixture, BasicInsertAndFind) {
    ebr::Guard guard(*manager_);
    
    // 1. 插入
    const KeyType key = 42;
    const ValueType value = "hello";
    EXPECT_TRUE(chain_->insert(key, value, *manager_));

    // 2. 查找已插入的节点
    auto result = chain_->find(key, *manager_);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), value);

    // 3. 查找不存在的节点
    auto non_existent_result = chain_->find(999, *manager_);
    EXPECT_FALSE(non_existent_result.has_value());
}

// 测试: 插入一个已存在的键，应失败并返回 false
TEST_F(LockFreeChainFixture, InsertExistingKeyFails) {
    ebr::Guard guard(*manager_);

    const KeyType key = 88;
    const ValueType original_value = "original";

    // 1. 首次插入
    ASSERT_TRUE(chain_->insert(key, original_value, *manager_));

    // 2. 尝试再次插入相同的键
    EXPECT_FALSE(chain_->insert(key, "new_value", *manager_));

    // 3. 验证原始值未被覆盖
    auto result = chain_->find(key, *manager_);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), original_value);
}

// 测试: 删除一个存在的节点
TEST_F(LockFreeChainFixture, RemoveExistingKey) {
    ebr::Guard guard(*manager_);

    const KeyType key_to_remove = 1;
    const ValueType value = "to_be_deleted";

    // 1. 插入节点
    ASSERT_TRUE(chain_->insert(key_to_remove, value, *manager_));
    // 确认它存在
    ASSERT_TRUE(chain_->find(key_to_remove, *manager_).has_value());

    // 2. 删除节点
    EXPECT_TRUE(chain_->remove(key_to_remove, *manager_));

    // 3. 验证节点已被删除
    auto result = chain_->find(key_to_remove, *manager_);
    EXPECT_FALSE(result.has_value());
}

// 测试: 删除一个不存在的节点，应失败并返回 false
TEST_F(LockFreeChainFixture, RemoveNonExistentKeyFails) {
    ebr::Guard guard(*manager_);
    
    // 插入一些其他数据，确保链表不为空
    chain_->insert(10, "ten", *manager_);
    chain_->insert(20, "twenty", *manager_);

    // 尝试删除一个不存在的键
    EXPECT_FALSE(chain_->remove(999, *manager_));
}

// 测试: 连续插入和删除多个节点
TEST_F(LockFreeChainFixture, MultipleOperations) {
    ebr::Guard guard(*manager_);

    // 插入
    ASSERT_TRUE(chain_->insert(1, "one", *manager_));
    ASSERT_TRUE(chain_->insert(2, "two", *manager_));
    ASSERT_TRUE(chain_->insert(3, "three", *manager_));

    // 删除中间一个
    ASSERT_TRUE(chain_->remove(2, *manager_));
    EXPECT_FALSE(chain_->find(2, *manager_).has_value());
    
    // 验证其他的还在
    EXPECT_TRUE(chain_->find(1, *manager_).has_value());
    EXPECT_TRUE(chain_->find(3, *manager_).has_value());

    // 删除剩下的
    ASSERT_TRUE(chain_->remove(1, *manager_));
    ASSERT_TRUE(chain_->remove(3, *manager_));

    // 验证链表为空（通过 getHead）
    // 注意：这里的 getHead 返回的是裸指针，可能包含标记位，需要清理
    auto head_ptr = chain_->getHead();
    EXPECT_EQ(Chain::Node::getUnmarked(head_ptr), nullptr);
}


// ... (之前的单线程测试用例保持不变) ...
#include <thread>
#include <vector>
#include <atomic>
#include <numeric>

// ============================================================================
// --- 多线程压力测试用例 ---
// ============================================================================

/**
 * @test ConcurrentInserts
 * @brief 多个线程同时向链表中插入不相交的数据集。
 *
 * 1. 设置线程数和每个线程要插入的元素数量。
 * 2. 每个线程负责一个唯一的键范围（例如，线程0插入0-999，线程1插入1000-1999）。
 * 3. 启动所有线程，让它们并发执行插入操作。
 * 4. 等待所有线程完成。
 * 5. 在主线程中，验证链表中所有插入的元素都存在。
 * 6. 验证链表的总节点数是否正确。
 */
TEST_F(LockFreeChainFixture, ConcurrentInserts) {
    const int num_threads = std::thread::hardware_concurrency(); // 使用硬件支持的并发线程数
    const int inserts_per_thread = 1000;
    std::vector<std::thread> threads;
    
    // 启动所有工作线程
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([this, i, inserts_per_thread]() {
            ebr::Guard guard(*manager_); // 每个线程都需要自己的 Guard
            
            int start_key = i * inserts_per_thread;
            int end_key = start_key + inserts_per_thread;
            
            for (int key = start_key; key < end_key; ++key) {
                std::string value = "value_" + std::to_string(key);
                ASSERT_TRUE(chain_->insert(key, value, *manager_));
            }
        });
    }

    // 等待所有线程完成
    for (auto& t : threads) {
        t.join();
    }

    // --- 验证阶段 ---
    ebr::Guard guard(*manager_);
    size_t final_count = 0;
    const int total_inserts = num_threads * inserts_per_thread;

    // 1. 验证所有插入的元素都存在
    for (int key = 0; key < total_inserts; ++key) {
        auto result = chain_->find(key, *manager_);
        ASSERT_TRUE(result.has_value()) << "Key " << key << " was not found!";
        EXPECT_EQ(result.value(), "value_" + std::to_string(key));
    }
    
    // 2. 遍历链表，计算实际节点数量并验证其正确性
    auto* head = Chain::Node::getUnmarked(chain_->getHead());
    while (head) {
        final_count++;
        head = Chain::Node::getUnmarked(head->next.load(std::memory_order_acquire));
    }
    EXPECT_EQ(final_count, total_inserts);
}


/**
 * @test MixedOperations
 * @brief 多个线程同时执行插入和删除操作。
 *
 * 1. 预先向链表中填充一定数量的偶数键。
 * 2. 启动多个线程，一半线程负责删除预填充的偶数键，另一半线程负责插入新的奇数键。
 * 3. 等待所有线程完成。
 * 4. 验证所有偶数键都已被删除。
 * 5. 验证所有新的奇数键都已成功插入。
 * 6. 验证链表的最终节点数。
 */
TEST_F(LockFreeChainFixture, MixedOperations) {
    const int num_threads = std::max(2u, std::thread::hardware_concurrency()); // 至少2个线程
    const int items_per_thread = 500;
    const int prefill_count = num_threads * items_per_thread;

    // --- 1. 预填充阶段 ---
    {
        ebr::Guard guard(*manager_);
        for (int i = 0; i < prefill_count; ++i) {
            // 插入偶数键
            int key = i * 2;
            chain_->insert(key, "prefill_" + std::to_string(key), *manager_);
        }
    }
    
    std::vector<std::thread> threads;
    std::atomic<bool> start_flag{false}; // 用于尽量让所有线程同时开始

    // --- 2. 并发操作阶段 ---
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([this, i, num_threads, items_per_thread, &start_flag]() {
            ebr::Guard guard(*manager_);
            
            while (!start_flag.load()) { /* spin wait */ }

            int start_index = i * items_per_thread;
            int end_index = start_index + items_per_thread;

            if (i % 2 == 0) { // 偶数线程负责删除
                for (int j = start_index; j < end_index; ++j) {
                    int key_to_remove = j * 2;
                    ASSERT_TRUE(chain_->remove(key_to_remove, *manager_));
                }
            } else { // 奇数线程负责插入
                for (int j = start_index; j < end_index; ++j) {
                    int key_to_insert = j * 2 + 1;
                    ASSERT_TRUE(chain_->insert(key_to_insert, "new_" + std::to_string(key_to_insert), *manager_));
                }
            }
        });
    }

    start_flag.store(true); // "发令枪"
    for (auto& t : threads) {
        t.join();
    }
    
    // --- 3. 验证阶段 ---
    ebr::Guard guard(*manager_);
    size_t final_count = 0;
    int num_deleter_threads = (num_threads + 1) / 2;
    int num_inserter_threads = num_threads / 2;
    int expected_final_count = prefill_count - (num_deleter_threads * items_per_thread) + (num_inserter_threads * items_per_thread);

    // 验证偶数键是否被删除，奇数键是否被插入
    for(int i = 0; i < prefill_count; ++i) {
        int even_key = i * 2;
        int odd_key = i * 2 + 1;

        int thread_owner_id = i / items_per_thread;
        if (thread_owner_id % 2 == 0) { // 这个范围的偶数键应该被删除了
            EXPECT_FALSE(chain_->find(even_key, *manager_).has_value()) << "Even key " << even_key << " should have been removed.";
        } else { // 这个范围的偶数键没有被删除者线程触碰
             EXPECT_TRUE(chain_->find(even_key, *manager_).has_value()) << "Even key " << even_key << " should still exist.";
        }
        
        if (thread_owner_id % 2 != 0) { // 这个范围的奇数键应该被插入了
            EXPECT_TRUE(chain_->find(odd_key, *manager_).has_value()) << "Odd key " << odd_key << " should have been inserted.";
        } else { // 这个范围的奇数键没有被插入者线程触碰
            EXPECT_FALSE(chain_->find(odd_key, *manager_).has_value()) << "Odd key " << odd_key << " should not exist.";
        }
    }
    
    // 再次遍历计算总数
    auto* head = Chain::Node::getUnmarked(chain_->getHead());
    while (head) {
        final_count++;
        head = Chain::Node::getUnmarked(head->next.load(std::memory_order_acquire));
    }
    EXPECT_EQ(final_count, expected_final_count);
}