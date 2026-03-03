// ==============================================================================
// AstraDB Main Entry Point
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include "astra/server/server.hpp"
#include "astra/base/logging.hpp"
#include "astra/base/config.hpp"
#include "astra/base/macros.hpp"

#include <iostream>
#include <string>
#include <csignal>
#include <memory>
#include <thread>

#include <cxxopts.hpp>

namespace {

std::unique_ptr<astra::server::Server> g_server;

void SignalHandler(int signal) {
  ASTRADB_LOG_INFO("Received signal {}, shutting down...", signal);
  if (g_server) {
    g_server->Stop();
  }
}

void SetupSignalHandlers() {
  std::signal(SIGINT, SignalHandler);
  std::signal(SIGTERM, SignalHandler);
  
  // Ignore SIGPIPE
  std::signal(SIGPIPE, SIG_IGN);
}

}  // namespace

int main(int argc, char** argv) {
  // Parse command line arguments using cxxopts
  cxxopts::Options options("astradb", "AstraDB - High-Performance Redis-Compatible Database");
  
  options.add_options()
    ("c,config", "Configuration file (default: astradb.toml)", cxxopts::value<std::string>()->default_value("astradb.toml"))
    ("h,host", "Bind address (overrides config)", cxxopts::value<std::string>())
    ("p,port", "Port number (overrides config)", cxxopts::value<uint16_t>())
    ("t,threads", "Number of IO threads (overrides config, 0 = auto)", cxxopts::value<size_t>())
    ("s,shards", "Number of database shards (overrides config)", cxxopts::value<size_t>())
    ("d,databases", "Number of databases (overrides config)", cxxopts::value<size_t>())
    ("l,log-level", "Log level (overrides config)", cxxopts::value<std::string>())
    ("v,verbose", "Enable verbose logging (debug level)", cxxopts::value<bool>()->default_value("false"))
    ("help", "Show help message", cxxopts::value<bool>()->default_value("false"));
  
  auto result = options.parse(argc, argv);
  
  // Show help if requested
  if (result["help"].as<bool>()) {
    std::cout << options.help() << std::endl;
    return 0;
  }
  
  // Get config file path
  std::string config_file = result["config"].as<std::string>();
  
  // Load config from file
  auto config = astra::base::ServerConfig::LoadFromFile(config_file);
  
  // Override config with command line arguments
  if (result.count("host")) {
    config.host = result["host"].as<std::string>();
  }
  if (result.count("port")) {
    config.port = result["port"].as<uint16_t>();
  }
  if (result.count("threads")) {
    config.thread_count = result["threads"].as<size_t>();
  }
  if (result.count("shards")) {
    config.num_shards = result["shards"].as<size_t>();
  }
  if (result.count("databases")) {
    config.num_databases = result["databases"].as<size_t>();
  }
  if (result.count("log-level")) {
    config.log_level = result["log-level"].as<std::string>();
  }
  if (result["verbose"].as<bool>()) {
    config.log_level = "debug";
  }
  
  // Parse log level
  spdlog::level::level_enum log_level = spdlog::level::info;
  if (config.log_level == "trace") {
    log_level = spdlog::level::trace;
  } else if (config.log_level == "debug") {
    log_level = spdlog::level::debug;
  } else if (config.log_level == "info") {
    log_level = spdlog::level::info;
  } else if (config.log_level == "warn") {
    log_level = spdlog::level::warn;
  } else if (config.log_level == "error") {
    log_level = spdlog::level::err;
  } else if (config.log_level == "critical") {
    log_level = spdlog::level::critical;
  } else if (config.log_level == "off") {
    log_level = spdlog::level::off;
  }
  
  // Initialize logging
  astra::base::InitLogging(config.log_file, log_level, config.log_async, config.log_queue_size);
  
  ASTRADB_LOG_INFO("========================================");
  ASTRADB_LOG_INFO("AstraDB - High-Performance Redis-Compatible Database");
  ASTRADB_LOG_INFO("Version: {}", ASTRADB_VERSION_STRING);
  ASTRADB_LOG_INFO("========================================");
  
  // Print build information
  ASTRADB_LOG_INFO("Build Configuration:");
  ASTRADB_LOG_INFO("  Platform: {}", 
#if defined(ASTRADB_PLATFORM_WINDOWS)
      "Windows"
#elif defined(ASTRADB_PLATFORM_MACOS)
      "macOS"
#elif defined(ASTRADB_PLATFORM_LINUX)
      "Linux"
#else
      "Unknown"
#endif
  );
  
  ASTRADB_LOG_INFO("  Architecture: {}",
#if defined(ASTRADB_ARCH_X64)
      "x86_64"
#elif defined(ASTRADB_ARCH_ARM64)
      "ARM64"
#else
      "Unknown"
#endif
  );
  
  ASTRADB_LOG_INFO("  Compiler: {}",
#if defined(ASTRADB_COMPILER_CLANG)
      "Clang"
#elif defined(ASTRADB_COMPILER_GCC)
      "GCC"
#elif defined(ASTRADB_COMPILER_MSVC)
      "MSVC"
#else
      "Unknown"
#endif
  );
  
  ASTRADB_LOG_INFO("  C++ Standard: {}", __cplusplus);
  
  // Print features
  ASTRADB_LOG_INFO("\nEnabled Features:");
#if defined(ASTRADB_ENABLE_TLS)
  ASTRADB_LOG_INFO("  [+] TLS Encryption");
#endif
#if defined(ASTRADB_ENABLE_ACL)
  ASTRADB_LOG_INFO("  [+] Access Control List (ACL)");
#endif
#if defined(ASTRADB_ENABLE_SIMD)
  ASTRADB_LOG_INFO("  [+] SIMD Optimizations");
#endif
  
  ASTRADB_LOG_INFO("\n========================================");
  
  // Setup signal handlers
  SetupSignalHandlers();
  
  // Create server config from loaded config
  astra::server::ServerConfig server_config;
  server_config.host = config.host;
  server_config.port = config.port;
  server_config.max_connections = config.max_connections;
  server_config.num_databases = config.num_databases;
  server_config.num_shards = config.num_shards;
  server_config.thread_count = config.thread_count;
  
  // Copy persistence config
  server_config.persistence.enabled = config.persistence.enabled;
  server_config.persistence.data_dir = config.persistence.data_dir;
  server_config.persistence.write_buffer_size = config.persistence.write_buffer_size;
  server_config.persistence.cache_size = config.persistence.cache_size;
  server_config.persistence.sync_writes = config.persistence.sync_writes;
  
  // Copy cluster config
  server_config.cluster.enabled = config.cluster.enabled;
  server_config.cluster.node_id = config.cluster.node_id;
  server_config.cluster.bind_addr = config.cluster.bind_addr;
  server_config.cluster.gossip_port = config.cluster.gossip_port;
  server_config.cluster.shard_count = config.cluster.shard_count;
  server_config.cluster.seeds = config.cluster.seeds;
  
  ASTRADB_LOG_INFO("Server configuration:");
  ASTRADB_LOG_INFO("  Host: {}", server_config.host);
  ASTRADB_LOG_INFO("  Port: {}", server_config.port);
  ASTRADB_LOG_INFO("  Max Connections: {}", server_config.max_connections);
  ASTRADB_LOG_INFO("  Databases: {}", server_config.num_databases);
  ASTRADB_LOG_INFO("  Thread Count: {}", server_config.thread_count > 0 ? server_config.thread_count : std::thread::hardware_concurrency());
  ASTRADB_LOG_INFO("  Logging: {} ({}queue: {})", 
                  config.log_level, 
                  config.log_async ? "async, " : "sync, ",
                  config.log_queue_size);
  
  if (config.persistence.enabled) {
    ASTRADB_LOG_INFO("  Persistence: enabled (dir: {})", config.persistence.data_dir);
  }
  
  if (config.cluster.enabled) {
    ASTRADB_LOG_INFO("  Cluster: enabled (gossip port: {}, shards: {})", 
                    config.cluster.gossip_port, config.cluster.shard_count);
  }
  
  ASTRADB_LOG_INFO("========================================");
  ASTRADB_LOG_INFO("Starting AstraDB server...");
  ASTRADB_LOG_INFO("========================================");
  
  // Create and run server
  g_server = std::make_unique<astra::server::Server>(server_config);
  
  try {
    g_server->Run();
  } catch (const std::exception& e) {
    ASTRADB_LOG_ERROR("Server error: {}", e.what());
    return 1;
  }
  
  ASTRADB_LOG_INFO("========================================");
  ASTRADB_LOG_INFO("AstraDB stopped successfully!");
  ASTRADB_LOG_INFO("========================================");
  
  return 0;
}