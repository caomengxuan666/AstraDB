// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "astra/base/logging.hpp"
#include "worker.hpp"

namespace astra::server {

// Simple server configuration
struct ServerConfig {
  std::string host = "0.0.0.0";
  uint16_t port = 6379;
  size_t num_workers = 2;  // Number of workers (each has IO + Executor threads)
  bool use_so_reuseport = true;  // Enable SO_REUSEPORT for kernel load balancing
};

// Server using NO SHARING architecture
// Each worker is completely independent with its own IO and Executor threads
class Server {
 public:
  explicit Server(const ServerConfig& config);
  ~Server();

  // Start the server
  void Start();

  // Stop the server
  void Stop();

  // Check if server is running
  bool IsRunning() const { return running_; }

 private:
  // Configuration
  ServerConfig config_;

  // Workers (NO SHARING: each worker is completely independent)
  std::vector<std::unique_ptr<Worker>> workers_;

  // Server state
  std::atomic<bool> running_{false};

  // Disable copy and move
  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;
  Server(Server&&) = delete;
  Server& operator=(Server&&) = delete;
};

}  // namespace astra::server