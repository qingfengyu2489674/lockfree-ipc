#pragma once

#include <cstddef>
#include <cstdint>


class SizeClassConfig {
public:
    // ---- 编译期常量（策略相关）----
    static constexpr std::size_t kMinAlloc       = 32;                   // 最小请求按 32B 处理
    static constexpr std::size_t kAlignment      = 16;                   // 基本对齐
    static constexpr std::size_t kMaxSmallAlloc  = 1u * 1024u * 1024u;   // 小对象上限（> 则走大对象路径）
    static constexpr std::size_t kChunkSizeBytes = 2u * 1024u * 1024u;   // 与 CentralHeap 保持一致

    static constexpr std::size_t kClassCount = 59;

    static constexpr std::size_t ClassCount() noexcept { return kClassCount; }

public:

    // 将“请求字节数”映射为 size-class 下标（保证 0 <= idx < ClassCount()）
    static std::size_t SizeToClass(std::size_t nbytes) noexcept;

    // 将 size-class 下标映射回“规则化后的块尺寸”（与 .cpp 中表项一致）
    static std::size_t ClassToSize(std::size_t class_idx) noexcept;

    // 将任意请求尺寸规则化为实际分配尺寸（>= kMinAlloc，按 kAlignment 对齐）
    static std::size_t Normalize(std::size_t nbytes) noexcept;
};
