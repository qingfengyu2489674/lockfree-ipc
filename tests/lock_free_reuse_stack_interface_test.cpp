#include "gtest/gtest.h"

// 1. Standard library includes needed for the tests
#include <thread>
#include <vector>
#include <atomic>
#include <set>
#include <numeric>

// 2. Include the user's headers. The test assumes these files exist and are correct.
#include "Tool/StampPtrPacker.hpp"
#include "EBRManager/LockFreeReuseStack.hpp"

// 3. Define a simple Node structure for use in the tests.
//    This is required to instantiate the LockFreeReuseStack.
struct TestNode {
    // A unique identifier to verify that no nodes are lost or duplicated.
    int id;
    
    // The 'next' pointer is required by the LockFreeReuseStack implementation.
    TestNode* next;

    // Constructor for easy initialization.
    explicit TestNode(int unique_id = 0) : id(unique_id), next(nullptr) {}

    // Make nodes non-copyable to prevent accidental misuse.
    TestNode(const TestNode&) = delete;
    TestNode& operator=(const TestNode&) = delete;

    // ** FIX: Explicitly default the move operations to allow usage in std::vector **
    TestNode(TestNode&&) noexcept = default;
    TestNode& operator=(TestNode&&) noexcept = default;
};

// ============================================================================
// Google Test Cases
// ============================================================================

// Test fixture for basic, single-threaded sanity checks.
class LockFreeReuseStackInterfaceTest : public ::testing::Test {
protected:
    LockFreeReuseStack<TestNode> stack;
};

// Test: Popping from a default-constructed (empty) stack should yield nullptr.
TEST_F(LockFreeReuseStackInterfaceTest, PopFromEmptyStack) {
    ASSERT_EQ(stack.pop(), nullptr);
}

// Test: A single element pushed should be the same element that is popped.
TEST_F(LockFreeReuseStackInterfaceTest, PushPopSingleElement) {
    TestNode node(100);
    stack.push(&node);
    
    TestNode* popped_node = stack.pop();
    ASSERT_EQ(popped_node, &node);
    EXPECT_EQ(popped_node->id, 100);

    // After popping the only element, the stack should be empty again.
    ASSERT_EQ(stack.pop(), nullptr);
}

// Test: The stack must follow a Last-In, First-Out (LIFO) order.
TEST_F(LockFreeReuseStackInterfaceTest, ObeysLIFOOrder) {
    TestNode node1(1), node2(2), node3(3);
    
    stack.push(&node1);
    stack.push(&node2);
    stack.push(&node3); // node3 is the last one in
    
    // Elements should be popped in the reverse order of how they were pushed.
    ASSERT_EQ(stack.pop(), &node3);
    ASSERT_EQ(stack.pop(), &node2);
    ASSERT_EQ(stack.pop(), &node1);

    // The stack must be empty after all elements are popped.
    ASSERT_EQ(stack.pop(), nullptr);
}

// A dedicated test suite for concurrency-related stress tests.
class LockFreeReuseStackConcurrencyTest : public ::testing::Test {};

// Test: Multiple threads concurrently push and pop a large number of elements.
// This test is designed to expose race conditions, ABA problems (which StampPtrPacker
// should solve), and memory consistency issues.
TEST_F(LockFreeReuseStackConcurrencyTest, StressTestWithConcurrentProducersAndConsumers) {
    // --- Test Configuration ---
    const int PRODUCER_THREADS = std::thread::hardware_concurrency() > 1 ? std::thread::hardware_concurrency() / 2 : 1;
    const int CONSUMER_THREADS = std::thread::hardware_concurrency() > 1 ? std::thread::hardware_concurrency() / 2 : 1;
    const int NODES_PER_PRODUCER = 20000;
    const int TOTAL_NODES = PRODUCER_THREADS * NODES_PER_PRODUCER;

    // The shared stack instance to be tested.
    LockFreeReuseStack<TestNode> stack;

    // Pre-allocate all nodes to ensure the test focuses on the stack's performance,
    // not the underlying memory allocator's performance or thread-safety.
    std::vector<TestNode> node_pool;
    node_pool.reserve(TOTAL_NODES);
    for (int i = 0; i < TOTAL_NODES; ++i) {
        node_pool.emplace_back(i);
    }

    std::atomic<int> producers_finished_count(0);
    std::vector<std::vector<TestNode*>> consumer_popped_nodes(CONSUMER_THREADS);
    std::vector<std::thread> threads;

    // --- Launch Producer Threads ---
    for (int i = 0; i < PRODUCER_THREADS; ++i) {
        threads.emplace_back([&, i] {
            const int start_index = i * NODES_PER_PRODUCER;
            const int end_index = start_index + NODES_PER_PRODUCER;
            for (int j = start_index; j < end_index; ++j) {
                stack.push(&node_pool[j]);
            }
            producers_finished_count.fetch_add(1, std::memory_order_release);
        });
    }

    // --- Launch Consumer Threads ---
    for (int i = 0; i < CONSUMER_THREADS; ++i) {
        threads.emplace_back([&, i] {
            auto& my_popped_nodes = consumer_popped_nodes[i];
            while (true) {
                TestNode* node = stack.pop();
                if (node) {
                    my_popped_nodes.push_back(node);
                } else {
                    // If all producers are done and the stack is empty, we can stop.
                    if (producers_finished_count.load(std::memory_order_acquire) == PRODUCER_THREADS) {
                        // Perform one last pop to catch any straggler nodes from a race condition.
                        if (stack.pop() == nullptr) {
                            break; // Exit the loop.
                        }
                    }
                }
            }
        });
    }

    // --- Wait for all threads to complete their work ---
    for (auto& t : threads) {
        t.join();
    }

    // --- Verification Phase ---

    // 1. After all threads finish, the stack must be empty.
    ASSERT_EQ(stack.pop(), nullptr) << "The stack should be empty after the stress test.";

    // 2. Aggregate results from all consumers and verify integrity.
    std::set<int> all_popped_ids;
    size_t total_popped_count = 0;
    for (const auto& popped_nodes : consumer_popped_nodes) {
        total_popped_count += popped_nodes.size();
        for (const auto* node : popped_nodes) {
            all_popped_ids.insert(node->id);
        }
    }

    // 3. The total number of popped nodes must exactly match the total number pushed.
    //    This checks for lost nodes.
    ASSERT_EQ(total_popped_count, TOTAL_NODES) << "Mismatch between pushed and popped counts. Some nodes were lost.";

    // 4. The number of unique IDs must also match the total number.
    //    This checks for duplicated pops (one node popped by multiple threads).
    ASSERT_EQ(all_popped_ids.size(), TOTAL_NODES) << "Duplicate nodes were detected. A node was popped more than once.";
}


#include "gtest/gtest.h"

// ... (Keep all the existing code in your test file: includes, TestNode, existing tests) ...

// TEST: A much more intense stress test where every thread is both a producer and a consumer.
// This creates maximum contention and unpredictable interleaving of operations, making it
// very effective at finding subtle race conditions and ABA-related bugs.
TEST_F(LockFreeReuseStackConcurrencyTest, MixedWorkloadHighContentionStressTest) {
    // --- Test Configuration ---
    // Use all available cores to create as much contention as possible.
    const unsigned int NUM_THREADS = std::thread::hardware_concurrency();
    ASSERT_GE(NUM_THREADS, 1);
    
    const int OPERATIONS_PER_THREAD = 50000;
    const int TOTAL_NODES = NUM_THREADS * 100; // A smaller pool of nodes to encourage reuse.

    // The shared stack instance to be tested.
    LockFreeReuseStack<TestNode> stack;

    // A pool of nodes that threads will initially push onto the stack.
    std::vector<TestNode> node_pool;
    node_pool.reserve(TOTAL_NODES);
    for (int i = 0; i < TOTAL_NODES; ++i) {
        node_pool.emplace_back(i);
    }
    
    // Initially, push all nodes onto the stack so threads have something to pop.
    for(int i = 0; i < TOTAL_NODES; ++i) {
        stack.push(&node_pool[i]);
    }

    std::vector<std::thread> threads;
    // Each thread will have a local vector to store the nodes it currently holds.
    std::vector<std::vector<TestNode*>> thread_local_nodes(NUM_THREADS);

    // --- Launch Worker Threads ---
    for (unsigned int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([&, i] {
            auto& my_nodes = thread_local_nodes[i];
            // Seed a simple random number generator for this thread.
            unsigned int rand_state = i;

            for (int j = 0; j < OPERATIONS_PER_THREAD; ++j) {
                // Randomly decide whether to push or pop.
                // Give a slight bias towards popping if we hold nodes, and pushing if we don't.
                if (!my_nodes.empty() && (rand_r(&rand_state) % 10 < 7)) { // 70% chance to push
                    // Push a node from our local stash.
                    TestNode* node_to_push = my_nodes.back();
                    my_nodes.pop_back();
                    stack.push(node_to_push);
                } else {
                    // Pop a node from the shared stack.
                    TestNode* popped_node = stack.pop();
                    if (popped_node) {
                        my_nodes.push_back(popped_node);
                    }
                }
            }
        });
    }

    // --- Wait for all threads to complete ---
    for (auto& t : threads) {
        t.join();
    }

    // --- Final Verification ---
    // At this point, every node should either be on the stack or in one of the
    // thread-local vectors. The total count must be exactly TOTAL_NODES, with no duplicates.

    std::set<int> final_node_ids;
    size_t final_node_count = 0;

    // 1. Collect nodes remaining in the thread-local vectors.
    for (const auto& local_nodes : thread_local_nodes) {
        for (const auto* node : local_nodes) {
            final_node_ids.insert(node->id);
            final_node_count++;
        }
    }

    // 2. Drain any nodes remaining on the shared stack.
    while (TestNode* node = stack.pop()) {
        final_node_ids.insert(node->id);
        final_node_count++;
    }

    // 3. The total number of nodes found must exactly match the original number.
    //    This checks for node loss.
    ASSERT_EQ(final_node_count, TOTAL_NODES) << "The total number of nodes in the system has changed. Nodes were lost.";

    // 4. The number of unique node IDs found must also match the total number.
    //    This checks for node duplication (the same node ending up in two places).
    ASSERT_EQ(final_node_ids.size(), TOTAL_NODES) << "Duplicate nodes were found in the system.";
}