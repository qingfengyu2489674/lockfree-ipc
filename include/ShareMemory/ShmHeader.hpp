// ShmHeader.hpp
#pragma once
#include <cstdint>
#include <cstddef>
#include <atomic>

enum class ShmState : std::uint8_t {
    kUninit       = 0,
    kConstructing = 1,   // 以后要做跨进程 once 时会用到
    kReady        = 2,
    kFailed       = 3,
};

struct alignas(64) ShmHeader {
    std::uint32_t magic = 0x43484541;
    std::uint32_t version = 1;
    std::atomic<ShmState> state{ShmState::kUninit};
    std::uint64_t heap_off = 0;
    std::uint64_t data_off = 0;
    std::size_t   region_bytes = 0;
};
static_assert(std::is_trivially_copyable_v<ShmState>, "enum must be trivial");
