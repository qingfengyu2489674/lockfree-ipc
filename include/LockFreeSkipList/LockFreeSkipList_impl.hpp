#pragma once
#include "EBRManager/guard.hpp"

// --- 辅助函数 (标记指针 & 随机层高) ---

template<typename K, typename V, typename C>
typename LockFreeSkipList<K, V, C>::Node* 
LockFreeSkipList<K, V, C>::getMarked_(Node* ptr) const noexcept {
    return reinterpret_cast<Node*>(reinterpret_cast<uintptr_t>(ptr) | 1);
}

template<typename K, typename V, typename C>
bool LockFreeSkipList<K, V, C>::isMarked_(typename LockFreeSkipList<K, V, C>::Node* ptr) const noexcept {
    return (reinterpret_cast<uintptr_t>(ptr) & 1) != 0;
}

template<typename K, typename V, typename C>
typename LockFreeSkipList<K, V, C>::Node* 
LockFreeSkipList<K, V, C>::getUnmarked_(Node* ptr) const noexcept {
    return reinterpret_cast<Node*>(reinterpret_cast<uintptr_t>(ptr) & ~1);
}

template<typename K, typename V, typename C>
std::mt19937& LockFreeSkipList<K, V, C>::get_random_engine_() {
    thread_local static std::mt19937 engine(std::random_device{}());
    return engine;
}

template<typename K, typename V, typename C>
int LockFreeSkipList<K, V, C>::random_height_(){
    std::geometric_distribution<> dist(0.5);
    int height = dist(this->get_random_engine_()) + 1;
    return std::min(height, kMaxHeight);
}


// --- 构造与析构 ---
template<typename K, typename V, typename C>
LockFreeSkipList<K, V, C>::LockFreeSkipList(EBRManager& ebr_manager)
    : ebr_manager_(ebr_manager), compare_(){
    K min_key = std::numeric_limits<K>::min(); 
    head_ = Node::createHead(min_key, kMaxHeight);
}

template<typename K, typename V, typename C>
LockFreeSkipList<K, V, C>::~LockFreeSkipList() {
    Node* current = head_;
    while (current != nullptr) {
        Node* next = getUnmarked_(current->forward_[0].load(std::memory_order_relaxed));
        Node::destroy(current);
        current = next;
    }
}


// --- 核心实现 (最终修复版) ---
template<typename K, typename V, typename C>
void LockFreeSkipList<K, V, C>::findNode_(const K& key, Node* prevs[], Node* nexts[]) {
search_again:
    Node* pred = head_;

    for (int level = kMaxHeight - 1; level >= 0; --level) {
        Node* curr = pred->forward_[level].load(std::memory_order_acquire);

        while (true) {
            Node* succ = getUnmarked_(curr);
            if (succ == nullptr) {
                break;
            }

            Node* succ_next_l0 = succ->forward_[0].load(std::memory_order_acquire);

            if (isMarked_(succ_next_l0)) {
                Node* succ_next = succ->forward_[level].load(std::memory_order_acquire);
                if (!pred->forward_[level].compare_exchange_strong(curr, getUnmarked_(succ_next), std::memory_order_release)) {
                    goto search_again;
                }
                curr = pred->forward_[level].load(std::memory_order_acquire);
            } else {
                if (compare_(succ->key, key)) {
                    pred = succ;
                    curr = succ->forward_[level].load(std::memory_order_acquire); 
                } else {
                    break;
                }
            }
        }
        
        prevs[level] = pred;
        nexts[level] = curr;
    }
}


template<typename K, typename V, typename C>
bool LockFreeSkipList<K, V, C>::find(const K& key, V& value) {
    ebr::Guard guard(ebr_manager_);
    Node* prevs[kMaxHeight];
    Node* nexts[kMaxHeight];
    findNode_(key, prevs, nexts);

    Node* node = getUnmarked_(nexts[0]);
    if (node && !compare_(key, node->key) && !compare_(node->key, key)) {
        value = node->value;
        Node* final_check = node->forward_[0].load(std::memory_order_acquire);
        return !isMarked_(final_check);
    }
    return false;
}


template<typename K, typename V, typename C>
bool LockFreeSkipList<K, V, C>::insert(const K& key, const V& value) {
    ebr::Guard guard(ebr_manager_);
    Node* prevs[kMaxHeight];
    Node* nexts[kMaxHeight];
    
    while (true) {
        findNode_(key, prevs, nexts);
        Node* node = getUnmarked_(nexts[0]);

        if (node && !compare_(key, node->key) && !compare_(node->key, key)) {
            return false;
        }

        int height = random_height_();
        Node* new_node = Node::create(key, value, height);
        
        for (int i = 0; i < height; ++i) {
            new_node->forward_[i].store(getUnmarked_(nexts[i]), std::memory_order_relaxed);
        }
        
        if (prevs[0]->forward_[0].compare_exchange_strong(nexts[0], new_node, std::memory_order_release)) {
            for (int i = 1; i < height; ++i) {
                prevs[i]->forward_[i].compare_exchange_strong(nexts[i], new_node, std::memory_order_release);
            }
            return true;
        } else {
            Node::destroy(new_node);
        }
    }
}


template<typename K, typename V, typename C>
bool LockFreeSkipList<K, V, C>::tryMarkForRemoval_(Node* node_to_delete) {
    Node* succ_l0 = node_to_delete->forward_[0].load(std::memory_order_acquire);
    while (true) {
        if (isMarked_(succ_l0)) {
            return false;
        }
        if (node_to_delete->forward_[0].compare_exchange_weak(succ_l0, getMarked_(succ_l0), std::memory_order_release)) {
            return true;
        }
    }
}


template<typename K, typename V, typename C>
bool LockFreeSkipList<K, V, C>::remove(const K& key) {
    ebr::Guard guard(ebr_manager_);
    
    Node* prevs[kMaxHeight];
    Node* nexts[kMaxHeight];

    while (true) {
        findNode_(key, prevs, nexts);
        Node* node_to_delete = getUnmarked_(nexts[0]);

        if (!node_to_delete || (compare_(key, node_to_delete->key) || compare_(node_to_delete->key, key))) {
            return false;
        }
        
        if (!tryMarkForRemoval_(node_to_delete)) {
            continue;
        }
        
        findNode_(key, prevs, nexts);
        
        ebr_manager_.retire(node_to_delete);
        return true;
    }
}