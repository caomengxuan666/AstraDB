#include "astra/persistence/rocksdb_adapter.hpp"

#include <filesystem>

namespace astra {
namespace persistence {

RocksDBAdapter::RocksDBAdapter(const Config& config)
    : config_(config), logger_(spdlog::get("astradb")) {
  if (!logger_) {
    logger_ = spdlog::default_logger();
  }

  // Ensure directory exists
  std::filesystem::path db_path(config_.db_path);
  if (!std::filesystem::exists(db_path.parent_path())) {
    std::filesystem::create_directories(db_path.parent_path());
  }

  // Configure RocksDB options
  rocksdb::Options options;
  ConfigureOptions(options);

  // Open database
  rocksdb::Status status = rocksdb::DB::Open(options, config_.db_path, &db_);

  if (!status.ok()) {
    logger_->error("Failed to open RocksDB at {}: {}", config_.db_path,
                   status.ToString());
    db_ = nullptr;
    return;
  }

  logger_->info("RocksDB opened successfully at {}", config_.db_path);
}

RocksDBAdapter::~RocksDBAdapter() {
  if (db_) {
    db_->Close();
    logger_->info("RocksDB closed at {}", config_.db_path);
  }
}

bool RocksDBAdapter::Put(const std::string& key, const std::string& value) {
  if (!db_) {
    logger_->error("RocksDB is not open");
    return false;
  }

  rocksdb::Status status = db_->Put(rocksdb::WriteOptions(), key, value);

  if (!status.ok()) {
    logger_->error("Failed to put key {}: {}", key, status.ToString());
    return false;
  }

  return true;
}

std::optional<std::string> RocksDBAdapter::Get(const std::string& key) {
  if (!db_) {
    logger_->error("RocksDB is not open");
    return std::nullopt;
  }

  std::string value;
  rocksdb::Status status = db_->Get(rocksdb::ReadOptions(), key, &value);

  if (status.IsNotFound()) {
    return std::nullopt;
  }

  if (!status.ok()) {
    logger_->error("Failed to get key {}: {}", key, status.ToString());
    return std::nullopt;
  }

  return value;
}

bool RocksDBAdapter::Delete(const std::string& key) {
  if (!db_) {
    logger_->error("RocksDB is not open");
    return false;
  }

  rocksdb::Status status = db_->Delete(rocksdb::WriteOptions(), key);

  if (!status.ok()) {
    logger_->error("Failed to delete key {}: {}", key, status.ToString());
    return false;
  }

  return true;
}

bool RocksDBAdapter::Exists(const std::string& key) {
  if (!db_) {
    logger_->error("RocksDB is not open");
    return false;
  }

  std::string value;
  rocksdb::Status status = db_->Get(rocksdb::ReadOptions(), key, &value);

  return status.ok();
}

bool RocksDBAdapter::BatchPut(
    const std::vector<std::pair<std::string, std::string>>& kvs) {
  if (!db_) {
    logger_->error("RocksDB is not open");
    return false;
  }

  rocksdb::WriteBatch batch;
  for (const auto& kv : kvs) {
    batch.Put(kv.first, kv.second);
  }

  rocksdb::Status status = db_->Write(rocksdb::WriteOptions(), &batch);

  if (!status.ok()) {
    logger_->error("Failed to batch write: {}", status.ToString());
    return false;
  }

  return true;
}

size_t RocksDBAdapter::GetApproximateCount() {
  if (!db_) {
    logger_->error("RocksDB is not open");
    return 0;
  }

  uint64_t count;
  if (!db_->GetIntProperty(rocksdb::DB::Properties::kEstimateNumKeys, &count)) {
    logger_->error("Failed to get approximate count");
    return 0;
  }

  return static_cast<size_t>(count);
}

bool RocksDBAdapter::Flush() {
  if (!db_) {
    logger_->error("RocksDB is not open");
    return false;
  }

  rocksdb::Status status = db_->Flush(rocksdb::FlushOptions());

  if (!status.ok()) {
    logger_->error("Failed to flush: {}", status.ToString());
    return false;
  }

  return true;
}

bool RocksDBAdapter::Compact() {
  if (!db_) {
    logger_->error("RocksDB is not open");
    return false;
  }

  rocksdb::Status status =
      db_->CompactRange(rocksdb::CompactRangeOptions(), nullptr, nullptr);

  if (!status.ok()) {
    logger_->error("Failed to compact: {}", status.ToString());
    return false;
  }

  return true;
}

std::string RocksDBAdapter::GetStatistics() {
  if (!db_) {
    logger_->error("RocksDB is not open");
    return "";
  }

  std::string stats;
  if (!db_->GetProperty("rocksdb.stats", &stats)) {
    logger_->error("Failed to get statistics");
    return "";
  }

  return stats;
}

void RocksDBAdapter::ConfigureOptions(rocksdb::Options& options) {
  // Basic settings
  options.create_if_missing = config_.create_if_missing;

  // Performance settings
  options.write_buffer_size = config_.write_buffer_size;
  options.max_write_buffer_number = config_.max_write_buffer_number;
  options.max_log_file_size = config_.max_file_size;
  options.max_open_files = config_.max_open_files;

  // Compression
  if (config_.enable_compression) {
    options.compression = config_.compression_type;
  } else {
    options.compression = rocksdb::kNoCompression;
  }

  // WAL
  if (config_.enable_wal) {
    options.wal_dir = config_.db_path + "/wal";
    options.wal_recovery_mode = config_.wal_recovery_mode;
  }
  // Note: To disable WAL, set WriteOptions::disableWAL when writing

  // Statistics - not supported in RocksDB 10.x
  // TODO: Find alternative way to get statistics

  // Performance optimizations
  options.use_fsync = false;             // Use fdatasync instead of fsync
  options.bytes_per_sync = 1024 * 1024;  // 1MB

  // Table factory settings
  rocksdb::BlockBasedTableOptions table_options;
  table_options.block_cache = rocksdb::NewLRUCache(config_.cache_size);
  table_options.block_size = 16 * 1024;  // 16KB block size
  options.table_factory.reset(
      rocksdb::NewBlockBasedTableFactory(table_options));

  // Merge operator - not supported in RocksDB 10.x
  // TODO: Find alternative for StringAppendOperator
}

}  // namespace persistence
}  // namespace astra
