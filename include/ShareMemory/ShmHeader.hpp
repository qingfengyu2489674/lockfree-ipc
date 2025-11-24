#pragma once
#include <cstdint>
#include <atomic>
#include <type_traits>

// 共享内存的生命周期状态
enum class ShmState : std::uint8_t {
    kUninit       = 0,
    kInitializing = 1,
    kReady        = 2,
};

// 物理布局结构体
struct alignas(64) ShmHeader {
    static constexpr std::uint32_t kMagic = 0x43484541;

    std::uint32_t magic;         // 4
    std::uint32_t version;       // 4
    
    std::atomic<ShmState> state;     // 1
    std::atomic<ShmState> app_state; // 1
    
    // 填充至 16 字节边界，方便后面放 uint64
    // 当前使用了 10 字节，需要填充 6 字节
    std::uint8_t reserved[6];    
    
    std::uint64_t heap_offset;   // 8 (Offset 16)
    std::uint64_t total_size;    // 8 (Offset 24)

    // 剩余填充：64 - 32 = 32
    std::uint8_t padding[32];   
};


// 编译期检查
static_assert(std::is_trivially_copyable_v<ShmHeader>, "ShmHeader must be POD");
static_assert(sizeof(ShmHeader) == 64, "ShmHeader size must be 64 bytes");