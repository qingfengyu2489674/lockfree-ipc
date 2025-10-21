#pragma once
#include <pthread.h>
#include <cstddef>
#include <cstdint>

class ShmMutexLock {
public:
    ShmMutexLock();
    ~ShmMutexLock();

    ShmMutexLock(const ShmMutexLock&) = delete;
    ShmMutexLock& operator=(const ShmMutexLock&) = delete;
    ShmMutexLock(ShmMutexLock&&) = delete;
    ShmMutexLock& operator=(ShmMutexLock&&) = delete;

    void lock() const;
    bool try_lock() const noexcept;
    void unlock() const noexcept;

private:
    alignas(64) mutable pthread_mutex_t mtx_{};
};
