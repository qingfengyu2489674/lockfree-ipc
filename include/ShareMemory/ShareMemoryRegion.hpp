#pragma once
#include <string>
#include <cstddef>

class ShareMemoryRegion {
public:
    ShareMemoryRegion(const std::string& name, size_t getMemorySize, bool create = true);
    virtual ~ShareMemoryRegion();

    void* getMappedAddress() const noexcept;
    size_t getMemorySize() const noexcept;

    static void unlinkSegment(const std::string& name) noexcept;

private:
    std::string name_;
    int fd_{-1};
    size_t size_{0};
    void* addr_{nullptr};
};
