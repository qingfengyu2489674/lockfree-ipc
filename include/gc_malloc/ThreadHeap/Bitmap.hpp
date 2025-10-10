#pragma once

#include <cstddef>
#include <cstdint>

// 简单位图：管理外部提供的缓冲区
class Bitmap {
public:
    explicit Bitmap(std::size_t capacity_in_bits,
                    unsigned char* buffer,
                    std::size_t buffer_size_in_bytes);
    
    virtual ~Bitmap() = default;

    void   markAsUsed(std::size_t bit_index);
    void   markAsFree(std::size_t bit_index);
    bool   isUsed(std::size_t bit_index) const;
    std::size_t findFirstFree(std::size_t start_bit = 0) const;

    static constexpr std::size_t k_not_found = static_cast<std::size_t>(-1);

private:
    unsigned char* buffer_;          // 外部位图缓冲区
    const std::size_t capacity_in_bits_; // 管理的有效位数
};
