#pragma once

#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/slice.h>
#include <rocksdb/status.h>
#include <rocksdb/table.h>
#include <rocksdb/write_batch.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "astra/base/logging.hpp"
#include "astra/base/macros.hpp"

namespace astra {
namespace persistence {

/**
 * @brief RocksDB adapter for AstraDB cold data storage
 *
 * This adapter provides a simple interface to RocksDB for storing
 * cold data that has been evicted from memory. Each worker has its
 * own RocksDB instance to maintain NO SHADING architecture.
 *
 * Features:
 * - Simple key-value operations
 * - Batch write support
 * - Automatic compression
 * - Write-Ahead Log (WAL) for durability
 */
class RocksDBAdapter {
 public:
  /**
   * @brief RocksDB configuration
   */
  struct Config {
    // Database path (required)
    std::string db_path;

    // Cache size (default: 128MB)
    size_t cache_size = 128 * 1024 * 1024;

    // Write buffer size (default: 32MB)
    size_t write_buffer_size = 32 * 1024 * 1024;

    // Maximum number of write buffers (default: 3)
    int max_write_buffer_number = 3;

    // Maximum level 1 file size (default: 64MB)
    size_t max_file_size = 64 * 1024 * 1024;

    // Enable compression (default: false - compression requires external
    // libraries)
    bool enable_compression = false;

    // Compression type (default: kNoCompression)
    rocksdb::CompressionType compression_type = rocksdb::kNoCompression;

    // Enable WAL (default: true)
    bool enable_wal = true;

    // WAL recovery mode (default: kPointInTimeRecovery)
    rocksdb::WALRecoveryMode wal_recovery_mode =
        rocksdb::WALRecoveryMode::kPointInTimeRecovery;

    // Maximum number of open files (default: 1000)
    int max_open_files = 1000;

    // Create database if not exists (default: true)
    bool create_if_missing = true;

    // Enable statistics (default: false)
    bool enable_statistics = false;
  };

  /**
   * @brief Constructor
   * @param config RocksDB configuration
   */
  explicit RocksDBAdapter(const Config& config);

  /**
   * @brief Destructor
   */
  ~RocksDBAdapter();

  /**
   * @brief Put a key-value pair
   * @param key Key
   * @param value Value
   * @return true on success, false on failure
   */
  bool Put(const std::string& key, const std::string& value);

  /**
   * @brief Get a value by key
   * @param key Key
   * @return Value if found, std::nullopt otherwise
   */
  std::optional<std::string> Get(const std::string& key);

  /**
   * @brief Delete a key
   * @param key Key
   * @return true on success, false on failure
   */
  bool Delete(const std::string& key);

  /**
   * @brief Check if a key exists
   * @param key Key
   * @return true if exists, false otherwise
   */
  bool Exists(const std::string& key);

  /**
   * @brief Batch write multiple key-value pairs
   * @param kvs Vector of key-value pairs
   * @return true on success, false on failure
   */
  bool BatchPut(const std::vector<std::pair<std::string, std::string>>& kvs);

  /**
   * @brief Get approximate number of keys in the database
   * @return Approximate number of keys
   */
  size_t GetApproximateCount();

  /**
   * @brief Flush memtable to disk
   * @return true on success, false on failure
   */
  bool Flush();

  /**
   * @brief Compact database
   * @return true on success, false on failure
   */
  bool Compact();

  /**
   * @brief Check if database is open
   * @return true if open, false otherwise
   */
  bool IsOpen() const { return db_ != nullptr; }

  /**
   * @brief Get database path
   * @return Database path
   */
  const std::string& GetPath() const { return config_.db_path; }

  /**
   * @brief Get statistics string (if enabled)
   * @return Statistics string
   */
  std::string GetStatistics();

  // Prevent copying and moving
  ASTRABI_DISABLE_COPY(RocksDBAdapter);
  ASTRABI_DISABLE_MOVE(RocksDBAdapter);

 private:
  /**
   * @brief Configure RocksDB options
   * @param options Options to configure
   */
  void ConfigureOptions(rocksdb::Options& options);

 private:
  Config config_;
  std::unique_ptr<rocksdb::DB> db_;
  std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace persistence
}  // namespace astra
