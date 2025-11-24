#pragma once
#include <cstdint>
#include <type_traits>

enum class MemoryOrder {
    Relaxed,
    Acquire,
    Release,
    AcqRel,
    SeqCst
};


template <typename T>
class Atomic {
    static_assert(sizeof(T) == 4 || sizeof(T) == 8, "Atomic<T> only supprots 4 or 8 byte types");

private:
    alignas(sizeof(T)) volatile T data;

public:
    Atomic() noexcept = default;
    constexpr Atomic(T val) noexcept : data(val) {}

    Atomic(const Atomic&) = delete;
    Atomic& operator=(const Atomic&) = delete;

    T load(MemoryOrder order = MemoryOrder::SeqCst) const noexcept {
        T v;
        asm volatile (
            "mov %1, %0"
            : "=r" (v)
            : "m" (data)
            : "memory"
        );

        return v;
    }

    void store(T val, MemoryOrder order = MemoryOrder::SeqCst) noexcept {
        if (order == MemoryOrder::SeqCst) {
            asm volatile (
                "mov %1, %0 \n\t"
                "mfence"
                : "=m" (data)
                : "r" (val)
                : "memory"
            );
        } else {
            asm volatile (
                "mov %1, %0"
                : "=m" (data)
                : "r" (val)
                : "memory"
            );
        }
    }

    bool compare_exchange_strong(T& expected, T desired,
                                 MemoryOrder success_order,
                                 MemoryOrder failure_order) noexcept {
        bool success;
        T prev = expected;

        asm volatile (
            "lock cmpxchg %3, %1"
            : "=@ccz" (success),
              "+m" (data),
              "+a" (prev)
            
            : "q" (desired)

            : "memory"
        );

        if(!success) {
            expected = prev;
        }

        return success;
    }

    
    bool compare_exchange_weak(T& expected, T desired,
                                 MemoryOrder success_order,
                                 MemoryOrder failure_order) noexcept {
        return compare_exchange_strong(expected, desired, success_order, failure_order);
    }
};

static inline void atomic_thread_fence(MemoryOrder order) {
    if (order == MemoryOrder::SeqCst) {
        // 硬件全屏障：这也是最昂贵的操作
        asm volatile("mfence" ::: "memory");
    } else {
        // 其他所有情况，在 x86 上只需要阻止编译器乱序即可
        asm volatile("" ::: "memory");
    }
}