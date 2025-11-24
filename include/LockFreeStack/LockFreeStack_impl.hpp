// LockFreeStack_impl.hpp
#pragma once

template <class T, class AllocPolicy>
LockFreeStack<T, AllocPolicy>::LockFreeStack(hp_organizer_type& hp_organizer) noexcept
    : hp_organizer_(hp_organizer), head_{0} {}


template <class T, class AllocPolicy>
LockFreeStack<T, AllocPolicy>::~LockFreeStack() noexcept {
    value_type discard_val;
    while (tryPop(discard_val)) {
        // 循环弹出直到为空，tryPop 内部会调用 retire
    }
}


template <class T, class AllocPolicy>
void LockFreeStack<T, AllocPolicy>::push(const value_type& v) {
    auto* new_node = AllocPolicy::template allocate<node_type>(v);
    HeadPacker packer(head_); 
    auto current = packer.load(MemoryOrder::Relaxed);
    do {
        new_node->next = current.ptr;
    } while (!packer.casBump(current, new_node, MemoryOrder::Release, MemoryOrder::Relaxed));
}

template <class T, class AllocPolicy>
void LockFreeStack<T, AllocPolicy>::push(value_type&& v) {
    auto* new_node = AllocPolicy::template allocate<node_type>(std::move(v));

    HeadPacker packer(head_);
    auto current = packer.load(MemoryOrder::Relaxed);
    
    do {
        new_node->next = current.ptr;
    } while (!packer.casBump(current, new_node, MemoryOrder::Release, MemoryOrder::Relaxed));
}

template <class T, class AllocPolicy>
bool LockFreeStack<T, AllocPolicy>::tryPop(value_type& out) noexcept {

    auto* slot = hp_organizer_.acquireTlsSlot();
    if (!slot) return false;

    HeadPacker packer(head_);

    for (;;) {
        auto old_head_packed = packer.load(MemoryOrder::Acquire);
        node_type* old_head_ptr = old_head_packed.ptr;

        if (!old_head_ptr) {
            slot->clear(0);
            return false;
        }

        slot->protect(0, old_head_ptr);
        
        atomic_thread_fence(MemoryOrder::SeqCst);

        if (old_head_packed != packer.load(MemoryOrder::Acquire)) {
            continue;
        }

        node_type* next = old_head_ptr->next;

        if (packer.casBump(
                old_head_packed, next,
                MemoryOrder::AcqRel,
                MemoryOrder::Relaxed)) {
            
            out = std::move(old_head_ptr->value);
            hp_organizer_.retire(old_head_ptr);

            slot->clear(0);
            return true;
        }

    }
}

template <class T, class AllocPolicy>
bool LockFreeStack<T, AllocPolicy>::isEmpty() const noexcept {
    HeadPacker packer(const_cast<Atomic<uint64_t>&>(head_)); 
    return packer.load(MemoryOrder::Acquire).ptr == nullptr;
}
