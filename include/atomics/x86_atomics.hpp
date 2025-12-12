#pragma once
#include <cstdint>
#include <type_traits>

// 引入标准原子库 (仅用于 TSan 模式或非 x86 环境兜底)
#include <atomic>

// 内存序定义
enum class MemoryOrder {
    Relaxed,
    Acquire,
    Release,
    AcqRel,
    SeqCst
};

// 辅助函数：将自定义枚举转换为标准库枚举 (仅 TSan 模式使用)
static inline std::memory_order to_std_order(MemoryOrder order) {
    switch (order) {
        case MemoryOrder::Relaxed: return std::memory_order_relaxed;
        case MemoryOrder::Acquire: return std::memory_order_acquire;
        case MemoryOrder::Release: return std::memory_order_release;
        case MemoryOrder::AcqRel:  return std::memory_order_acq_rel;
        case MemoryOrder::SeqCst:  return std::memory_order_seq_cst;
    }
    return std::memory_order_seq_cst;
}

template <typename T>
class Atomic {
    static_assert(sizeof(T) == 4 || sizeof(T) == 8, "Atomic<T> only supports 4 or 8 byte types");

private:
/* 
 * ============================================================================
 * [模式 A] TSan / 调试模式
 * 条件：开启了 ThreadSanitizer 或定义了 LOCKFREE_RUNNING_ON_TSAN 宏
 * 作用：使用 std::atomic 代理，让 TSan 能够正确追踪 happens-before 关系。
 *       如果这里用汇编，TSan 会报 "Read of size 8" 的假阳性错误。
 * ============================================================================
 */
#if defined(__SANITIZE_THREAD__) || defined(LOCKFREE_RUNNING_ON_TSAN)
    // 调试信息，确认当前走的是 std::atomic 路径
    // #pragma message ">>> [Atomic] TSan Detected: Switching to std::atomic backend <<<"
    
    std::atomic<T> data;

public:
    Atomic() noexcept = default;
    constexpr Atomic(T val) noexcept : data(val) {}
    Atomic(const Atomic&) = delete;
    Atomic& operator=(const Atomic&) = delete;

    T load(MemoryOrder order = MemoryOrder::SeqCst) const noexcept {
        return data.load(to_std_order(order));
    }

    void store(T val, MemoryOrder order = MemoryOrder::SeqCst) noexcept {
        data.store(val, to_std_order(order));
    }

    bool compare_exchange_strong(T& expected, T desired,
                                 MemoryOrder success_order,
                                 MemoryOrder failure_order) noexcept {
        return data.compare_exchange_strong(expected, desired, 
                                            to_std_order(success_order), 
                                            to_std_order(failure_order));
    }

    bool compare_exchange_weak(T& expected, T desired,
                               MemoryOrder success_order,
                               MemoryOrder failure_order) noexcept {
        return data.compare_exchange_weak(expected, desired, 
                                          to_std_order(success_order), 
                                          to_std_order(failure_order));
    }

/* 
 * ============================================================================
 * [模式 B] 生产 / 高性能模式 (Original Assembly Implementation)
 * 条件：未开启 TSan
 * 作用：使用手写的 x86 内联汇编，保留作者原始的高性能优化成果。
 * ============================================================================
 */
#else 
    alignas(sizeof(T)) volatile T data;

public:
    Atomic() noexcept = default;
    constexpr Atomic(T val) noexcept : data(val) {}
    Atomic(const Atomic&) = delete;
    Atomic& operator=(const Atomic&) = delete;

    T load(MemoryOrder order = MemoryOrder::SeqCst) const noexcept {
        T v;
        // x86 TSO 模型下，普通的 mov + 编译器屏障即可满足 Acquire 语义
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
            // SeqCst 写需要 mfence 防止 StoreBuffer 导致重排
            asm volatile (
                "mov %1, %0 \n\t"
                "mfence"
                : "=m" (data)
                : "r" (val)
                : "memory"
            );
        } else {
            // Release 写在 x86 上也是普通的 mov + 编译器屏障
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
        // lock cmpxchg 自身带有 Full Barrier 语义
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
        // x86 上没有 LL/SC，所以 weak 和 strong 实现一致
        return compare_exchange_strong(expected, desired, success_order, failure_order);
    }
#endif
};

// 内存屏障同样做条件编译处理
static inline void atomic_thread_fence(MemoryOrder order) {
#if defined(__SANITIZE_THREAD__) || defined(LOCKFREE_RUNNING_ON_TSAN)
    std::atomic_thread_fence(to_std_order(order));
#else
    if (order == MemoryOrder::SeqCst) {
        asm volatile("mfence" ::: "memory");
    } else {
        asm volatile("" ::: "memory");
    }
#endif
}