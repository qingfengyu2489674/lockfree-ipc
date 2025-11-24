#pragma once

#include "ShareMemory/ShmResourceManager.hpp"
#include "ShareMemory/ShmHeader.hpp"
#include <string>
#include <cstdint> // for uint8_t

class ShmSegment {
public:
    // 构造函数声明
    ShmSegment(const std::string& name, size_t size);

    // 析构函数（如果是默认析构，可以不写或写 =default，但如果 resource_ 需要特殊处理则需实现）
    ~ShmSegment() = default;

    // 禁用拷贝
    ShmSegment(const ShmSegment&) = delete;
    ShmSegment& operator=(const ShmSegment&) = delete;
    
    // 允许移动 (在头文件 default 即可，除非需要隐藏 ShmResourceManager 的实现细节)
    ShmSegment(ShmSegment&&) = default;
    ShmSegment& operator=(ShmSegment&&) = default;

    // 获取堆区偏移 (Header 之后) - 保持 inline 以获得最高性能
    void* getHeapSection() const noexcept {
        return base_ptr_ + sizeof(ShmHeader);
    }
    
    // 获取原始指针 - 保持 inline
    void* getBaseAddress() const noexcept {
        return base_ptr_;
    }

    // 获取大小 - 保持 inline
    size_t getSize() const noexcept { 
        return resource_.getSize(); 
    }

    // 静态解绑函数声明
    static void unlink(const std::string& name);

private:
    ShmResourceManager resource_;
    uint8_t* base_ptr_{nullptr};
    ShmHeader* header_ptr_{nullptr};

    // 私有辅助函数声明
    void format();
    void waitReady();
};