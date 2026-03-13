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

 private:
  astra::container::DashMap<std::string, KeyMetadata> metadata_map_;
  std::atomic<int64_t> next_scan_time_;
};

}  // namespace astra::storage
