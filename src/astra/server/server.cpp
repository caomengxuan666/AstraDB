// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include "server.hpp"

namespace astra::server {

Server::Server(const ServerConfig& config)
    : config_(config), running_(false) {
  ASTRADB_LOG_INFO("Creating server with NO SHARING architecture");
  ASTRADB_LOG_INFO("Config: host={}, port={}, workers={}", config.host,
                   config.port, config.num_workers);

  // Create workers (each worker is completely independent)
  for (size_t i = 0; i < config.num_workers; ++i) {
    workers_.push_back(std::make_unique<Worker>(i, config.host, config.port));
  }

  ASTRADB_LOG_INFO("Server created successfully with {} workers", workers_.size());
}

Server::~Server() { ASTRADB_LOG_INFO("Server destroyed"); }

void Server::Start() {
  ASTRADB_LOG_INFO("Starting server...");

  // Start all workers
  for (auto& worker : workers_) {
    worker->Start();
  }

  running_ = true;
  ASTRADB_LOG_INFO("Server started successfully with {} workers", workers_.size());
}

void Server::Stop() {
  if (!running_) {
    return;
  }

  ASTRADB_LOG_INFO("Stopping server...");
  running_ = false;

  // Stop all workers
  for (auto& worker : workers_) {
    worker->Stop();
  }

  ASTRADB_LOG_INFO("Server stopped");
}

}  // namespace astra::server
