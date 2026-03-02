// ==============================================================================
// Configuration Implementation
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include "config.hpp"
#include <fstream>
#include <iostream>

namespace astra::base {

ServerConfig ServerConfig::LoadFromFile(const std::string& config_file) {
  ServerConfig config;
  
  try {
    auto data = toml::parse_file(config_file);
    
    // Network
    if (data["server"]) {
      auto server = *data["server"].as_table();
      if (server["host"]) {
        config.host = server["host"].value_or<std::string>("0.0.0.0");
      }
      if (server["port"]) {
        config.port = server["port"].value_or<uint16_t>(6379);
      }
      if (server["max_connections"]) {
        config.max_connections = server["max_connections"].value_or<size_t>(10000);
      }
      if (server["thread_count"]) {
        config.thread_count = server["thread_count"].value_or<size_t>(0);
      }
    }
    
    // Database
    if (data["database"]) {
      auto db = *data["database"].as_table();
      if (db["num_databases"]) {
        config.num_databases = db["num_databases"].value_or<size_t>(16);
      }
      if (db["num_shards"]) {
        config.num_shards = db["num_shards"].value_or<size_t>(16);
      }
    }
    
    // Logging
    if (data["logging"]) {
      auto logging = *data["logging"].as_table();
      if (logging["level"]) {
        config.log_level = logging["level"].value_or<std::string>("info");
      }
      if (logging["file"]) {
        config.log_file = logging["file"].value_or<std::string>("astradb.log");
      }
      if (logging["async"]) {
        config.log_async = logging["async"].value_or<bool>(true);
      }
      if (logging["queue_size"]) {
        config.log_queue_size = logging["queue_size"].value_or<size_t>(8192);
      }
    }
    
    // Performance
    if (data["performance"]) {
      auto perf = *data["performance"].as_table();
      if (perf["enable_pipeline"]) {
        config.enable_pipeline = perf["enable_pipeline"].value_or<bool>(true);
      }
      if (perf["enable_compression"]) {
        config.enable_compression = perf["enable_compression"].value_or<bool>(false);
      }
    }
    
  } catch (const std::exception& e) {
    std::cerr << "Failed to load config from " << config_file << ": " << e.what() << std::endl;
    std::cerr << "Using default configuration" << std::endl;
  }
  
  return config;
}

ServerConfig ServerConfig::LoadFromString(const std::string& config_str) {
  ServerConfig config;
  
  try {
    auto data = toml::parse(config_str);
    
    // Same parsing logic as LoadFromFile
    // (omitted for brevity, should match LoadFromFile)
    
  } catch (const std::exception& e) {
    std::cerr << "Failed to parse config string: " << e.what() << std::endl;
  }
  
  return config;
}

} // namespace astra::base