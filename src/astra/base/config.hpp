// ==============================================================================
// Configuration
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include <string>
#include <memory>
#include <toml++/toml.hpp>

namespace astra::base {

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
  
  // Load from TOML file
  static ServerConfig LoadFromFile(const std::string& config_file);
  static ServerConfig LoadFromString(const std::string& config_str);
};

} // namespace astra::base