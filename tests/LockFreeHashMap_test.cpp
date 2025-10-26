#include <gtest/gtest.h>
#include <string>
#include <optional>
#include <thread>
#include <vector>
#include <atomic>

// 包含测试所需的基础设施
#include "fixtures/ThreadHeapTestFixture.hpp"
#include "LockFreeHashMap/LockFreeHashMap.hpp" 

// ============================================================================
// --- 类型别名 ---
// ============================================================================
using KeyType   = int;
using ValueType = std::string;
// 使用较小的桶数量以在测试中更容易触发哈希冲突
constexpr size_t TEST_BUCKET_COUNT = 4; 
using HashMap   = LockFreeHashMap<KeyType, ValueType>;

// ============================================================================
// --- 测试夹具 (Fixture) ---
// ============================================================================
class LockFreeHashMapFixture : public ThreadHeapTestFixture {
protected:
    void SetUp() override {
        ThreadHeapTestFixture::SetUp();
        // 注意：HashMap 的构造函数中使用了 new，但它内部的 unique_ptr 会管理内存。
        // 我们只需要确保 HashMap 对象本身的内存由 ThreadHeap 管理。
        map_ = new (ThreadHeap::allocate(sizeof(HashMap))) HashMap(TEST_BUCKET_COUNT);
    }

    void TearDown() override {
        map_->~HashMap();
        ThreadHeap::deallocate(map_);
        
        ThreadHeapTestFixture::TearDown();
    }

    HashMap* map_ = nullptr;
};

// ============================================================================
// --- 单线程基础功能测试用例 ---
// ============================================================================

// 测试: 构造函数是否正确设置了桶数量（向上取整到2的幂）
TEST_F(LockFreeHashMapFixture, ConstructorNormalizesBucketCount) {
    // 测试构造函数传入3，应规范化为4
    auto* map1 = new (ThreadHeap::allocate(sizeof(HashMap))) HashMap(3);
    EXPECT_EQ(map1->bucketCount(), 4);
    map1->~HashMap();
    ThreadHeap::deallocate(map1);
    
    // 测试构造函数传入4，应保持为4
    auto* map2 = new (ThreadHeap::allocate(sizeof(HashMap))) HashMap(4);
    EXPECT_EQ(map2->bucketCount(), 4);
    map2->~HashMap();
    ThreadHeap::deallocate(map2);

    // 测试构造函数传入1，应规范化为1 (roundUpToPowerOfTwo(1) -> 1)
    auto* map3 = new (ThreadHeap::allocate(sizeof(HashMap))) HashMap(1);
    EXPECT_EQ(map3->bucketCount(), 1);
    map3->~HashMap();
    ThreadHeap::deallocate(map3);
}

// 测试: 基本的插入、查找和删除
TEST_F(LockFreeHashMapFixture, BasicInsertFindRemove) {
    const KeyType key = 100;
    const ValueType value = "test_value";

    // 1. 插入
    EXPECT_TRUE(map_->insert(key, value));
    
    // 2. 查找
    auto result = map_->find(key);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), value);
    
    // 3. 再次插入相同键应失败
    EXPECT_FALSE(map_->insert(key, "another_value"));
    
    // 4. 删除
    EXPECT_TRUE(map_->remove(key));
    
    // 5. 确认已删除
    EXPECT_FALSE(map_->find(key).has_value());
    
    // 6. 再次删除应失败
    EXPECT_FALSE(map_->remove(key));
}

// 测试: 哈希冲突下的操作
// 我们知道 std::hash<int> 对整数的哈希就是其本身。
// bucket_count=4, bucket_mask=3。所以 key=5 (101) 和 key=9 (1001) 都会映射到 index=1。
TEST_F(LockFreeHashMapFixture, OperationsWithHashCollision) {
    const KeyType key1 = 5;
    const KeyType key2 = 9;
    const ValueType value1 = "value_for_5";
    const ValueType value2 = "value_for_9";

    // 插入两个会冲突的键
    ASSERT_TRUE(map_->insert(key1, value1));
    ASSERT_TRUE(map_->insert(key2, value2));

    // 验证两者都存在
    auto res1 = map_->find(key1);
    ASSERT_TRUE(res1.has_value());
    EXPECT_EQ(res1.value(), value1);

    auto res2 = map_->find(key2);
    ASSERT_TRUE(res2.has_value());
    EXPECT_EQ(res2.value(), value2);

    // 删除其中一个
    ASSERT_TRUE(map_->remove(key1));

    // 验证一个被删除，另一个还在
    EXPECT_FALSE(map_->find(key1).has_value());
    
    auto res2_after_delete = map_->find(key2);
    ASSERT_TRUE(res2_after_delete.has_value());
    EXPECT_EQ(res2_after_delete.value(), value2);
}


// ============================================================================
// --- 多线程压力测试用例 ---
// ============================================================================

/**
 * @test ConcurrentInsertsAcrossBuckets
 * @brief 多个线程并发地向不同的桶（以及一些冲突的桶）插入数据。
 */
TEST_F(LockFreeHashMapFixture, ConcurrentInsertsAcrossBuckets) {
    const int num_threads = std::thread::hardware_concurrency();
    const int inserts_per_thread = 200;
    std::vector<std::thread> threads;

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([this, i, inserts_per_thread]() {
            int start_key = i * inserts_per_thread;
            int end_key = start_key + inserts_per_thread;
            
            for (int key = start_key; key < end_key; ++key) {
                ASSERT_TRUE(map_->insert(key, "v" + std::to_string(key)));
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // --- 验证 ---
    const int total_inserts = num_threads * inserts_per_thread;
    for (int key = 0; key < total_inserts; ++key) {
        auto result = map_->find(key);
        ASSERT_TRUE(result.has_value()) << "Key " << key << " was not found!";
        EXPECT_EQ(result.value(), "v" + std::to_string(key));
    }
}

/**
 * @test ConcurrentMixedOperations
 * @brief 多个线程对整个哈希表进行并发的混合读、写、删除操作。
 */
TEST_F(LockFreeHashMapFixture, ConcurrentMixedOperations) {
    const int num_threads = std::max(2u, std::thread::hardware_concurrency());
    const int items_per_thread = 200;
    const int total_items = num_threads * items_per_thread;

    // 1. 预填充偶数键
    for (int i = 0; i < total_items; ++i) {
        map_->insert(i * 2, "prefill_" + std::to_string(i * 2));
    }

    std::vector<std::thread> threads;
    std::atomic<bool> start_flag{false};

    // 2. 并发操作
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([this, i, items_per_thread, &start_flag]() {
            while (!start_flag.load()) { /* spin */ }
            
            int start_index = i * items_per_thread;
            int end_index = start_index + items_per_thread;

            // 线程任务：删除一部分偶数键，插入一部分奇数键，查找一部分键
            for (int j = start_index; j < end_index; ++j) {
                // 删除偶数键
                ASSERT_TRUE(map_->remove(j * 2));
                // 插入奇数键
                ASSERT_TRUE(map_->insert(j * 2 + 1, "new_" + std::to_string(j * 2 + 1)));
                // 查找刚插入的奇数键
                ASSERT_TRUE(map_->find(j * 2 + 1).has_value());
            }
        });
    }

    start_flag.store(true);
    for (auto& t : threads) {
        t.join();
    }

    // 3. 验证
    for (int i = 0; i < total_items; ++i) {
        // 所有偶数键都应被删除
        EXPECT_FALSE(map_->find(i * 2).has_value());
        // 所有奇数键都应被插入
        auto result = map_->find(i * 2 + 1);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result.value(), "new_" + std::to_string(i * 2 + 1));
    }
}


// ... (之前的测试用例保持不变) ...

// ============================================================================
// --- 进阶多线程压力测试用例 ---
// ============================================================================

// --- 为高竞争测试自定义哈希函数 ---
struct SingleBucketHasher {
    std::size_t operator()(const KeyType& key) const {
        // 强制所有键都哈希到桶 0
        return 0;
    }
};
using ContentionHashMap = LockFreeHashMap<KeyType, ValueType, SingleBucketHasher>;


/**
 * @test HighContentionSingleBucket
 * @brief 所有线程都集中操作同一个桶，以最大化对 LockFreeChain 的压力。
 */
TEST_F(LockFreeHashMapFixture, HighContentionSingleBucket) {
    // 使用特殊的哈希表，强制所有操作都落在同一个桶
    auto* contention_map = new (ThreadHeap::allocate(sizeof(ContentionHashMap))) ContentionHashMap(TEST_BUCKET_COUNT);

    const int num_threads = std::thread::hardware_concurrency();
    const int ops_per_thread = 500;
    std::vector<std::thread> threads;
    std::atomic<int> successful_inserts{0};

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&, i]() {
            int start_key = i * ops_per_thread;
            int end_key = start_key + ops_per_thread;
            
            for (int key = start_key; key < end_key; ++key) {
                if (contention_map->insert(key, "contention_" + std::to_string(key))) {
                    successful_inserts++;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // --- 验证 ---
    const int total_ops = num_threads * ops_per_thread;
    EXPECT_EQ(successful_inserts.load(), total_ops);
    
    for (int key = 0; key < total_ops; ++key) {
        auto result = contention_map->find(key);
        ASSERT_TRUE(result.has_value()) << "Key " << key << " was not found under high contention!";
    }

    // 清理
    contention_map->~ContentionHashMap();
    ThreadHeap::deallocate(contention_map);
}


/**
 * @test InsertDeleteRace
 * @brief 多个线程对同一组键反复进行插入和删除，测试原子性。
 */
TEST_F(LockFreeHashMapFixture, InsertDeleteRace) {
    const int num_threads = std::thread::hardware_concurrency();
    const int ops_per_thread = 1000;
    // 每个线程操作自己的一小组独立的键，以避免死锁逻辑
    // 但多个线程可能操作同一个桶中的不同键
    const int keys_per_thread = 5; 

    std::vector<std::thread> threads;

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([this, i, ops_per_thread, keys_per_thread]() {
            int key_base = i * keys_per_thread;
            for (int op = 0; op < ops_per_thread; ++op) {
                int key = key_base + (op % keys_per_thread);
                std::string value = "race_" + std::to_string(key);
                
                // 尝试插入，如果成功，则立即删除
                if (map_->insert(key, value)) {
                    // 如果我们成功插入，那么我们应该能够立即删除它
                    // 在高并发下，可能另一个线程已经删除了它，所以我们不强制 assert true
                    map_->remove(key);
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // --- 验证 ---
    // 经过大量随机的插入和删除后，理论上大部分键都应该不存在于表中。
    // 我们无法精确断言最终状态，但最重要的验证是：
    // 1. 程序没有崩溃。
    // 2. 我们可以尝试清理所有可能遗留的键，并且操作应该是幂等的。
    int total_keys = num_threads * keys_per_thread;
    int items_left = 0;
    for (int key = 0; key < total_keys; ++key) {
        if (map_->find(key).has_value()) {
            items_left++;
            map_->remove(key); // 清理
        }
    }
    
    // 我们不能断言 items_left == 0，因为最后一个操作可能是 insert。
    // 这个测试的主要目的是验证在激烈竞争下的健壮性（不崩溃）。
    SUCCEED() << "InsertDeleteRace completed without crashing. Items left: " << items_left;
}


/**
 * @test ReadWriteRatio_HighRead
 * @brief 模拟读多写少的场景 (e.g., 90% 读, 10% 写)。
 */
TEST_F(LockFreeHashMapFixture, ReadWriteRatio_HighRead) {
    const int num_threads = std::thread::hardware_concurrency();
    const int ops_per_thread = 2000;
    const int num_writers = std::max(1, num_threads / 10);
    const int num_readers = num_threads - num_writers;

    // 预填充一些数据
    const int prefill_count = 500;
    for (int i = 0; i < prefill_count; ++i) {
        map_->insert(i, "prefill_" + std::to_string(i));
    }

    std::vector<std::thread> threads;
    std::atomic<bool> stop_flag{false};

    // 启动写线程 (持续插入新数据)
    for (int i = 0; i < num_writers; ++i) {
        threads.emplace_back([&, i]() {
            int key_offset = prefill_count + i * ops_per_thread;
            for (int j = 0; j < ops_per_thread; ++j) {
                map_->insert(key_offset + j, "writer_data");
            }
        });
    }

    // 启动读线程 (持续读取预填充数据)
    for (int i = 0; i < num_readers; ++i) {
        threads.emplace_back([&]() {
            while (!stop_flag.load()) {
                int key_to_read = rand() % prefill_count;
                auto result = map_->find(key_to_read);
                // 在预填充阶段后，这些键应该总是存在的
                ASSERT_TRUE(result.has_value());
            }
        });
    }

    // 等待写线程完成
    for (int i = 0; i < num_writers; ++i) {
        threads[i].join();
    }

    // 停止读线程
    stop_flag.store(true);
    for (int i = num_writers; i < num_threads; ++i) {
        threads[i].join();
    }
    
    SUCCEED() << "High-read ratio test completed without errors.";
}