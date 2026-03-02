// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <asio.hpp>
#include <memory>
#include <string>
#include <vector>

#include "astra/commands/command_handler.hpp"
#include "astra/commands/database.hpp"
#include "astra/network/connection.hpp"
#include "astra/base/logging.hpp"
#include "shard.hpp"

namespace astra::server {

// Server configuration
struct ServerConfig {
  std::string host = "0.0.0.0";
  uint16_t port = 6379;
  size_t thread_count = 0;  // 0 = number of CPU cores
  size_t max_connections = 10000;
  size_t num_databases = 16;
  size_t num_shards = 16;  // Number of database shards
};

// Redis-compatible server
class Server {
 public:
  explicit Server(const ServerConfig& config);
  ~Server();
  
  // Start the server (blocking)
  void Run();
  
  // Start the server in a background thread
  void Start();
  
  // Stop the server
  void Stop();
  
  // Get server status
  bool IsRunning() const { return running_; }
  
  // Get configuration
  const ServerConfig& GetConfig() const { return config_; }
  
  // Get command registry
  commands::CommandRegistry& GetRegistry() { return registry_; }
  
 private:
  void DoAccept();
  void OnAccept(asio::error_code ec, asio::ip::tcp::socket socket);
  
  void HandleCommand(const protocol::Command& cmd, 
                     std::shared_ptr<network::Connection> conn);
  
  void SendResponse(std::shared_ptr<network::Connection> conn,
                    const commands::CommandResult& result);
  
  void StartExpirationCleaner();
  void CleanupExpiredKeys();
  
  ServerConfig config_;
  
  ShardManager shard_manager_;
  asio::io_context io_context_;
  std::unique_ptr<asio::executor_work_guard<asio::io_context::executor_type>> work_guard_;
  std::unique_ptr<asio::ip::tcp::acceptor> acceptor_;
  
  commands::CommandRegistry registry_;
  
  network::ConnectionPool connection_pool_;
  
  std::vector<std::thread> io_threads_;
  std::thread expiration_cleaner_thread_;
  std::atomic<bool> running_;
  std::atomic<bool> cleaner_running_;
  std::atomic<uint64_t> total_commands_;
};

}  // namespace astra::server