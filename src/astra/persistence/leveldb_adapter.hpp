// ==============================================================================
// LevelDB Adapter - Persistence Layer (Optimized with Abseil)
// ==============================================================================
// License: Apache 2.0
// ==============================================================================
// Design Principles:
// - noexcept for all performance-critical paths
// - Abseil containers (absl::flat_hash_map) instead of absl::flat_hash_map
// - absl::Span for zero-copy views
// - Cross-platform: Linux/Windows/macOS
// ==============================================================================

#pragma once

#include <leveldb/db.h>
#include <leveldb/write_batch.h>
#include <leveldb/cache.h>
#include <leveldb/filter_policy.h>

#include <absl/container/flat_hash_map.h>
#include <absl/strings/string_view.h>
#include <absl/types/span.h>
#include <absl/functional/function_ref.h>

#include <memory>
#include <string>
#include <mutex>
#include <atomic>

#include <absl/synchronization/mutex.h>
#include "astra/base/macros.hpp"
#include <absl/synchronization/mutex.h>
#include "astra/base/logging.hpp"

namespace astra::persistence {

// LevelDB configuration options
struct LevelDBOptions {
  std::string db_path = "./data/astradb";
  size_t cache_size = 64 * 1024 * 1024;  // 64MB LRU cache
  size_t write_buffer_size = 4 * 1024 * 1024;  // 4MB write buffer
  size_t bloom_filter_bits = 10;  // Bloom filter bits per key
  bool create_if_missing = true;
  bool error_if_exists = false;
  bool compress = true;
};

// Write options for put operations
struct WriteOptions {
  bool sync = false;  // Wait for disk sync
};

// Read options for get operations
struct ReadOptions {
  bool verify_checksums = true;
  bool fill_cache = true;
};

// Key prefix for different data types
enum class KeyPrefix : char {
  kString = 'S',
  kHash = 'H',
  kSet = 'E',
  kZSet = 'Z',
  kList = 'L',
  kMeta = 'M',  // Metadata
  kTTL = 'T',   // TTL index
};

// Result type for operations that may fail
template<typename T>
class Result {
 public:
  constexpr Result() noexcept : has_value_(false), value_{} {}
  constexpr Result(T val) noexcept : has_value_(true), value_(std::move(val)) {}
  
  constexpr bool ok() const noexcept { return has_value_; }
  constexpr bool has_value() const noexcept { return has_value_; }
  constexpr explicit operator bool() const noexcept { return has_value_; }
  
  constexpr const T& value() const noexcept { return value_; }
  constexpr T& value() noexcept { return value_; }
  
  constexpr const T& operator*() const noexcept { return value_; }
  constexpr T& operator*() noexcept { return value_; }
  
  constexpr const T* operator->() const noexcept { return &value_; }
  constexpr T* operator->() noexcept { return &value_; }
  
  static constexpr Result NotFound() noexcept { return Result{}; }
  
 private:
  bool has_value_;
  T value_;
};

// Specialization for void
template<>
class Result<void> {
 public:
  constexpr Result(bool success) noexcept : success_(success) {}
  constexpr bool ok() const noexcept { return success_; }
  constexpr explicit operator bool() const noexcept { return success_; }
  
  static constexpr Result Ok() noexcept { return Result{true}; }
  static constexpr Result Error() noexcept { return Result{false}; }
  
 private:
  bool success_;
};

// LevelDB adapter - wraps LevelDB operations with noexcept guarantees
class LevelDBAdapter {
 public:
  LevelDBAdapter() noexcept 
      : db_(nullptr), 
        cache_(nullptr), 
        filter_policy_(nullptr),
        is_open_(false) {}
  
  ~LevelDBAdapter() noexcept { 
    Close(); 
  }

  // Disable copy
  LevelDBAdapter(const LevelDBAdapter&) = delete;
  LevelDBAdapter& operator=(const LevelDBAdapter&) = delete;

  // Enable move
  LevelDBAdapter(LevelDBAdapter&& other) noexcept
      : db_(other.db_),
        cache_(other.cache_),
        filter_policy_(other.filter_policy_),
        options_(std::move(other.options_)),
        is_open_(other.is_open_.load(std::memory_order_relaxed)) {
    other.db_ = nullptr;
    other.cache_ = nullptr;
    other.filter_policy_ = nullptr;
    other.is_open_.store(false, std::memory_order_relaxed);
  }

  LevelDBAdapter& operator=(LevelDBAdapter&& other) noexcept {
    if (this != &other) {
      Close();
      db_ = other.db_;
      cache_ = other.cache_;
      filter_policy_ = other.filter_policy_;
      options_ = std::move(other.options_);
      is_open_.store(other.is_open_.load(std::memory_order_relaxed), std::memory_order_relaxed);
      other.db_ = nullptr;
      other.cache_ = nullptr;
      other.filter_policy_ = nullptr;
      other.is_open_.store(false, std::memory_order_relaxed);
    }
    return *this;
  }

  // Open database with options - returns true on success
  bool Open(const LevelDBOptions& options) noexcept {
    absl::MutexLock lock(&mutex_);
    
    if (is_open_.load(std::memory_order_acquire)) {
      return true;  // Already open
    }

    options_ = options;

    // Configure LevelDB options
    leveldb::Options ldb_options;
    ldb_options.create_if_missing = options.create_if_missing;
    ldb_options.error_if_exists = options.error_if_exists;
    ldb_options.write_buffer_size = options.write_buffer_size;
    
    // Enable compression
    if (options.compress) {
      ldb_options.compression = leveldb::kSnappyCompression;
    }

    // Create LRU cache
    cache_ = leveldb::NewLRUCache(options.cache_size);
    ldb_options.block_cache = cache_;

    // Create bloom filter
    filter_policy_ = leveldb::NewBloomFilterPolicy(options.bloom_filter_bits);
    ldb_options.filter_policy = filter_policy_;

    // Open database
    leveldb::Status status = leveldb::DB::Open(ldb_options, options.db_path, &db_);
    
    if (!status.ok()) {
      ASTRADB_LOG_ERROR("Failed to open LevelDB at {}: {}", options.db_path, status.ToString());
      return false;
    }

    is_open_.store(true, std::memory_order_release);
    ASTRADB_LOG_INFO("LevelDB opened successfully at {}", options.db_path);
    return true;
  }

  // Close database
  void Close() noexcept {
    absl::MutexLock lock(&mutex_);
    
    if (!is_open_.load(std::memory_order_acquire)) {
      return;
    }
    
    if (db_) {
      delete db_;
      db_ = nullptr;
    }
    
    if (filter_policy_) {
      delete filter_policy_;
      filter_policy_ = nullptr;
    }
    
    if (cache_) {
      delete cache_;
      cache_ = nullptr;
    }
    
    is_open_.store(false, std::memory_order_release);
  }

  // Check if database is open
  bool IsOpen() const noexcept { 
    return is_open_.load(std::memory_order_acquire); 
  }

  // ========== Basic Key-Value Operations ==========

  // Put a key-value pair - noexcept
  bool Put(absl::string_view key, absl::string_view value, 
           const WriteOptions& options = {}) noexcept {
    if (ASTRADB_UNLIKELY(!db_)) return false;
    
    leveldb::WriteOptions write_options;
    write_options.sync = options.sync;
    
    leveldb::Slice key_slice(key.data(), key.size());
    leveldb::Slice value_slice(value.data(), value.size());
    
    leveldb::Status status = db_->Put(write_options, key_slice, value_slice);
    if (ASTRADB_UNLIKELY(!status.ok())) {
      ASTRADB_LOG_ERROR("Failed to put key '{}': {}", key, status.ToString());
      return false;
    }
    return true;
  }

  // Get a value by key - returns Result<std::string>
  Result<std::string> Get(absl::string_view key, 
                          const ReadOptions& options = {}) noexcept {
    if (ASTRADB_UNLIKELY(!db_)) return Result<std::string>::NotFound();
    
    leveldb::ReadOptions read_options;
    read_options.verify_checksums = options.verify_checksums;
    read_options.fill_cache = options.fill_cache;
    
    leveldb::Slice key_slice(key.data(), key.size());
    std::string value;
    leveldb::Status status = db_->Get(read_options, key_slice, &value);
    
    if (status.IsNotFound()) {
      return Result<std::string>::NotFound();
    }
    
    if (ASTRADB_UNLIKELY(!status.ok())) {
      ASTRADB_LOG_ERROR("Failed to get key '{}': {}", key, status.ToString());
      return Result<std::string>::NotFound();
    }
    
    return Result<std::string>(std::move(value));
  }

  // Delete a key - noexcept
  bool Delete(absl::string_view key, const WriteOptions& options = {}) noexcept {
    if (ASTRADB_UNLIKELY(!db_)) return false;
    
    leveldb::WriteOptions write_options;
    write_options.sync = options.sync;
    
    leveldb::Slice key_slice(key.data(), key.size());
    leveldb::Status status = db_->Delete(write_options, key_slice);
    
    if (ASTRADB_UNLIKELY(!status.ok())) {
      ASTRADB_LOG_ERROR("Failed to delete key '{}': {}", key, status.ToString());
      return false;
    }
    return true;
  }

  // Check if key exists - noexcept
  bool Exists(absl::string_view key) noexcept {
    return Get(key).ok();
  }

  // ========== Batch Operations ==========

  // Write batch for atomic writes
  class WriteBatch {
   public:
    WriteBatch() noexcept = default;
    
    void Put(absl::string_view key, absl::string_view value) noexcept {
      leveldb::Slice key_slice(key.data(), key.size());
      leveldb::Slice value_slice(value.data(), value.size());
      batch_.Put(key_slice, value_slice);
    }
    
    void Delete(absl::string_view key) noexcept {
      leveldb::Slice key_slice(key.data(), key.size());
      batch_.Delete(key_slice);
    }
    
    void Clear() noexcept {
      batch_.Clear();
    }
    
    size_t ApproximateSize() const noexcept {
      return batch_.ApproximateSize();
    }

   private:
    friend class LevelDBAdapter;
    leveldb::WriteBatch batch_;
  };

  // Execute a write batch atomically
  bool Write(WriteBatch& batch, const WriteOptions& options = {}) noexcept {
    if (ASTRADB_UNLIKELY(!db_)) return false;
    
    leveldb::WriteOptions write_options;
    write_options.sync = options.sync;
    
    leveldb::Status status = db_->Write(write_options, &batch.batch_);
    if (ASTRADB_UNLIKELY(!status.ok())) {
      ASTRADB_LOG_ERROR("Failed to write batch: {}", status.ToString());
      return false;
    }
    return true;
  }

  // ========== Iteration ==========

  // Iterate over all keys with optional prefix using callback
  // Callback returns false to stop iteration
  void Scan(absl::string_view prefix,
            absl::FunctionRef<bool(absl::string_view, absl::string_view)> callback,
            const ReadOptions& options = {}) noexcept {
    if (ASTRADB_UNLIKELY(!db_)) return;
    
    leveldb::ReadOptions read_options;
    read_options.verify_checksums = options.verify_checksums;
    read_options.fill_cache = options.fill_cache;
    
    std::unique_ptr<leveldb::Iterator> it(db_->NewIterator(read_options));
    
    if (!prefix.empty()) {
      leveldb::Slice prefix_slice(prefix.data(), prefix.size());
      it->Seek(prefix_slice);
    } else {
      it->SeekToFirst();
    }
    
    for (; it->Valid(); it->Next()) {
      absl::string_view key(it->key().data(), it->key().size());
      
      // Check if key still matches prefix
      if (!prefix.empty() && !key.starts_with(prefix)) {
        break;
      }
      
      absl::string_view value(it->value().data(), it->value().size());
      if (!callback(key, value)) {
        break;
      }
    }
    
    if (ASTRADB_UNLIKELY(!it->status().ok())) {
      ASTRADB_LOG_ERROR("Iterator error: {}", it->status().ToString());
    }
  }

  // Get all keys with a prefix - returns vector
  std::vector<std::string> GetKeys(absl::string_view prefix = "") noexcept {
    std::vector<std::string> keys;
    keys.reserve(1024);  // Pre-allocate for performance
    
    Scan(prefix, [&keys](absl::string_view key, absl::string_view) {
      keys.emplace_back(key);
      return true;
    });
    
    return keys;
  }

  // Get all key-value pairs with a prefix
  absl::flat_hash_map<std::string, std::string> GetAll(
      absl::string_view prefix = "") noexcept {
    absl::flat_hash_map<std::string, std::string> items;
    items.reserve(1024);
    
    Scan(prefix, [&items](absl::string_view key, absl::string_view value) {
      items.emplace(key, value);
      return true;
    });
    
    return items;
  }

  // ========== Key Encoding Helpers ==========

  // Encode key with type prefix - constexpr for compile-time optimization
  static std::string EncodeKey(KeyPrefix prefix, absl::string_view key) noexcept {
    std::string result;
    result.reserve(2 + key.size());
    result.push_back(static_cast<char>(prefix));
    result.push_back(':');
    result.append(key.data(), key.size());
    return result;
  }

  // Encode hash field key
  static std::string EncodeHashKey(absl::string_view key, 
                                   absl::string_view field) noexcept {
    std::string result;
    result.reserve(2 + key.size() + 1 + field.size());
    result.push_back(static_cast<char>(KeyPrefix::kHash));
    result.push_back(':');
    result.append(key.data(), key.size());
    result.push_back(':');
    result.append(field.data(), field.size());
    return result;
  }

  // Encode set member key
  static std::string EncodeSetKey(absl::string_view key, 
                                  absl::string_view member) noexcept {
    std::string result;
    result.reserve(2 + key.size() + 1 + member.size());
    result.push_back(static_cast<char>(KeyPrefix::kSet));
    result.push_back(':');
    result.append(key.data(), key.size());
    result.push_back(':');
    result.append(member.data(), member.size());
    return result;
  }

  // Encode zset member key (score stored in value)
  static std::string EncodeZSetKey(absl::string_view key, 
                                   absl::string_view member) noexcept {
    std::string result;
    result.reserve(2 + key.size() + 1 + member.size());
    result.push_back(static_cast<char>(KeyPrefix::kZSet));
    result.push_back(':');
    result.append(key.data(), key.size());
    result.push_back(':');
    result.append(member.data(), member.size());
    return result;
  }

  // Encode list index key
  static std::string EncodeListKey(absl::string_view key, int64_t index) noexcept {
    std::string result;
    result.reserve(2 + key.size() + 1 + 21);  // 21 for max int64_t string
    result.push_back(static_cast<char>(KeyPrefix::kList));
    result.push_back(':');
    result.append(key.data(), key.size());
    result.push_back(':');
    result.append(absl::StrCat(index));
    return result;
  }

  // Encode TTL index key (for expiration scanning)
  static std::string EncodeTTLKey(int64_t expire_time_ms, 
                                  absl::string_view key) noexcept {
    std::string result;
    result.reserve(2 + 21 + 1 + key.size());  // 21 for max int64_t string
    result.push_back(static_cast<char>(KeyPrefix::kTTL));
    result.push_back(':');
    result.append(absl::StrCat(expire_time_ms));
    result.push_back(':');
    result.append(key.data(), key.size());
    return result;
  }

  // ========== Statistics ==========

  // Get approximate number of keys
  uint64_t GetApproximateKeys() const noexcept {
    if (ASTRADB_UNLIKELY(!db_)) return 0;
    
    std::string keys_str;
    db_->GetProperty("leveldb.approximate-keys", &keys_str);
    
    try {
      return std::stoull(keys_str);
    } catch (...) {
      return 0;
    }
  }

  // Get database size on disk
  uint64_t GetDiskSize() const noexcept {
    if (ASTRADB_UNLIKELY(!db_)) return 0;
    
    std::string size_str;
    db_->GetProperty("leveldb.approximate-size", &size_str);
    
    try {
      return std::stoull(size_str);
    } catch (...) {
      return 0;
    }
  }

  // Compact the database
  void Compact() noexcept {
    if (ASTRADB_UNLIKELY(!db_)) return;
    db_->CompactRange(nullptr, nullptr);
    ASTRADB_LOG_INFO("Database compaction completed");
  }

  // Compact a key range
  void CompactRange(absl::string_view start, absl::string_view end) noexcept {
    if (ASTRADB_UNLIKELY(!db_)) return;
    leveldb::Slice start_slice(start.data(), start.size());
    leveldb::Slice end_slice(end.data(), end.size());
    db_->CompactRange(&start_slice, &end_slice);
  }

 private:
  leveldb::DB* db_;
  leveldb::Cache* cache_;
  const leveldb::FilterPolicy* filter_policy_;
  LevelDBOptions options_;
  absl::Mutex mutex_;
  std::atomic<bool> is_open_;
};

}  // namespace astra::persistence