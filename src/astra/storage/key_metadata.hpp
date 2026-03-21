// ==============================================================================
// Key Metadata Management
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>

#include "astra/container/dash_map.hpp"

namespace astra::storage {

// Key type enumeration
enum class KeyType : uint8_t {
  kNone = 0,
  kString = 1,
  kHash = 2,
  kSet = 3,
  kZSet = 4,
  kList = 5,
  kStream = 6
};

// Key metadata - stored with every key
struct KeyMetadata {
  KeyType type;
  std::optional<int64_t> expire_time_ms;  // 0 or empty means no expiration
  uint64_t version = 0;  // Version for WATCH/optimistic locking

  // Eviction tracking fields
  uint32_t access_time_ms = 0;   // LRU access timestamp (24-bit, wraps every 194 days)
  uint8_t lfu_counter = 0;       // LFU counter (8-bit, logarithmic)
  uint32_t estimated_size = 0;   // Estimated memory size in bytes

  KeyMetadata() : type(KeyType::kNone) {}

  explicit KeyMetadata(KeyType t) : type(t) {}

  bool IsExpired() const {
    if (!expire_time_ms.has_value()) {
      return false;
    }
    return *expire_time_ms > 0 && *expire_time_ms < GetCurrentTimeMs();
  }

  void SetExpireMs(int64_t ms) {
    if (ms <= 0) {
      expire_time_ms = std::nullopt;
    } else {
      expire_time_ms = ms;
    }
  }

  void SetExpireSeconds(int64_t seconds) { SetExpireMs(seconds * 1000); }

  std::optional<int64_t> GetTtlMs() const {
    if (!expire_time_ms.has_value()) {
      return std::nullopt;
    }
    int64_t now = GetCurrentTimeMs();
    if (*expire_time_ms <= 0 || *expire_time_ms <= now) {
      return std::nullopt;
    }
    return *expire_time_ms - now;
  }

  std::optional<int64_t> GetTtlSeconds() const {
    auto ttl_ms = GetTtlMs();
    if (!ttl_ms.has_value()) {
      return std::nullopt;
    }
    return *ttl_ms / 1000;
  }

  static int64_t GetCurrentTimeMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch())
        .count();
  }

  // ========== Access Tracking (LRU/LFU) ==========

  // Update access information (called on every read/write)
  void UpdateAccess() {
    access_time_ms = static_cast<uint32_t>(GetCurrentTimeMs() & 0xFFFFFF);  // 24-bit
    lfu_counter = std::min(static_cast<uint8_t>(lfu_counter + 1), static_cast<uint8_t>(255));
  }

  // Get access time for LRU
  uint32_t GetAccessTime() const { return access_time_ms; }

  // Get LFU counter
  uint8_t GetLFUCounter() const { return lfu_counter; }

  // Reset LFU counter (for periodic decay)
  void ResetLFUCounter() { lfu_counter = lfu_counter >> 1; }

  // ========== Memory Estimation ==========

  // Set estimated memory size
  void SetEstimatedSize(uint32_t size) { estimated_size = size; }

  // Get estimated memory size
  uint32_t GetEstimatedSize() const { return estimated_size; }

  // Increment estimated size
  void AddEstimatedSize(int32_t delta) {
    if (delta > 0) {
      estimated_size += static_cast<uint32_t>(delta);
    } else if (delta < 0 && estimated_size >= static_cast<uint32_t>(-delta)) {
      estimated_size -= static_cast<uint32_t>(-delta);
    }
  }
};

// Key metadata manager - manages metadata for all keys in a database
class KeyMetadataManager {
 public:
  KeyMetadataManager() : next_scan_time_(0) {}

  ~KeyMetadataManager() = default;

  // Register a key with its type (increments version on update)
  void RegisterKey(const std::string& key, KeyType type) {
    KeyMetadata existing;
    uint64_t new_version = 1;

    // If key exists, increment version
    if (metadata_map_.Get(key, &existing)) {
      new_version = existing.version + 1;
    }

    KeyMetadata metadata(type);
    metadata.version = new_version;
    metadata_map_.Insert(key, std::move(metadata));
  }

  // Unregister a key
  void UnregisterKey(const std::string& key) { metadata_map_.Remove(key); }

  // Check if a key exists and is not expired
  bool IsValid(const std::string& key) {
    KeyMetadata metadata;
    if (!metadata_map_.Get(key, &metadata)) {
      return false;
    }
    if (metadata.IsExpired()) {
      metadata_map_.Remove(key);
      return false;
    }
    return true;
  }

  // Get key type
  std::optional<KeyType> GetKeyType(const std::string& key) {
    KeyMetadata metadata;
    if (!metadata_map_.Get(key, &metadata)) {
      return std::nullopt;
    }
    if (metadata.IsExpired()) {
      metadata_map_.Remove(key);
      return std::nullopt;
    }
    return metadata.type;
  }

  // Set expiration time
  bool SetExpireMs(const std::string& key, int64_t expire_time_ms) {
    KeyMetadata metadata;
    if (!metadata_map_.Get(key, &metadata)) {
      return false;
    }
    metadata.SetExpireMs(expire_time_ms);
    metadata_map_.Insert(key, std::move(metadata));
    return true;
  }

  bool SetExpireSeconds(const std::string& key, int64_t seconds) {
    return SetExpireMs(key, KeyMetadata::GetCurrentTimeMs() + seconds * 1000);
  }

  // Get TTL
  int64_t GetTtlMs(const std::string& key) const {
    KeyMetadata metadata;
    if (!metadata_map_.Get(key, &metadata)) {
      return -2;  // Key does not exist
    }

    if (metadata.IsExpired()) {
      return -2;  // Key is expired
    }

    if (!metadata.expire_time_ms.has_value()) {
      return -1;  // Key exists but has no expiration
    }

    auto ttl_ms = metadata.GetTtlMs();
    if (!ttl_ms.has_value()) {
      return -1;  // Key exists but has no expiration
    }
    return *ttl_ms;
  }

  int64_t GetTtlSeconds(const std::string& key) {
    int64_t ttl_ms = GetTtlMs(key);
    if (ttl_ms < 0) {
      return ttl_ms;  // Return -2 or -1 as is
    }
    return ttl_ms / 1000;  // Convert to seconds
  }

  // Get expire time
  std::optional<int64_t> GetExpireTimeMs(const std::string& key) {
    KeyMetadata metadata;
    if (!metadata_map_.Get(key, &metadata)) {
      return std::nullopt;  // Key does not exist
    }

    if (metadata.IsExpired()) {
      return std::nullopt;  // Key is expired
    }

    return metadata.expire_time_ms;
  }

  // Remove expiration
  bool Persist(const std::string& key) {
    KeyMetadata metadata;
    if (!metadata_map_.Get(key, &metadata)) {
      return false;
    }
    metadata.expire_time_ms = std::nullopt;
    metadata_map_.Insert(key, std::move(metadata));
    return true;
  }

  // Get all expired keys (for cleanup)
  std::vector<std::string> GetExpiredKeys() const {
    std::vector<std::string> expired_keys;
    [[maybe_unused]] int64_t now = KeyMetadata::GetCurrentTimeMs();

    // Note: This is a simplified implementation
    // In production, we'd need a more efficient way to iterate
    // For now, we'll rely on lazy deletion in Get/IsValid

    return expired_keys;
  }

  // Get metadata map size
  size_t Size() const { return metadata_map_.Size(); }

  // Get all keys (for KEYS command)
  std::vector<std::string> GetAllKeys() { return metadata_map_.GetAllKeys(); }

  // Get key version (for WATCH)
  uint64_t GetKeyVersion(const std::string& key) {
    KeyMetadata metadata;
    if (!metadata_map_.Get(key, &metadata)) {
      return 0;  // Key doesn't exist
    }
    return metadata.version;
  }

  // Increment key version (called on every write)
  void IncrementKeyVersion(const std::string& key) {
    KeyMetadata metadata;
    if (metadata_map_.Get(key, &metadata)) {
      metadata.version++;
      metadata_map_.Insert(key, std::move(metadata));
    }
  }

  // Clear all metadata
  void Clear() { metadata_map_.Clear(); }

  // ========== Access Tracking (LRU/LFU) ==========

  // Update access information for a key
  bool UpdateAccessInfo(const std::string& key) {
    KeyMetadata metadata;
    if (!metadata_map_.Get(key, &metadata)) {
      return false;
    }
    metadata.UpdateAccess();
    metadata_map_.Insert(key, std::move(metadata));
    return true;
  }

  // Get access time for a key
  uint32_t GetAccessTime(const std::string& key) {
    KeyMetadata metadata;
    if (!metadata_map_.Get(key, &metadata)) {
      return 0;
    }
    return metadata.GetAccessTime();
  }

  // Get LFU counter for a key
  uint8_t GetLFUCounter(const std::string& key) {
    KeyMetadata metadata;
    if (!metadata_map_.Get(key, &metadata)) {
      return 0;
    }
    return metadata.GetLFUCounter();
  }

  // Decay LFU counters for all keys (call periodically)
  void DecayLFUCounters() {
    // Note: This is a simplified implementation
    // In production, we'd need a more efficient way to iterate
    // For now, we'll rely on lazy decay on access
  }

  // ========== Memory Estimation ==========

  // Update estimated size for a key
  bool UpdateEstimatedSize(const std::string& key, uint32_t size) {
    KeyMetadata metadata;
    if (!metadata_map_.Get(key, &metadata)) {
      return false;
    }
    metadata.SetEstimatedSize(size);
    metadata_map_.Insert(key, std::move(metadata));
    return true;
  }

  // Add delta to estimated size for a key
  bool AddEstimatedSize(const std::string& key, int32_t delta) {
    KeyMetadata metadata;
    if (!metadata_map_.Get(key, &metadata)) {
      return false;
    }
    metadata.AddEstimatedSize(delta);
    metadata_map_.Insert(key, std::move(metadata));
    return true;
  }

  // Get estimated size for a key
  uint32_t GetEstimatedSize(const std::string& key) {
    KeyMetadata metadata;
    if (!metadata_map_.Get(key, &metadata)) {
      return 0;
    }
    return metadata.GetEstimatedSize();
  }

  // Get total estimated memory usage
  uint64_t GetTotalEstimatedMemory() {
    uint64_t total = 0;
    auto all_metadata = metadata_map_.GetAllKeyValuePairs();
    for (const auto& [key, metadata] : all_metadata) {
      (void)key;  // Unused
      total += metadata.GetEstimatedSize();
    }
    return total;
  }

  // Check if key has TTL (for volatile eviction policies)
  bool HasTTL(const std::string& key) {
    KeyMetadata metadata;
    if (!metadata_map_.Get(key, &metadata)) {
      return false;
    }
    return metadata.expire_time_ms.has_value();
  }

 private:
  astra::container::DashMap<std::string, KeyMetadata> metadata_map_;
  std::atomic<int64_t> next_scan_time_;
};

}  // namespace astra::storage
