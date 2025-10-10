#pragma once
#include <pthread.h>
#include <cstddef>
#include <cstdint>

/**
 * @brief 共享内存可用的互斥封装（可与 std::lock_guard / std::unique_lock 搭配）
 * - 跨进程：PTHREAD_PROCESS_SHARED
 * - 鲁棒：   PTHREAD_MUTEX_ROBUST（owner 崩溃可一致性恢复）
 * - 接口：   BasicLockable/Lockable（lock / unlock / try_lock）
 * - 约定：   仅“创建者进程”在共享内存上 placement-new 构造；附着进程只使用，不析构
 */
class ShmMutexLock {
public:
    ShmMutexLock();                  // .cpp: init PSHARED + ROBUST
    ~ShmMutexLock();                 // .cpp: pthread_mutex_destroy

    ShmMutexLock(const ShmMutexLock&) = delete;
    ShmMutexLock& operator=(const ShmMutexLock&) = delete;
    ShmMutexLock(ShmMutexLock&&) = delete;
    ShmMutexLock& operator=(ShmMutexLock&&) = delete;

    // ===== BasicLockable / Lockable =====
    void lock() const;               // 内部处理 EOWNERDEAD -> pthread_mutex_consistent
    bool try_lock() const noexcept;
    void unlock() const noexcept;

private:
    // 允许在 const 成员函数中加锁（逻辑常量性）
    alignas(64) mutable pthread_mutex_t mtx_{};
}

/*
用法：
#include <mutex>
ShmMutexLock mu;

void f() {
    std::lock_guard<ShmMutexLock> lk(mu); // 构造->lock，析构->unlock
}

bool g() {
    std::unique_lock<ShmMutexLock> lk(mu, std::try_to_lock);
    if (!lk.owns_lock()) return false;
    // ...
    return true;
}
*/
;
