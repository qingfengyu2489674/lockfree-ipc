#pragma once
#include "ShareMemory/ShmResourceManager.hpp"
#include "ShareMemory/ShmHeader.hpp"
#include <string>
#include <thread>
#include <chrono>
#include <stdexcept>
#include <iostream>
#include <cstring>

class ShmSegment {
public:
    ShmSegment(const std::string& name, size_t size) 
        : resource_(name, size) { 
        
        base_ptr_ = static_cast<uint8_t*>(resource_.getBaseAddress());
        header_ptr_ = reinterpret_cast<ShmHeader*>(base_ptr_);

        if (resource_.isCreator()) {
            format();
        } else {
            waitReady();
        }
    }

    ShmSegment(const ShmSegment&) = delete;
    ShmSegment& operator=(const ShmSegment&) = delete;
    
    ShmSegment(ShmSegment&&) = default;
    ShmSegment& operator=(ShmSegment&&) = default;

    // 获取堆区偏移 (Header 之后)
    void* getHeapSection() const noexcept {
        return base_ptr_ + sizeof(ShmHeader);
    }
    
    // 获取原始指针 (通常 Allocator 需要这个计算偏移)
    void* getBaseAddress() const noexcept {
        return base_ptr_;
    }

    size_t getSize() const noexcept { return resource_.getSize(); }

    static void unlink(const std::string& name) {
        ShmResourceManager::unlink(name);
    }

private:
    ShmResourceManager resource_;
    uint8_t* base_ptr_{nullptr};
    ShmHeader* header_ptr_{nullptr};

    void format() {
        // 先设为 Initializing
        header_ptr_->state.store(ShmState::kInitializing, std::memory_order_relaxed);
        header_ptr_->app_state.store(ShmState::kUninit, std::memory_order_relaxed);
        
        header_ptr_->magic = ShmHeader::kMagic;
        header_ptr_->version = 1;
        header_ptr_->total_size = resource_.getSize();
        header_ptr_->heap_offset = sizeof(ShmHeader);

        // 安全清零
        std::memset(base_ptr_ + sizeof(ShmHeader), 0, resource_.getSize() - sizeof(ShmHeader));

        // 发布 Ready
        header_ptr_->state.store(ShmState::kReady, std::memory_order_release);
    }

    void waitReady() {
        int retries = 0;
        while (header_ptr_->state.load(std::memory_order_acquire) != ShmState::kReady) {
            if (retries++ > 5000) throw std::runtime_error("Timeout waiting for ShmSegment");
            std::this_thread::yield();
        }
        if (header_ptr_->magic != ShmHeader::kMagic) {
            throw std::runtime_error("ShmSegment Magic Mismatch");
        }
    }
};