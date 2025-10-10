// lf_atomic_min.hpp
#pragma once
#include <cstdint>
#include <type_traits>

// ------------ 指针原子操作（固定语义） ------------
// 语义：load_acquire / store_release / cas_acq_rel

#include <atomic>
#include <cstddef>
#include <type_traits>

// ---- load ----
template<class T>
static inline T* load_acquire_ptr(T* volatile* p) noexcept {
    std::atomic_thread_fence(std::memory_order_acquire);
    return *p;
}

template<class T>
static inline T* load_acquire_ptr(T* const volatile* p) noexcept { // 允许 const
    std::atomic_thread_fence(std::memory_order_acquire);
    return *p;
}

// ---- store ----
template<class T>
static inline void store_release_ptr(T* volatile* p, T* v) noexcept {
    std::atomic_thread_fence(std::memory_order_release);
    *p = v;
}

template<class T>
static inline void store_release_ptr(T* const volatile* p, T* v) noexcept { // 允许 const
    std::atomic_thread_fence(std::memory_order_release);
    *const_cast<T* volatile*>(p) = v;
}

// 专门给 nullptr 的重载，避免模板推导失败
template<class T>
static inline void store_release_ptr(T* volatile* p, std::nullptr_t) noexcept {
    std::atomic_thread_fence(std::memory_order_release);
    *p = nullptr;
}

// 返回是否成功；失败会把 expected 刷新为当前值
template <class T>
static inline bool cas_acq_rel_ptr(T* volatile* p, T*& expected, T* desired) noexcept {
    return __atomic_compare_exchange_n(
        p, &expected, desired,
        /*weak=*/true,
        __ATOMIC_ACQ_REL,   // 成功：获取+释放
        __ATOMIC_ACQUIRE    // 失败：获取
    );
}

// ------------ 整数原子操作（常用的那几个） ------------
// 语义：load_acquire / store_release / cas_acq_rel / fetch_add_acq_rel

template <class I,
          class = typename std::enable_if<std::is_integral<I>::value || std::is_enum<I>::value>::type>
static inline I load_acquire(I const volatile* p) noexcept {
    return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}

template <class I,
          class = typename std::enable_if<std::is_integral<I>::value || std::is_enum<I>::value>::type>
static inline void store_release(I volatile* p, I v) noexcept {
    __atomic_store_n(p, v, __ATOMIC_RELEASE);
}

template <class I,
          class = typename std::enable_if<std::is_integral<I>::value || std::is_enum<I>::value>::type>
static inline bool cas_acq_rel(I volatile* p, I& expected, I desired) noexcept {
    return __atomic_compare_exchange_n(
        p, &expected, desired,
        /*weak=*/true,
        __ATOMIC_ACQ_REL,
        __ATOMIC_ACQUIRE
    );
}

template <class I,
          class = typename std::enable_if<std::is_integral<I>::value>::type>
static inline I fetch_add_acq_rel(I volatile* p, I delta) noexcept {
    return __atomic_fetch_add(p, delta, __ATOMIC_ACQ_REL);
}

