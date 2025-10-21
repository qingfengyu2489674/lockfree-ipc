#pragma once

#include <atomic>
#include <cstdint>

template <typename T>
class StampPtrPacker {
public:
    StampPtrPacker() = delete;
    using type = uint64_t;
    using atomic_type = std::atomic<type>;

private:
    static constexpr int kStampBits = 16;
    static constexpr int kPointerBits = 64 - kStampBits;
    static constexpr type kPointerMask = (1ULL << kPointerBits) - 1;

public:
    static type pack(T* ptr, uint16_t stamp);
    static T* unpackPtr(type packed_val);
    static uint16_t unpackStamp(type packed_val);
};

template <typename T>
typename StampPtrPacker<T>::type 
StampPtrPacker<T>::pack(T* ptr, uint16_t stamp) {
    uint64_t ptr_val = reinterpret_cast<uint64_t>(ptr);
    return (static_cast<type>(stamp) << kPointerBits) | (ptr_val & kPointerMask);
}

template <typename T>
T* StampPtrPacker<T>::unpackPtr(type packed_val) {
    uint64_t stored_ptr = packed_val & kPointerMask;
    if ((stored_ptr >> (kPointerBits - 1)) & 1) {
        stored_ptr |= ~kPointerMask;
    }
    return reinterpret_cast<T*>(stored_ptr);
}

template <typename T>
uint16_t StampPtrPacker<T>::unpackStamp(type packed_val) {
    return static_cast<uint16_t>(packed_val >> kPointerBits);
}
