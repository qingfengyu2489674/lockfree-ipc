#include <gtest/gtest.h>
#include <vector>
#include <atomic>

// ====================================================================
//                      Step 1: 包含所需头文件
// ====================================================================

#include "fixtures/ThreadHeapTestFixture.hpp"
#include "EBRManager/ThreadHeapAllocator.hpp" // **注意: 包含新的、简化的分配器**
#include "gc_malloc/ThreadHeap/ThreadHeap.hpp"

// ====================================================================
//      Step 2: 创建一个可监控的 "侦察兵" 分配器
// ====================================================================
// 这个分配器完全模仿 ThreadHeapAllocator 的行为，
// 但增加了静态计数器，以便我们可以在测试中观察其行为。
// 它直接调用 ThreadHeap 的 allocate 和 deallocate。

template <typename T>
class SpyThreadHeapAllocator {
public:
    using value_type = T;

    // 静态、原子计数器
    static inline std::atomic<size_t> allocation_count = 0;
    static inline std::atomic<size_t> deallocation_count = 0;

    // 在测试开始前重置计数器
    static void reset_counters() {
        allocation_count = 0;
        deallocation_count = 0;
    }

    // 构造函数
    SpyThreadHeapAllocator() noexcept = default;
    template <typename U>
    SpyThreadHeapAllocator(const SpyThreadHeapAllocator<U>&) noexcept {}

    // 分配函数：调用 ThreadHeap::allocate 并增加计数
    T* allocate(size_t n) {
        allocation_count++;
        return static_cast<T*>(ThreadHeap::allocate(n * sizeof(T)));
    }

    // 释放函数：调用 ThreadHeap::deallocate 并增加计数
    void deallocate(T* p, size_t n) noexcept {
        (void)n;
        deallocation_count++;
        ThreadHeap::deallocate(p);
    }
};

// 比较函数
template <typename T1, typename T2>
bool operator==(const SpyThreadHeapAllocator<T1>&, const SpyThreadHeapAllocator<T2>&) noexcept {
    return true;
}
template <typename T1, typename T2>
bool operator!=(const SpyThreadHeapAllocator<T1>&, const SpyThreadHeapAllocator<T2>&) noexcept {
    return false;
}


// ====================================================================
//      Step 3: 创建继承自您指定夹具的测试夹具
// ====================================================================
class AllocatorInjectionTest : public ThreadHeapTestFixture {
protected:
    // 在每个测试开始时，重置我们的侦察兵计数器
    void SetUp() override {
        ThreadHeapTestFixture::SetUp();
        SpyThreadHeapAllocator<void>::reset_counters(); // 用 void 特化来访问静态成员
    }
};

// ====================================================================
//                     Step 4: 编写更新后的测试用例
// ====================================================================

// --- 现有测试用例 (已更新) ---

TEST_F(AllocatorInjectionTest, VectorAllocation_InvokesCustomAllocator) {
    using VectorWithSpyAllocator = std::vector<int, SpyThreadHeapAllocator<int>>;

    {
        ASSERT_EQ(SpyThreadHeapAllocator<int>::allocation_count, 0);
        ASSERT_EQ(SpyThreadHeapAllocator<int>::deallocation_count, 0);
        
        VectorWithSpyAllocator vec;
        vec.push_back(100);
        EXPECT_EQ(SpyThreadHeapAllocator<int>::allocation_count, 1);
        EXPECT_EQ(SpyThreadHeapAllocator<int>::deallocation_count, 0);
        
        vec.resize(100); 
        EXPECT_EQ(SpyThreadHeapAllocator<int>::allocation_count, 2);
        EXPECT_EQ(SpyThreadHeapAllocator<int>::deallocation_count, 1);
    } 
    
    EXPECT_EQ(SpyThreadHeapAllocator<int>::allocation_count, 2);
    EXPECT_EQ(SpyThreadHeapAllocator<int>::deallocation_count, 2);
    EXPECT_EQ(SpyThreadHeapAllocator<int>::allocation_count, SpyThreadHeapAllocator<int>::deallocation_count);
}

TEST_F(AllocatorInjectionTest, Vector_CanBeConstructed_WithThreadHeapAllocator) {
    // 这个测试现在直接使用最终的 ThreadHeapAllocator
    using VectorOnThreadHeap = std::vector<int, ThreadHeapAllocator<int>>;
    
    ASSERT_NO_THROW({
        VectorOnThreadHeap vec;
        vec.push_back(1);
    });

    VectorOnThreadHeap vec;
    vec.push_back(42);
    ASSERT_EQ(vec.size(), 1);
    ASSERT_EQ(vec[0], 42);
}


// ====================================================================
//                     --- 新增测试用例 (已更新) ---
// ====================================================================

TEST_F(AllocatorInjectionTest, PushBack_TriggersReallocation_WhenCapacityIsExceeded) {
    using VectorWithSpyAllocator = std::vector<int, SpyThreadHeapAllocator<int>>;
    VectorWithSpyAllocator vec;

    vec.push_back(1);
    ASSERT_EQ(SpyThreadHeapAllocator<int>::allocation_count, 1);
    ASSERT_EQ(SpyThreadHeapAllocator<int>::deallocation_count, 0);

    const size_t initial_capacity = vec.capacity();
    ASSERT_GT(initial_capacity, 0);

    for (size_t i = 1; i < initial_capacity; ++i) {
        vec.push_back(i + 1);
    }
    ASSERT_EQ(SpyThreadHeapAllocator<int>::allocation_count, 1);
    ASSERT_EQ(vec.size(), vec.capacity());

    vec.push_back(initial_capacity + 1);

    EXPECT_EQ(SpyThreadHeapAllocator<int>::allocation_count, 2);
    EXPECT_EQ(SpyThreadHeapAllocator<int>::deallocation_count, 1);
    EXPECT_GT(vec.capacity(), initial_capacity);
}

TEST_F(AllocatorInjectionTest, Clear_DoesNotDeallocate_AndCapacityIsReused) {
    using VectorWithSpyAllocator = std::vector<int, SpyThreadHeapAllocator<int>>;
    VectorWithSpyAllocator vec;

    vec.resize(10);
    ASSERT_EQ(SpyThreadHeapAllocator<int>::allocation_count, 1);
    ASSERT_EQ(SpyThreadHeapAllocator<int>::deallocation_count, 0);

    const size_t capacity_before_clear = vec.capacity();
    const size_t allocs_before_clear = SpyThreadHeapAllocator<int>::allocation_count;
    ASSERT_GE(capacity_before_clear, 10);

    vec.clear();

    EXPECT_EQ(vec.size(), 0);
    EXPECT_EQ(vec.capacity(), capacity_before_clear);
    EXPECT_EQ(SpyThreadHeapAllocator<int>::allocation_count, allocs_before_clear);
    EXPECT_EQ(SpyThreadHeapAllocator<int>::deallocation_count, 0);

    for (size_t i = 0; i < capacity_before_clear; ++i) {
        vec.push_back(i);
    }

    EXPECT_EQ(SpyThreadHeapAllocator<int>::allocation_count, allocs_before_clear);
    EXPECT_EQ(SpyThreadHeapAllocator<int>::deallocation_count, 0);
    EXPECT_EQ(vec.size(), capacity_before_clear);
}