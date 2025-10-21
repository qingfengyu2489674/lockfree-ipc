#include "gtest/gtest.h"
#include "fixtures/ThreadHeapTestFixture.hpp"

// 1. Standard library includes for testing
#include <thread>
#include <vector>
#include <atomic>
#include <set>
#include <mutex> // Used for mocking ShmMutexLock in a single-process test


#include "EBRManager/ThreadSlot.hpp"
#include "EBRManager/ThreadHeapAllocator.hpp"
#include "EBRManager/ThreadSlotManager.hpp"

// ============================================================================
// Test Fixture Setup
// Inherits from ThreadHeapTestFixture as requested.
// ============================================================================
class ThreadSlotManagerTest : public ThreadHeapTestFixture {};

// ============================================================================
// Test Cases
// ============================================================================

// Test: Basic single-threaded functionality.
// Verifies that a thread gets a valid slot, and subsequent calls return the same slot.
TEST_F(ThreadSlotManagerTest, SingleThreadGetLocalSlot) {
    // Create the manager on the shared heap provided by the fixture.
    auto* manager = new (ThreadHeap::allocate(sizeof(ThreadSlotManager))) ThreadSlotManager();

    // First call should allocate a new slot.
    ThreadSlot* slot1 = manager->getLocalSlot();
    ASSERT_NE(slot1, nullptr);

    // Second call from the same thread should return the exact same slot.
    ThreadSlot* slot2 = manager->getLocalSlot();
    ASSERT_EQ(slot1, slot2);

    // Cleanup
    manager->~ThreadSlotManager();
    ThreadHeap::deallocate(manager);
}

// Test: forEachSlot functionality.
// Verifies that it correctly iterates over all allocated slots and that their
// initial state is correct according to the ThreadSlot class definition.
TEST_F(ThreadSlotManagerTest, ForEachSlotIteratesCorrectlyAndChecksInitialState) {
    auto* manager = new (ThreadHeap::allocate(sizeof(ThreadSlotManager))) ThreadSlotManager();

    // Trigger initial allocation by having one thread get a slot.
    ThreadSlot* first_slot = manager->getLocalSlot();
    ASSERT_NE(first_slot, nullptr);

    std::atomic<size_t> visited_count = 0;
    manager->forEachSlot([&](const ThreadSlot& slot) {
        visited_count++;
        // Now we can test the real ThreadSlot's state.
        // A newly constructed slot should be neither active nor registered.
        uint64_t initial_state = slot.loadState();
        EXPECT_FALSE(ThreadSlot::isActive(initial_state));
        EXPECT_FALSE(ThreadSlot::isRegistered(initial_state));
    });

    // The initial expansion creates kInitialCapacity slots.
    // kInitialCapacity is a private member, so we assume its value from the header (32).
    const size_t expected_initial_capacity = 32;
    ASSERT_EQ(visited_count.load(), expected_initial_capacity);

    // Cleanup
    manager->~ThreadSlotManager();
    ThreadHeap::deallocate(manager);
}

// Test: High-contention concurrency and expansion.
// This is the most critical test. It launches many threads to concurrently request slots,
// forcing the manager to expand its capacity multiple times under race conditions.
TEST_F(ThreadSlotManagerTest, ConcurrentGetLocalSlotReturnsUniqueSlotsAndExpands) {
    // We will use more threads than the initial capacity to force expansion.
    const int NUM_THREADS = 100;
    
    auto* manager = new (ThreadHeap::allocate(sizeof(ThreadSlotManager))) ThreadSlotManager();
    
    std::vector<std::thread> threads;
    std::vector<ThreadSlot*> results(NUM_THREADS, nullptr);

    // *** FIX: Add a barrier to synchronize threads ***
    // This ensures all threads acquire their slots BEFORE any thread exits.
    std::atomic<int> ready_barrier(0);

    // Launch all threads
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([&, i] {
            // Each thread gets its own local slot.
            results[i] = manager->getLocalSlot();

            // Signal that this thread has acquired its slot and is ready.
            ready_barrier.fetch_add(1, std::memory_order_release);

            // Wait until ALL threads have acquired their slots.
            // This prevents this thread from exiting and releasing its slot prematurely.
            while (ready_barrier.load(std::memory_order_acquire) < NUM_THREADS) {
                // Yield to prevent burning CPU in a tight loop.
                std::this_thread::yield();
            }
        });
    }

    // Wait for all threads to complete their synchronized work.
    for (auto& t : threads) {
        t.join();
    }

    // --- Verification ---
    // At this point, we are sure that at one moment in time, all 100 threads
    // held 100 different slots concurrently.

    // 1. Verify that every thread received a valid, non-null slot.
    for (int i = 0; i < NUM_THREADS; ++i) {
        ASSERT_NE(results[i], nullptr) << "Thread " << i << " received a null slot.";
    }

    // 2. Verify that every slot is unique. This is the most important check.
    std::set<ThreadSlot*> unique_slots(results.begin(), results.end());
    ASSERT_EQ(unique_slots.size(), NUM_THREADS) << "Duplicate slots were assigned to different threads.";

    // 3. Verify that the manager expanded correctly.
    std::atomic<size_t> final_slot_count = 0;
    manager->forEachSlot([&](const ThreadSlot&) {
        final_slot_count++;
    });
    // With 100 threads, capacity should have grown to 128 (32 + 32 + 64).
    ASSERT_EQ(final_slot_count.load(), 128);

    // Cleanup
    manager->~ThreadSlotManager();
    ThreadHeap::deallocate(manager);
}