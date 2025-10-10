#include "gc_malloc/CentralHeap/AlignedChunkAllocatorByMmap.hpp" 

#include <gc_malloc/CentralHeap/sys/mman.hpp> 
#include <cassert>
#include <cerrno>
#include <cstring>
#include <iostream>

void* AlignedChunkAllocatorByMmap::allocate(size_t size) {

    assert(size > 0 && size % kAlignmentSize == 0 &&
            "Allocation size must be a positive multiple of kAlignmentSize (2MB)");


    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    int protection = PROT_READ | PROT_WRITE;

    // 多申请一个2MB，通过裁剪返回对齐的内存块
    const size_t over_alloc_size = size + kAlignmentSize;
    void* raw_ptr = mmap(nullptr,           // 内核选择地址
                     over_alloc_size,   // 请求长度
                     protection,        // 读写权限
                     flags,             // 标志位
                     -1,                // 匿名映射 
                     0);                // 偏移为0

    if(raw_ptr == MAP_FAILED) {
        return nullptr;
    }

    // 计算需要裁剪的位置和长度
    uintptr_t raw_addr = reinterpret_cast<uintptr_t>(raw_ptr);
    uintptr_t aligned_addr = (raw_addr + kAlignmentSize -1 ) & ~(kAlignmentSize -1);
    uintptr_t aligned_end_addr = aligned_addr + size;
    uintptr_t raw_end_addr = raw_addr + over_alloc_size;

    // 裁剪头部
    size_t head_trim_size = aligned_addr - raw_addr;
    if(head_trim_size > 0) {
        munmap(raw_ptr, head_trim_size);
    }

    // 裁剪尾部
    size_t tail_trim_size = raw_end_addr - aligned_end_addr;
    if(tail_trim_size > 0 ) {
        void* start_of_tail = reinterpret_cast<void*>(aligned_end_addr);
        munmap(start_of_tail, tail_trim_size);
    }

    // 返回中间对齐的部分
    return reinterpret_cast<void*>(aligned_addr);
}

void AlignedChunkAllocatorByMmap::deallocate(void* ptr,size_t size) {
    assert(ptr != nullptr && "Cannot deallocate a null pointer.");
    assert(size > 0 && "Deallocation size must be positive.");

    if(munmap(ptr, size) != 0) {
        std::cerr << "[FATAL] MmapHugePageSource::deallocate: munmap failed. "
                  << "System error: " << strerror(errno) << " (errno=" << errno << ")." 
                  << std::endl;
        assert(false && "munmap failed! Check if ptr and size are valid.");
    }
}