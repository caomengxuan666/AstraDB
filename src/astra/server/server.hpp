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
#include "astra/persistence/leveldb_adapter.hpp"
#include "astra/cluster/gossip_manager.hpp"
#include "astra/cluster/shard_manager.hpp"
#include "shard.hpp"

namespace astra::server {

// Persistence configuration
struct PersistenceConfig {
  bool enabled = false;
  std::string data_dir = "./data";
  bool sync_writes = false;
  size_t write_buffer_size = 4 * 1024 * 1024;  // 4MB
  size_t cache_size = 256 * 1024 * 1024;        // 256MB
};

// Cluster configuration
struct ClusterConfig {
  bool enabled = false;
  std::string node_id;
  std::string bind_addr = "0.0.0.0";
  uint16_t gossip_port = 7946;
  std::vector<std::string> seeds;  // Seed nodes for cluster join
  uint32_t shard_count = 256;      // Number of hash slots / shard count
};

// Server configuration
struct ServerConfig {
  std::string host = "0.0.0.0";
  uint16_t port = 6379;
  size_t thread_count = 0;  // 0 = number of CPU cores
  size_t max_connections = 10000;
  size_t num_databases = 16;
  size_t num_shards = 16;  // Number of database shards
  PersistenceConfig persistence;
  ClusterConfig cluster;
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
  
  // Get persistence layer
  persistence::LevelDBAdapter* GetPersistence() { return persistence_.get(); }
  
  // Get cluster shard manager
  cluster::ShardManager* GetClusterShardManager() { return cluster_shard_manager_.get(); }
  
  // Get gossip manager
  cluster::GossipManager* GetGossipManager() { return gossip_manager_.get(); }
  
  // Check if cluster mode is enabled
  bool IsClusterEnabled() const { return config_.cluster.enabled; }
  
  // Check if persistence is enabled
  bool IsPersistenceEnabled() const { return config_.persistence.enabled; }
  
  // Cluster operations
  bool ClusterMeet(const std::string& ip, int port);
  cluster::GossipManager* GetGossipManagerMutable() { return gossip_manager_.get(); }
  
 private:
  void DoAccept();
  void OnAccept(asio::error_code ec, asio::ip::tcp::socket socket);
  
  void HandleCommand(const protocol::Command& cmd, 
                     std::shared_ptr<network::Connection> conn);
  
  void SendResponse(std::shared_ptr<network::Connection> conn,
                    const commands::CommandResult& result);
  
  void StartExpirationCleaner();
  void CleanupExpiredKeys();
  
  void StartGossipTick();
  void GossipTickLoop();
  
  bool InitPersistence() noexcept;
  bool InitCluster() noexcept;
  
  ServerConfig config_;
  
  LocalShardManager local_shard_manager_;
  
  // Persistence layer (optional)
  std::unique_ptr<persistence::LevelDBAdapter> persistence_;
  
  // Cluster management (optional)
  std::unique_ptr<cluster::ShardManager> cluster_shard_manager_;
  std::unique_ptr<cluster::GossipManager> gossip_manager_;
  
  asio::io_context io_context_;
  std::unique_ptr<asio::executor_work_guard<asio::io_context::executor_type>> work_guard_;
  std::unique_ptr<asio::ip::tcp::acceptor> acceptor_;
  
  commands::CommandRegistry registry_;
  
  network::ConnectionPool connection_pool_;
  
  std::vector<std::thread> io_threads_;
  std::thread expiration_cleaner_thread_;
  std::thread gossip_tick_thread_;
  std::atomic<bool> running_;
  std::atomic<bool> cleaner_running_;
  std::atomic<bool> gossip_running_;
  std::atomic<uint64_t> total_commands_;
};

}  // namespace astra::server