#pragma once

#include <cstddef> // For size_t
#include <new>     // For placement new

// 包含了这个分配器唯一依赖的底层内存管理器
#include "gc_malloc/ThreadHeap/ThreadHeap.hpp"

// ====================================================================
//    一个专用的、符合C++标准的、直接使用ThreadHeap的分配器
// ====================================================================

template <typename T>
class ThreadHeapAllocator {
public:
    using value_type = T;

    ThreadHeapAllocator() noexcept = default;

    template <typename U>
    ThreadHeapAllocator(const ThreadHeapAllocator<U>&) noexcept {}

    // 只分配原始内存，不构造对象
    T* allocate(size_t n) {
        // 请求分配 n 个 T 对象的空间
        // 直接调用 ThreadHeap 来分配 n * sizeof(T) 字节的原始内存
        return static_cast<T*>(ThreadHeap::allocate(n * sizeof(T)));
    }

    // 只释放原始内存，不析构对象
    void deallocate(T* p, size_t n) noexcept {
        // 容器通知我们归还 p 指向的、大小为 n 个 T 对象的空间
        // 直接调用 ThreadHeap 来释放这块内存
        (void)n; // 避免 "unused parameter" 警告
        ThreadHeap::deallocate(p);
    }
};

// --- 必需的样板代码：比较两个分配器是否相等 ---
// 因为所有 ThreadHeapAllocator 实例都共享同一个全局的 ThreadHeap，
// 所以它们总是相等的。
template <typename T1, typename T2>
bool operator==(const ThreadHeapAllocator<T1>&, const ThreadHeapAllocator<T2>&) noexcept {
    return true;
}

template <typename T1, typename T2>
bool operator!=(const ThreadHeapAllocator<T1>&, const ThreadHeapAllocator<T2>&) noexcept {
    return false;
}