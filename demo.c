#include "EBRManager/ebr.hpp"

// getMarked_, isMarked_, getUnmarked_, get_random_engine_, random_height_ 函数保持不变
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
    int height = dist(get_random_engine_()) + 1;
    return std::min(height, kMaxHeight);
}


// 构造函数和析构函数保持不变
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


// [已修复] helpUnlink_ 函数
// 它的任务很纯粹：尝试将 prev 的 next 从 old_next CAS 到 new_next
// 返回 bool 表示是否成功，这对于某些复杂逻辑很有用
template<typename K, typename V, typename C>
bool LockFreeSkipList<K, V, C>::helpUnlink_(Node* prev, Node* old_next, Node* new_next, int level) {
    return prev->forward_[level].compare_exchange_strong(old_next, new_next, std::memory_order_release);
}


// [已修复] findNode_ 函数 (核心改动)
// 这是数据结构正确性的基石
template<typename K, typename V, typename C>
void LockFreeSkipList<K, V, C>::findNode_(const K& key, Node* prevs[], Node* nexts[]) {
retry:
    Node* pred = head_;

    for (int level = kMaxHeight - 1; level >= 0; --level) {
        Node* curr = pred->forward_[level].load(std::memory_order_acquire);

        while (true) {
            Node* succ = getUnmarked_(curr);
            if (succ == nullptr) { // 到达链表末尾
                break;
            }

            Node* succ_next = succ->forward_[level].load(std::memory_order_acquire);

            // 检查当前节点 succ 是否已被标记删除
            if (isMarked_(succ_next)) {
                // 帮助物理删除，尝试将 pred 的指针从 curr(可能带标记) 绕行到 succ_next(不带标记)
                // 注意：CAS的期望值是 curr，而不是 succ
                if (!helpUnlink_(pred, curr, getUnmarked_(succ_next), level)) {
                    // 如果帮助失败，说明 pred 和 curr 之间的关系已被其他线程改变
                    // 整个查找路径可能已失效，必须从头开始
                    goto retry;
                }
                // 帮助成功，pred 不变，重新加载 pred 的后继节点继续查找
                curr = pred->forward_[level].load(std::memory_order_acquire);
            } else {
                // 节点 succ 未被标记，是有效节点
                if (compare_(succ->key, key)) {
                    // key 仍然太小，继续向右前进
                    pred = succ;
                    curr = succ_next;
                } else {
                    // 找到了第一个 key >= 目标 key 的节点
                    break;
                }
            }
        }
        
        prevs[level] = pred;
        nexts[level] = curr; // curr 是那个可能带标记的指针
    }
}

// find 函数保持不变
template<typename K, typename V, typename C>
bool LockFreeSkipList<K, V, C>::find(const K& key, V& value) {
    ebr::Guard guard(ebr_manager_);
    Node* prevs[kMaxHeight];
    Node* nexts[kMaxHeight];
    findNode_(key, prevs, nexts);

    Node* node = getUnmarked_(nexts[0]); // 要用 getUnmarked_ 获取真实节点
    if (node && !compare_(key, node->key) && !compare_(node->key, key)) {
        value = node->value;
        // 再次检查节点是否在我们读取值后被标记删除了
        Node* final_check = node->forward_[0].load(std::memory_order_acquire);
        return !isMarked_(final_check);
    }
    return false;
}

// insert 函数保持不变，它的逻辑相对是健壮的
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
        
        // 链接新节点的指针
        for (int i = 0; i < height; ++i) {
            new_node->forward_[i].store(getUnmarked_(nexts[i]), std::memory_order_relaxed);
        }
        
        // 在第0层进行原子性的“提交”
        if (prevs[0]->forward_[0].compare_exchange_strong(nexts[0], new_node, std::memory_order_release)) {
            // 成功！现在尽力而为地链接上层
            for (int i = 1; i < height; ++i) {
                while(true) {
                    findNode_(key, prevs, nexts); // 重新定位以获得最新的 prevs 和 nexts
                    
                    if (prevs[i]->forward_[i].compare_exchange_strong(nexts[i], new_node, std::memory_order_release)) {
                        break; 
                    }
                    
                    // 如果 CAS 失败，检查 new_node 是否已被其他线程链接（这不太可能，但作为防御）
                    // 或者检查我们的 prevs[i] 是否已经过时
                    Node* current_next = getUnmarked_(prevs[i]->forward_[i].load());
                    if (current_next && compare_(new_node->key, current_next->key)) {
                        // 有新节点插入在我们和目标位置之间，我们可能需要放弃
                        break; 
                    }
                }
            }
            return true;
        } else {
            Node::destroy(new_node);
        }
    }
}


// [已修复] tryMarkForRemoval_ 函数
// 它的目标是成功标记 level 0，并返回是否是自己标记成功的
template<typename K, typename V, typename C>
bool LockFreeSkipList<K, V, C>::tryMarkForRemoval_(Node* node_to_delete, int height) {
    // 从上到下标记所有层
    for (int level = height - 1; level >= 1; --level) {
        Node* succ = node_to_delete->forward_[level].load(std::memory_order_acquire);
        while (!isMarked_(succ)) {
            node_to_delete->forward_[level].compare_exchange_weak(succ, getMarked_(succ), std::memory_order_release);
            // 不管上层是否标记成功，继续往下
            succ = node_to_delete->forward_[level].load(std::memory_order_acquire);
        }
    }

    // 在 level 0 进行决定性标记
    Node* succ_l0 = node_to_delete->forward_[0].load(std::memory_order_acquire);
    while (true) {
        if (isMarked_(succ_l0)) {
            return false; // 已被其他线程标记
        }
        if (node_to_delete->forward_[0].compare_exchange_weak(succ_l0, getMarked_(succ_l0), std::memory_order_release)) {
            return true; // 我们成功标记了
        }
        // CAS失败，succ_l0被更新为当前值，用新值重试
    }
}


// [已修复] remove 函数
template<typename K, typename V, typename C>
bool LockFreeSkipList<K, V, C>::remove(const K& key) {
    ebr::Guard guard(ebr_manager_);
    
    Node* prevs[kMaxHeight];
    Node* nexts[kMaxHeight];
    Node* node_to_delete = nullptr;

    while (true) {
        findNode_(key, prevs, nexts);
        node_to_delete = getUnmarked_(nexts[0]);

        if (!node_to_delete || (compare_(key, node_to_delete->key) || compare_(node_to_delete->key, key))) {
            return false;
        }
        
        // 尝试标记节点，如果失败（说明已被别人标记），让 findNode_ 在下一轮清理
        if (!tryMarkForRemoval_(node_to_delete, node_to_delete->height)) {
            continue;
        }
        
        // 成功标记后，提交给 EBR 回收。物理删除将由所有线程通过 findNode_ 协作完成。
        ebr_manager_.retire(node_to_delete);
        return true;
    }
}