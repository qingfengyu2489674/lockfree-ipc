#include "gc_malloc/ThreadHeap/Bitmap.hpp"
#include <cstring>   // For std::memset
#include <stdexcept> // For std::runtime_error


// 构造函数
Bitmap::Bitmap(size_t capacity_in_bits, unsigned char* buffer, size_t buffer_size_in_bytes)
    : buffer_(buffer),
      capacity_in_bits_(capacity_in_bits)
{
    if (buffer_ == nullptr) {
        throw std::runtime_error("Bitmap buffer cannot be null.");
    }

    const size_t required_bytes = (capacity_in_bits_ + 7) / 8;

    if (buffer_size_in_bytes < required_bytes) {
        throw std::runtime_error("Bitmap buffer is smaller than required.");
    }
    
    // 3. 将所有有效字节初始化为 0 (空闲)
    std::memset(buffer_, 0, required_bytes);

    // 4. 处理最后一个有效字节，将其中超出容量的无效位设置为 1 (占用)
    const size_t remainder_bits = capacity_in_bits_ % 8;
    if (remainder_bits > 0) {
        unsigned char& last_byte = buffer_[required_bytes - 1];
        
        unsigned char mask = (0xFF << remainder_bits);
        last_byte |= mask;
    }
    
    // 5. 将所有超出有效范围的、预留的备用字节全部设置为 0xFF (占用)
    //    这确保了位图的整个预留空间都是安全的状态。
    if (buffer_size_in_bytes > required_bytes) {
        std::memset(buffer_ + required_bytes, 0xFF, buffer_size_in_bytes - required_bytes);
    }
}



// 将指定索引的位设置为 1，表示该内存块已被占用。
void Bitmap::markAsUsed(size_t bit_index) {
    if (bit_index >= capacity_in_bits_) {
        return;
    }
    const size_t byte_index = bit_index / 8;
    const unsigned char bit_mask = 1 << (bit_index % 8);
    buffer_[byte_index] |= bit_mask;
}

// 将指定索引的位设置为 0，表示该内存块已被释放/变为空闲。
void Bitmap::markAsFree(size_t bit_index) {
    if (bit_index >= capacity_in_bits_) {
        return;
    }
    const size_t byte_index = bit_index / 8;
    const unsigned char bit_mask = 1 << (bit_index % 8);
    buffer_[byte_index] &= ~bit_mask;
}

// 检查指定索引的位是否为 1 (即是否被占用)。
bool Bitmap::isUsed(size_t bit_index) const {
    if (bit_index >= capacity_in_bits_) {
        return true;
    }
    const size_t byte_index = bit_index / 8;
    const unsigned char bit_mask = 1 << (bit_index % 8);
    return (buffer_[byte_index] & bit_mask) != 0;
}

// 从指定的起始位置开始，查找第一个为 0 (空闲) 的位。
size_t Bitmap::findFirstFree(size_t start_bit) const {
    size_t current_bit = start_bit;
    const size_t required_bytes = (capacity_in_bits_ + 7) / 8;

    // 1. 处理起始字节中剩余的位
    size_t byte_index = current_bit / 8;
    if (byte_index < required_bytes) {
        size_t bit_in_byte = current_bit % 8;
        if (bit_in_byte != 0) {
            for (size_t bit = bit_in_byte; bit < 8; ++bit) {
                if (current_bit >= capacity_in_bits_) return k_not_found;
                if (!isUsed(current_bit)) {
                    return current_bit;
                }
                current_bit++;
            }
            byte_index++;
        }
    }

    // 2. 快速检查完整的字节
    //    我们可以一次检查一个字节是否为 0xFF (全被占用)。
    while (byte_index < required_bytes && buffer_[byte_index] == 0xFF) {
        byte_index++;
    }

    // 3. 逐位检查找到的第一个不完全占用的字节
    if (byte_index < required_bytes) {
        current_bit = byte_index * 8;
        for (size_t bit = 0; bit < 8; ++bit) {
            if (current_bit >= capacity_in_bits_) return k_not_found;
            if (!isUsed(current_bit)) {
                return current_bit;
            }
            current_bit++;
        }
    }

    return k_not_found; // 没有找到任何空闲位
}
