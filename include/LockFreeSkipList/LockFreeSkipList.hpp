#pragma once

#include <functional>
#include <limits>
#include <random>
#include <cstdint>

#include "LockFreeSkipList/LockFreeSkipListNode.hpp"
#include "EBRManager/EBRManager.hpp"

template<typename Key, typename Value, typename Compare = std::less<Key>>
class LockFreeSkipList {
public:
    explicit LockFreeSkipList(EBRManager& ebr_manager);
    ~LockFreeSkipList();

    LockFreeSkipList(const LockFreeSkipList&) = delete;
    LockFreeSkipList& operator=(const LockFreeSkipList&) = delete;
    LockFreeSkipList(LockFreeSkipList&&) = delete;
    LockFreeSkipList& operator=(LockFreeSkipList&&) = delete;

    bool find(const Key& key, Value& value);
    bool insert(const Key& key, const Value& value);
    bool remove(const Key& key);

public:
    using Node = LockFreeSkipListNode<Key, Value>;
    static constexpr int kMaxHeight = 4;
    Node* head_;
    EBRManager& ebr_manager_;
    Compare compare_;

public:
    void findNode_(const Key& key, Node* prevs[], Node* nexts[]);
    bool tryMarkForRemoval_(Node* node_to_delete);
    void helpUnlink_(Node* prev, Node* marked_next, int level);

    bool isMarked_(Node* ptr) const noexcept;
    Node* getMarked_(Node* ptr) const noexcept;
    Node* getUnmarked_(Node* ptr) const noexcept;

    int random_height_();
    static std::mt19937& get_random_engine_();
};



#include "LockFreeSkipList/LockFreeSkipList_impl.hpp"