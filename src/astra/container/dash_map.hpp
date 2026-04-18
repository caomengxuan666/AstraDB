// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <absl/container/flat_hash_map.h>
#include <absl/hash/hash.h>
#include <absl/strings/string_view.h>

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

// DashMap - Lock-free concurrent hash map (NO SHARING architecture)
// In NO SHARING architecture, each Worker has its own DashMap instance
// that is only accessed by that Worker's single Executor thread.
// Therefore, no locking is required - this is a simple wrapper around
// absl::flat_hash_map for API compatibility.
template <typename Key, typename Value>
class DashMap {
 public:
  using KeyType = Key;
  using ValueType = Value;
  using EqualType = StringEqual<Key>;
  using MapType = absl::flat_hash_map<Key, Value, DashHash, EqualType>;

  explicit DashMap(size_t initial_capacity = 16) : size_(0) {
    map_.reserve(initial_capacity);
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
    auto [it, inserted] = map_.insert_or_assign(key, value);
    if (inserted) {
      ++size_;
    }
    return inserted;
  }

  // Get value by key
  // Returns true if found, false otherwise
  bool Get(const Key& key, Value* out_value) const {
    auto it = map_.find(key);
    if (it != map_.end()) {
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
    auto erased = map_.erase(key);
    if (erased > 0) {
      --size_;
      return true;
    }
    return false;
  }

  // Check if a key exists
  bool Contains(const Key& key) const { return map_.contains(key); }

  // Get the number of elements
  size_t Size() const { return size_; }

  // Check if the map is empty
  bool Empty() const { return size_ == 0; }

  // Clear all elements
  void Clear() {
    map_.clear();
    size_ = 0;
  }

  // Get all keys
  std::vector<Key> GetAllKeys() const {
    std::vector<Key> keys;
    keys.reserve(map_.size());
    for (const auto& [key, _] : map_) {
      keys.push_back(key);
    }
    return keys;
  }

  // Get all key-value pairs
  std::vector<std::pair<Key, Value>> GetAllKeyValuePairs() const {
    std::vector<std::pair<Key, Value>> pairs;
    pairs.reserve(map_.size());
    for (const auto& [key, value] : map_) {
      pairs.emplace_back(key, value);
    }
    return pairs;
  }

  // Get total capacity (always 1 for compatibility, no sharding)
  size_t NumShards() const { return 1; }

  // For string keys, allow heterogeneous lookup
  template <typename K = Key>
  std::enable_if_t<std::is_same_v<K, std::string>, bool> Insert(
      const absl::string_view key, const Value& value) {
    return Insert(std::string(key), value);
  }

  template <typename K = Key>
  std::enable_if_t<std::is_same_v<K, std::string>, bool> Get(
      const absl::string_view key, Value* out_value) const {
    auto it = map_.find(key);
    if (it != map_.end()) {
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
    return map_.find(key) != map_.end();
  }

 private:
  MapType map_;
  mutable size_t size_;
};

// StringMap - Specialized DashMap for string keys
using StringMap = DashMap<std::string, std::string>;

// DashSet - Set version of DashMap (no locking needed)
template <typename Key>
class DashSet {
 public:
  explicit DashSet(size_t initial_capacity = 16) : map_(initial_capacity) {}

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
