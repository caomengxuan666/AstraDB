// ==============================================================================
// AstraDB Main Entry Point
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include <csignal>
#include <cxxopts.hpp>
#include <fmt/color.h>
#include <fmt/core.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "astra/base/config.hpp"
#include "astra/base/logging.hpp"
#include "astra/base/macros.hpp"
#include "astra/base/version.hpp"
#include "astra/server/server.hpp"

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

  // Ignore SIGPIPE (Unix/Linux only)
#if !defined(_WIN32) && !defined(_WIN64)
  std::signal(SIGPIPE, SIG_IGN);
#endif
}

}  // namespace

int main(int argc, char** argv) {
  // Parse command line arguments using cxxopts
  cxxopts::Options options(
      "astradb", "AstraDB - High-Performance Redis-Compatible Database");

  options.add_options()(
      "c,config", "Configuration file (default: astradb.toml)",
      cxxopts::value<std::string>()->default_value("astradb.toml"))(
      "h,host", "Bind address (overrides config)",
      cxxopts::value<std::string>())("p,port", "Port number (overrides config)",
                                     cxxopts::value<uint16_t>())(
      "t,threads", "Number of IO threads (overrides config, 0 = auto)",
      cxxopts::value<size_t>())("s,shards",
                                "Number of database shards (overrides config)",
                                cxxopts::value<size_t>())(
      "d,databases", "Number of databases (overrides config)",
      cxxopts::value<size_t>())("l,log-level", "Log level (overrides config)",
                                cxxopts::value<std::string>())(
      "v,verbose", "Enable verbose logging (debug level)",
      cxxopts::value<bool>()->default_value("false"))(
      "help", "Show help message",
      cxxopts::value<bool>()->default_value("false"));

  auto result = options.parse(argc, argv);

  // Show help if requested
  if (result["help"].as<bool>()) {
    std::cout << options.help() << std::endl;
    return 0;
  }

  // Get config file path
  std::string config_file = result["config"].as<std::string>();

  // Prioritize development config if not explicitly specified
  if (config_file == "astradb.toml") {
    // Check if development config exists
    const std::string dev_config = "astradb-dev.toml";
    std::ifstream dev_file(dev_config);
    if (dev_file.good()) {
      config_file = dev_config;
    }
    dev_file.close();
  }

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
  astra::base::InitLogging(config.log_file, log_level, config.log_async,
                           config.log_queue_size);

  // Print AstraDB startup banner with ASCII art logo (always visible)
  // Use vibrant gradient colors from cyan to magenta via RGB
  fmt::print("\n");
  fmt::print(fmt::fg(fmt::rgb(0, 255, 255)) | fmt::emphasis::bold, "               AAA                                       tttt                                             DDDDDDDDDDDDD      BBBBBBBBBBBBBBBBB   \n");
  fmt::print(fmt::fg(fmt::rgb(0, 230, 255)) | fmt::emphasis::bold, "              A:::A                                   ttt:::t                                             D::::::::::::DDD   B::::::::::::::::B  \n");
  fmt::print(fmt::fg(fmt::rgb(0, 200, 255)) | fmt::emphasis::bold, "             A:::::A                                  t:::::t                                             D:::::::::::::::DD B::::::BBBBBB:::::B \n");
  fmt::print(fmt::fg(fmt::rgb(0, 175, 255)) | fmt::emphasis::bold, "            A:::::::A                                 t:::::t                                             DDD:::::DDDDD:::::DBB:::::B     B:::::B\n");
  fmt::print(fmt::fg(fmt::rgb(0, 150, 255)) | fmt::emphasis::bold, "           A:::::::::A             ssssssssss   ttttttt:::::ttttttt   rrrrr   rrrrrrrrr   aaaaaaaaaaaaa     D:::::D    D:::::D B::::B     B:::::B\n");
  fmt::print(fmt::fg(fmt::rgb(0, 125, 255)) | fmt::emphasis::bold, "          A:::::A:::::A          ss::::::::::s  t:::::::::::::::::t   r::::rrr:::::::::r  a::::::::::::a    D:::::D     D:::::DB::::B     B:::::B\n");
  fmt::print(fmt::fg(fmt::rgb(0, 100, 255)) | fmt::emphasis::bold, "         A:::::A A:::::A       ss:::::::::::::s t:::::::::::::::::t   r:::::::::::::::::r aaaaaaaaa:::::a   D:::::D     D:::::DB::::BBBBBB:::::B \n");
  fmt::print(fmt::fg(fmt::rgb(0, 75, 255)) | fmt::emphasis::bold,  "        A:::::A   A:::::A      s::::::ssss:::::stttttt:::::::tttttt   rr::::::rrrrr::::::r         a::::a   D:::::D     D:::::DB:::::::::::::BB  \n");
  fmt::print(fmt::fg(fmt::rgb(0, 50, 255)) | fmt::emphasis::bold,  "       A:::::A     A:::::A      s:::::s  ssssss       t:::::t          r:::::r     r:::::r  aaaaaaa:::::a   D:::::D     D:::::DB::::BBBBBB:::::B \n");
  fmt::print(fmt::fg(fmt::rgb(0, 25, 255)) | fmt::emphasis::bold,  "      A:::::AAAAAAAAA:::::A       s::::::s            t:::::t          r:::::r     rrrrrrraa::::::::::::a   D:::::D     D:::::DB::::B     B:::::B\n");
  fmt::print(fmt::fg(fmt::rgb(75, 0, 255)) | fmt::emphasis::bold,  "     A:::::::::::::::::::::A         s::::::s         t:::::t          r:::::r           a::::aaaa::::::a   D:::::D     D:::::DB::::B     B:::::B\n");
  fmt::print(fmt::fg(fmt::rgb(125, 0, 255)) | fmt::emphasis::bold, "    A:::::AAAAAAAAAAAAA:::::A  ssssss   s:::::s       t:::::t    ttttttr:::::r          a::::a    a:::::a   D:::::D    D:::::D B::::B     B:::::B\n");
  fmt::print(fmt::fg(fmt::rgb(175, 0, 255)) | fmt::emphasis::bold, "   A:::::A             A:::::A s:::::ssss::::::s      t::::::tttt:::::tr:::::r          a::::a    a:::::a DDD:::::DDDDD:::::DBB:::::BBBBBB::::::B\n");
  fmt::print(fmt::fg(fmt::rgb(200, 0, 255)) | fmt::emphasis::bold, "  A:::::A               A:::::As::::::::::::::s       tt::::::::::::::tr:::::r          a:::::aaaa::::::a D:::::::::::::::DD B:::::::::::::::::B \n");
  fmt::print(fmt::fg(fmt::rgb(225, 0, 255)) | fmt::emphasis::bold, " A:::::A                 A:::::As:::::::::::ss          tt:::::::::::ttr:::::r           a::::::::::aa:::aD::::::::::::DDD   B::::::::::::::::B  \n");
  fmt::print(fmt::fg(fmt::rgb(255, 0, 255)) | fmt::emphasis::bold, "AAAAAAA                   AAAAAAAsssssssssss              ttttttttttt  rrrrrrr            aaaaaaaaaa  aaaaDDDDDDDDDDDDD      BBBBBBBBBBBBBBBBB \n");
  fmt::print("\n");

  // Print version information
  ASTRADB_LOG_INFO("Version:     {}", ASTRADB_VERSION);
  ASTRADB_LOG_INFO("Build:       {} {}", __DATE__, __TIME__);
  ASTRADB_LOG_INFO("Git Branch:  {}", ASTRADB_GIT_BRANCH);
  ASTRADB_LOG_INFO("Git Commit:  {} ({})", ASTRADB_GIT_COMMIT_HASH,
                   ASTRADB_GIT_COMMIT_SHORT);
  ASTRADB_LOG_INFO("Git Status:  {}", ASTRADB_GIT_IS_DIRTY ? "Dirty" : "Clean");
  ASTRADB_LOG_INFO("========================================");

  // Print build information
  ASTRADB_LOG_INFO("Build Configuration:");

  // Platform detection
  const char* platform_name = "Unknown";
#if defined(ASTRADB_PLATFORM_WINDOWS)
  platform_name = "Windows";
#elif defined(ASTRADB_PLATFORM_MACOS)
  platform_name = "macOS";
#elif defined(ASTRADB_PLATFORM_LINUX)
  platform_name = "Linux";
#endif
  ASTRADB_LOG_INFO("  Platform: {}", platform_name);

  // Architecture detection
  const char* arch_name = "Unknown";
#if defined(ASTRADB_ARCH_X64)
  arch_name = "x86_64";
#elif defined(ASTRADB_ARCH_ARM64)
  arch_name = "ARM64";
#endif
  ASTRADB_LOG_INFO("  Architecture: {}", arch_name);

  // Compiler detection
  const char* compiler_name = "Unknown";
#if defined(ASTRADB_COMPILER_CLANG)
  compiler_name = "Clang";
#elif defined(ASTRADB_COMPILER_GCC)
  compiler_name = "GCC";
#elif defined(ASTRADB_COMPILER_MSVC)
  compiler_name = "MSVC";
#endif
  ASTRADB_LOG_INFO("  Compiler: {}", compiler_name);

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
#if defined(ASIO_HAS_IO_URING)
  ASTRADB_LOG_INFO("  [+] io_uring Support (Linux 5.1+)");
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
  server_config.num_workers = config.thread_count > 0 ? config.thread_count : std::thread::hardware_concurrency();
  server_config.use_async_commands = config.use_async_commands;
  server_config.use_per_worker_io = config.use_per_worker_io;
  server_config.use_so_reuseport = config.use_so_reuseport;

  // Copy persistence config
  server_config.persistence.enabled = config.persistence.enabled;
  server_config.persistence.data_dir = config.persistence.data_dir;
  server_config.persistence.write_buffer_size =
      config.persistence.write_buffer_size;
  server_config.persistence.cache_size = config.persistence.cache_size;
  server_config.persistence.sync_writes = config.persistence.sync_writes;

  // Copy cluster config
  server_config.cluster.enabled = config.cluster.enabled;
  server_config.cluster.node_id = config.cluster.node_id;
  server_config.cluster.bind_addr = config.cluster.bind_addr;
  server_config.cluster.gossip_port = config.cluster.gossip_port;
  server_config.cluster.shard_count = config.cluster.shard_count;
  server_config.cluster.seeds = config.cluster.seeds;

  // Also copy to direct member variables (used by Server)
  server_config.cluster_enabled = config.cluster.enabled;
  server_config.cluster_node_id = config.cluster.node_id;
  server_config.cluster_bind_addr = config.cluster.bind_addr;
  server_config.cluster_gossip_port = config.cluster.gossip_port;
  server_config.cluster_shard_count = config.cluster.shard_count;
  server_config.cluster_seeds = config.cluster.seeds;

  // Copy metrics config
  server_config.metrics.enabled = config.metrics.enabled;
  server_config.metrics.bind_addr = config.metrics.bind_addr;
  server_config.metrics.port = config.metrics.port;

  // Copy memory config
  server_config.memory.max_memory = config.memory.max_memory;
  server_config.memory.eviction_policy = config.memory.eviction_policy;
  server_config.memory.eviction_threshold = config.memory.eviction_threshold;
  server_config.memory.eviction_samples = config.memory.eviction_samples;
  server_config.memory.enable_tracking = config.memory.enable_tracking;

  // Copy RocksDB config
  server_config.rocksdb.enabled = config.rocksdb.enabled;
  server_config.rocksdb.data_dir = config.rocksdb.data_dir;
  server_config.rocksdb.enable_wal = config.rocksdb.enable_wal;
  server_config.rocksdb.cache_size = config.rocksdb.cache_size;
  server_config.rocksdb.create_if_missing = config.rocksdb.create_if_missing;

  ASTRADB_LOG_INFO("Server configuration:");
  ASTRADB_LOG_INFO("  Host: {}", server_config.host);
  ASTRADB_LOG_INFO("  Port: {}", server_config.port);
  ASTRADB_LOG_INFO("  Max Connections: {}", server_config.max_connections);
  ASTRADB_LOG_INFO("  Databases: {}", server_config.num_databases);
  ASTRADB_LOG_INFO("  Thread Count: {}",
                   server_config.thread_count > 0
                       ? server_config.thread_count
                       : std::thread::hardware_concurrency());
  ASTRADB_LOG_INFO("  Logging: {} ({}queue: {})", config.log_level,
                   config.log_async ? "async, " : "sync, ",
                   config.log_queue_size);

  if (config.persistence.enabled) {
    ASTRADB_LOG_INFO("  Persistence: enabled (dir: {})",
                     config.persistence.data_dir);
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
    g_server->Start();

    // Wait for server to stop (NO SHARING architecture - Start() is
    // non-blocking)
    while (g_server->IsRunning()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  } catch (const std::exception& e) {
    ASTRADB_LOG_ERROR("Server error: {}", e.what());
    return 1;
  }

  ASTRADB_LOG_INFO("========================================");
  ASTRADB_LOG_INFO("AstraDB stopped successfully!");
  ASTRADB_LOG_INFO("========================================");

  return 0;
}
