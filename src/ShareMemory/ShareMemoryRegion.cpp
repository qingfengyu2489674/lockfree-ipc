#include "ShareMemory/ShareMemoryRegion.hpp"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdexcept>

ShareMemoryRegion::ShareMemoryRegion(const std::string& name, size_t size, bool create)
    : name_(name), size_(size) {
    int flags = create ? (O_CREAT | O_RDWR) : O_RDWR;
    fd_ = shm_open(name_.c_str(), flags, 0666);
    if (fd_ < 0)
        throw std::runtime_error("shm_open failed");

    if (create && ftruncate(fd_, size_) != 0)
        throw std::runtime_error("ftruncate failed");

    addr_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (addr_ == MAP_FAILED)
        throw std::runtime_error("mmap failed");
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
