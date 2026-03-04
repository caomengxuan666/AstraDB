// ==============================================================================
// Configuration
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include <string>
#include <memory>
#include <vector>
#include <toml++/toml.hpp>

namespace astra::base {

// Persistence configuration
struct PersistenceConfig {
  bool enabled = false;
  std::string data_dir = "./data/astradb";
  size_t write_buffer_size = 4 * 1024 * 1024;  // 4MB
  size_t cache_size = 256 * 1024 * 1024;        // 256MB
  bool sync_writes = false;
};

// Cluster configuration
struct ClusterConfig {
  bool enabled = false;
  std::string node_id;
  std::string bind_addr = "0.0.0.0";
  uint16_t gossip_port = 7946;
  uint32_t shard_count = 256;
  std::vector<std::string> seeds;
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
  
  // Persistence (LevelDB)
  PersistenceConfig persistence;
  
  // Cluster
  ClusterConfig cluster;
  
  // Load from TOML file
  static ServerConfig LoadFromFile(const std::string& config_file);
  static ServerConfig LoadFromString(const std::string& config_str);
};

} // namespace astra::base