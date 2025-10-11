#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <atomic>
#include <immintrin.h> // 可选：_mm_pause()

template <class T, std::size_t Capacity>
class LockFreeBoundedStack {
    static_assert(Capacity > 0, "Capacity must be > 0");
    static_assert(std::is_trivially_copyable<T>::value, "T must be trivially copyable");
    static_assert(std::is_trivially_destructible<T>::value, "T must be trivially destructible");

public:
    using value_type = T;
    // 对外常量保持不变
    static constexpr std::uint32_t KInvalid = 0xFFFF'FFFFu; // 空栈标记

public:
    LockFreeBoundedStack() noexcept;
    ~LockFreeBoundedStack() = default;

    LockFreeBoundedStack(const LockFreeBoundedStack&) = delete;
    LockFreeBoundedStack& operator=(const LockFreeBoundedStack&) = delete;

    // 接口保持不变：允许失败（满/竞争）
    bool tryPush(const value_type& v) noexcept;
    bool tryPop(value_type& out) noexcept;

private:
    // 头部：低32位 index（KInvalid 表示空），高32位 tag（每次成功+1）
    alignas(64) std::atomic<std::uint64_t> top_bits_;

    // 槽位改为原子，使用 EMPTY 哨兵值避免并发写竞争与“重复弹出”
    alignas(64) std::atomic<value_type>     storage_[Capacity];

    // EMPTY 哨兵值：与 KInvalid 对齐（请保证 push 的值 != EMPTY）
    static constexpr value_type EMPTY = static_cast<value_type>(KInvalid);

    // —— 以下在 .cpp 实现 —— //
    static constexpr std::uint64_t pack(std::uint32_t idx, std::uint32_t tag) noexcept;
    static constexpr std::uint32_t unpackIdx(std::uint64_t bits) noexcept;
    static constexpr std::uint32_t unpackTag(std::uint64_t bits) noexcept;

    std::uint64_t loadTopRelaxed() const noexcept;
    bool          casTop(std::uint64_t expected, std::uint64_t desired) noexcept;

    // 仍保留这些名称（便于与你既有代码对齐）；内部基于原子槽位实现
    void          storeAt(std::uint32_t idx, const value_type& v) noexcept;
    value_type    loadAt(std::uint32_t idx) const noexcept;
};

// ====== 工具：打包/拆包 top ======
template <class T, std::size_t Capacity>
constexpr std::uint64_t
LockFreeBoundedStack<T, Capacity>::pack(std::uint32_t idx, std::uint32_t tag) noexcept {
    return (std::uint64_t(tag) << 32) | std::uint64_t(idx);
}

template <class T, std::size_t Capacity>
constexpr std::uint32_t
LockFreeBoundedStack<T, Capacity>::unpackIdx(std::uint64_t bits) noexcept {
    return static_cast<std::uint32_t>(bits & 0xFFFF'FFFFull);
}

template <class T, std::size_t Capacity>
constexpr std::uint32_t
LockFreeBoundedStack<T, Capacity>::unpackTag(std::uint64_t bits) noexcept {
    return static_cast<std::uint32_t>(bits >> 32);
}

// ====== 构造 ======
template <class T, std::size_t Capacity>
LockFreeBoundedStack<T, Capacity>::LockFreeBoundedStack() noexcept
    : top_bits_(pack(KInvalid, 0)) {
    for (std::size_t i = 0; i < Capacity; ++i) {
        storage_[i].store(EMPTY, std::memory_order_relaxed);
    }
}

// ====== 顶部原子操作 ======
template <class T, std::size_t Capacity>
std::uint64_t
LockFreeBoundedStack<T, Capacity>::loadTopRelaxed() const noexcept {
    return top_bits_.load(std::memory_order_relaxed);
}

template <class T, std::size_t Capacity>
bool
LockFreeBoundedStack<T, Capacity>::casTop(std::uint64_t expected, std::uint64_t desired) noexcept {
    std::uint64_t exp = expected; // compare_exchange_* 可能修改 expected
    return top_bits_.compare_exchange_strong(
        exp, desired,
        std::memory_order_acq_rel,   // 成功：发布+获取
        std::memory_order_relaxed    // 失败：放松
    );
}

// ====== 槽位读写（发布语义折叠进槽位值） ======
template <class T, std::size_t Capacity>
void
LockFreeBoundedStack<T, Capacity>::storeAt(std::uint32_t idx, const value_type& v) noexcept {
    // 占坑成功的线程发布数据：store(release)
    storage_[idx].store(v, std::memory_order_release);
}

template <class T, std::size_t Capacity>
typename LockFreeBoundedStack<T, Capacity>::value_type
LockFreeBoundedStack<T, Capacity>::loadAt(std::uint32_t idx) const noexcept {
    // 仅用于调试/单线程；真正消费在 tryPop 里处理
    return storage_[idx].load(std::memory_order_acquire);
}

// ====== push：占坑 → store(release) 值 ======
template <class T, std::size_t Capacity>
bool
LockFreeBoundedStack<T, Capacity>::tryPush(const value_type& v) noexcept {
    if (v == EMPTY) return false; // 避免与 EMPTY 冲突

    const std::uint64_t old = loadTopRelaxed();
    const std::uint32_t idx = unpackIdx(old);
    const std::uint32_t tag = unpackTag(old);

    std::uint32_t next_idx;
    if (idx == KInvalid) {
        next_idx = 0;
    } else {
        if (idx == static_cast<std::uint32_t>(Capacity - 1)) {
            return false; // 满
        }
        next_idx = idx + 1;
    }

    // 先占坑：推进 top
    const std::uint64_t desired = pack(next_idx, tag + 1);
    if (!casTop(old, desired)) {
        return false; // 竞争失败
    }

    // 只有占坑者写槽位，发布值
    storage_[next_idx].store(v, std::memory_order_release);
    return true;
}

// ====== pop：占有 idx → 等槽位非 EMPTY → exchange(EMPTY) 取走 ======
template <class T, std::size_t Capacity>
bool
LockFreeBoundedStack<T, Capacity>::tryPop(value_type& out) noexcept {
    const std::uint64_t old = loadTopRelaxed();
    const std::uint32_t idx = unpackIdx(old);
    const std::uint32_t tag = unpackTag(old);

    if (idx == KInvalid) {
        return false; // 空
    }

    const std::uint32_t next_idx = (idx == 0) ? KInvalid : (idx - 1);
    const std::uint64_t desired  = pack(next_idx, tag + 1);

    if (!casTop(old, desired)) {
        return false; // 竞争失败
    }

    // 等待发布：看到非 EMPTY（acquire）再取走
    value_type v = storage_[idx].load(std::memory_order_acquire);
    while (v == EMPTY) {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386) || defined(_M_IX86)
        _mm_pause();
#endif
        v = storage_[idx].load(std::memory_order_acquire);
    }

    // 取走并清空（防止重复消费）
    out = storage_[idx].exchange(EMPTY, std::memory_order_acq_rel);
    return out != EMPTY; // 理论上恒真；防御性返回
}

// ====== 显式实例化（若使用分离编译，请根据实际用到的组合添加） ======
// 例：
// template class LockFreeBoundedStack<std::uint32_t, 32>;
// template class LockFreeBoundedStack<std::uint32_t, 4096>;
// template class LockFreeBoundedStack<std::uint32_t, 65536>;
