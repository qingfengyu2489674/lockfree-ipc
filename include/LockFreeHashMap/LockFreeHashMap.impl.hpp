#pragma once

#include "LockFreeHashMap.hpp"
#include <climits>


template <typename K, typename V, typename Hash, typename KeyEqual>
LockFreeHashMap<K, V, Hash, KeyEqual>::LockFreeHashMap(size_t initial_bucket_count)
    : ebr_(), 
      bucket_mask_(roundUpToPowerOfTwo_(initial_bucket_count) - 1),
      bucket_count_(bucket_mask_ + 1),
      buckets_(std::make_unique<Chain[]>(bucket_count_)),
      hasher_()
{
    if (initial_bucket_count == 0) {
        throw std::invalid_argument("initial_bucket_count cannot be zero.");
    }
}


template <typename K, typename V, typename Hash, typename KeyEqual>
LockFreeHashMap<K, V, Hash, KeyEqual>::~LockFreeHashMap() {}

// --- 公共成员函数 ---

template <typename K, typename V, typename Hash, typename KeyEqual>
std::optional<V> LockFreeHashMap<K, V, Hash, KeyEqual>::find(const K& key) {
    ebr::Guard guard(ebr_);
    size_t index = getBucketIndex_(key);
    return buckets_[index].find(key, ebr_);
}


template <typename K, typename V, typename Hash, typename KeyEqual>
template <typename KeyType, typename ValueType>
bool LockFreeHashMap<K, V, Hash, KeyEqual>::insert(KeyType&& key, ValueType&& value) {
    ebr::Guard guard(ebr_);
    // 注意：这里需要使用 key 的引用来计算哈希，而不是 std::forward<KeyType>(key)
    const K& key_ref = key; 
    size_t index = getBucketIndex_(key_ref);
    return buckets_[index].insert(std::forward<KeyType>(key), std::forward<ValueType>(value), ebr_);
}


template <typename K, typename V, typename Hash, typename KeyEqual>
bool LockFreeHashMap<K, V, Hash, KeyEqual>::remove(const K& key) {
    ebr::Guard guard(ebr_);
    size_t index = getBucketIndex_(key);
    return buckets_[index].remove(key, ebr_);
}

template <typename K, typename V, typename Hash, typename KeyEqual>
size_t LockFreeHashMap<K, V, Hash, KeyEqual>::bucketCount() const noexcept {
    return bucket_count_;
}

// --- 私有辅助函数 ---

template <typename K, typename V, typename Hash, typename KeyEqual>
size_t LockFreeHashMap<K, V, Hash, KeyEqual>::getBucketIndex_(const K& key) const {
    // 使用位运算替代取模，效率更高
    return hasher_(key) & bucket_mask_;
}

template <typename K, typename V, typename Hash, typename KeyEqual>
size_t LockFreeHashMap<K, V, Hash, KeyEqual>::roundUpToPowerOfTwo_(size_t n) {
    if (n == 0) return 1; // 至少为1，然后会变成2
    
    size_t power = 1;
    while (power < n) {
        power <<= 1;
    }
    return power;
}
