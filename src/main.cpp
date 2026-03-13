// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include <chrono>
#include <csignal>
#include <filesystem>
#include <memory>
#include <thread>

#include "absl/log/log.h"
#include "absl/status/status.h"

#include "astra/base/logging.hpp"
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

#if !defined(_WIN32) && !defined(_WIN64)
  std::signal(SIGPIPE, SIG_IGN);
#endif
}

}  // namespace

absl::Status RunServer() {
  // Initialize logging first
  astra::base::InitLogging("", spdlog::level::debug, false, 1024);

  ASTRADB_LOG_INFO("========================================");
  ASTRADB_LOG_INFO("AstraDB Server (NO SHARING Architecture)");
  ASTRADB_LOG_INFO("========================================");

  // Setup signal handlers
  SetupSignalHandlers();

  // Load configuration from file if exists
  astra::server::NoSharingServerConfig config;
  if (std::filesystem::exists("astradb.toml")) {
    ASTRADB_LOG_INFO("Loading configuration from astradb.toml");
    config = astra::server::NoSharingServerConfig::LoadFromFile("astradb.toml");
  } else {
    ASTRADB_LOG_INFO("No astradb.toml found, using default configuration");
    config.host = "0.0.0.0";
    config.port = 6379;
    config.num_workers = 2;
  }  // Number of workers (each has IO + Executor threads)
  config.use_so_reuseport = true;  // Enable SO_REUSEPORT
  
  // Optional: Enable AOF persistence
  // config.aof_enabled = true;
  // config.aof_path = "./data/aof/appendonly.aof";
  
  // Optional: Enable cluster
  // config.cluster_enabled = true;
  // config.cluster_node_id = "node-1";
  
  // Optional: Enable ACL
  // config.acl_enabled = true;
  // config.acl_default_user = "default";
  
  // Optional: Enable metrics
  // config.metrics_enabled = true;
  // config.metrics_port = 9090;

  ASTRADB_LOG_INFO("Creating server with configuration:");
  ASTRADB_LOG_INFO("  Host: {}", config.host);
  ASTRADB_LOG_INFO("  Port: {}", config.port);
  ASTRADB_LOG_INFO("  Workers: {}", config.num_workers);
  ASTRADB_LOG_INFO("  SO_REUSEPORT: {}", config.use_so_reuseport ? "enabled" : "disabled");
  ASTRADB_LOG_INFO("  AOF enabled: {}", config.aof.enabled ? "yes" : "no");
  ASTRADB_LOG_INFO("  Cluster enabled: {}", config.cluster_enabled ? "yes" : "no");
  ASTRADB_LOG_INFO("  ACL enabled: {}", config.acl_enabled ? "yes" : "no");
  ASTRADB_LOG_INFO("  Metrics enabled: {}", config.metrics_enabled ? "yes" : "no");

  // Create server
  g_server = std::make_unique<astra::server::Server>(config);

  // Start server
  ASTRADB_LOG_INFO("Starting server...");
  g_server->Start();

  ASTRADB_LOG_INFO("Server running. Press Ctrl+C to stop.");

  // Keep main thread alive
  while (g_server->IsRunning()) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  // Server stopped
  ASTRADB_LOG_INFO("Server stopped.");

  return absl::OkStatus();
}

int main(int argc, char** argv) {
  absl::Status status = RunServer();
  
  if (!status.ok()) {
    ASTRADB_LOG_ERROR("Server failed: {}", status.ToString());
    return 1;
  }
  
  return 0;
}