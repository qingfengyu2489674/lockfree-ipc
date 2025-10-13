#include "ShareMemory/ShareMemoryRegion.hpp"
#include "ShareMemory/ShmHeader.hpp"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdexcept>
#include <cstring>   // std::memset
#include <cassert>

ShareMemoryRegion::ShareMemoryRegion(const std::string& name, size_t size, bool create)
    : name_(name), size_(size) {

    int flags = create ? (O_CREAT | O_RDWR) : O_RDWR;
    fd_ = shm_open(name_.c_str(), flags, 0666);
    if (fd_ < 0)
        throw std::runtime_error("shm_open failed for " + name_);

    if (create && ftruncate(fd_, size_) != 0)
        throw std::runtime_error("ftruncate failed for " + name_);

    addr_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (addr_ == MAP_FAILED)
        throw std::runtime_error("mmap failed for " + name_);

    // ------------------------------------------------------------
    // 关键：初始化 ShmHeader（仅第一次创建时执行）
    // ------------------------------------------------------------
    if (create) {
        assert(size_ >= sizeof(ShmHeader) && "shared memory too small");
        auto* header = static_cast<ShmHeader*>(addr_);

        // 写入魔数、版本、状态机初值
        header->magic         = 0x43484541; // "AHEC" = CentralHeap
        header->version       = 1;
        header->state.store(ShmState::kUninit, std::memory_order_relaxed);
        header->heap_off      = 0;
        header->data_off      = 0;
        header->region_bytes  = 0;

        // 为安全起见，把 ShmHeader 之后的区域清零
        std::memset(reinterpret_cast<unsigned char*>(addr_) + sizeof(ShmHeader),
                    0, size_ - sizeof(ShmHeader));
    }
}

ShareMemoryRegion::~ShareMemoryRegion() {
    if (addr_ && addr_ != MAP_FAILED)
        munmap(addr_, size_);
    if (fd_ >= 0)
        close(fd_);
}

void* ShareMemoryRegion::getMappedAddress() const noexcept { return addr_; }
size_t ShareMemoryRegion::getMemorySize() const noexcept { return size_; }

void ShareMemoryRegion::unlinkSegment(const std::string& name) noexcept {
    shm_unlink(name.c_str());
}
