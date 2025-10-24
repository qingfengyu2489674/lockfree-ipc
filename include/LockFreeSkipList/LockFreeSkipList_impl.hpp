#include "EBRManager/ebr.hpp"

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

template<typename K, typename V, typename C>
LockFreeSkipList<K, V, C>::LockFreeSkipList(EBRManager& ebr_manager)
    : ebr_manager_(ebr_manager), compare_(){
    // 创建一个逻辑上为“负无穷大”的键
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

template<typename K, typename V, typename C>
void LockFreeSkipList<K, V, C>::findNode_(const K& key, Node* prevs[], Node* nexts[]) {
retry:
    Node* pred = head_;

    for (int level = kMaxHeight - 1; level >= 0; --level) {
        // 1. 获取 pred 在当前层的后继节点
        Node* curr = getUnmarked_(pred->forward_[level].load(std::memory_order_acquire));

        // 2. 只要 curr 存在且其键值小于目标 key，就继续向右前进
        while (curr != nullptr && compare_(curr->key, key)) {
            pred = curr;
            curr = getUnmarked_(curr->forward_[level].load(std::memory_order_acquire));
        }

        // 3. 检查 curr 是否被标记。如果被标记，说明链表结构已变，需要重试
        if (curr != nullptr) {
            Node* next_ptr = curr->forward_[level].load(std::memory_order_acquire);
            if (isMarked_(next_ptr)) {
                // 帮助解链接并从头开始整个查找过程
                helpUnlink_(pred, curr, level); 
                goto retry;
            }
        }
        
        // 4. 循环结束后:
        //    - pred 是最后一个 key < target_key 的节点
        //    - curr 是第一个 key >= target_key 的节点 (或者是 nullptr)
        prevs[level] = pred;
        nexts[level] = curr;
    }
}


template<typename K, typename V, typename C>
bool LockFreeSkipList<K, V, C>::find(const K& key, V& value) { // 移除 const
    ebr::Guard guard(ebr_manager_);

    Node* prevs[kMaxHeight];
    Node* nexts[kMaxHeight];
    findNode_(key, prevs, nexts);

    Node* node = nexts[0];
    if (node && !compare_(key, node->key) && !compare_(node->key, key)) { // 等价于 node->key == key
        value = node->value;
        // 再次检查节点是否在我们读取值后被标记删除了
        Node* next = node->forward_[0].load(std::memory_order_acquire);
        return !isMarked_(next);
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

        Node* node = nexts[0];
        if (node && !compare_(key, node->key) && !compare_(node->key, key)) {
            // 键已存在
            return false;
        }

        int height = random_height_();
        Node* new_node = Node::create(key, value, height);

        // 先链接好新节点的上层指针
        for (int i = 1; i < height; ++i) {
            new_node->forward_[i].store(nexts[i], std::memory_order_relaxed);
        }
        new_node->forward_[0].store(nexts[0], std::memory_order_relaxed);

        // 在第0层进行原子性的“提交”
        if (prevs[0]->forward_[0].compare_exchange_strong(nexts[0], new_node, std::memory_order_release)) {
            // 成功！现在尽力而为地链接上层
            for (int i = 1; i < height; ++i) {
                while(true) {
                    if (prevs[i]->forward_[i].compare_exchange_strong(nexts[i], new_node, std::memory_order_relaxed)) {
                        break; // cas操作成功，跳出这一层
                    }
                    findNode_(key, prevs, nexts); // 失败了更新快照，再次尝试
                    if (getUnmarked_(prevs[i]->forward_[i]) != nexts[i]) break; // 另一个节点被插入，放弃这一层
                }
            }
            return true;
        } else {
            // CAS 失败，有并发冲突。销毁我们创建的节点并重试。
            Node::destroy(new_node);
        }
    }
}



template<typename K, typename V, typename C>
bool LockFreeSkipList<K, V, C>::tryMarkForRemoval_(Node* node_to_delete) {
    for (int level = node_to_delete->height - 1; level >= 0; --level) {
        Node* next = node_to_delete->forward_[level].load(std::memory_order_acquire);
        while (!isMarked_(next)) {
            if (node_to_delete->forward_[level].compare_exchange_weak(next, getMarked_(next), std::memory_order_release)) {
                // 只要成功标记一层，逻辑删除就成功了
                if (level == 0) return true; 
                break; // 移动到下一层
            }
        }
    }
    return true;
}


template<typename K, typename V, typename C>
void LockFreeSkipList<K, V, C>::helpUnlink_(Node* prev, Node* marked_next, int level) {
    Node* unmarked = getUnmarked_(marked_next);
    Node* next_of_next = unmarked->forward_[level].load(std::memory_order_acquire);
    // 尝试将 prev 的指针从 marked_next 绕行到 next_of_next
    prev->forward_[level].compare_exchange_strong(marked_next, next_of_next, std::memory_order_release);
}


template<typename K, typename V, typename C>
bool LockFreeSkipList<K, V, C>::remove(const K& key) {
    ebr::Guard guard(ebr_manager_);
    
    Node* prevs[kMaxHeight];
    Node* nexts[kMaxHeight];
    Node* node_to_delete = nullptr;

    while (true) {
        findNode_(key, prevs, nexts);
        node_to_delete = nexts[0];

        if (!node_to_delete || (compare_(key, node_to_delete->key) || compare_(node_to_delete->key, key))) {
            // 键不存在
            return false;
        }

        // 尝试标记节点的所有层级以进行逻辑删除
        tryMarkForRemoval_(node_to_delete);
        
        // 逻辑删除后，帮助物理删除（解链接）
        // findNode_ 在下一次循环中会自动帮助解链接
        // 我们也可以在这里主动帮助
        for(int i = node_to_delete->height -1; i>=0; --i)
        {
             Node* next = node_to_delete->forward_[i].load(std::memory_order_acquire);
             if(!isMarked_(next)) continue; // Not our mark, or unlinked already.
             helpUnlink_(prevs[i], nexts[i], i);
        }

        ebr_manager_.retire(node_to_delete);
        return true;
    }
}

