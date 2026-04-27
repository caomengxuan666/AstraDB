// ==============================================================================
// AstraDB Main Entry Point
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include <fmt/color.h>
#include <fmt/core.h>

#include <csignal>
#include <cxxopts.hpp>
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
  auto config = astra::server::ServerConfig::LoadFromFile(config_file);

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

  // Print AstraDB startup banner (constellation logo + info)
  // Width: ~56 chars, fits standard 80-column terminals
  // Left: symmetric star-chart constellation, Right: product info

  auto C = [](int r, int g, int b) { return fmt::fg(fmt::rgb(r, g, b)); };
  auto B = fmt::emphasis::bold;
  auto LINE = C(80, 160, 240);
  auto DOT = C(255, 255, 0);
  auto STAR = C(255, 200, 0) | B;
  auto CENTER = C(255, 255, 255) | B;
  auto TEXT = C(180, 210, 255);
  auto VER = C(255, 180, 60);

  fmt::print("\n");
  fmt::print(STAR, "        ★                   ★      ");
  fmt::print(TEXT, " AstraDB  ");
  fmt::print(VER, "v{}\n", ASTRADB_VERSION);
  fmt::print(LINE, "       ╱ ╲                 ╱ ╲     ");
  fmt::print(TEXT, " Redis-compatible\n");
  fmt::print(DOT, "      ·───·");
  fmt::print(LINE, "───────────────");
  fmt::print(DOT, "·───·    ");
  fmt::print(TEXT, " NO SHARING · C++23\n");
  fmt::print(LINE, "     ╱                         ╲    ");
  fmt::print(TEXT, " 250+ Commands · SIMD\n");
  fmt::print(STAR, "    ★");
  fmt::print(LINE, "───────·───────────·───────");
  fmt::print(STAR, "★");
  fmt::print(TEXT, "   ★ Constellations ★\n");

  fmt::print(LINE, "     ╲     ╱ ╲           ╲     ╱");
  fmt::print(TEXT, "    "
#ifdef __clang__
  "Compiler: Clang"
#elif defined(__GNUC__)
  "Compiler: GCC"
#else
  "Compiler: Unknown"
#endif
  "\n");

  fmt::print(DOT, "      ·───");
  fmt::print(STAR, "★");
  fmt::print(DOT, "───·           ·───");
  fmt::print(STAR, "★");
  fmt::print(TEXT, "   "
#if defined(__linux__)
  "Platform: Linux"
#elif defined(__APPLE__)
  "Platform: macOS"
#elif defined(_WIN32)
  "Platform: Windows"
#endif
  " · "
#if defined(__x86_64__) || defined(_M_X64)
  "x86_64"
#elif defined(__aarch64__)
  "aarch64"
#endif
  "\n");

  fmt::print(LINE, "       ╲ ╱     ╲         ╱");
  fmt::print(TEXT, "       "
#ifdef __AVX2__
  "SIMD: AVX2"
#elif defined(__SSE4_2__)
  "SIMD: SSE4.2"
#elif defined(__ARM_NEON)
  "SIMD: NEON"
#else
  "SIMD: SSE2/SSE4"
#endif
  "\n");

  fmt::print(STAR, "        ★       ★───●───★");
  fmt::print(TEXT, "   Vector Search: hnswlib\n");

  fmt::print(LINE, "       ╱ ╲     ╱         ╲");
  fmt::print(TEXT, "       RocksDB · FlatBuffers\n");

  fmt::print(DOT, "      ·───·───");
  fmt::print(STAR, "★           ");
  fmt::print(LINE, "╲");
  fmt::print(TEXT, "          TLS: ");
#ifdef ASTRADB_ENABLE_TLS
  fmt::print(TEXT, "enabled\n");
#else
  fmt::print(TEXT, "disabled\n");
#endif

  fmt::print(LINE, "     ╱                         ╲");
  fmt::print(TEXT, "    Cluster: "
#ifdef ASTRADB_CLUSTER_ENABLED
  "enabled"
#else
  "disabled"
#endif
  "\n");

  fmt::print(STAR, "    ★");
  fmt::print(LINE, "───────·───────────·───────");
  fmt::print(STAR, "★\n");
  fmt::print(LINE, "     ╲                         ╱");
  fmt::print(TEXT, "    Threads: auto-detect\n");
  fmt::print(DOT, "      ·───·");
  fmt::print(LINE, "───────────────");
  fmt::print(DOT, "·───·\n");
  fmt::print(LINE, "       ╲ ╱                 ╲ ╱\n");
  fmt::print(STAR, "        ★                   ★\n");
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
  server_config.num_workers = config.thread_count > 0
                                  ? config.thread_count
                                  : std::thread::hardware_concurrency();
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
  server_config.cluster.use_tcp = config.cluster.use_tcp;

  // Also copy to direct member variables (used by Server)
  server_config.cluster_enabled = config.cluster.enabled;
  server_config.cluster_node_id = config.cluster.node_id;
  server_config.cluster_bind_addr = config.cluster.bind_addr;
  server_config.cluster_gossip_port = config.cluster.gossip_port;
  server_config.cluster_shard_count = config.cluster.shard_count;
  server_config.cluster_seeds = config.cluster.seeds;

  // Copy ACL config
  server_config.acl_enabled = config.acl_enabled;
  server_config.acl_default_user = config.acl_default_user;
  server_config.acl_default_password = config.acl_default_password;

  // Copy metrics config
  server_config.metrics.enabled = config.metrics.enabled;
  server_config.metrics.bind_addr = config.metrics.bind_addr;
  server_config.metrics.port = config.metrics.port;
  server_config.metrics_enabled = config.metrics.enabled;
  server_config.metrics_bind_addr = config.metrics.bind_addr;
  server_config.metrics_port = config.metrics.port;

  // Copy memory config
  server_config.memory.max_memory = config.memory.max_memory;
  server_config.memory.eviction_policy = config.memory.eviction_policy;
  server_config.memory.eviction_threshold = config.memory.eviction_threshold;
  server_config.memory.eviction_samples = config.memory.eviction_samples;
  server_config.memory.enable_tracking = config.memory.enable_tracking;

  // Copy replication config
  server_config.replication.enabled = config.replication.enabled;
  server_config.replication.role = config.replication.role;
  server_config.replication.master_host = config.replication.master_host;
  server_config.replication.master_port = config.replication.master_port;
  server_config.replication.master_auth = config.replication.master_auth;
  server_config.replication.read_only = config.replication.read_only;
  server_config.replication.repl_backlog_size =
      config.replication.repl_backlog_size;
  server_config.replication.repl_timeout = config.replication.repl_timeout;

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
