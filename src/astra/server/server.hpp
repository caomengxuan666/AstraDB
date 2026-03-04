// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <asio.hpp>
#include <memory>
#include <string>
#include <vector>

#include "astra/commands/command_handler.hpp"
#include "astra/commands/database.hpp"
#include "astra/commands/pubsub_commands.hpp"
#include "astra/network/connection.hpp"
#include "astra/base/logging.hpp"
#include "astra/persistence/leveldb_adapter.hpp"
#include "astra/persistence/aof_writer.hpp"
#include "astra/persistence/rdb_writer.hpp"
#include "astra/replication/replication_manager.hpp"
#include "astra/cluster/gossip_manager.hpp"
#include "astra/cluster/shard_manager.hpp"
#include "astra/core/metrics.hpp"
#include "astra/core/async/executor.hpp"
#include "astra/core/async/future.hpp"
#include "astra/core/memory/buffer_pool.hpp"
#include "shard.hpp"

namespace astra::server {

// Persistence configuration
struct PersistenceConfig {
  bool enabled = false;
  std::string data_dir = "./data";
  bool sync_writes = false;
  size_t write_buffer_size = 4 * 1024 * 1024;  // 4MB
  size_t cache_size = 256 * 1024 * 1024;        // 256MB
  
  // AOF configuration
  bool aof_enabled = false;
  std::string aof_path = "./data/aof/appendonly.aof";
  bool aof_sync_everysec = true;  // true = everysec, false = always
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

// Metrics configuration
struct MetricsConfig {
  bool enabled = true;
  std::string bind_addr = "0.0.0.0";
  uint16_t port = 9090;  // Prometheus metrics port
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
  MetricsConfig metrics;
  
  // Async configuration
  bool use_async_commands = true;  // Use coroutines for command processing
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
  
  // Check if AOF is enabled
  bool IsAofEnabled() const { return aof_writer_ != nullptr; }
  
  // Get AOF writer (for internal use)
  persistence::AofWriter* GetAofWriter() { return aof_writer_.get(); }
  
  // Get RDB writer (for internal use)
  persistence::RdbWriter* GetRdbWriter() { return rdb_writer_.get(); }
  
  // Get replication manager (for internal use)
  replication::ReplicationManager* GetReplicationManager() { return replication_manager_.get(); }
  
  // Get main IO context (for internal use)
  asio::io_context& GetIoContext() { return io_context_; }
  
  // Cluster operations
  bool ClusterMeet(const std::string& ip, int port);
  cluster::GossipManager* GetGossipManagerMutable() { return gossip_manager_.get(); }

  // Pub/Sub operations
  commands::PubSubManager* GetPubSubManager() { return pubsub_manager_.get(); }

  // Get all active connections
  const absl::flat_hash_map<uint64_t, std::weak_ptr<network::Connection>>& GetConnections() const {
    return connections_;
  }

 private:
  void DoAccept();
  void OnAccept(asio::error_code ec, asio::ip::tcp::socket socket);
  
  void HandleCommand(const protocol::Command& cmd, 
                     std::shared_ptr<network::Connection> conn);
  
  // Coroutine-based command handler (async/await)
  asio::awaitable<void> HandleCommandAsync(const protocol::Command& cmd,
                                          std::shared_ptr<network::Connection> conn);
  
  void SendResponse(std::shared_ptr<network::Connection> conn,
                    const commands::CommandResult& result);
  
  void StartExpirationCleaner();
  void CleanupExpiredKeys();
  
  void StartGossipTick();
  void GossipTickLoop();
  
  void StartAofRewriteChecker();
  void AofRewriteCheckerLoop();
  bool PerformAofRewrite();
  
  void StartRdbSaver();
  void RdbSaverLoop();
  bool PerformRdbSave();
  
  bool InitPersistence() noexcept;
  bool InitCluster() noexcept;
  
  ServerConfig config_;
  
  LocalShardManager local_shard_manager_;
  
  // Persistence layer (optional)
  std::unique_ptr<persistence::LevelDBAdapter> persistence_;
  std::unique_ptr<persistence::AofWriter> aof_writer_;
  std::unique_ptr<persistence::RdbWriter> rdb_writer_;
  
  // Replication layer (optional)
  std::unique_ptr<replication::ReplicationManager> replication_manager_;
  
  // Cluster management (optional)
  std::unique_ptr<cluster::ShardManager> cluster_shard_manager_;
  std::unique_ptr<cluster::GossipManager> gossip_manager_;
  
  // CORE components
  std::unique_ptr<core::async::Executor> executor_;
  std::unique_ptr<core::memory::BufferPool> buffer_pool_;
  
  asio::io_context io_context_;
  std::unique_ptr<asio::executor_work_guard<asio::io_context::executor_type>> work_guard_;
  std::unique_ptr<asio::ip::tcp::acceptor> acceptor_;
  
  commands::CommandRegistry registry_;
  
  // Connection pool (uses buffer_pool_, created after buffer_pool init)
  std::unique_ptr<network::ConnectionPool> connection_pool_;
  
  std::vector<std::thread> io_threads_;
  std::thread expiration_cleaner_thread_;
  std::thread gossip_tick_thread_;
  std::thread aof_rewrite_thread_;
  std::thread rdb_save_thread_;
  std::atomic<bool> running_;
  std::atomic<bool> cleaner_running_;
  std::atomic<bool> aof_rewrite_running_;
  std::atomic<bool> rdb_save_running_;
  std::atomic<uint64_t> total_commands_;

  // ============== Pub/Sub Support ==============
  // Channel subscriptions: channel -> set of connection IDs
  absl::flat_hash_map<std::string, absl::flat_hash_set<uint64_t>> channel_subscribers_;
  // Pattern subscriptions: pattern -> set of connection IDs
  absl::flat_hash_map<std::string, absl::flat_hash_set<uint64_t>> pattern_subscribers_;
  // Connection subscriptions: conn_id -> set of channels/patterns
  absl::flat_hash_map<uint64_t, absl::flat_hash_set<std::string>> conn_channels_;
  absl::flat_hash_map<uint64_t, absl::flat_hash_set<std::string>> conn_patterns_;
  // Connection to send messages
  absl::flat_hash_map<uint64_t, std::weak_ptr<network::Connection>> connections_;
  absl::Mutex pubsub_mutex_;

  // PubSubManager implementation
  class ServerPubSubManager : public commands::PubSubManager {
   public:
    explicit ServerPubSubManager(Server* server) : server_(server) {}

    void Subscribe(uint64_t conn_id, const std::vector<std::string>& channels) override;
    void Unsubscribe(uint64_t conn_id, const std::vector<std::string>& channels) override;
    void PSubscribe(uint64_t conn_id, const std::vector<std::string>& patterns) override;
    void PUnsubscribe(uint64_t conn_id, const std::vector<std::string>& patterns) override;
    size_t Publish(const std::string& channel, const std::string& message) override;
    size_t GetSubscriptionCount(uint64_t conn_id) const override;
    bool IsSubscribed(uint64_t conn_id) const override;

   private:
    Server* server_;
    void SendSubscribeReply(uint64_t conn_id, const std::string& channel, size_t count);
    void SendUnsubscribeReply(uint64_t conn_id, const std::string& channel, size_t count);
  };

  std::unique_ptr<ServerPubSubManager> pubsub_manager_;

  // Helper to register connection for pub/sub
  void RegisterPubSubConnection(uint64_t conn_id, std::shared_ptr<network::Connection> conn);
  void UnregisterPubSubConnection(uint64_t conn_id);

  std::atomic<bool> gossip_running_;
};

}  // namespace astra::server