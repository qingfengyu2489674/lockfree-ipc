// LockFreeLinkedList/LockFreeLinkedList.hpp
#pragma once

#include <atomic>
#include <cstddef>
#include <type_traits> // For std::remove_pointer

#include "LockFreeLinkedList/LockFreeListNode.hpp"
#include "Hazard/HazardPointerOrganizer.hpp"

class DefaultHeapPolicy;

template <class T, class AllocPolicy = DefaultHeapPolicy>
class LockFreeLinkedList {
public:
    using value_type = T;
    using node_type  = LockFreeListNode<T>;
    using node_ptr   = node_type*;
    using size_type  = std::size_t;

    static constexpr std::size_t kHazardPointers = 2; 
    using hp_organizer_type = HazardPointerOrganizer<node_type, kHazardPointers, AllocPolicy>;

    
public:
    explicit LockFreeLinkedList(hp_organizer_type& hp_organizer) noexcept;
    
    ~LockFreeLinkedList() noexcept;

    bool insert(const value_type& value);
    bool remove(const value_type& value);
    bool contains(const value_type& value) noexcept;
    bool isEmpty() const noexcept;

private:
    void find(
        const value_type& value, 
        node_ptr& prev, 
        node_ptr& curr, 
        decltype(std::declval<hp_organizer_type>().acquireTlsSlot()) slot 
    );

    node_type head_sentinel_;
    hp_organizer_type& hp_organizer_;
};

#include "LockFreeLinkedList_impl.hpp"