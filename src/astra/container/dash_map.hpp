// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <absl/container/flat_hash_map.h>
#include <absl/hash/hash.h>
#include <absl/strings/string_view.h>
#include <absl/synchronization/mutex.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "astra/base/macros.hpp"

namespace astra::container {

// Hash function using Abseil (SwissTable hash)
struct DashHash {
  using is_transparent = void;

  template <typename T>
  size_t operator()(const T& value) const {
    return absl::Hash<T>{}(value);
  }
};

// String comparison for heterogeneous lookup (only for string keys)
template <typename Key>
struct StringEqual {
  bool operator()(const Key& lhs, const Key& rhs) const { return lhs == rhs; }
};

// Specialization for string types
template <>
struct StringEqual<std::string> {
  using is_transparent = void;

  bool operator()(absl::string_view lhs, absl::string_view rhs) const {
    return lhs == rhs;
  }

  bool operator()(absl::string_view lhs, const std::string& rhs) const {
    return lhs == rhs;
  }

  bool operator()(const std::string& lhs, absl::string_view rhs) const {
    return lhs == rhs;
  }

  bool operator()(const std::string& lhs, const std::string& rhs) const {
    return lhs == rhs;
  }
};

// DashMap - Zero-overhead concurrent hash map
// Uses segmented sharding for lock-free reads and minimal lock contention
template <typename Key, typename Value>
class DashMap {
 public:
  using KeyType = Key;
  using ValueType = Value;
  using EqualType = StringEqual<Key>;
  using MapType = absl::flat_hash_map<Key, Value, DashHash, EqualType>;

  explicit DashMap(size_t num_shards = 16) : size_(0) {
    shards_.reserve(num_shards);
    for (size_t i = 0; i < num_shards; ++i) {
      shards_.push_back(std::make_unique<Shard>());
    }
  }

  ~DashMap() = default;

  // Non-copyable, non-movable
  DashMap(const DashMap&) = delete;
  DashMap& operator=(const DashMap&) = delete;
  DashMap(DashMap&&) = delete;
  DashMap& operator=(DashMap&&) = delete;

  // Insert or update a key-value pair
  // Returns true if a new key was inserted, false if updated
  bool Insert(const Key& key, const Value& value) {
    size_t shard_index = GetShardIndex(key);
    Shard* shard = shards_[shard_index].get();

    absl::MutexLock lock(&shard->mutex);
    auto [it, inserted] = shard->map.insert_or_assign(key, value);
    if (inserted) {
      size_.fetch_add(1, std::memory_order_relaxed);
    }
    return inserted;
  }

  // Get value by key
  // Returns true if found, false otherwise
  bool Get(const Key& key, Value* out_value) const {
    size_t shard_index = GetShardIndex(key);
    const Shard* shard = shards_[shard_index].get();

    absl::ReaderMutexLock lock(&shard->mutex);
    auto it = shard->map.find(key);
    if (it != shard->map.end()) {
      if (out_value) {
        *out_value = it->second;
      }
      return true;
    }
    return false;
  }

  // Remove a key
  // Returns true if the key was removed, false if not found
  bool Remove(const Key& key) {
    size_t shard_index = GetShardIndex(key);
    Shard* shard = shards_[shard_index].get();

    absl::MutexLock lock(&shard->mutex);
    auto erased = shard->map.erase(key);
    if (erased > 0) {
      size_.fetch_sub(1, std::memory_order_relaxed);
      return true;
    }
    return false;
  }

  // Check if a key exists
  bool Contains(const Key& key) const {
    size_t shard_index = GetShardIndex(key);
    const Shard* shard = shards_[shard_index].get();

    absl::ReaderMutexLock lock(&shard->mutex);
    return shard->map.contains(key);
  }

  // Get the number of elements
  size_t Size() const { return size_.load(std::memory_order_relaxed); }

  // Check if the map is empty
  bool Empty() const { return Size() == 0; }

  // Clear all elements
  void Clear() {
    for (auto& shard : shards_) {
      absl::MutexLock lock(&shard->mutex);
      shard->map.clear();
    }
    size_.store(0, std::memory_order_relaxed);
  }

  // Get all keys
  std::vector<Key> GetAllKeys() const {
    std::vector<Key> keys;
    for (auto& shard : shards_) {
      absl::ReaderMutexLock lock(&shard->mutex);
      keys.reserve(keys.size() + shard->map.size());
      for (const auto& [key, _] : shard->map) {
        keys.push_back(key);
      }
    }
    return keys;
  }

  // Get all key-value pairs
  std::vector<std::pair<Key, Value>> GetAllKeyValuePairs() const {
    std::vector<std::pair<Key, Value>> pairs;
    for (auto& shard : shards_) {
      absl::ReaderMutexLock lock(&shard->mutex);
      pairs.reserve(pairs.size() + shard->map.size());
      for (const auto& [key, value] : shard->map) {
        pairs.emplace_back(key, value);
      }
    }
    return pairs;
  }

  // Get total number of shards
  size_t NumShards() const { return shards_.size(); }

  // For string keys, allow heterogeneous lookup
  template <typename K = Key>
  std::enable_if_t<std::is_same_v<K, std::string>, bool> Insert(
      const absl::string_view key, const Value& value) {
    return Insert(std::string(key), value);
  }

  template <typename K = Key>
  std::enable_if_t<std::is_same_v<K, std::string>, bool> Get(
      const absl::string_view key, Value* out_value) const {
    size_t shard_index = GetShardIndex(absl::Hash<absl::string_view>{}(key));
    const Shard* shard = shards_[shard_index].get();

    absl::ReaderMutexLock lock(&shard->mutex);
    auto it = shard->map.find(key);
    if (it != shard->map.end()) {
      if (out_value) {
        *out_value = it->second;
      }
      return true;
    }
    return false;
  }

  template <typename K = Key>
  std::enable_if_t<std::is_same_v<K, std::string>, bool> Remove(
      const absl::string_view key) {
    return Remove(std::string(key));
  }

  template <typename K = Key>
  std::enable_if_t<std::is_same_v<K, std::string>, bool> Contains(
      const absl::string_view key) const {
    size_t shard_index = GetShardIndex(absl::Hash<absl::string_view>{}(key));
    const Shard* shard = shards_[shard_index].get();

    absl::ReaderMutexLock lock(&shard->mutex);
    return shard->map.find(key) != shard->map.end();
  }

 private:
  struct Shard {
    MapType map;
    mutable absl::Mutex mutex;
  };

  size_t GetShardIndex(const Key& key) const {
    size_t hash_value = DashHash{}(key);
    return hash_value % shards_.size();
  }

  size_t GetShardIndex(size_t hash_value) const {
    return hash_value % shards_.size();
  }

  std::vector<std::unique_ptr<Shard>> shards_;
  std::atomic<size_t> size_;
};

// StringMap - Specialized DashMap for string keys
using StringMap = DashMap<std::string, std::string>;

// DashSet - Set version of DashMap
template <typename Key>
class DashSet {
 public:
  explicit DashSet(size_t num_shards = 16) : map_(num_shards) {}

  bool Insert(const Key& key) { return map_.Insert(key, Dummy{}); }

  bool Contains(const Key& key) const { return map_.Contains(key); }

  bool Remove(const Key& key) { return map_.Remove(key); }

  size_t Size() const { return map_.Size(); }

  bool Empty() const { return map_.Empty(); }

  void Clear() { map_.Clear(); }

  std::vector<Key> GetAll() const { return map_.GetAllKeys(); }

 private:
  struct Dummy {};
  DashMap<Key, Dummy> map_;
};

// StringSet - Specialized DashSet for strings
using StringSet = DashSet<std::string>;

}  // namespace astra::container
