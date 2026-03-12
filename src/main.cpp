// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <thread>

#include "astra/base/logging.hpp"
#include "astra/server/server.hpp"

namespace {

std::unique_ptr<astra::server::Server> g_server;

void SignalHandler(int signal) {
  std::cout << "[Main] Received signal " << signal << ", shutting down..."
            << std::endl;
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

int main(int argc, char** argv) {
  // Initialize logging first (required for logging in other parts)
  astra::base::InitLogging("", spdlog::level::debug, false, 1024);

  std::cout << "======================================" << std::endl;
  std::cout << "AstraDB Server (Per-Worker IO)" << std::endl;
  std::cout << "======================================" << std::endl;

  ASTRADB_LOG_INFO("========================================");
  ASTRADB_LOG_INFO("AstraDB Server (Per-Worker IO)");
  ASTRADB_LOG_INFO("========================================");

  // Setup signal handlers
  SetupSignalHandlers();

  // Create server configuration
  astra::server::ServerConfig config;
  config.host = "0.0.0.0";
  config.port = 6379;
  config.num_workers = 2;  // Number of workers (each worker has IO + Executor threads)
  config.use_so_reuseport = true;  // Enable SO_REUSEPORT

  std::cout << "[Main] Creating server with configuration:" << std::endl;
  std::cout << "  Host: " << config.host << std::endl;
  std::cout << "  Port: " << config.port << std::endl;
  std::cout << "  Workers: " << config.num_workers << " (each has IO + Executor threads)" << std::endl;
  std::cout << "  SO_REUSEPORT: " << (config.use_so_reuseport ? "enabled" : "disabled")
            << std::endl;

  ASTRADB_LOG_INFO("Creating server with configuration:");
  ASTRADB_LOG_INFO("  Host: {}", config.host);
  ASTRADB_LOG_INFO("  Port: {}", config.port);
  ASTRADB_LOG_INFO("  Workers: {}", config.num_workers);
  ASTRADB_LOG_INFO("  SO_REUSEPORT: {}", config.use_so_reuseport ? "enabled" : "disabled");

  // Create server
  g_server = std::make_unique<astra::server::Server>(config);

  // Start server
  std::cout << "[Main] Starting server..." << std::endl;
  g_server->Start();

  std::cout << "[Main] Server running. Press Ctrl+C to stop." << std::endl;
  ASTRADB_LOG_INFO("Server running. Press Ctrl+C to stop.");

  // Keep main thread alive
  while (g_server->IsRunning()) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  // Wait for server to finish
  std::cout << "[Main] Server stopped." << std::endl;
  ASTRADB_LOG_INFO("Server stopped.");

  return 0;
}
