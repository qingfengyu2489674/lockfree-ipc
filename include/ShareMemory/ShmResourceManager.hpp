#pragma once
#include <string>
#include <cstddef>

class ShmResourceManager {
public:
    ShmResourceManager(const std::string& name, size_t size);

    ~ShmResourceManager();

    ShmResourceManager(ShmResourceManager&& other) noexcept;
    ShmResourceManager& operator=(ShmResourceManager&& other) noexcept;

    ShmResourceManager(const ShmResourceManager&) = delete;
    ShmResourceManager& operator=(const ShmResourceManager&) = delete;

    void* getBaseAddress() const noexcept { return addr_; }
    
    size_t getSize() const noexcept { return size_; }

    // 状态查询：当前管理者是否是该资源的“初次创建者”
    // 这决定了上层逻辑是否需要执行初始化操作
    bool isCreator() const noexcept { return is_creator_; }

    // 静态工具：主动销毁 OS 中的共享内存段
    static void unlink(const std::string& name);

private:
    std::string name_;
    size_t size_{0};
    int fd_{-1};
    void* addr_{nullptr};
    bool is_creator_{false}; // 身份标记
};