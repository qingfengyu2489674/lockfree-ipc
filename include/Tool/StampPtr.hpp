#pragma once

#include <cstdint>
#include <utility>

template <typename T, typename AtomicT>
class StampPtr {
public:
    using PtrType = T*;
    using StampType = uint16_t;
    using PackedType = uint64_t;

    struct Unpacked {
        PtrType ptr{nullptr};
        StampType stamp{0};

        bool operator==(const Unpacked& other) const {
            return ptr == other.ptr && stamp == other.stamp;
        }
        bool operator!=(const Unpacked& other) const {
            return !(*this == other);
        }
    };

private:
    static constexpr int kStampBits = 16;
    static constexpr int kPointerBits = 64 - kStampBits;
    static constexpr PackedType kPointerMask = (1ULL << kPointerBits) - 1;

    static PackedType pack(PtrType ptr, StampType stamp) {
        uint64_t ptr_val = reinterpret_cast<uint64_t>(ptr);
        return (static_cast<PackedType>(stamp) << kPointerBits) | (ptr_val & kPointerMask);
    }

    static PtrType unpackPtr(PackedType packed_val) {
        uint64_t stored_ptr = packed_val & kPointerMask;
        if ((stored_ptr >> (kPointerBits - 1)) & 1) {
            stored_ptr |= ~kPointerMask;
        }
        return reinterpret_cast<PtrType>(stored_ptr);
    }

    static StampType unpackStamp(PackedType packed_val) {
        return static_cast<StampType>(packed_val >> kPointerBits);
    }

public:
    explicit StampPtr(AtomicT& slot) noexcept : slot_(slot) {}

    template <typename OrderT>
    Unpacked load(OrderT order) const noexcept {
        PackedType packed = slot_.load(order);
        return { unpackPtr(packed), unpackStamp(packed) };
    }

    template <typename OrderT>
    void store(Unpacked desired, OrderT order) noexcept {
        slot_.store(pack(desired.ptr, desired.stamp), order);
    }

    template <typename OrderT>
    bool casBump(Unpacked& expected, PtrType desired_ptr,
                 OrderT succ,
                 OrderT fail) noexcept 
    {
        PackedType expected_packed = pack(expected.ptr, expected.stamp);
        StampType new_stamp = static_cast<StampType>(expected.stamp + 1);
        PackedType desired_packed = pack(desired_ptr, new_stamp);

        bool success = slot_.compare_exchange_weak(expected_packed, desired_packed, succ, fail);
        
        if (!success) {
            expected.ptr = unpackPtr(expected_packed);
            expected.stamp = unpackStamp(expected_packed);
        }

        return success;
    }

private:
    AtomicT& slot_; 
};