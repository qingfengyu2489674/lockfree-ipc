#include "ShareMemory/ShmResourceManager.hpp"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdexcept>
#include <cerrno>
#include <cstring>
#include <utility> // for std::move

ShmResourceManager::ShmResourceManager(const std::string& name, size_t size)
    : name_(name), size_(size) {
    
    // 1. 尝试原子创建 (O_CREAT | O_EXCL)
    // 这是判断 is_creator 最可靠的方法
    fd_ = shm_open(name_.c_str(), O_RDWR | O_CREAT | O_EXCL, 0666);

    if (fd_ >= 0) {
        // 打开成功 -> 我是创建者
        is_creator_ = true;
        
        // 创建者负责设置大小
        if (ftruncate(fd_, size_) != 0) {
            std::string err = "ftruncate failed: " + std::string(strerror(errno));
            close(fd_);
            shm_unlink(name_.c_str()); // 失败回滚
            throw std::runtime_error(err);
        }
    } else {
        if (errno == EEXIST) {
            // 文件已存在 -> 我是连接者 (Attacher)
            is_creator_ = false;
            
            // 重新以普通读写模式打开
            fd_ = shm_open(name_.c_str(), O_RDWR, 0666);
            if (fd_ < 0) {
                throw std::runtime_error("shm_open (attach) failed: " + std::string(strerror(errno)));
            }
        } else {
            // 其他系统错误
            throw std::runtime_error("shm_open (create) failed: " + std::string(strerror(errno)));
        }
    }

    // 2. 内存映射 (无论创建者还是连接者都需要)
    addr_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (addr_ == MAP_FAILED) {
        std::string err = "mmap failed: " + std::string(strerror(errno));
        close(fd_);
        if (is_creator_) {
            // 如果我是创建者但映射失败，应该清理掉文件，避免留下损坏的空文件
            shm_unlink(name_.c_str());
        }
        throw std::runtime_error(err);
    }
}

ShmResourceManager::~ShmResourceManager() {
    // 释放映射
    if (addr_ && addr_ != MAP_FAILED) {
        munmap(addr_, size_);
    }
    // 关闭文件描述符
    if (fd_ >= 0) {
        close(fd_);
    }
}

ShmResourceManager::ShmResourceManager(ShmResourceManager&& other) noexcept
    : name_(std::move(other.name_))
    , size_(other.size_)
    , fd_(other.fd_)
    , addr_(other.addr_)
    , is_creator_(other.is_creator_) {
    
    // Reset 源对象
    other.fd_ = -1;
    other.addr_ = nullptr;
    other.size_ = 0;
    other.is_creator_ = false;
}

ShmResourceManager& ShmResourceManager::operator=(ShmResourceManager&& other) noexcept {
    if (this != &other) {
        // 释放旧资源
        this->~ShmResourceManager();
        
        // 转移新资源
        name_ = std::move(other.name_);
        size_ = other.size_;
        fd_ = other.fd_;
        addr_ = other.addr_;
        is_creator_ = other.is_creator_;

        // Reset 源对象
        other.fd_ = -1;
        other.addr_ = nullptr;
        other.size_ = 0;
        other.is_creator_ = false;
    }
    return *this;
}

void ShmResourceManager::unlink(const std::string& name) {
    shm_unlink(name.c_str());
}