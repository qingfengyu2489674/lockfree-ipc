// SizeClassConfig.cpp
#include "gc_malloc/ThreadHeap/SizeClassConfig.hpp"

#include <cstddef>
#include <cstdint>
#include <cassert>

namespace {

// 编译期静态常量表：规则化块尺寸（单位：字节）
// 约束：最小 32B，全部 16B 对齐；向上取整映射。
// 增长策略：小尺寸更细粒度，尺寸越大步长越大，直到 1 MiB。
constexpr std::size_t kClassSizeTable[] = {
    // 32..256（细粒度）
    32, 48, 64, 80, 96, 112, 128, 160, 192, 224, 256,
    // 320..1024
    320, 384, 448, 512, 640, 768, 896, 1024,
    // 1280..4096
    1280, 1536, 1792, 2048, 2560, 3072, 3584, 4096,
    // 5120..8192
    5120, 6144, 7168, 8192,
    // 10240..32768
    10240, 12288, 14336, 16384, 20480, 24576, 28672, 32768,
    // 40960..65536
    40960, 49152, 57344, 65536,
    // 81920..131072
    81920, 98304, 114688, 131072,
    // 163840..262144
    163840, 196608, 229376, 262144,
    // 327680..524288
    327680, 393216, 458752, 524288,
    // 655360..1048576 (1 MiB)
    655360, 786432, 917504, 1048576
};

// 本地计算的表项数
constexpr std::size_t kLocalClassCount =
    sizeof(kClassSizeTable) / sizeof(kClassSizeTable[0]);

// 与头文件里的编译期常量强一致校验（防止忘改其中一处）
static_assert(SizeClassConfig::kClassCount == kLocalClassCount,
              "kClassSizeTable count must match SizeClassConfig::kClassCount");

// 运行时二分查找：返回第一个 >= n 的索引（若超出则返回最后一个索引）
inline std::size_t UpperIndexCeil(std::size_t n) noexcept {
    std::size_t lo = 0, hi = kLocalClassCount;
    while (lo < hi) {
        std::size_t mid = lo + ((hi - lo) >> 1);
        if (kClassSizeTable[mid] < n) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    // lo 为第一个 >= n 的位置；若 n 大于表最大值，则 lo==kLocalClassCount，回退到最后一个
    return (lo < kLocalClassCount) ? lo : (kLocalClassCount - 1);
}

static_assert(kClassSizeTable[0] == SizeClassConfig::kMinAlloc,
              "First class must be 32 bytes.");
static_assert((kClassSizeTable[kLocalClassCount - 1] % SizeClassConfig::kAlignment) == 0,
              "Alignment must match.");
static_assert(kClassSizeTable[kLocalClassCount - 1] == SizeClassConfig::kMaxSmallAlloc,
              "Last class should be 1 MiB to match kMaxSmallAlloc.");

} // namespace

// ---- 接口实现 ----

// 注意：不再实现 SizeClassConfig::ClassCount() （它在 .hpp 里是 constexpr）

std::size_t SizeClassConfig::ClassToSize(std::size_t class_idx) noexcept {
    assert(class_idx < kLocalClassCount && "class_idx out of range");
    if (class_idx >= kLocalClassCount) return 0;
    return kClassSizeTable[class_idx];
}

std::size_t SizeClassConfig::SizeToClass(std::size_t nbytes) noexcept {
    // 低于最小值按最小值处理
    if (nbytes <= kMinAlloc) return 0;

    // 超过小对象上限则映射到最后一个 size-class（上层通常会走大对象路径）
    if (nbytes > kMaxSmallAlloc) return kLocalClassCount - 1;

    // 在表中找到第一个 >= nbytes 的 class
    return UpperIndexCeil(nbytes);
}

std::size_t SizeClassConfig::Normalize(std::size_t nbytes) noexcept {
    return ClassToSize(SizeToClass(nbytes));
}
