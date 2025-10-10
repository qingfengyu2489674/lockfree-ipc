// ShmFreeChunkList.cpp
#include "ShmFreeChunkList.hpp"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <pthread.h>

// 可选：将 pthread 错误码转成可读异常
static void throw_pthread_error(const char* where, int rc) {
    throw std::runtime_error(std::string(where) + " failed: " + std::strerror(rc));
}

// RAII 互斥锁守卫（支持鲁棒互斥量）
class RobustLockGuard {
public:
    explicit RobustLockGuard(pthread_mutex_t* m) : m_(m), locked_(false) {
        int rc = ::pthread_mutex_lock(m_);
        if (rc == EOWNERDEAD) {
            int rc2 = ::pthread_mutex_consistent(m_);
            if (rc2 != 0) throw_pthread_error("pthread_mutex_consistent", rc2);
            locked_ = true;
        } else if (rc != 0) {
            throw_pthread_error("pthread_mutex_lock", rc);
        } else {
            locked_ = true;
        }
    }
    ~RobustLockGuard() {
        if (locked_) {
            (void)::pthread_mutex_unlock(m_);
        }
    }
    RobustLockGuard(const RobustLockGuard&) = delete;
    RobustLockGuard& operator=(const RobustLockGuard&) = delete;

private:
    pthread_mutex_t* m_;
    bool locked_;
};

// ======================= 构造 / 析构 =======================

ShmFreeChunkList::ShmFreeChunkList() {
    pthread_mutexattr_t attr;
    int rc = ::pthread_mutexattr_init(&attr);
    if (rc != 0) throw_pthread_error("pthread_mutexattr_init", rc);

    rc = ::pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    if (rc != 0) {
        ::pthread_mutexattr_destroy(&attr);
        throw_pthread_error("pthread_mutexattr_setpshared", rc);
    }

    rc = ::pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
    if (rc != 0) {
        ::pthread_mutexattr_destroy(&attr);
        throw_pthread_error("pthread_mutexattr_setrobust", rc);
    }

    rc = ::pthread_mutex_init(&mutex_, &attr);
    ::pthread_mutexattr_destroy(&attr);
    if (rc != 0) throw_pthread_error("pthread_mutex_init", rc);

    head_ = nullptr;
    chunk_count_ = 0;
}

ShmFreeChunkList::~ShmFreeChunkList() {
    // 仅由“创建者进程”的最终清理阶段调用
    ::pthread_mutex_destroy(&mutex_);
}

// ======================= 基本操作 =======================

void* ShmFreeChunkList::acquire() {
    RobustLockGuard g(&mutex_);

    if (head_ == nullptr) {
        return nullptr;
    }

    FreeNode* n = head_;
    head_ = n->next;

    // 计数与结构在同一临界区维护强一致
    if (chunk_count_ > 0) {
        --chunk_count_;
    } else {
        // 理论上不应出现；可视项目风格改为断言或忽略
        // assert(false);
    }

    return static_cast<void*>(n);
}

void ShmFreeChunkList::deposit(void* chunk) {
    if (!chunk) return;

    RobustLockGuard g(&mutex_);

    auto* n = static_cast<FreeNode*>(chunk);
    n->next = head_;
    head_ = n;
    ++chunk_count_;
}

size_t ShmFreeChunkList::getCacheCount() const {
    // mutex_ 在头文件声明为 mutable，可直接加锁
    RobustLockGuard g(const_cast<pthread_mutex_t*>(&mutex_));
    return chunk_count_;
}
