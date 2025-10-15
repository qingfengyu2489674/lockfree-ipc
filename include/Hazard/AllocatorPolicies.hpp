#pragma once
#include "gc_malloc/ThreadHeap/ThreadHeap.hpp"
#include <new> // For placement new

// 默认策略：使用你现有的 ThreadHeap
struct DefaultHeapPolicy {
    template <class T, class... Args>
    static T* allocate(Args&&... args) {
        void* mem = ThreadHeap::allocate(sizeof(T));
        return ::new (mem) T(std::forward<Args>(args)...);
    }

    template <class T>
    static void deallocate(T* p) noexcept {
        if (p) {
            p->~T();
            ThreadHeap::deallocate(p);
        }
    }
};

// 示例：未来你可能想使用标准的 new/delete
struct StandardAllocPolicy {
    template <class T, class... Args>
    static T* allocate(Args&&... args) {
        return new T(std::forward<Args>(args)...);
    }

    template <class T>
    static void deallocate(T* p) noexcept {
        delete p;
    }
};