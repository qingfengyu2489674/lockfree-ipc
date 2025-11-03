#pragma once
#include "EBRManager/guard.hpp"

#include <cstdint>

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
        Packed pnext = current->nextSlot(0).load(std::memory_order_relaxed);
        Node*  next  = getUnmarked_(Packer::unpackPtr(pnext));
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
        Packed pred_next_packed = pred->nextSlot(level).load(std::memory_order_acquire);
        Node*  curr     = Packer::unpackPtr(pred_next_packed);

        while (true) {
            Node* succ = getUnmarked_(curr);
            if (succ == nullptr) {
                break;
            }

            Packed succ_l0_p = succ->nextSlot(0).load(std::memory_order_acquire);
            Node*  succ_l0   = Packer::unpackPtr(succ_l0_p);

            if (isMarked_(succ_l0)) {
                Packed succ_next_p = succ->nextSlot(level).load(std::memory_order_acquire);
                Node*  succ_next   = getUnmarked_(Packer::unpackPtr(succ_next_p));

                if (!Packer::casBump(pred->nextSlot(level),
                                            pred_next_packed, succ_next,
                                            std::memory_order_release,
                                            std::memory_order_acquire)) {
                    // CAS 失败，exp_pred 已刷新；重新从这一层开始
                    goto search_again;
                }
                curr = Packer::unpackPtr(pred_next_packed);
            } else {
                if (compare_(succ->key, key)) {
                    pred = succ;
                    pred_next_packed = pred->nextSlot(level).load(std::memory_order_acquire);
                    curr     = Packer::unpackPtr(pred_next_packed);
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
        // 再确认未被逻辑删除
        Packed check_p = node->nextSlot(0).load(std::memory_order_acquire);
        return !isMarked_(Packer::unpackPtr(check_p));
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
            return false; // 已存在
        }

        int   height   = random_height_();
        Node* new_node = Node::create(key, value, height);

        // 初始化 new_node 的 forward
        for (int i = 0; i < height; ++i) {
            // 以 nexts[i]（可能带标记）为初值，存入其“去标记”的版本
            new_node->nextSlot(i).store(Packer::pack(getUnmarked_(nexts[i]), 0),
                                        std::memory_order_relaxed);
        }

        // 先在 level 0 挂接（必须成功）
        auto& slot0   = prevs[0]->nextSlot(0);
        auto  exp0    = slot0.load(std::memory_order_acquire);
        Node* expect0 = Packer::unpackPtr(exp0);
        if (getUnmarked_(expect0) != getUnmarked_(nexts[0])) {
            Node::destroy(new_node);
            continue; // 形势变了，重来
        }
        if (!Packer::casBump(slot0, exp0, new_node,
                                    std::memory_order_release,
                                    std::memory_order_acquire)) {
            Node::destroy(new_node);
            continue;
        }

        // 尝试在更高层建立前向链接（best effort）
        for (int i = 1; i < height; ++i) {
            auto& slot_i   = prevs[i]->nextSlot(i);
            auto  exp_i    = slot_i.load(std::memory_order_acquire);
            Node* expect_i = Packer::unpackPtr(exp_i);
            // 只要 prevs[i] 仍然指向 nexts[i]，就尝试把它改为 new_node
            if (getUnmarked_(expect_i) == getUnmarked_(nexts[i])) {
                Packer::casBump(slot_i, exp_i, new_node,
                                       std::memory_order_release,
                                       std::memory_order_acquire);
            }
            // 失败也没关系，后续 find 会逐步修复
        }

        return true;
    }
}


template<typename K, typename V, typename C>
bool LockFreeSkipList<K, V, C>::tryMarkForRemoval_(Node* node_to_delete) {
    while (true) {
        Packed exp = node_to_delete->nextSlot(0).load(std::memory_order_acquire);
        Node*  succ = Packer::unpackPtr(exp);
        if (isMarked_(succ)) return false; // 已标记

        Node* markedSucc = getMarked_(succ);
        if (Packer::casBump(node_to_delete->nextSlot(0), exp, markedSucc,
                                   std::memory_order_release,
                                   std::memory_order_acquire)) {
            return true; // 标记成功
        }
        // 否则 exp 已刷新，重试
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

        if (!node_to_delete ||
            compare_(key, node_to_delete->key) || compare_(node_to_delete->key, key)) {
            return false; // 不存在
        }

        if (!tryMarkForRemoval_(node_to_delete)) {
            continue; // 别人先一步，重来
        }

        // 重新走一遍以触发物理删除（各层跳过）
        findNode_(key, prevs, nexts);

        ebr_manager_.retire(node_to_delete);
        return true;
    }
}
