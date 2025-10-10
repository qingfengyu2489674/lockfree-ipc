#pragma once
#include <cstddef>
#include <type_traits>
#include <utility>
#include "atomic_intrinsics.hpp"
#include "LockFreeNode.hpp"
#include "LockFreeRetiredList.hpp"

// 无锁栈（Treiber 栈）：模板声明
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
    alignas(64) LockFreeNode<T>*   top_{nullptr};
    // 指针持有，名称按你要求：retiredList_
    LockFreeRetiredList<T>*        retiredList_{nullptr};

};


template <typename T>
LockFreeStack<T>::LockFreeStack() : top_(nullptr), retiredList_(new LockFreeRetiredList<T>()) {}

template <typename T>
LockFreeStack<T>::~LockFreeStack() {
    // 1) 清空工作栈
    LockFreeNode<T>* p = load_acquire_ptr(&top_);
    while (p) {
        LockFreeNode<T>* q = p->next;
        delete p;
        p = q;
    }
    store_release_ptr(&top_, nullptr);

    // 2) 清空退休链并释放对象
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

template <typename T>
bool LockFreeStack<T>::pop(T& out) {
    LockFreeNode<T>* old = load_acquire_ptr(&top_);
    while (old) {
        LockFreeNode<T>* next = old->next;
        if (cas_acq_rel_ptr(&top_, old, next)) {
            out = std::move(old->value);
            retiredList_->push(old); // 延迟回收，避免 ABA/UAF
            return true;
        }
        // 若需要退避，可在此插入 cpu_pause()（如你在 lf_atomic_min.hpp 中保留）
    }
    return false;
}

template <typename T>
bool LockFreeStack<T>::empty() const noexcept {
    return load_acquire_ptr(&top_) == nullptr;
}

template <typename T>
void LockFreeStack<T>::pushImpl_(LockFreeNode<T>* n) noexcept {
    LockFreeNode<T>* old = load_acquire_ptr(&top_);
    do { n->next = old; }
    while (!cas_acq_rel_ptr(&top_, old, n)); // 成功即线性化点
}

