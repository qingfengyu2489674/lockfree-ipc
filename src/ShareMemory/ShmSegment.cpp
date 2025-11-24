#include "ShareMemory/ShmSegment.hpp"

#include <thread>
#include <chrono>
#include <stdexcept>
#include <iostream>
#include <cstring> // for std::memset

ShmSegment::ShmSegment(const std::string& name, size_t size) 
    : resource_(name, size) { 
    
    base_ptr_ = static_cast<uint8_t*>(resource_.getBaseAddress());
    header_ptr_ = reinterpret_cast<ShmHeader*>(base_ptr_);

    // 根据是否是创建者决定初始化还是等待
    if (resource_.isCreator()) {
        format();
    } else {
        waitReady();
    }
}

void ShmSegment::unlink(const std::string& name) {
    ShmResourceManager::unlink(name);
}

void ShmSegment::format() {
    // 先设为 Initializing，防止其他进程读取到未初始化的数据
    header_ptr_->state.store(ShmState::kInitializing, std::memory_order_relaxed);
    header_ptr_->app_state.store(ShmState::kUninit, std::memory_order_relaxed);
    
    header_ptr_->magic = ShmHeader::kMagic;
    header_ptr_->version = 1;
    header_ptr_->total_size = resource_.getSize();
    header_ptr_->heap_offset = sizeof(ShmHeader);

    // 安全清零 Header 之后的数据区
    std::memset(base_ptr_ + sizeof(ShmHeader), 0, resource_.getSize() - sizeof(ShmHeader));

    // 发布 Ready，释放语义确保前面的写入对其他线程/进程可见
    header_ptr_->state.store(ShmState::kReady, std::memory_order_release);
}

void ShmSegment::waitReady() {
    int retries = 0;
    // 获取语义确保读取到最新的状态
    while (header_ptr_->state.load(std::memory_order_acquire) != ShmState::kReady) {
        if (retries++ > 5000) {
            throw std::runtime_error("Timeout waiting for ShmSegment: " + std::to_string(retries));
        }
        // 避免忙等待占用 CPU
        std::this_thread::yield();
    }

    if (header_ptr_->magic != ShmHeader::kMagic) {
        throw std::runtime_error("ShmSegment Magic Mismatch");
    }
}