# src/CMakeLists.txt

# 将 calculator.cpp 编译成一个名为 "mylib" 的静态库
add_library(mylib STATIC
    ShareMemory/ShareMemoryRegion.cpp

    gc_malloc/CentralHeap/AlignedChunkAllocatorByMmap.cpp
    gc_malloc/CentralHeap/ShmChunkAllocator.cpp
    gc_malloc/CentralHeap/ShmFreeChunkList.cpp
    gc_malloc/CentralHeap/FreeChunkListCache.cpp
    gc_malloc/CentralHeap/CentralHeap.cpp
    gc_malloc/ThreadHeap/Bitmap.cpp
    gc_malloc/ThreadHeap/MemSubPool.cpp
    gc_malloc/ThreadHeap/MemSubPoolList.cpp
    gc_malloc/ThreadHeap/SizeClassPoolManager.cpp
    gc_malloc/ThreadHeap/BlockHeader.cpp
    gc_malloc/ThreadHeap/ManagedList.cpp
    gc_malloc/ThreadHeap/SizeClassConfig.cpp
    gc_malloc/ThreadHeap/ThreadHeap.cpp
    gc_malloc/ThreadHeap/ProcessAllocatorContext.cpp

    EBRManager/ThreadSlot.cpp
    EBRManager/ThreadSlotManager.cpp
    EBRManager/GarbageNode.cpp
    EBRManager/LockFreeSingleLinkedList.cpp
    EBRManager/EBRManager.cpp

    Tool/ShmMutexLock.cpp


)

# 头文件目录（公开给依赖 mylib 的目标）
target_include_directories(mylib PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
