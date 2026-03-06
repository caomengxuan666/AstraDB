// ==============================================================================
// Snapshot Manager - RDB-like Snapshot Persistence (Optimized with Abseil)
// ==============================================================================
// License: Apache 2.0
// ==============================================================================
// Design Principles:
// - noexcept for all performance-critical paths
// - Abseil containers instead of STL
// - Cross-platform: Linux/Windows/macOS
// - Binary format for fast serialization
// ==============================================================================

#pragma once

#include <absl/synchronization/mutex.h>
#include "leveldb_adapter.hpp"

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <absl/strings/string_view.h>
#include <absl/strings/str_cat.h>
#include <absl/time/time.h>

#include <memory>
#include <mutex>
#include <atomic>
#include <fstream>
#include <filesystem>

#include <absl/synchronization/mutex.h>
#include "astra/base/macros.hpp"
#include <absl/synchronization/mutex.h>
#include "astra/base/logging.hpp"

namespace astra::persistence {

// Snapshot format version
constexpr uint32_t SNAPSHOT_VERSION = 2;  // Version 2 with optimizations

// Snapshot magic number "ASTR"
constexpr uint32_t SNAPSHOT_MAGIC = 0x41535452;

// Snapshot header (packed for binary compatibility)
struct alignas(16) SnapshotHeader {
  uint32_t magic;
  uint32_t version;
  uint64_t create_time_ms;
  uint64_t key_count;
  uint32_t checksum;      // CRC32 of data
  uint32_t reserved;      // For future use
};

static_assert(sizeof(SnapshotHeader) == 32, "SnapshotHeader must be 32 bytes");

// Snapshot options
struct SnapshotOptions {
  std::string snapshot_dir = "./data/snapshots";
  size_t max_snapshots = 5;  // Keep last N snapshots
  bool compress = true;
  bool verify_checksum = true;
  size_t batch_size = 10000;  // Batch write threshold
};

// Forward declaration
class Database;

// Snapshot manager - handles database snapshots (RDB-like)
class SnapshotManager {
 public:
  SnapshotManager() noexcept = default;
  ~SnapshotManager() noexcept = default;

  // Non-copyable
  SnapshotManager(const SnapshotManager&) = delete;
  SnapshotManager& operator=(const SnapshotManager&) = delete;

  // Not movable (contains mutex)
  SnapshotManager(SnapshotManager&&) noexcept = delete;
  SnapshotManager& operator=(SnapshotManager&&) noexcept = delete;

  // Initialize with options
  bool Init(const SnapshotOptions& options) noexcept {
    options_ = options;
    
    // Create snapshot directory if not exists
    std::error_code ec;
    std::filesystem::create_directories(options.snapshot_dir, ec);
    if (ec) {
      ASTRADB_LOG_ERROR("Failed to create snapshot directory: {}", ec.message());
      return false;
    }
    
    initialized_.store(true, std::memory_order_release);
    return true;
  }

  // Create a full snapshot of the database
  bool CreateSnapshot(LevelDBAdapter& adapter,
                      const std::string& name = "") noexcept {
    if (ASTRADB_UNLIKELY(!initialized_.load(std::memory_order_acquire))) {
      ASTRADB_LOG_ERROR("SnapshotManager not initialized");
      return false;
    }

    absl::MutexLock lock(&mutex_);

    const auto start_time = absl::Now();

    // Generate snapshot name if not provided
    std::string snapshot_name = name.empty() ? GenerateSnapshotName() : name;
    std::string snapshot_path = absl::StrCat(options_.snapshot_dir, "/", snapshot_name, ".snap");

    ASTRADB_LOG_INFO("Creating snapshot: {}", snapshot_path);

    // Open snapshot file
    std::ofstream ofs(snapshot_path, std::ios::binary);
    if (ASTRADB_UNLIKELY(!ofs.is_open())) {
      ASTRADB_LOG_ERROR("Failed to create snapshot file: {}", snapshot_path);
      return false;
    }

    // Write header placeholder
    SnapshotHeader header{};
    header.magic = SNAPSHOT_MAGIC;
    header.version = SNAPSHOT_VERSION;
    header.create_time_ms = GetCurrentTimeMs();
    header.key_count = 0;
    header.checksum = 0;
    header.reserved = 0;

    ofs.write(reinterpret_cast<const char*>(&header), sizeof(header));

    // Write data entries by type
    uint64_t key_count = 0;

    // Save all data types
    const auto write_entries = [&](KeyPrefix prefix, EntryType type) {
      std::string prefix_str = std::string(1, static_cast<char>(prefix)) + ":";
      adapter.Scan(prefix_str, [&](absl::string_view key, absl::string_view value) {
        WriteEntry(ofs, type, key, value);
        ++key_count;
        return true;
      });
    };

    write_entries(KeyPrefix::kString, EntryType::kString);
    write_entries(KeyPrefix::kHash, EntryType::kHash);
    write_entries(KeyPrefix::kSet, EntryType::kSet);
    write_entries(KeyPrefix::kZSet, EntryType::kZSet);
    write_entries(KeyPrefix::kList, EntryType::kList);
    write_entries(KeyPrefix::kMeta, EntryType::kMeta);
    write_entries(KeyPrefix::kTTL, EntryType::kTTL);

    // Write end marker
    WriteEndMarker(ofs);

    // Update header with actual key count
    header.key_count = key_count;
    ofs.seekp(0);
    ofs.write(reinterpret_cast<const char*>(&header), sizeof(header));

    ofs.close();

    const auto duration = absl::Now() - start_time;
    const auto duration_ms = absl::ToInt64Milliseconds(duration);

    ASTRADB_LOG_INFO("Snapshot created: {} keys in {}ms", key_count, duration_ms);

    // Cleanup old snapshots
    CleanupOldSnapshots();

    return true;
  }

  // Restore database from snapshot
  bool RestoreSnapshot(LevelDBAdapter& adapter,
                       const std::string& name = "") noexcept {
    if (ASTRADB_UNLIKELY(!initialized_.load(std::memory_order_acquire))) {
      ASTRADB_LOG_ERROR("SnapshotManager not initialized");
      return false;
    }

    absl::MutexLock lock(&mutex_);

    // Find latest snapshot if name not provided
    std::string snapshot_path;
    if (name.empty()) {
      snapshot_path = FindLatestSnapshot();
      if (snapshot_path.empty()) {
        ASTRADB_LOG_ERROR("No snapshot found");
        return false;
      }
    } else {
      snapshot_path = absl::StrCat(options_.snapshot_dir, "/", name, ".snap");
    }

    ASTRADB_LOG_INFO("Restoring snapshot: {}", snapshot_path);

    // Open snapshot file
    std::ifstream ifs(snapshot_path, std::ios::binary);
    if (ASTRADB_UNLIKELY(!ifs.is_open())) {
      ASTRADB_LOG_ERROR("Failed to open snapshot file: {}", snapshot_path);
      return false;
    }

    // Read header
    SnapshotHeader header;
    ifs.read(reinterpret_cast<char*>(&header), sizeof(header));

    // Validate header
    if (header.magic != SNAPSHOT_MAGIC) {
      ASTRADB_LOG_ERROR("Invalid snapshot magic: expected {}, got {}", 
                    SNAPSHOT_MAGIC, header.magic);
      return false;
    }

    if (header.version > SNAPSHOT_VERSION) {
      ASTRADB_LOG_ERROR("Unsupported snapshot version: {}", header.version);
      return false;
    }

    ASTRADB_LOG_INFO("Snapshot info: version={}, keys={}, created={}",
                 header.version, header.key_count, header.create_time_ms);

    // Read and restore entries using batch writes
    uint64_t restored = 0;
    LevelDBAdapter::WriteBatch batch;
    size_t batch_size = 0;

    while (ifs.good()) {
      EntryType type;
      std::string key, value;

      if (!ReadEntry(ifs, type, key, value)) {
        break;
      }

      // Add to batch
      batch.Put(key, value);
      ++batch_size;
      ++restored;

      // Write batch when threshold reached
      if (batch_size >= options_.batch_size) {
        adapter.Write(batch);
        batch.Clear();
        batch_size = 0;
        
        // Log progress every 100k keys
        if (restored % 100000 == 0) {
          ASTRADB_LOG_INFO("Restored {} keys...", restored);
        }
      }
    }

    // Write remaining batch
    if (batch_size > 0) {
      adapter.Write(batch);
    }

    ifs.close();

    ASTRADB_LOG_INFO("Snapshot restored: {} keys", restored);
    return true;
  }

  // List available snapshots
  std::vector<std::string> ListSnapshots() const noexcept {
    std::vector<std::string> snapshots;

    if (!std::filesystem::exists(options_.snapshot_dir)) {
      return snapshots;
    }

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(options_.snapshot_dir, ec)) {
      if (entry.path().extension() == ".snap") {
        snapshots.push_back(entry.path().stem().string());
      }
    }

    // Sort by name (timestamp-based, newest first)
    std::sort(snapshots.begin(), snapshots.end(), std::greater<>());

    return snapshots;
  }

  // Delete a snapshot
  bool DeleteSnapshot(const std::string& name) noexcept {
    std::string path = absl::StrCat(options_.snapshot_dir, "/", name, ".snap");
    
    std::error_code ec;
    return std::filesystem::remove(path, ec);
  }

  // Get latest snapshot info
  bool HasSnapshot() const noexcept {
    return !FindLatestSnapshot().empty();
  }

 private:
  // Entry types in snapshot
  enum class EntryType : uint8_t {
    kString = 0,
    kHash = 1,
    kSet = 2,
    kZSet = 3,
    kList = 4,
    kMeta = 5,
    kTTL = 6,
    kEnd = 255
  };

  // Write an entry to snapshot (binary format)
  void WriteEntry(std::ofstream& ofs, EntryType type,
                  absl::string_view key, absl::string_view value) noexcept {
    // Write type (1 byte)
    uint8_t type_byte = static_cast<uint8_t>(type);
    ofs.write(reinterpret_cast<const char*>(&type_byte), 1);

    // Write key length (4 bytes) and key
    const uint32_t key_len = static_cast<uint32_t>(key.size());
    ofs.write(reinterpret_cast<const char*>(&key_len), 4);
    ofs.write(key.data(), key_len);

    // Write value length (4 bytes) and value
    const uint32_t value_len = static_cast<uint32_t>(value.size());
    ofs.write(reinterpret_cast<const char*>(&value_len), 4);
    ofs.write(value.data(), value_len);
  }

  // Write end marker
  void WriteEndMarker(std::ofstream& ofs) noexcept {
    uint8_t end_marker = static_cast<uint8_t>(EntryType::kEnd);
    ofs.write(reinterpret_cast<const char*>(&end_marker), 1);
  }

  // Read an entry from snapshot
  bool ReadEntry(std::ifstream& ifs, EntryType& type,
                 std::string& key, std::string& value) noexcept {
    // Read type
    uint8_t type_byte;
    ifs.read(reinterpret_cast<char*>(&type_byte), 1);
    
    if (ifs.eof() || type_byte == static_cast<uint8_t>(EntryType::kEnd)) {
      return false;
    }
    
    type = static_cast<EntryType>(type_byte);

    // Read key
    uint32_t key_len;
    ifs.read(reinterpret_cast<char*>(&key_len), 4);
    if (ifs.eof()) return false;
    
    key.resize(key_len);
    ifs.read(key.data(), key_len);

    // Read value
    uint32_t value_len;
    ifs.read(reinterpret_cast<char*>(&value_len), 4);
    if (ifs.eof()) return false;
    
    value.resize(value_len);
    ifs.read(value.data(), value_len);

    return true;
  }

  // Generate snapshot name based on timestamp
  std::string GenerateSnapshotName() const noexcept {
    const auto now = absl::Now();
    const int64_t ms = absl::ToUnixMillis(now);
    return absl::StrCat("snapshot_", ms);
  }

  // Find latest snapshot
  std::string FindLatestSnapshot() const noexcept {
    auto snapshots = ListSnapshots();
    if (snapshots.empty()) {
      return "";
    }
    return absl::StrCat(options_.snapshot_dir, "/", snapshots[0], ".snap");
  }

  // Cleanup old snapshots
  void CleanupOldSnapshots() noexcept {
    auto snapshots = ListSnapshots();
    while (snapshots.size() > options_.max_snapshots) {
      DeleteSnapshot(snapshots.back());
      snapshots.pop_back();
    }
  }

  // Get current time in milliseconds since epoch
  static int64_t GetCurrentTimeMs() noexcept {
    return absl::ToUnixMillis(absl::Now());
  }

  SnapshotOptions options_;
  mutable absl::Mutex mutex_;
  std::atomic<bool> initialized_{false};
};

}  // namespace astra::persistence