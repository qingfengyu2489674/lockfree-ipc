#pragma once

#include <atomic>

#include "EBRManager/EBRManager.hpp"

namespace ebr {
class Guard {
public:
    explicit Guard(EBRManager& manager) : manager_(manager) {
        manager.enter();
    }

    ~Guard() {
        manager_.leave();
    }

    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
    Guard(Guard&&) = delete;
    Guard& operator=(Guard&&) = delete;

private:
    EBRManager& manager_;
};

template<typename T>
inline T* read(const std::atomic<T*>& ptr) {
    return ptr.load(std::memory_order_acquire);
}

template<typename T>
inline void retire(EBRManager& manager, T* ptr) {
    manager.retire(ptr);
}

}