#pragma once
#include <cstddef>
#include "atomic_intrinsics.hpp"
#include "LockFreeNode.hpp"

// 延迟回收链（Treiber 栈），模板声明
template <typename T>
class LockFreeRetiredList {
public:
    LockFreeRetiredList() noexcept;
    LockFreeRetiredList(const LockFreeRetiredList&)            = delete;
    LockFreeRetiredList& operator=(const LockFreeRetiredList&) = delete;
    ~LockFreeRetiredList();

    // 并发阶段：把节点压入退休链
    void push(LockFreeNode<T>* n) noexcept;

    // 非并发阶段：清理全部节点
    void clear() noexcept;

private:
    alignas(64) LockFreeNode<T>* head_{nullptr};
};

template <typename T>
LockFreeRetiredList<T>::LockFreeRetiredList() noexcept : head_(nullptr) {}

template <typename T>
LockFreeRetiredList<T>::~LockFreeRetiredList() { clear(); }

template <typename T>
void LockFreeRetiredList<T>::push(LockFreeNode<T>* n) noexcept {
    LockFreeNode<T>* old = load_acquire_ptr(&head_);
    do { n->next = old; }
    while (!cas_acq_rel_ptr(&head_, old, n));
}

template <typename T>
void LockFreeRetiredList<T>::clear() noexcept {
    // 非并发阶段直接线性回收
    LockFreeNode<T>* p = head_;
    while (p) {
        LockFreeNode<T>* q = p->next;
        delete p;
        p = q;
    }
    store_release_ptr(&head_, nullptr);
}

