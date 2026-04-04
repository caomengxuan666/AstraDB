// ==============================================================================
// Database Interface
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <absl/random/random.h>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "astra/container/dash_map.hpp"
#include "astra/container/linked_list.hpp"
#include "astra/container/stream_data.hpp"
#include "astra/container/zset/btree_zset.hpp"
#include "astra/core/memory/eviction_manager.hpp"
#include "astra/core/memory/memory_tracker.hpp"
#include "astra/core/memory/object_size_estimator.hpp"
#include "astra/core/memory/string_pool.hpp"
#include "astra/persistence/data_serializer.hpp"
#include "astra/persistence/rocksdb_adapter.hpp"
#include "astra/protocol/resp/resp_types.hpp"
#include "astra/storage/key_metadata.hpp"

// Forward declarations
namespace astra::server {
class WorkerScheduler;
}

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
  // Callback for batch cross-worker requests
  struct BatchRequestContext {
    size_t worker_id;    // This worker's ID
    size_t num_workers;  // Total number of workers
    std::function<std::vector<std::string>(
        size_t target_worker, const std::string& cmd_type,
        const std::vector<std::string>& keys,
        const std::vector<std::string>& args)>
        send_request;
  };

  using BatchRequestCallback = std::function<std::vector<std::string>(
      const std::string& cmd_type, const std::vector<std::string>& keys,
      const std::vector<std::string>& args)>;

  using StringMap = astra::container::DashMap<std::string, StringValue>;
  using SetType = astra::container::DashSet<std::string>;
  using HashType = astra::container::DashMap<std::string, std::string>;
  using ZSetType = astra::container::ZSet<std::string, double>;
  using ListType = astra::container::StringList;

  Database() : string_pool_(std::make_unique<core::memory::StringPool>()) {}
  ~Database() = default;

  // Disable copy and move
  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;

  // Set batch request callback (for multi-key commands)
  void SetBatchRequestCallback(BatchRequestCallback callback) {
    batch_request_callback_ = std::move(callback);
  }

  // Set AOF callback (for persistence)
  void SetAofCallback(std::function<void(const std::string&)> callback) {
    aof_callback_ = std::move(callback);
  }

  // ========== String Pool Optimization ==========

  // Get pooled string view from pool (optimized for frequent keys)
  std::string_view GetPooledString(const std::string& str) {
    return string_pool_->AllocateString(str.data(), str.size());
  }

  // Get pooled string view from pool
  std::string_view GetPooledString(const char* data, size_t len) {
    return string_pool_->AllocateString(data, len);
  }

  // Get string pool for statistics
  core::memory::StringPool* GetStringPool() { return string_pool_.get(); }

  // ========== Memory Tracking ==========

  // Set memory tracker (called by Shard)
  void SetMemoryTracker(core::memory::MemoryTracker* tracker) {
    memory_tracker_ = tracker;
  }

  // Get memory tracker
  core::memory::MemoryTracker* GetMemoryTracker() const {
    return memory_tracker_;
  }

  // Set RocksDB adapter for cold data storage (called by Shard)
  void SetRocksDBAdapter(persistence::RocksDBAdapter* adapter) {
    rocksdb_adapter_ = adapter;
  }

  // Get RocksDB adapter
  persistence::RocksDBAdapter* GetRocksDBAdapter() const {
    return rocksdb_adapter_;
  }

  // Initialize eviction manager (called after SetMemoryTracker)
  void InitializeEvictionManager(core::memory::GetTotalMemoryCallback
                                     get_total_memory_callback = nullptr) {
    if (memory_tracker_ && !eviction_manager_) {
      eviction_manager_ = std::make_unique<core::memory::EvictionManager>(
          memory_tracker_, &metadata_manager_,
          std::move(get_total_memory_callback));

      // Set eviction callback to delete keys from all data structures
      eviction_manager_->SetEvictionCallback(
          [this](const std::string& key, astra::storage::KeyType type) -> bool {
            return this->EvictKey(key, type);
          });
    }
  }

  // Evict a key (called by EvictionManager)
  bool EvictKey(const std::string& key, astra::storage::KeyType type) {
    (void)type;  // Type is already known from metadata

    ASTRADB_LOG_DEBUG("EvictKey: attempting to evict key: {}", key);

    // Save to RocksDB before removal (if enabled)
    if (rocksdb_adapter_) {
      auto* serializer = persistence::SerializerFactory::GetSerializer(type);
      if (serializer) {
        bool success = false;
        std::string serialized;

        switch (type) {
          case astra::storage::KeyType::kString: {
            StringValue value;
            if (strings_.Get(key, &value)) {
              serialized = serializer->Serialize(key, &value.value);
              success = !serialized.empty();
            }
            break;
          }
          case astra::storage::KeyType::kHash: {
            auto hash_data = hashes_.GetAllKeyValuePairs();
            serialized = serializer->Serialize(key, &hash_data);
            success = !serialized.empty();
            break;
          }
          case astra::storage::KeyType::kSet: {
            auto set_data = sets_.GetAllKeys();
            serialized = serializer->Serialize(key, &set_data);
            success = !serialized.empty();
            break;
          }
          case astra::storage::KeyType::kZSet: {
            std::shared_ptr<ZSetType> zset;
            if (zsets_.Get(key, &zset)) {
              auto zset_data = zset->GetRangeByRank(0, -1, false, true);
              serialized = serializer->Serialize(key, &zset_data);
              success = !serialized.empty();
            }
            break;
          }
          case astra::storage::KeyType::kList: {
            std::shared_ptr<ListType> list;
            if (lists_.Get(key, &list)) {
              auto list_data = list->Range(0, -1);
              serialized = serializer->Serialize(key, &list_data);
              success = !serialized.empty();
            }
            break;
          }
          case astra::storage::KeyType::kStream: {
            // TODO: Implement stream serialization
            ASTRADB_LOG_WARN(
                "EvictKey: Stream type not yet supported for RocksDB");
            break;
          }
          default:
            break;
        }

        if (success && !serialized.empty()) {
          if (!rocksdb_adapter_->Put(key, serialized)) {
            ASTRADB_LOG_WARN("EvictKey: failed to write key to RocksDB: {}",
                             key);
          } else {
            ASTRADB_LOG_DEBUG("EvictKey: saved key to RocksDB: {}", key);
          }
        }
      }
    }

    // Remove from all data structures
    bool removed = strings_.Remove(key) || hashes_.Remove(key) ||
                   sets_.Remove(key) || zsets_.Remove(key) ||
                   lists_.Remove(key) || streams_.Remove(key);

    if (removed) {
      // Get estimated size before deletion
      uint32_t estimated_size = metadata_manager_.GetEstimatedSize(key);
      uint32_t metadata_size =
          core::memory::ObjectSizeEstimator::EstimateMetadataSize(key);

      ASTRADB_LOG_DEBUG(
          "EvictKey: successfully removed key: {}, estimated_size={}, "
          "metadata_size={}",
          key, estimated_size, metadata_size);

      // Subtract memory from tracker
      if (memory_tracker_) {
        memory_tracker_->SubtractMemory(estimated_size + metadata_size);
      }

      // Remove from metadata
      metadata_manager_.UnregisterKey(key);

      return true;
    }

    ASTRADB_LOG_DEBUG("EvictKey: failed to remove key: {}", key);
    return false;
  }

  // ========== Stream Operations ==========

  StreamData* GetStream(const std::string& key) {
    std::shared_ptr<StreamData> stream;
    if (streams_.Get(key, &stream)) {
      return stream.get();
    }
    return nullptr;
  }

  StreamData* GetOrCreateStream(const std::string& key) {
    std::shared_ptr<StreamData> stream;
    if (streams_.Get(key, &stream)) {
      return stream.get();
    }

    // Create new stream
    stream = std::make_shared<StreamData>();
    streams_.Insert(key, stream);
    metadata_manager_.RegisterKey(
        key, astra::storage::KeyType::kString);  // Use String type for now
    return stream.get();
  }

  // ========== String Operations ==========

  bool Set(const std::string& key, StringValue value) {
    // Get old value for memory tracking
    StringValue old_value;
    bool key_existed = strings_.Get(key, &old_value);
    const std::string& old_value_str = key_existed ? old_value.value : "";
    const std::string& new_value_str = value.value;

    // Update metadata (register key first, so
    // metadata_manager_.UpdateEstimatedSize can find it)
    strings_.Insert(key, std::move(value));
    metadata_manager_.RegisterKey(key, astra::storage::KeyType::kString);
    metadata_manager_.UpdateAccessInfo(key);

    // Update memory tracker using helper (after key is registered)
    if (memory_tracker_) {
      core::memory::MemoryTrackerHelper::UpdateString(
          memory_tracker_, &metadata_manager_, key, old_value_str,
          new_value_str);
    }

    // Check and perform eviction if needed (performance optimized)
    // Only check eviction when memory is close to threshold to avoid
    // performance impact
    if (eviction_manager_ && memory_tracker_ &&
        memory_tracker_->ShouldCheckEviction()) {
      eviction_manager_->CheckAndEvict();
    }

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
      // Update access time for LRU/LFU
      metadata_manager_.UpdateAccessInfo(key);

      // Record access for 2Q strategy if active
      if (eviction_manager_ && memory_tracker_ &&
          memory_tracker_->GetEvictionPolicy() ==
              core::memory::EvictionPolicy::k2Q) {
        // Access to 2Q strategy is handled via callback
        // We'll add this later
      }

      return value;
    }
    return std::nullopt;
  }

  // GETRANGE key start end - Get substring of string value
  std::string GetRange(const std::string& key, int64_t start, int64_t end) {
    auto value = Get(key);
    if (!value.has_value()) {
      return "";
    }

    const std::string& str = value->value;
    int64_t len = static_cast<int64_t>(str.size());

    // Handle negative indices
    if (start < 0) {
      start = len + start;
    }
    if (end < 0) {
      end = len + end;
    }

    // Clamp to valid range
    if (start < 0) {
      start = 0;
    }
    if (end >= len) {
      end = len - 1;
    }

    if (start > end || start >= len) {
      return "";
    }

    return str.substr(start, end - start + 1);
  }

  // SETRANGE key offset value - Overwrite part of string
  size_t SetRange(const std::string& key, int64_t offset,
                  const std::string& value) {
    auto existing = Get(key);
    std::string str;

    if (existing.has_value()) {
      str = existing->value;
    }

    // Extend string if needed
    if (offset < 0) {
      return str.size();  // Invalid offset
    }

    size_t new_len = offset + value.size();
    if (new_len > str.size()) {
      str.resize(new_len, '\0');
    }

    // Copy value at offset
    std::copy(value.begin(), value.end(), str.begin() + offset);

    Set(key, str);
    return str.size();
  }

  bool Del(const std::string& key) {
    // Get estimated size before deletion
    uint32_t estimated_size = metadata_manager_.GetEstimatedSize(key);
    uint32_t metadata_size =
        core::memory::ObjectSizeEstimator::EstimateMetadataSize(key);

    // Remove from all data structures
    bool removed = strings_.Remove(key) || hashes_.Remove(key) ||
                   sets_.Remove(key) || zsets_.Remove(key) ||
                   lists_.Remove(key) || streams_.Remove(key);

    if (removed && memory_tracker_) {
      // Subtract memory from tracker (data + metadata)
      memory_tracker_->SubtractMemory(estimated_size + metadata_size);
    }

    metadata_manager_.UnregisterKey(key);
    return removed;
  }

  size_t Del(const std::vector<std::string>& keys) {
    std::atomic<size_t> count{0};

    // Use TBB parallel_for for large key sets
    if (keys.size() > 100) {
      tbb::parallel_for(tbb::blocked_range<size_t>(0, keys.size()),
                        [&](const tbb::blocked_range<size_t>& range) {
                          for (size_t i = range.begin(); i != range.end();
                               ++i) {
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

  bool Exists(const std::string& key) { return metadata_manager_.IsValid(key); }

  // Get key version for WATCH support
  uint64_t GetKeyVersion(const std::string& key) {
    return metadata_manager_.GetKeyVersion(key);
  }

  std::vector<std::optional<std::string>> MGet(
      const std::vector<std::string>& keys) {
    std::vector<std::optional<std::string>> results(keys.size());

    // Use TBB parallel_for for large key sets
    if (keys.size() > 100) {
      tbb::parallel_for(tbb::blocked_range<size_t>(0, keys.size()),
                        [&](const tbb::blocked_range<size_t>& range) {
                          for (size_t i = range.begin(); i != range.end();
                               ++i) {
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

  bool HSet(const std::string& key, const std::string& field,
            const std::string& value) {
    // Check if hash already exists
    auto hash = GetHash(key);

    // Check if field already exists
    std::string old_field_value;
    bool field_existed = false;
    if (hash) {
      field_existed = hash->Get(field, &old_field_value);
    }

    // Create new hash if needed
    if (!hash) {
      hash = std::make_shared<HashType>(16);
      hashes_.Insert(key, hash);
      metadata_manager_.RegisterKey(key, astra::storage::KeyType::kHash);
    }

    // Update memory tracker using helper
    if (memory_tracker_) {
      core::memory::MemoryTrackerHelper::UpdateHashField(
          memory_tracker_, &metadata_manager_, key, field_existed,
          old_field_value, value);
    }

    // Check and perform eviction if needed (performance optimized)
    if (eviction_manager_ && memory_tracker_ &&
        memory_tracker_->ShouldCheckEviction()) {
      eviction_manager_->CheckAndEvict();
    }

    // Insert field
    return hash->Insert(field, value);
  }

  std::optional<std::string> HGet(const std::string& key,
                                  const std::string& field) {
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

  std::vector<std::pair<std::string, std::string>> HGetAll(
      const std::string& key) {
    if (!metadata_manager_.IsValid(key)) {
      return {};
    }

    auto hash = GetHash(key);
    if (!hash) {
      return {};
    }
    return hash->GetAllKeyValuePairs();
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

  // HINCRBY key field increment
  int64_t HIncrBy(const std::string& key, const std::string& field,
                  int64_t increment) {
    auto hash = GetHash(key);
    if (!hash) {
      hash = std::make_shared<HashType>(16);
      hashes_.Insert(key, hash);
      metadata_manager_.RegisterKey(key, astra::storage::KeyType::kHash);
    }

    std::string value;
    int64_t int_value = 0;
    if (hash->Get(field, &value)) {
      try {
        if (!absl::SimpleAtoi(value, &int_value)) {
          return 0;
        }
      } catch (...) {
        return 0;  // Will be handled by command
      }
    }

    int_value += increment;
    hash->Insert(field, absl::StrCat(int_value));
    return int_value;
  }

  // HINCRBYFLOAT key field increment
  double HIncrByFloat(const std::string& key, const std::string& field,
                      double increment) {
    auto hash = GetHash(key);
    if (!hash) {
      hash = std::make_shared<HashType>(16);
      hashes_.Insert(key, hash);
      metadata_manager_.RegisterKey(key, astra::storage::KeyType::kHash);
    }

    std::string value;
    double float_value = 0.0;
    if (hash->Get(field, &value)) {
      try {
        if (!absl::SimpleAtod(value, &float_value)) {
          return 0.0;
        }
      } catch (...) {
        return 0.0;  // Will be handled by command
      }
    }

    float_value += increment;

    // Format without trailing zeros
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(17) << float_value;
    std::string str_value = oss.str();

    // Remove trailing zeros
    size_t dot_pos = str_value.find('.');
    if (dot_pos != std::string::npos) {
      size_t last_non_zero = str_value.find_last_not_of('0');
      if (last_non_zero != std::string::npos && last_non_zero > dot_pos) {
        str_value = str_value.substr(0, last_non_zero + 1);
      }
      // Remove trailing dot
      if (str_value.back() == '.') {
        str_value.pop_back();
      }
    }

    hash->Insert(field, str_value);
    return float_value;
  }

  // HSETNX key field value - Set only if field doesn't exist
  bool HSetNx(const std::string& key, const std::string& field,
              const std::string& value) {
    auto hash = GetHash(key);
    if (!hash) {
      hash = std::make_shared<HashType>(16);
      hashes_.Insert(key, hash);
      metadata_manager_.RegisterKey(key, astra::storage::KeyType::kHash);
      hash->Insert(field, value);
      return true;
    }

    // Check if field already exists
    if (hash->Contains(field)) {
      return false;
    }

    hash->Insert(field, value);
    return true;
  }

  // HMGET key field [field ...]
  std::vector<std::optional<std::string>> HMGet(
      const std::string& key, const std::vector<std::string>& fields) {
    std::vector<std::optional<std::string>> results;

    if (!metadata_manager_.IsValid(key)) {
      results.resize(fields.size());
      return results;
    }

    auto hash = GetHash(key);
    if (!hash) {
      results.resize(fields.size());
      return results;
    }

    for (const auto& field : fields) {
      std::string value;
      if (hash->Get(field, &value)) {
        results.emplace_back(value);
      } else {
        results.emplace_back(std::nullopt);
      }
    }
    return results;
  }

  // ========== Set Operations ==========

  bool SAdd(const std::string& key, const std::string& member) {
    // Check if set already exists
    auto set = GetSet(key);

    // Check if member already exists
    bool member_existed = false;
    if (set) {
      member_existed = set->Contains(member);
    }

    // Create new set if needed
    if (!set) {
      set = std::make_shared<SetType>(16);
      sets_.Insert(key, set);
      metadata_manager_.RegisterKey(key, astra::storage::KeyType::kSet);
    }

    // Update memory tracker using helper
    if (memory_tracker_) {
      core::memory::MemoryTrackerHelper::UpdateSetMember(
          memory_tracker_, &metadata_manager_, key, member_existed, member);
    }

    // Check and perform eviction if needed (performance optimized)
    if (eviction_manager_ && memory_tracker_ &&
        memory_tracker_->ShouldCheckEviction()) {
      eviction_manager_->CheckAndEvict();
    }

    // Insert member
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

  std::optional<std::string> SPop(const std::string& key) {
    if (!metadata_manager_.IsValid(key)) {
      return std::nullopt;
    }

    auto set = GetSet(key);
    if (!set || set->Empty()) {
      return std::nullopt;
    }

    // Get a random member and remove it
    auto members = set->GetAll();
    if (members.empty()) {
      return std::nullopt;
    }

    // Use absl::BitGen for random selection
    static absl::BitGen bitgen;
    size_t idx = absl::Uniform<size_t>(bitgen, 0, members.size());
    std::string member = members[idx];
    set->Remove(member);
    return member;
  }

  std::optional<std::string> SRandMember(const std::string& key) {
    if (!metadata_manager_.IsValid(key)) {
      return std::nullopt;
    }

    auto set = GetSet(key);
    if (!set || set->Empty()) {
      return std::nullopt;
    }

    // Get a random member without removing
    auto members = set->GetAll();
    if (members.empty()) {
      return std::nullopt;
    }

    // Use absl::BitGen for random selection
    static absl::BitGen bitgen;
    size_t idx = absl::Uniform<size_t>(bitgen, 0, members.size());
    return members[idx];
  }

  // SMOVE source destination member
  // Move member from source set to destination set
  bool SMove(const std::string& source, const std::string& destination,
             const std::string& member) {
    if (!metadata_manager_.IsValid(source)) {
      return false;
    }

    auto source_set = GetSet(source);
    if (!source_set || !source_set->Contains(member)) {
      return false;
    }

    // Remove from source
    source_set->Remove(member);

    // Add to destination
    auto dest_set = GetSet(destination);
    if (!dest_set) {
      dest_set = std::make_shared<SetType>(16);
      sets_.Insert(destination, dest_set);
      metadata_manager_.RegisterKey(destination, astra::storage::KeyType::kSet);
    }
    dest_set->Insert(member);

    return true;
  }

  // SINTER key [key ...]
  // Return intersection of multiple sets
  // Dragonfly-style: Distribute to all relevant shards and aggregate results
  std::vector<std::string> SInter(const std::vector<std::string>& keys) {
    if (keys.empty()) {
      return {};
    }

    // Check if batch request callback is available (for multi-worker
    // architecture)
    if (batch_request_callback_) {
      return batch_request_callback_("SINTER", keys, {});
    }

    // Single-worker mode: process locally
    return SInterLocal(keys);
  }

  // SINTER local implementation (all keys on this shard)
  std::vector<std::string> SInterLocal(const std::vector<std::string>& keys) {
    if (keys.empty()) {
      return {};
    }

    // Get members from the first set
    auto first_set = GetSet(keys[0]);
    if (!first_set) {
      return {};
    }

    auto result = first_set->GetAll();

    // Intersect with remaining sets
    for (size_t i = 1; i < keys.size(); ++i) {
      if (!metadata_manager_.IsValid(keys[i])) {
        return {};
      }

      auto current_set = GetSet(keys[i]);
      if (!current_set) {
        return {};
      }

      // Find intersection
      std::vector<std::string> temp;
      auto current_members = current_set->GetAll();
      absl::flat_hash_set<std::string> current_set_members(
          current_members.begin(), current_members.end());

      for (const auto& member : result) {
        if (current_set_members.find(member) != current_set_members.end()) {
          temp.push_back(member);
        }
      }

      result = std::move(temp);

      if (result.empty()) {
        break;
      }
    }

    return result;
  }

  // SUNION key [key ...]
  // Return union of multiple sets
  std::vector<std::string> SUnion(const std::vector<std::string>& keys) {
    absl::flat_hash_set<std::string> union_set;

    for (const auto& key : keys) {
      if (!metadata_manager_.IsValid(key)) {
        continue;
      }

      auto set = GetSet(key);
      if (set) {
        auto members = set->GetAll();
        union_set.insert(members.begin(), members.end());
      }
    }

    return std::vector<std::string>(union_set.begin(), union_set.end());
  }

  // SDIFF key [key ...]
  // Return difference of multiple sets (first set minus all others)
  std::vector<std::string> SDiff(const std::vector<std::string>& keys) {
    if (keys.empty()) {
      return {};
    }

    // Get members from the first set
    auto first_set = GetSet(keys[0]);
    if (!first_set) {
      return {};
    }

    auto result = first_set->GetAll();

    // Remove members from all other sets
    for (size_t i = 1; i < keys.size(); ++i) {
      if (!metadata_manager_.IsValid(keys[i])) {
        continue;
      }

      auto current_set = GetSet(keys[i]);
      if (current_set) {
        auto current_members = current_set->GetAll();
        absl::flat_hash_set<std::string> current_set_members(
            current_members.begin(), current_members.end());

        // Remove members that exist in current set
        auto it = result.begin();
        while (it != result.end()) {
          if (current_set_members.find(*it) != current_set_members.end()) {
            it = result.erase(it);
          } else {
            ++it;
          }
        }
      }
    }

    return result;
  }

  // SINTERSTORE destination key [key ...]
  // Store intersection in destination set, return number of members
  size_t SInterStore(const std::string& destination,
                     const std::vector<std::string>& keys) {
    auto members = SInter(keys);

    // Clear destination set if it exists
    auto dest_set = GetSet(destination);
    if (dest_set) {
      dest_set->Clear();
    } else {
      dest_set = std::make_shared<SetType>(members.size());
      sets_.Insert(destination, dest_set);
      metadata_manager_.RegisterKey(destination, astra::storage::KeyType::kSet);
    }

    // Add members to destination
    for (const auto& member : members) {
      dest_set->Insert(member);
    }

    return members.size();
  }

  // SUNIONSTORE destination key [key ...]
  // Store union in destination set, return number of members
  size_t SUnionStore(const std::string& destination,
                     const std::vector<std::string>& keys) {
    auto members = SUnion(keys);

    // Clear destination set if it exists
    auto dest_set = GetSet(destination);
    if (dest_set) {
      dest_set->Clear();
    } else {
      dest_set = std::make_shared<SetType>(members.size());
      sets_.Insert(destination, dest_set);
      metadata_manager_.RegisterKey(destination, astra::storage::KeyType::kSet);
    }

    // Add members to destination
    for (const auto& member : members) {
      dest_set->Insert(member);
    }

    return members.size();
  }

  // SDIFFSTORE destination key [key ...]
  // Store difference in destination set, return number of members
  size_t SDiffStore(const std::string& destination,
                    const std::vector<std::string>& keys) {
    auto members = SDiff(keys);

    // Clear destination set if it exists
    auto dest_set = GetSet(destination);
    if (dest_set) {
      dest_set->Clear();
    } else {
      dest_set = std::make_shared<SetType>(members.size());
      sets_.Insert(destination, dest_set);
      metadata_manager_.RegisterKey(destination, astra::storage::KeyType::kSet);
    }

    // Add members to destination
    for (const auto& member : members) {
      dest_set->Insert(member);
    }

    return members.size();
  }

  // ========== Sorted Set Operations ==========

  bool ZAdd(const std::string& key, double score, const std::string& member) {
    // Check if zset already exists
    auto zset = GetZSet(key);

    // Check if member already exists
    bool member_existed = false;
    if (zset) {
      member_existed = zset->Contains(member);
    }

    // Create new zset if needed
    if (!zset) {
      zset = std::make_shared<ZSetType>(1024);
      zsets_.Insert(key, zset);
      metadata_manager_.RegisterKey(key, astra::storage::KeyType::kZSet);
    }

    // Update memory tracker using helper
    if (memory_tracker_) {
      core::memory::MemoryTrackerHelper::UpdateZSetMember(
          memory_tracker_, &metadata_manager_, key, member_existed, member,
          score);
    }

    // Check and perform eviction if needed (performance optimized)
    if (eviction_manager_ && memory_tracker_ &&
        memory_tracker_->ShouldCheckEviction()) {
      eviction_manager_->CheckAndEvict();
    }

    // Add member
    return zset->Add(member, score);
  }

  std::optional<double> ZScore(const std::string& key,
                               const std::string& member) {
    if (!metadata_manager_.IsValid(key)) {
      return std::nullopt;
    }

    auto zset = GetZSet(key);
    if (!zset) {
      return std::nullopt;
    }
    return zset->GetScore(member);
  }

  std::vector<std::pair<std::string, double>> ZRangeByRank(
      const std::string& key, int64_t start, int64_t stop, bool reverse = false,
      bool with_scores = false) {
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

    return zset->GetRangeByRank(static_cast<uint64_t>(start),
                                static_cast<uint64_t>(stop), false,
                                with_scores);
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

  // ZREVRANGE - Return a range of members in a sorted set, by score, with
  // scores ordered from high to low
  std::vector<std::pair<std::string, double>> ZRange(const std::string& key,
                                                     int64_t start,
                                                     int64_t stop, bool reverse,
                                                     bool with_scores) {
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

    return zset->GetRangeByRank(static_cast<uint64_t>(start),
                                static_cast<uint64_t>(stop), reverse,
                                with_scores);
  }

  // ZPOPMIN - Remove and return the member with the lowest score in a sorted
  // set
  std::optional<std::pair<std::string, double>> ZPopMin(
      const std::string& key) {
    if (!metadata_manager_.IsValid(key)) {
      return std::nullopt;
    }

    auto zset = GetZSet(key);
    if (!zset || zset->Size() == 0) {
      return std::nullopt;
    }

    auto result = zset->GetRangeByRank(0, 0, false, true);
    if (!result.empty()) {
      auto [member, score] = result[0];
      zset->Remove(member);
      if (zset->Size() == 0) {
        metadata_manager_.UnregisterKey(key);
      }
      return result[0];
    }

    return std::nullopt;
  }

  // ZPOPMAX - Remove and return the member with the highest score in a sorted
  // set
  std::optional<std::pair<std::string, double>> ZPopMax(
      const std::string& key) {
    if (!metadata_manager_.IsValid(key)) {
      return std::nullopt;
    }

    auto zset = GetZSet(key);
    if (!zset || zset->Size() == 0) {
      return std::nullopt;
    }

    auto result =
        zset->GetRangeByRank(zset->Size() - 1, zset->Size() - 1, false, true);
    if (!result.empty()) {
      auto [member, score] = result[0];
      zset->Remove(member);
      if (zset->Size() == 0) {
        metadata_manager_.UnregisterKey(key);
      }
      return result[0];
    }

    return std::nullopt;
  }

  // ZRANGEBYSCORE - Return a range of members in a sorted set, by score
  std::vector<std::pair<std::string, double>> ZRangeByScore(
      const std::string& key, const std::string& min_str,
      const std::string& max_str, bool reverse = false,
      bool with_scores = false, bool has_limit = false, int64_t offset = 0,
      int64_t count = -1) {
    if (!metadata_manager_.IsValid(key)) {
      return {};
    }

    auto zset = GetZSet(key);
    if (!zset) {
      return {};
    }

    // Parse min/max scores with possible prefixes (-inf, +inf, (, [)
    auto parse_score = [](const std::string& s) -> std::pair<bool, double> {
      if (s == "-inf" || s == "-INF") {
        return {true, -std::numeric_limits<double>::infinity()};
      }
      if (s == "+inf" || s == "+INF" || s == "inf" || s == "INF") {
        return {true, std::numeric_limits<double>::infinity()};
      }

      bool exclusive = false;
      std::string score_str = s;
      if (!s.empty() && (s[0] == '(' || s[0] == '[')) {
        exclusive = (s[0] == '(');
        score_str = s.substr(1);
      }

      double value;
      if (absl::SimpleAtod(score_str, &value)) {
        return {exclusive, value};
      }
      return {false, 0.0};
    };

    auto [min_exclusive, min_score] = parse_score(min_str);
    auto [max_exclusive, max_score] = parse_score(max_str);

    // Get range by score
    auto results = zset->GetRangeByScore(min_score, max_score, with_scores);

    // Filter out exclusive boundaries
    std::vector<std::pair<std::string, double>> filtered;
    for (const auto& [member, score] : results) {
      if (min_exclusive && score <= min_score) continue;
      if (max_exclusive && score >= max_score) continue;
      filtered.push_back({member, score});
    }

    // Reverse if needed
    if (reverse) {
      std::reverse(filtered.begin(), filtered.end());
    }

    // Apply LIMIT if specified
    if (has_limit) {
      std::vector<std::pair<std::string, double>> limited;
      int64_t start = offset;
      int64_t end = (count < 0) ? static_cast<int64_t>(filtered.size())
                                : (offset + count);

      if (start < 0) {
        start = static_cast<int64_t>(filtered.size()) + start;
        if (start < 0) start = 0;
      }

      if (start >= static_cast<int64_t>(filtered.size())) {
        return {};
      }

      if (end > static_cast<int64_t>(filtered.size())) {
        end = static_cast<int64_t>(filtered.size());
      }

      for (int64_t i = start; i < end; ++i) {
        limited.push_back(filtered[static_cast<size_t>(i)]);
      }

      return limited;
    }

    return filtered;
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

  double ZIncrBy(const std::string& key, double increment,
                 const std::string& member) {
    auto zset = GetZSet(key);
    if (!zset) {
      zset = std::make_shared<ZSetType>(1024);
      zsets_.Insert(key, zset);
      metadata_manager_.RegisterKey(key, astra::storage::KeyType::kZSet);
    }

    auto current_score = zset->GetScore(member);
    double new_score = current_score.value_or(0.0) + increment;
    zset->Add(member, new_score);
    return new_score;
  }

  std::optional<uint64_t> ZRank(const std::string& key,
                                const std::string& member,
                                bool reverse = false) {
    if (!metadata_manager_.IsValid(key)) {
      return std::nullopt;
    }

    auto zset = GetZSet(key);
    if (!zset) {
      return std::nullopt;
    }
    return zset->GetRank(member, reverse);
  }

  // ZUNIONSTORE destination numkeys key [key ...] [WEIGHTS weight [weight ...]]
  // [AGGREGATE SUM|MIN|MAX] Store union of sorted sets at keys in destination
  size_t ZUnionStore(const std::string& destination, size_t numkeys,
                     const std::vector<std::string>& keys,
                     const std::vector<double>& weights,
                     const std::string& aggregate) {
    if (keys.size() != numkeys) {
      return 0;
    }

    // Aggregate all members from all sets with weights
    absl::flat_hash_map<std::string, double> aggregated_scores;

    for (size_t i = 0; i < numkeys; ++i) {
      if (!metadata_manager_.IsValid(keys[i])) {
        continue;
      }

      auto zset = GetZSet(keys[i]);
      if (!zset) {
        continue;
      }

      auto members = zset->GetRangeByRank(0, zset->Size() - 1, false, true);
      double weight = (i < weights.size()) ? weights[i] : 1.0;

      for (const auto& [member, score] : members) {
        double weighted_score = score * weight;
        auto it = aggregated_scores.find(member);

        if (it == aggregated_scores.end()) {
          aggregated_scores[member] = weighted_score;
        } else {
          // Apply aggregation function
          if (aggregate == "SUM") {
            it->second += weighted_score;
          } else if (aggregate == "MIN") {
            it->second = std::min(it->second, weighted_score);
          } else if (aggregate == "MAX") {
            it->second = std::max(it->second, weighted_score);
          }
        }
      }
    }

    // Clear destination set if it exists
    auto dest_zset = GetZSet(destination);
    if (dest_zset) {
      dest_zset->Clear();
    } else {
      dest_zset = std::make_shared<ZSetType>(aggregated_scores.size());
      zsets_.Insert(destination, dest_zset);
      metadata_manager_.RegisterKey(destination,
                                    astra::storage::KeyType::kZSet);
    }

    // Add aggregated members to destination
    for (const auto& [member, score] : aggregated_scores) {
      dest_zset->Add(member, score);
    }

    return aggregated_scores.size();
  }

  // ZINTERSTORE destination numkeys key [key ...] [WEIGHTS weight [weight ...]]
  // [AGGREGATE SUM|MIN|MAX] Store intersection of sorted sets at keys in
  // destination
  size_t ZInterStore(const std::string& destination, size_t numkeys,
                     const std::vector<std::string>& keys,
                     const std::vector<double>& weights,
                     const std::string& aggregate) {
    if (keys.size() != numkeys || numkeys == 0) {
      return 0;
    }

    // Get members from the first set
    auto first_zset = GetZSet(keys[0]);
    if (!first_zset) {
      return 0;
    }

    auto first_members =
        first_zset->GetRangeByRank(0, first_zset->Size() - 1, false, true);
    absl::flat_hash_map<std::string, double> intersection_scores;

    for (const auto& [member, score] : first_members) {
      intersection_scores[member] =
          score * ((weights.size() > 0) ? weights[0] : 1.0);
    }

    // Intersect with remaining sets
    for (size_t i = 1; i < numkeys; ++i) {
      if (!metadata_manager_.IsValid(keys[i])) {
        return 0;
      }

      auto current_zset = GetZSet(keys[i]);
      if (!current_zset) {
        return 0;
      }

      auto current_members = current_zset->GetRangeByRank(
          0, current_zset->Size() - 1, false, true);
      absl::flat_hash_map<std::string, double> current_map(
          current_members.begin(), current_members.end());
      double weight = (i < weights.size()) ? weights[i] : 1.0;

      absl::flat_hash_map<std::string, double> temp;
      for (const auto& [member, score] : intersection_scores) {
        auto it = current_map.find(member);
        if (it != current_map.end()) {
          double weighted_score = it->second * weight;
          if (aggregate == "SUM") {
            temp[member] = score + weighted_score;
          } else if (aggregate == "MIN") {
            temp[member] = std::min(score, weighted_score);
          } else if (aggregate == "MAX") {
            temp[member] = std::max(score, weighted_score);
          }
        }
      }

      intersection_scores = std::move(temp);

      if (intersection_scores.empty()) {
        return 0;
      }
    }

    // Clear destination set if it exists
    auto dest_zset = GetZSet(destination);
    if (dest_zset) {
      dest_zset->Clear();
    } else {
      dest_zset = std::make_shared<ZSetType>(intersection_scores.size());
      zsets_.Insert(destination, dest_zset);
      metadata_manager_.RegisterKey(destination,
                                    astra::storage::KeyType::kZSet);
    }

    // Add intersection members to destination
    for (const auto& [member, score] : intersection_scores) {
      dest_zset->Add(member, score);
    }

    return intersection_scores.size();
  }

  // ZDIFF numkeys key [key ...]
  // Return difference of sorted sets (first set minus all others)
  std::vector<std::pair<std::string, double>> ZDiff(
      size_t numkeys, const std::vector<std::string>& keys) {
    if (keys.size() != numkeys || numkeys == 0) {
      return {};
    }

    // Get members from the first set
    auto first_zset = GetZSet(keys[0]);
    if (!first_zset) {
      return {};
    }

    auto result =
        first_zset->GetRangeByRank(0, first_zset->Size() - 1, false, true);
    absl::flat_hash_map<std::string, double> result_map(result.begin(),
                                                        result.end());

    // Remove members from all other sets
    for (size_t i = 1; i < numkeys; ++i) {
      if (!metadata_manager_.IsValid(keys[i])) {
        continue;
      }

      auto current_zset = GetZSet(keys[i]);
      if (current_zset) {
        auto current_members = current_zset->GetRangeByRank(
            0, current_zset->Size() - 1, false, true);
        absl::flat_hash_set<std::string> current_set;
        for (const auto& [member, _] : current_members) {
          current_set.insert(member);
        }

        // Remove members that exist in current set
        auto it = result.begin();
        while (it != result.end()) {
          if (current_set.find(it->first) != current_set.end()) {
            it = result.erase(it);
          } else {
            ++it;
          }
        }
      }
    }

    return result;
  }

  // ZDIFFSTORE destination numkeys key [key ...]
  // Store difference of sorted sets in destination
  size_t ZDiffStore(const std::string& destination, size_t numkeys,
                    const std::vector<std::string>& keys) {
    auto members = ZDiff(numkeys, keys);

    // Clear destination set if it exists
    auto dest_zset = GetZSet(destination);
    if (dest_zset) {
      dest_zset->Clear();
    } else {
      dest_zset = std::make_shared<ZSetType>(members.size());
      zsets_.Insert(destination, dest_zset);
      metadata_manager_.RegisterKey(destination,
                                    astra::storage::KeyType::kZSet);
    }

    // Add members to destination
    for (const auto& [member, score] : members) {
      dest_zset->Add(member, score);
    }

    return members.size();
  }

  // ========== List Operations ==========

  bool LPush(const std::string& key, const std::string& value) {
    // Check if list already exists
    auto list = GetList(key);

    // Create new list if needed
    if (!list) {
      list = std::make_shared<ListType>();
      lists_.Insert(key, list);
      metadata_manager_.RegisterKey(key, astra::storage::KeyType::kList);
    }

    // Update memory tracker using helper (element is always new for push)
    if (memory_tracker_) {
      core::memory::MemoryTrackerHelper::UpdateListElement(
          memory_tracker_, &metadata_manager_, key, false, value);
    }

    // Check and perform eviction if needed (performance optimized)
    if (eviction_manager_ && memory_tracker_ &&
        memory_tracker_->ShouldCheckEviction()) {
      eviction_manager_->CheckAndEvict();
    }

    // Push element
    list->PushLeft(value);
    return true;
  }

  bool RPush(const std::string& key, const std::string& value) {
    // Check if list already exists
    auto list = GetList(key);

    // Create new list if needed
    if (!list) {
      list = std::make_shared<ListType>();
      lists_.Insert(key, list);
      metadata_manager_.RegisterKey(key, astra::storage::KeyType::kList);
    }

    // Update memory tracker using helper (element is always new for push)
    if (memory_tracker_) {
      core::memory::MemoryTrackerHelper::UpdateListElement(
          memory_tracker_, &metadata_manager_, key, false, value);
    }

    // Check and perform eviction if needed (performance optimized)
    if (eviction_manager_ && memory_tracker_ &&
        memory_tracker_->ShouldCheckEviction()) {
      eviction_manager_->CheckAndEvict();
    }

    // Push element
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

  std::vector<std::string> LRange(const std::string& key, int64_t start,
                                  int64_t stop) {
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

  bool LInsert(const std::string& key, int64_t pivot_index,
               const std::string& value, bool before = true) {
    if (!metadata_manager_.IsValid(key)) {
      return false;
    }

    auto list = GetList(key);
    if (!list) {
      return false;
    }
    return list->Insert(pivot_index, value, before);
  }

  std::optional<std::string> RPopLPush(const std::string& source,
                                       const std::string& destination) {
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

  int64_t GetTtlMs(const std::string& key) const {
    return metadata_manager_.GetTtlMs(key);
  }

  std::optional<int64_t> GetExpireTimeMs(const std::string& key) {
    return metadata_manager_.GetExpireTimeMs(key);
  }

  bool Persist(const std::string& key) {
    return metadata_manager_.Persist(key);
  }

  std::vector<std::string> GetExpiredKeys() {
    return metadata_manager_.GetExpiredKeys();
  }

  // ========== Utility Operations ==========

  size_t Size() const { return strings_.Size(); }

  // Get total key count across all data types
  size_t GetKeyCount() const {
    return strings_.Size() + hashes_.Size() + lists_.Size() + sets_.Size() +
           zsets_.Size() + streams_.Size();
  }

  // Get expired keys count
  size_t GetExpiredKeysCount() const {
    return metadata_manager_.GetExpiredKeys().size();
  }

  // ========== RDB Persistence Operations ==========

  // ForEachKey callback type
  using ForEachKeyCallback =
      std::function<void(const std::string& key, astra::storage::KeyType type,
                         const std::string& value, int64_t ttl_ms)>;

  // Iterate through all keys in the database
  // callback(key, type, value, ttl_ms)
  // value is serialized as string for all types
  void ForEachKey(ForEachKeyCallback callback) const {
    // Iterate strings
    auto string_pairs = strings_.GetAllKeyValuePairs();
    for (const auto& [key, str_value] : string_pairs) {
      auto ttl_ms = GetTtlMs(key);
      callback(key, astra::storage::KeyType::kString, str_value.value, ttl_ms);
    }

    // Iterate hashes (serialize as field-value pairs)
    auto hash_keys = hashes_.GetAllKeys();
    for (const auto& key : hash_keys) {
      std::shared_ptr<HashType> hash;
      if (hashes_.Get(key, &hash)) {
        auto field_value_pairs = hash->GetAllKeyValuePairs();
        std::string serialized_hash;
        for (const auto& [field, value] : field_value_pairs) {
          serialized_hash += field + ":" + value + "\n";
        }
        auto ttl_ms = GetTtlMs(key);
        callback(key, astra::storage::KeyType::kHash, serialized_hash, ttl_ms);
      }
    }

    // Iterate lists (serialize as element list)
    auto list_keys = lists_.GetAllKeys();
    for (const auto& key : list_keys) {
      std::shared_ptr<ListType> list;
      if (lists_.Get(key, &list)) {
        std::string serialized_list;
        auto elements = list->Range(0, -1);
        for (const auto& elem : elements) {
          serialized_list += elem + "\n";
        }
        auto ttl_ms = GetTtlMs(key);
        callback(key, astra::storage::KeyType::kList, serialized_list, ttl_ms);
      }
    }

    // Iterate sets (serialize as member list)
    auto set_keys = sets_.GetAllKeys();
    for (const auto& key : set_keys) {
      std::shared_ptr<SetType> set;
      if (sets_.Get(key, &set)) {
        std::string serialized_set;
        auto members = set->GetAll();
        for (const auto& member : members) {
          serialized_set += member + "\n";
        }
        auto ttl_ms = GetTtlMs(key);
        callback(key, astra::storage::KeyType::kSet, serialized_set, ttl_ms);
      }
    }

    // Iterate sorted sets (serialize as member:score pairs)
    auto zset_keys = zsets_.GetAllKeys();
    for (const auto& key : zset_keys) {
      std::shared_ptr<ZSetType> zset;
      if (zsets_.Get(key, &zset)) {
        std::string serialized_zset;
        auto members_scores = zset->GetAll();
        for (const auto& [member, score] : members_scores) {
          serialized_zset += member + ":" + std::to_string(score) + "\n";
        }
        auto ttl_ms = GetTtlMs(key);
        callback(key, astra::storage::KeyType::kZSet, serialized_zset, ttl_ms);
      }
    }
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
  size_t DbSize() const { return metadata_manager_.Size(); }

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
  astra::container::DashMap<std::string, std::shared_ptr<StreamData>> streams_;
  astra::storage::KeyMetadataManager metadata_manager_;
  std::unique_ptr<core::memory::StringPool> string_pool_;
  core::memory::MemoryTracker* memory_tracker_ = nullptr;  // Not owned
  std::unique_ptr<core::memory::EvictionManager> eviction_manager_;  // Owned
  persistence::RocksDBAdapter* rocksdb_adapter_ =
      nullptr;                                   // Not owned, managed by Worker
  BatchRequestCallback batch_request_callback_;  // For cross-worker requests
  std::function<void(const std::string&)> aof_callback_;  // For persistence
};

// Database manager - manages multiple databases
class DatabaseManager {
 public:
  explicit DatabaseManager(size_t num_dbs = 16) {
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

  size_t GetDatabaseCount() const { return databases_.size(); }

  // Set memory tracker for all databases (called by Shard)
  void SetMemoryTrackerForAll(core::memory::MemoryTracker* tracker) {
    for (auto& db : databases_) {
      db->SetMemoryTracker(tracker);
      db->InitializeEvictionManager();
    }
  }

 private:
  std::vector<std::unique_ptr<Database>> databases_;
};

}  // namespace astra::commands
