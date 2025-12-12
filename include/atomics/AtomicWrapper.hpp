#pragma once

#ifdef RL_TEST
    #include "relacy/relacy.hpp"
    template<typename T> using AtomicType = rl::atomic<T>;
    using MemoryOrder = rl::memory_order;
    #define MO_RELAXED rl::memory_order_relaxed
    #define MO_ACQUIRE rl::memory_order_acquire
    #define MO_RELEASE rl::memory_order_release
    #define MO_SEQ_CST rl::memory_order_seq_cst
    #define MO_CAS_FUNC compare_exchange_weak
#else
    #include <atomic>
    template<typename T> using AtomicType = std::atomic<T>;
    using MemoryOrder = std::memory_order;
    #define MO_RELAXED std::memory_order_relaxed
    #define MO_ACQUIRE std::memory_order_acquire
    #define MO_RELEASE std::memory_order_release
    #define MO_SEQ_CST std::memory_order_seq_cst
    #define MO_CAS_FUNC compare_exchange_weak
#endif