#include <vector>
#include <atomic>
#include <memory>
#include <mutex>

#include "Tool/ShmMutexLock.hpp"
#include "EBRManager/LockFreeReuseStack.hpp"
#include "EBRManager/ThreadSlot.hpp"
#include "EBRManager/ThreadHeapAllocator.hpp"

#pragma once

#include <cstddef> // for size_t
#include "gc_malloc/ThreadHeap/ThreadHeap.hpp" // 包含内存释放函数

namespace detail {

// 一个用于 std::unique_ptr 的自定义删除器。
template <typename T>
struct ThreadHeapArrayDeleter {
    // 删除器需要存储数组的大小，以便正确调用所有析构函数。
    // 默认初始化为0，以处理空指针的情况。
    size_t count = 0;

    void operator()(T* ptr) const noexcept {
        if (!ptr || count == 0) {
            return;
        }

        for (size_t i = count; i > 0; --i) {
            ptr[i - 1].~T();
        }
        ThreadHeap::deallocate(ptr);
    }
};

} // namespace detail


class ThreadSlotManager {
public:
    ThreadSlotManager();
    ~ThreadSlotManager();

    ThreadSlot* getLocalSlot();

    template<typename Callable>
    void forEachSlot(Callable func) const;

    ThreadSlotManager(const ThreadSlotManager&) = delete;
    ThreadSlotManager& operator=(const ThreadSlotManager&) = delete;
    ThreadSlotManager(ThreadSlotManager&&) = delete;
    ThreadSlotManager& operator=(ThreadSlotManager&&) = delete;

private:
    class LocalSlotProxy {
    public:
        LocalSlotProxy() noexcept : manager_(nullptr), slot_(nullptr) {}
        ~LocalSlotProxy();

        LocalSlotProxy(const LocalSlotProxy&) = delete;
        LocalSlotProxy& operator=(const LocalSlotProxy&) = delete;

        ThreadSlot* get() const noexcept { return slot_; }
        bool hasSlot() const noexcept { return slot_ != nullptr; }

        void acquire(ThreadSlotManager* manager, ThreadSlot* slot) {
            manager_ = manager;
            slot_ = slot;
        }

    private:
        ThreadSlotManager* manager_;
        ThreadSlot* slot_;
    };

    ThreadSlot* acquireSlot_();
    void releaseSlot_(ThreadSlot* slot) noexcept;
    ThreadSlot* expandAndAcquire();
    
    static constexpr size_t kInitialCapacity = 32;
    using Segment = std::unique_ptr<ThreadSlot, detail::ThreadHeapArrayDeleter<ThreadSlot>>;
    using SegmentVector = std::vector<Segment, ThreadHeapAllocator<Segment>>;

    LockFreeReuseStack<ThreadSlot> free_slots_;
    SegmentVector segments_;
    std::atomic<size_t> capacity_;
    mutable ShmMutexLock resize_lock_;
};



template<typename Callable>
void ThreadSlotManager::forEachSlot(Callable func) const {
    // 加锁以确保在遍历期间 segments_ 向量不会被其他线程修改（例如扩容）。
    // 这提供了一个稳定的、一致的快照视图。
    std::lock_guard<ShmMutexLock> lock(resize_lock_);

    // 遍历每一个内存段 (Segment)
    // 注意：这里的拼写遵循了您的代码，但建议改为 segments_
    for (const auto& segment : segments_) {
        const ThreadSlot* slots_array = segment.get();
        const size_t count = segment.get_deleter().count;
        
        // 遍历该内存段中的每一个槽位
        for (size_t i = 0; i < count; ++i) {
            // 因为 forEachSlot 是 const 成员函数，所以我们传递的是 const ThreadSlot&
            // 这保证了调用者无法在只读遍历中修改槽位状态。
            func(slots_array[i]);
        }
    }
}