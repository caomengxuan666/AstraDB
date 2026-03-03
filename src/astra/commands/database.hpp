// ==============================================================================
// Database Interface
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include <string>
#include <optional>
#include <vector>
#include <memory>
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include "astra/container/dash_map.hpp"
#include "astra/container/zset/btree_zset.hpp"
#include "astra/container/linked_list.hpp"
#include "astra/storage/key_metadata.hpp"
#include "astra/protocol/resp/resp_types.hpp"

namespace astra::commands {

// String value (no expiration here - use metadata instead)
struct StringValue {
  std::string value;

  StringValue() = default;
  explicit StringValue(std::string v) : value(std::move(v)) {}
};

// Key-value database
class Database {
 public:
  using StringMap = astra::container::DashMap<std::string, StringValue>;
  using SetType = astra::container::DashSet<std::string>;
  using HashType = astra::container::DashMap<std::string, std::string>;
  using ZSetType = astra::container::ZSet<std::string, double>;
  using ListType = astra::container::StringList;

  Database() = default;
  ~Database() = default;

  // Disable copy and move
  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;

  // ========== String Operations ==========

  bool Set(const std::string& key, StringValue value) {
    strings_.Insert(key, std::move(value));
    metadata_manager_.RegisterKey(key, astra::storage::KeyType::kString);
    return true;
  }

  bool Set(const std::string& key, const std::string& value) {
    StringValue str_value(value);
    return Set(key, std::move(str_value));
  }

  std::optional<StringValue> Get(const std::string& key) {
    // Check if key exists and is not expired
    if (!metadata_manager_.IsValid(key)) {
      return std::nullopt;
    }
    
    StringValue value;
    if (strings_.Get(key, &value)) {
      return value;
    }
    return std::nullopt;
  }

  bool Del(const std::string& key) {
    metadata_manager_.UnregisterKey(key);
    return strings_.Remove(key) || 
           hashes_.Remove(key) ||
           sets_.Remove(key) ||
           zsets_.Remove(key);
  }

  size_t Del(const std::vector<std::string>& keys) {
    std::atomic<size_t> count{0};
    
    // Use TBB parallel_for for large key sets
    if (keys.size() > 100) {
      tbb::parallel_for(tbb::blocked_range<size_t>(0, keys.size()),
        [&](const tbb::blocked_range<size_t>& range) {
          for (size_t i = range.begin(); i != range.end(); ++i) {
            if (Del(keys[i])) {
              count.fetch_add(1, std::memory_order_relaxed);
            }
          }
        });
    } else {
      // Sequential for small key sets
      for (const auto& key : keys) {
        if (Del(key)) {
          ++count;
        }
      }
    }
    
    return count.load();
  }

  bool Exists(const std::string& key) {
    return metadata_manager_.IsValid(key);
  }

  std::vector<std::optional<std::string>> MGet(const std::vector<std::string>& keys) {
    std::vector<std::optional<std::string>> results(keys.size());
    
    // Use TBB parallel_for for large key sets
    if (keys.size() > 100) {
      tbb::parallel_for(tbb::blocked_range<size_t>(0, keys.size()),
        [&](const tbb::blocked_range<size_t>& range) {
          for (size_t i = range.begin(); i != range.end(); ++i) {
            auto value = Get(keys[i]);
            if (value.has_value()) {
              results[i] = value->value;
            } else {
              results[i] = std::nullopt;
            }
          }
        });
    } else {
      // Sequential for small key sets
      for (size_t i = 0; i < keys.size(); ++i) {
        auto value = Get(keys[i]);
        if (value.has_value()) {
          results[i] = value->value;
        } else {
          results[i] = std::nullopt;
        }
      }
    }
    
    return results;
  }

  // ========== Hash Operations ==========

  bool HSet(const std::string& key, const std::string& field, const std::string& value) {
    auto hash = GetHash(key);
    if (!hash) {
      hash = std::make_shared<HashType>(16);
      hashes_.Insert(key, hash);
      metadata_manager_.RegisterKey(key, astra::storage::KeyType::kHash);
    }
    return hash->Insert(field, value);
  }

  std::optional<std::string> HGet(const std::string& key, const std::string& field) {
    if (!metadata_manager_.IsValid(key)) {
      return std::nullopt;
    }
    
    auto hash = GetHash(key);
    if (!hash) {
      return std::nullopt;
    }
    std::string value;
    if (hash->Get(field, &value)) {
      return value;
    }
    return std::nullopt;
  }

  bool HDel(const std::string& key, const std::string& field) {
    if (!metadata_manager_.IsValid(key)) {
      return false;
    }
    
    auto hash = GetHash(key);
    if (!hash) {
      return false;
    }
    return hash->Remove(field);
  }

  bool HExists(const std::string& key, const std::string& field) {
    if (!metadata_manager_.IsValid(key)) {
      return false;
    }
    
    auto hash = GetHash(key);
    if (!hash) {
      return false;
    }
    return hash->Contains(field);
  }

  size_t HDel(const std::string& key, const std::vector<std::string>& fields) {
    if (!metadata_manager_.IsValid(key)) {
      return 0;
    }
    
    auto hash = GetHash(key);
    if (!hash) {
      return 0;
    }
    size_t count = 0;
    for (const auto& field : fields) {
      if (hash->Remove(field)) {
        ++count;
      }
    }
    return count;
  }

  std::vector<std::pair<std::string, std::string>> HGetAll(const std::string& key) {
    if (!metadata_manager_.IsValid(key)) {
      return {};
    }
    
    auto hash = GetHash(key);
    if (!hash) {
      return {};
    }
    // Need to iterate through hash and get all field-value pairs
    // For now, return empty - will need to implement proper iteration
    return {};
  }

  size_t HLen(const std::string& key) {
    if (!metadata_manager_.IsValid(key)) {
      return 0;
    }
    
    auto hash = GetHash(key);
    if (!hash) {
      return 0;
    }
    return hash->Size();
  }

  // ========== Set Operations ==========

  bool SAdd(const std::string& key, const std::string& member) {
    auto set = GetSet(key);
    if (!set) {
      set = std::make_shared<SetType>(16);
      sets_.Insert(key, set);
      metadata_manager_.RegisterKey(key, astra::storage::KeyType::kSet);
    }
    return set->Insert(member);
  }

  bool SRem(const std::string& key, const std::string& member) {
    if (!metadata_manager_.IsValid(key)) {
      return false;
    }
    
    auto set = GetSet(key);
    if (!set) {
      return false;
    }
    return set->Remove(member);
  }

  bool SIsMember(const std::string& key, const std::string& member) {
    if (!metadata_manager_.IsValid(key)) {
      return false;
    }
    
    auto set = GetSet(key);
    if (!set) {
      return false;
    }
    return set->Contains(member);
  }

  std::vector<std::string> SMembers(const std::string& key) {
    if (!metadata_manager_.IsValid(key)) {
      return {};
    }
    
    auto set = GetSet(key);
    if (!set) {
      return {};
    }
    return set->GetAll();
  }

  size_t SCard(const std::string& key) {
    if (!metadata_manager_.IsValid(key)) {
      return 0;
    }
    
    auto set = GetSet(key);
    if (!set) {
      return 0;
    }
    return set->Size();
  }

  // ========== Sorted Set Operations ==========

  bool ZAdd(const std::string& key, double score, const std::string& member) {
    auto zset = GetZSet(key);
    if (!zset) {
      zset = std::make_shared<ZSetType>(1024);
      zsets_.Insert(key, zset);
      metadata_manager_.RegisterKey(key, astra::storage::KeyType::kZSet);
    }
    return zset->Add(member, score);
  }

  std::optional<double> ZScore(const std::string& key, const std::string& member) {
    if (!metadata_manager_.IsValid(key)) {
      return std::nullopt;
    }
    
    auto zset = GetZSet(key);
    if (!zset) {
      return std::nullopt;
    }
    return zset->GetScore(member);
  }

  std::vector<std::pair<std::string, double>> ZRange(
      const std::string& key, int64_t start, int64_t stop, bool with_scores = false) {
    if (!metadata_manager_.IsValid(key)) {
      return {};
    }
    
    auto zset = GetZSet(key);
    if (!zset) {
      return {};
    }
    
    uint64_t size = zset->Size();
    if (size == 0) {
      return {};
    }
    
    // Convert negative indices to positive
    if (start < 0) {
      start = static_cast<int64_t>(size) + start;
      if (start < 0) start = 0;
    }
    
    if (stop < 0) {
      stop = static_cast<int64_t>(size) + stop;
      if (stop < 0) stop = -1;
    }
    
    // Clamp to valid range
    if (start >= static_cast<int64_t>(size)) {
      return {};
    }
    
    if (stop >= static_cast<int64_t>(size)) {
      stop = static_cast<int64_t>(size) - 1;
    }
    
    return zset->GetRangeByRank(
        static_cast<uint64_t>(start),
        static_cast<uint64_t>(stop),
        false,
        with_scores
    );
  }

  bool ZRem(const std::string& key, const std::string& member) {
    if (!metadata_manager_.IsValid(key)) {
      return false;
    }
    
    auto zset = GetZSet(key);
    if (!zset) {
      return false;
    }
    return zset->Remove(member);
  }

  size_t ZCard(const std::string& key) {
    if (!metadata_manager_.IsValid(key)) {
      return 0;
    }
    
    auto zset = GetZSet(key);
    if (!zset) {
      return 0;
    }
    return zset->Size();
  }

  uint64_t ZCount(const std::string& key, double min, double max) {
    if (!metadata_manager_.IsValid(key)) {
      return 0;
    }
    
    auto zset = GetZSet(key);
    if (!zset) {
      return 0;
    }
    return zset->CountRange(min, max);
  }

  // ========== List Operations ==========

  bool LPush(const std::string& key, const std::string& value) {
    auto list = GetList(key);
    if (!list) {
      list = std::make_shared<ListType>();
      lists_.Insert(key, list);
      metadata_manager_.RegisterKey(key, astra::storage::KeyType::kList);
    }
    list->PushLeft(value);
    return true;
  }

  bool RPush(const std::string& key, const std::string& value) {
    auto list = GetList(key);
    if (!list) {
      list = std::make_shared<ListType>();
      lists_.Insert(key, list);
      metadata_manager_.RegisterKey(key, astra::storage::KeyType::kList);
    }
    list->PushRight(value);
    return true;
  }

  std::optional<std::string> LPop(const std::string& key) {
    if (!metadata_manager_.IsValid(key)) {
      return std::nullopt;
    }
    
    auto list = GetList(key);
    if (!list) {
      return std::nullopt;
    }
    
    auto value = list->PopLeft();
    if (!value.has_value() || list->Empty()) {
      lists_.Remove(key);
      metadata_manager_.UnregisterKey(key);
    }
    return value;
  }

  std::optional<std::string> RPop(const std::string& key) {
    if (!metadata_manager_.IsValid(key)) {
      return std::nullopt;
    }
    
    auto list = GetList(key);
    if (!list) {
      return std::nullopt;
    }
    
    auto value = list->PopRight();
    if (!value.has_value() || list->Empty()) {
      lists_.Remove(key);
      metadata_manager_.UnregisterKey(key);
    }
    return value;
  }

  size_t LLen(const std::string& key) {
    if (!metadata_manager_.IsValid(key)) {
      return 0;
    }
    
    auto list = GetList(key);
    if (!list) {
      return 0;
    }
    return list->Size();
  }

  std::optional<std::string> LIndex(const std::string& key, int64_t index) {
    if (!metadata_manager_.IsValid(key)) {
      return std::nullopt;
    }
    
    auto list = GetList(key);
    if (!list) {
      return std::nullopt;
    }
    return list->Index(index);
  }

  bool LSet(const std::string& key, int64_t index, const std::string& value) {
    if (!metadata_manager_.IsValid(key)) {
      return false;
    }
    
    auto list = GetList(key);
    if (!list) {
      return false;
    }
    return list->Set(index, value);
  }

  std::vector<std::string> LRange(const std::string& key, int64_t start, int64_t stop) {
    if (!metadata_manager_.IsValid(key)) {
      return {};
    }
    
    auto list = GetList(key);
    if (!list) {
      return {};
    }
    return list->Range(start, stop);
  }

  bool LTrim(const std::string& key, int64_t start, int64_t stop) {
    if (!metadata_manager_.IsValid(key)) {
      return false;
    }
    
    auto list = GetList(key);
    if (!list) {
      return false;
    }
    
    list->Trim(start, stop);
    
    // Remove key if list is empty after trim
    if (list->Empty()) {
      lists_.Remove(key);
      metadata_manager_.UnregisterKey(key);
    }
    return true;
  }

  size_t LRem(const std::string& key, const std::string& value, int64_t count) {
    if (!metadata_manager_.IsValid(key)) {
      return 0;
    }
    
    auto list = GetList(key);
    if (!list) {
      return 0;
    }
    
    size_t removed = list->Remove(value, count);
    
    // Remove key if list is empty after removal
    if (list->Empty()) {
      lists_.Remove(key);
      metadata_manager_.UnregisterKey(key);
    }
    return removed;
  }

  bool LInsert(const std::string& key, int64_t pivot_index, const std::string& value, bool before = true) {
    if (!metadata_manager_.IsValid(key)) {
      return false;
    }
    
    auto list = GetList(key);
    if (!list) {
      return false;
    }
    return list->Insert(pivot_index, value, before);
  }

  std::optional<std::string> RPopLPush(const std::string& source, const std::string& destination) {
    // Pop from source
    auto value = RPop(source);
    if (!value.has_value()) {
      return std::nullopt;
    }
    
    // Push to destination
    RPush(destination, *value);
    return value;
  }

  // ========== TTL Operations ==========

  bool SetExpireSeconds(const std::string& key, int64_t seconds) {
    return metadata_manager_.SetExpireSeconds(key, seconds);
  }

  bool SetExpireMs(const std::string& key, int64_t ms) {
    return metadata_manager_.SetExpireMs(key, ms);
  }

  int64_t GetTtlSeconds(const std::string& key) {
    return metadata_manager_.GetTtlSeconds(key);
  }

  int64_t GetTtlMs(const std::string& key) {
    return metadata_manager_.GetTtlMs(key);
  }

  bool Persist(const std::string& key) {
    return metadata_manager_.Persist(key);
  }

  std::vector<std::string> GetExpiredKeys() {
    return metadata_manager_.GetExpiredKeys();
  }

  // ========== Utility Operations ==========

  size_t Size() const {
    return strings_.Size();
  }

  // Get key type
  std::optional<astra::storage::KeyType> GetType(const std::string& key) {
    return metadata_manager_.GetKeyType(key);
  }

  // Get all keys (for KEYS command)
  std::vector<std::string> GetAllKeys() {
    return metadata_manager_.GetAllKeys();
  }

  // Get database size (number of keys)
  size_t DbSize() const {
    return metadata_manager_.Size();
  }

  // Flush all data
  void Clear() {
    strings_.Clear();
    hashes_.Clear();
    sets_.Clear();
    zsets_.Clear();
    lists_.Clear();
    metadata_manager_.Clear();
  }

 private:
  std::shared_ptr<HashType> GetHash(const std::string& key) {
    std::shared_ptr<HashType> hash;
    if (hashes_.Get(key, &hash)) {
      return hash;
    }
    return nullptr;
  }

  std::shared_ptr<SetType> GetSet(const std::string& key) {
    std::shared_ptr<SetType> set;
    if (sets_.Get(key, &set)) {
      return set;
    }
    return nullptr;
  }

  std::shared_ptr<ZSetType> GetZSet(const std::string& key) {
    std::shared_ptr<ZSetType> zset;
    if (zsets_.Get(key, &zset)) {
      return zset;
    }
    return nullptr;
  }

  std::shared_ptr<ListType> GetList(const std::string& key) {
    std::shared_ptr<ListType> list;
    if (lists_.Get(key, &list)) {
      return list;
    }
    return nullptr;
  }

  StringMap strings_;
  astra::container::DashMap<std::string, std::shared_ptr<HashType>> hashes_;
  astra::container::DashMap<std::string, std::shared_ptr<SetType>> sets_;
  astra::container::DashMap<std::string, std::shared_ptr<ZSetType>> zsets_;
  astra::container::DashMap<std::string, std::shared_ptr<ListType>> lists_;
  astra::storage::KeyMetadataManager metadata_manager_;
};

// Database manager - manages multiple databases
class DatabaseManager {
 public:
  DatabaseManager(size_t num_dbs = 16) : num_dbs_(num_dbs) {
    databases_.reserve(num_dbs);
    for (size_t i = 0; i < num_dbs; ++i) {
      databases_.push_back(std::make_unique<Database>());
    }
  }

  ~DatabaseManager() = default;

  Database* GetDatabase(int index) {
    if (index < 0 || static_cast<size_t>(index) >= databases_.size()) {
      return nullptr;
    }
    return databases_[index].get();
  }

  size_t GetDatabaseCount() const {
    return databases_.size();
  }

 private:
  size_t num_dbs_;
  std::vector<std::unique_ptr<Database>> databases_;
};

}  // namespace astra::commands