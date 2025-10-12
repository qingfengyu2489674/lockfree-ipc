#include "Tool/ShmMutexLock.hpp"

#include <pthread.h>
#include <cerrno>
#include <system_error>
#include <cstring>

// 小工具：将 pthread 返回码转成 C++ 异常（仅在 lock() 中使用）
static void throw_system_error(int ec, const char* what) {
    // 注意：pthread 函数返回的是“错误码”而不是设置 errno
    throw std::system_error(std::error_code(ec, std::generic_category()), what);
}

ShmMutexLock::ShmMutexLock() {
    pthread_mutexattr_t attr{};
    int rc = pthread_mutexattr_init(&attr);
    if (rc != 0){
        throw_system_error(rc, "pthread_mutexattr_init failed");
    }

    
    rc = pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    if(rc != 0) {
        pthread_mutexattr_destroy(&attr);
        throw_system_error(rc, "pthread_mutexattr_setpshared(PTHREAD_PROCESS_SHARED) failed");
    }

    rc = pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
    if (rc != 0) {
        pthread_mutexattr_destroy(&attr);
        throw_system_error(rc, "pthread_mutexattr_setrobust(PTHREAD_MUTEX_ROBUST) failed");
    }
        
    rc = pthread_mutex_init(&mtx_, &attr);
    pthread_mutexattr_destroy(&attr);

    if(rc != 0) 
        throw_system_error(rc, "pthread_mutex_init failed");
}

ShmMutexLock::~ShmMutexLock() {
    pthread_mutex_destroy(&mtx_);
}

void ShmMutexLock::lock() const {
    int rc = pthread_mutex_lock(&mtx_);
    if(rc == 0) return;

    if(rc == EOWNERDEAD) {
        int rc2 = pthread_mutex_consistent(&mtx_);
        if(rc2 != 0) {
            throw_system_error(rc2, "pthread_mutex_consistent failed");
        }
        return;
    }

    if(rc == ENOTRECOVERABLE) {
        throw_system_error(rc, "pthread_mutex_lock: mutex is not recoverable");
    }

    // 其他错误（EINVAL、EAGAIN 等）
    throw_system_error(rc, "pthread_mutex_lock failed");
}

bool ShmMutexLock::try_lock() const noexcept {
    int rc = pthread_mutex_trylock(&mtx_);
    if (rc == 0) return true;

    if (rc == EBUSY) return false; // 正常的忙碌情况

    if (rc == EOWNERDEAD) {
        // 与 lock() 逻辑一致：做一致化并视为成功
        if (pthread_mutex_consistent(&mtx_) == 0) return true;
        // 一致化失败则把它当作失败处理（不抛异常，因为 noexcept）
        // 调用者可以选择再用 lock() 获取详细错误
        pthread_mutex_unlock(&mtx_); // 防御性操作：尽力避免持有一个坏状态
        return false;
    }

    // ENOTRECOVERABLE / EINVAL / 其他错误：按失败处理（noexcept 环境）
    return false;
}

void ShmMutexLock::unlock() const noexcept {
    // unlock 若失败通常是未持有锁、损坏等，这里选择忽略返回值
    (void)pthread_mutex_unlock(&mtx_);
}
