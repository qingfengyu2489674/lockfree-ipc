#pragma once

#include "LockFreeChain.hpp"
#include "EBRManager/EBRManager.hpp"
#include "EBRManager/guard.hpp"

#include <memory>
#include <functional>
#include <optional>
#include <stdexcept>


template <typename K, 
          typename V, 
          typename Hash = std::hash<K>, 
          typename KeyEqual = std::equal_to<K>>
class LockFreeHashMap {
public:
    explicit LockFreeHashMap(size_t initial_bucket_count = 16);
    
    ~LockFreeHashMap();

    LockFreeHashMap(const LockFreeHashMap&) = delete;
    LockFreeHashMap& operator=(const LockFreeHashMap&) = delete;
    LockFreeHashMap(LockFreeHashMap&&) = delete;
    LockFreeHashMap& operator=(LockFreeHashMap&&) = delete;

    std::optional<V> find(const K& key);

    template <typename KeyType, typename ValueType>
    bool insert(KeyType&& key, ValueType&& value);

    bool remove(const K& key);

    size_t bucketCount() const noexcept;

private:
    using Chain = LockFreeChain<K, V, KeyEqual>;

    size_t getBucketIndex_(const K& key) const;
    static size_t roundUpToPowerOfTwo_(size_t n);

private:
    EBRManager ebr_;
    const size_t bucket_mask_;
    const size_t bucket_count_;
    std::unique_ptr<Chain[]> buckets_;
    Hash hasher_;
};


#include "LockFreeHashMap.impl.hpp"
