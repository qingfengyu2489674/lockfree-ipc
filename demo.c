#pragma once
#include <cstddef>
#include <utility>
#include <new>
#include "atomic_intrinsics.hpp"      // 提供 load_acquire_ptr / store_release_ptr / cas_acq_rel_ptr
#include "LockFreeNode.hpp"
#include "LockFreeRetiredList.hpp"

// ============================
//      Minimal Hazard Pointer
// ============================

// ---- 配置 ----
constexpr std::size_t hp_per_thread  = 1;        // 每线程 HP 个数（Treiber 栈 pop 只需 1 个）
constexpr std::size_t hp_max_threads = 1024;     // 允许出现过的线程上限（不复用）
constexpr std::size_t hp_max_slots   = hp_per_thread * hp_max_threads;

// ---- 全局槽 & 线性分配游标 ----
inline void*       hp_slots[hp_max_slots] = {nullptr};
inline std::size_t next_hp_slot = 0;

// ---- 原子访问（带内存序）----
inline void hp_store(std::size_t idx, void* p) noexcept {
    __atomic_store_n(&hp_slots[idx], p, __ATOMIC_RELEASE);
}
inline void* hp_load(std::size_t idx) noexcept {
    return __atomic_load_n(&hp_slots[idx], __ATOMIC_ACQUIRE);
}

// 遍历全部槽（供回收器扫描）
template <class F>
inline void for_each_hp_slot(F&& f) {
    for (std::size_t i = 0; i < hp_max_slots; ++i) {
        f(i, hp_load(i));
    }
}

// ---- 线程本地注册（方案A：只增不还；析构仅清空自己槽）----
class HPThreadReg {
public:
    HPThreadReg()
        : hpFirstSlotIndex_(static_cast<std::size_t>(-1))
    {
        // 原子领取我这线程的一段连续槽 [b, b + hp_per_thread)
        std::size_t b = __atomic_fetch_add(&next_hp_slot, hp_per_thread, __ATOMIC_ACQ_REL);
        // 用减法写法避免无符号溢出
        if (b > hp_max_slots - hp_per_thread) {
            std::terminate(); // 也可改为抛异常或记录错误
        }
        hpFirstSlotIndex_ = b;

        // 初始化为 nullptr（release-store）
        for (std::size_t i = 0; i < hp_per_thread; ++i) {
            hp_store(hpFirstSlotIndex_ + i, nullptr);
        }
    }

    ~HPThreadReg() {
        if (hpFirstSlotIndex_ != static_cast<std::size_t>(-1)) {
            // 清空自己的槽，避免“僵尸 HP”阻塞回收
            for (std::size_t i = 0; i < hp_per_thread; ++i) {
                hp_store(hpFirstSlotIndex_ + i, nullptr);
            }
        }
    }

    HPThreadReg(const HPThreadReg&)            = delete;
    HPThreadReg& operator=(const HPThreadReg&) = delete;

    // 设置/清空本线程的第 i 个 HP
    inline void set(void* p, std::size_t i = 0) noexcept {
        hp_store(hpFirstSlotIndex_ + i, p);
    }
    inline void clear(std::size_t i = 0) noexcept {
        hp_store(hpFirstSlotIndex_ + i, nullptr);
    }

    // 辅助：清本线程全部 HP；读取首槽下标
    inline void resetAll() noexcept {
        for (std::size_t i = 0; i < hp_per_thread; ++i) {
            hp_store(hpFirstSlotIndex_ + i, nullptr);
        }
    }
    inline std::size_t firstSlotIndex() const noexcept { return hpFirstSlotIndex_; }

private:
    // 本线程在全局数组中的首槽下标
    std::size_t hpFirstSlotIndex__;
};

// 线程本地上下文（首次用到时构造）
inline thread_local HPThreadReg tls_hp;

// 回收器工具：判断某指针当前是否被任意 HP 保护
inline bool any_protects(void* target) {
    if (!target) return false;
    for (std::size_t i = 0; i < hp_max_slots; ++i) {
        void* p = hp_load(i);              // acquire-load
        if (p && p == target) return true;
    }
    return false;
}


// ============================
//          LockFreeStack
// ============================

template <typename T>
class LockFreeStack {
public:
    LockFreeStack();
    LockFreeStack(const LockFreeStack&)            = delete;
    LockFreeStack& operator=(const LockFreeStack&) = delete;
    ~LockFreeStack();

    void push(const T& v);
    void push(T&& v);

    // 出栈：成功写入 out 返回 true；空则 false
    bool pop(T& out);

    bool empty() const noexcept;

private:
    // 私有实现：用 CAS 把新节点安装为 top（线性化点）
    void pushImpl_(LockFreeNode<T>* n) noexcept;

private:
    alignas(64) LockFreeNode<T>*         top_{nullptr};          // 栈顶指针
    alignas(64) LockFreeRetiredList<T>*  retiredList_{nullptr};  // 退休链（延迟回收）
};


// --------- 实现 ---------

template <typename T>
LockFreeStack<T>::LockFreeStack()
    : top_(nullptr),
      retiredList_(new LockFreeRetiredList<T>())
{}

template <typename T>
LockFreeStack<T>::~LockFreeStack() {
    // 1) 清空工作栈（假设已无并发访问）
    LockFreeNode<T>* p = load_acquire_ptr(&top_);
    while (p) {
        LockFreeNode<T>* q = p->next;
        delete p;
        p = q;
    }
    store_release_ptr(&top_, nullptr);

    // 2) 清空退休链
    if (retiredList_) {
        retiredList_->clear();
        delete retiredList_;
        retiredList_ = nullptr;
    }
}

template <typename T>
void LockFreeStack<T>::push(const T& v) {
    auto* n = new LockFreeNode<T>(v);
    pushImpl_(n);
}

template <typename T>
void LockFreeStack<T>::push(T&& v) {
    auto* n = new LockFreeNode<T>(std::move(v));
    pushImpl_(n);
}

// Treiber push：不需要 HP
template <typename T>
void LockFreeStack<T>::pushImpl_(LockFreeNode<T>* n) noexcept {
    LockFreeNode<T>* old = load_acquire_ptr(&top_);
    do {
        n->next = old;
        // 自旋直到 CAS 成功
    } while (!cas_acq_rel_ptr(&top_, old, n));
}

template <typename T>
bool LockFreeStack<T>::empty() const noexcept {
    return load_acquire_ptr(&top_) == nullptr;
}

// Treiber pop（带 HP 保护）
template <typename T>
bool LockFreeStack<T>::pop(T& out) {
    while (true) {
        // 1) 快照 top
        LockFreeNode<T>* old = load_acquire_ptr(&top_);
        if (!old) return false; // 空

        // 2) 设置 HP 保护 old（release-store）
        tls_hp.set(static_cast<void*>(old));

        // 3) 确认 top 未变（把 HP 与当前拓扑“对齐”）
        if (load_acquire_ptr(&top_) != old) {
            tls_hp.clear();
            continue; // 竞争失败，重试
        }

        // 4) 现在可以安全解引用 old->next（old 不会被回收/复用）
        LockFreeNode<T>* next = old->next;

        // 5) CAS 试图弹出
        if (cas_acq_rel_ptr(&top_, old, next)) {
            // 线性化点：成功出栈
            out = std::move(old->value);

            // 6) 清 HP（从此刻开始，回收器可能会释放 old）
            tls_hp.clear();

            // 7) 放入退休链，延迟回收（回收器内部用 any_protects 扫描 hp_slots）
            retiredList_->push(old);

            // （可选）也可在此主动触发一次扫描：
            // retiredList_->tryScanAndReclaim([](void* p){ return any_protects(p); });

            return true;
        }

        // CAS 失败，清 HP，重试
        tls_hp.clear();
        // 可在此放置 pause/backoff
    }
}
