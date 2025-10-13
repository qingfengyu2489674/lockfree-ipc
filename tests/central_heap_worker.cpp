#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>
#include <exception>

#include "ShareMemory/ShareMemoryRegion.hpp"
#include "gc_malloc/CentralHeap/CentralHeap.hpp"

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <shm_name> <region_bytes> <rounds>" << std::endl;
        return 1;
    }

    const char* shm_name = argv[1];
    const size_t region_bytes = std::stoull(argv[2]);
    const int rounds = std::stoi(argv[3]);

    try {
        ShareMemoryRegion shm(shm_name, region_bytes, /*create=*/false);
        void* base = shm.getMappedAddress();
        if (!base) return 101;
        
        auto& ch = CentralHeap::GetInstance(base, region_bytes);

        // 执行高频的“申请-释放”循环来制造锁争用
        for (int r = 0; r < rounds; ++r) {
            void* p = ch.acquireChunk(CentralHeap::kChunkSize);
            if (!p) {
                // 在这个测试场景下，我们不应该耗尽内存，所以这是一个错误
                std::cerr << "Worker (pid=" << getpid() << ") unexpectedly failed to acquire chunk on round " 
                          << r << std::endl;
                return 102;
            }
            ch.releaseChunk(p, CentralHeap::kChunkSize);
        }

    } catch (const std::exception& e) {
        std::cerr << "Worker (pid=" << getpid() << ") caught exception: " << e.what() << std::endl;
        return 103;
    } catch (...) {
        std::cerr << "Worker (pid=" << getpid() << ") caught unknown exception." << std::endl;
        return 104;
    }

    return 0; // 成功
}