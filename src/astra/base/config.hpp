// ==============================================================================
// Configuration
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include <memory>
#include <string>
#include <toml++/toml.hpp>
#include <vector>

namespace astra::base {

// Storage mode configuration
enum class StorageMode {
  kRedis,   // Traditional Redis mode (RDB + AOF + RocksDB for cold data)
  kRocksDB  // RocksDB all-in mode (RocksDB for all data + AOF for WAL)
};

struct StorageConfig {
  StorageMode mode = StorageMode::kRedis;  // Default to Redis-compatible mode

  // Common settings for both modes
  bool enable_rocksdb_cold_data =
      true;                        // Redis mode: use RocksDB for evicted data
  bool enable_compression = true;  // Enable data compression
  std::string compression_type = "zlib";  // zlib, zstd, none

  // Redis mode specific settings
  struct RedisModeConfig {
    bool rdb_enabled = true;
    std::string rdb_path = "./data/dump.rdb";
    bool rdb_auto_save = false;
    int rdb_save_interval = 300;  // seconds

    bool aof_enabled = false;
    std::string aof_path = "./data/aof/appendonly.aof";
    bool aof_sync_everysec = true;
  };
  RedisModeConfig redis_mode;

  // RocksDB mode specific settings
  struct RocksDBModeConfig {
    std::string data_dir = "./data/rocksdb";
    size_t cache_size = 256 * 1024 * 1024;        // 256MB
    size_t write_buffer_size = 64 * 1024 * 1024;  // 64MB
    bool enable_wal = true;
    bool create_if_missing = true;
    int max_open_files = -1;                        // -1 = unlimited
    size_t max_total_wal_size = 100 * 1024 * 1024;  // 100MB
  };
  RocksDBModeConfig rocksdb_mode;
};

// Legacy persistence configuration (for backward compatibility)
struct PersistenceConfig {
  bool enabled = false;
  std::string data_dir = "./data/astradb";
  size_t write_buffer_size = 4 * 1024 * 1024;  // 4MB
  size_t cache_size = 256 * 1024 * 1024;       // 256MB
  bool sync_writes = false;
};

// RocksDB configuration for cold data storage
struct RocksDBConfig {
  bool enabled = false;
  std::string data_dir = "./data/rocksdb";
  bool enable_wal = true;
  size_t cache_size = 256 * 1024 * 1024;  // 256MB
  bool create_if_missing = true;
};

// Cluster configuration
struct ClusterConfig {
  bool enabled = false;
  std::string node_id;
  std::string bind_addr = "0.0.0.0";
  uint16_t gossip_port = 7946;
  uint32_t shard_count = 256;
  bool use_tcp = false;  // false = UDP, true = TCP
  std::vector<std::string> seeds;
};

// Memory configuration
struct MemoryConfig {
  uint64_t max_memory = 0;  // 0 = no limit
  std::string eviction_policy = "noeviction";
  double eviction_threshold = 0.9;  // 90%
  uint32_t eviction_samples = 5;    // Number of samples for LRU/LFU
  bool enable_tracking = true;
};

// Replication configuration
struct ReplicationConfig {
  bool enabled = false;
  std::string role = "master";  // master or slave
  std::string master_host = "127.0.0.1";
  uint16_t master_port = 6379;
  std::string master_auth = "";
  bool read_only = false;
  uint64_t repl_backlog_size = 1 * 1024 * 1024;  // 1MB
  uint32_t repl_timeout = 60;                    // seconds
};

struct ServerConfig {
  // Network
  std::string host = "0.0.0.0";
  uint16_t port = 6379;
  size_t max_connections = 10000;
  size_t thread_count = 0;  // 0 = auto-detect

  // Database
  size_t num_databases = 16;
  size_t num_shards = 16;

  // Logging
  std::string log_level = "info";
  std::string log_file = "astradb.log";
  bool log_async = true;
  size_t log_queue_size = 8192;

  // Performance
  bool enable_pipeline = true;
  bool enable_compression = false;

  // Async / Coroutine
  bool use_async_commands = true;

  // Network Architecture
  bool use_per_worker_io =
      false;  // Use per-worker IO architecture (SO_REUSEPORT)
  bool use_so_reuseport =
      true;  // Enable SO_REUSEPORT for kernel load balancing

  // Storage configuration (NEW: unified storage mode)
  StorageConfig storage;

  // Legacy persistence configuration (for backward compatibility)
  PersistenceConfig persistence;

  // Legacy RocksDB for cold data storage (for backward compatibility)
  RocksDBConfig rocksdb;

  // Legacy AOF configuration (for backward compatibility)
  struct AofConfig {
    bool enabled = false;
    std::string path = "./data/aof/appendonly.aof";
    bool sync_everysec = true;
  };
  AofConfig aof;

  // Legacy RDB configuration (for backward compatibility)
  struct RdbConfig {
    bool enabled = true;
    std::string path = "./data/dump.rdb";
    bool auto_save = false;
    int save_interval = 300;  // seconds
  };
  RdbConfig rdb;

  // Cluster
  ClusterConfig cluster;

  // Memory
  MemoryConfig memory;

  // Replication
  ReplicationConfig replication;

  // Metrics
  struct MetricsConfig {
    bool enabled = true;
    std::string bind_addr = "0.0.0.0";
    uint16_t port = 9100;
    // Stats aggregation frequency (0 = disabled, 10 = 10 seconds, 60 = 1
    // minute) Default: 10 seconds for optimal performance and monitoring Set to
    // 0 for maximum performance (INFO command will return outdated data) Set to
    // 1 for detailed monitoring (may impact performance)
    int stats_frequency_seconds = 10;
  };
  MetricsConfig metrics;

  // Load from TOML file
  static ServerConfig LoadFromFile(const std::string& config_file);
  static ServerConfig LoadFromString(const std::string& config_str);
};

}  // namespace astra::base
