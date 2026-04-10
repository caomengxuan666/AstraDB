// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "astra/base/config.hpp"
#include "astra/base/logging.hpp"
#include "managers.hpp"  // Manager class definitions
#include "worker.hpp"

// Include cluster headers for full type definitions
#include "astra/cluster/cluster_config.hpp"
#include "astra/cluster/cluster_manager.hpp"
#include "astra/cluster/gossip_manager.hpp"
#include "astra/cluster/shard_manager.hpp"

namespace astra::server {

// Forward declarations
namespace astra::persistence {
class PersistenceManager;
}

// Note: ClusterManager, GossipManager, and ShardManager are included in
// server.cpp to avoid circular dependencies

namespace astra::commands {
class PubSubManager;
}

namespace astra::security {
class AclManager;
enum class AclPermission;
}  // namespace astra::security

namespace astra::core {
class MetricsManager;
}

// NO SHARING Server Configuration
// Extends base ServerConfig with NO SHARING architecture specific settings
struct NoSharingServerConfig : public ::astra::base::ServerConfig {
  // NO SHARING architecture settings
  size_t num_workers = 2;  // Number of workers (each has IO + Executor threads)
  bool use_so_reuseport =
      true;  // Enable SO_REUSEPORT for kernel load balancing

  // Cluster
  bool cluster_enabled = false;
  std::string cluster_node_id;
  std::string cluster_bind_addr = "0.0.0.0";
  uint16_t cluster_gossip_port = 7946;
  std::vector<std::string> cluster_seeds;
  uint32_t cluster_shard_count = 256;

  // ACL
  bool acl_enabled = true;
  std::string acl_default_user = "default";
  std::string acl_default_password = "";

  // Metrics
  bool metrics_enabled = false;
  std::string metrics_bind_addr = "0.0.0.0";
  [[maybe_unused]] uint16_t metrics_port = 9100;

  // Stats aggregation frequency (0 = disabled, 10 = 10 seconds, 60 = 1 minute)
  // Default: 10 seconds for optimal performance and monitoring
  int stats_frequency_seconds = 10;

  // Load configuration from file
  static NoSharingServerConfig LoadFromFile(const std::string& config_file) {
    // Load base configuration
    auto base_config = ::astra::base::ServerConfig::LoadFromFile(config_file);

    // Convert to NoSharingServerConfig
    NoSharingServerConfig config;

    // Copy base fields
    config.host = base_config.host;
    config.port = base_config.port;
    config.max_connections = base_config.max_connections;
    config.thread_count = base_config.thread_count;
    config.num_databases = base_config.num_databases;
    config.num_shards = base_config.num_shards;
    config.log_level = base_config.log_level;
    config.log_file = base_config.log_file;
    config.log_async = base_config.log_async;
    config.log_queue_size = base_config.log_queue_size;
    config.enable_pipeline = base_config.enable_pipeline;
    config.enable_compression = base_config.enable_compression;
    config.use_async_commands = base_config.use_async_commands;
    config.use_per_worker_io = base_config.use_per_worker_io;
    config.use_so_reuseport = base_config.use_so_reuseport;
    config.persistence = base_config.persistence;
    config.cluster = base_config.cluster;

    // Map cluster fields
    config.cluster_enabled = base_config.cluster.enabled;
    config.cluster_node_id = base_config.cluster.node_id;
    config.cluster_bind_addr = base_config.cluster.bind_addr;
    config.cluster_gossip_port = base_config.cluster.gossip_port;
    config.cluster_seeds = base_config.cluster.seeds;
    config.cluster_shard_count = base_config.cluster.shard_count;

    config.metrics = base_config.metrics;
    config.metrics_enabled = base_config.metrics.enabled;
    config.metrics_bind_addr = base_config.metrics.bind_addr;
    config.metrics_port = base_config.metrics.port;
    config.stats_frequency_seconds =
        base_config.metrics.stats_frequency_seconds;

    // Copy AOF configuration
    config.aof = base_config.aof;

    // Copy RDB configuration
    config.rdb = base_config.rdb;

    // Copy Memory configuration
    config.memory = base_config.memory;

    // Copy RocksDB configuration
    config.rocksdb = base_config.rocksdb;

    // Copy Replication configuration
    config.replication = base_config.replication;

    // Load ACL configuration from TOML
    try {
      toml::table config_table = toml::parse_file(config_file);

      // ACL configuration
      if (config_table.contains("acl")) {
        auto* acl_table = config_table["acl"].as_table();
        if (acl_table && acl_table->contains("enabled")) {
          if (auto* enabled_val = acl_table->get("enabled")) {
            config.acl_enabled = enabled_val->value<bool>().value_or(true);
          }
        }
        if (acl_table && acl_table->contains("default_user")) {
          if (auto* user_val = acl_table->get("default_user")) {
            config.acl_default_user =
                user_val->value<std::string>().value_or("default");
          }
        }
        if (acl_table && acl_table->contains("default_password")) {
          if (auto* password_val = acl_table->get("default_password")) {
            config.acl_default_password =
                password_val->value<std::string>().value_or("");
          }
        }
      }
    } catch (const std::exception& e) {
      // Use default ACL configuration if parsing fails
      config.acl_enabled = true;
      config.acl_default_user = "default";
      config.acl_default_password = "";
    }

    return config;
  }
};

// For backward compatibility
using ServerConfig = NoSharingServerConfig;

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

  // Getters for managers (for command handlers)
  class PersistenceManager* GetPersistenceManager() {
    return persistence_manager_.get();
  }
  cluster::ClusterManager* GetClusterManager() {
    return cluster_manager_.get();
  }
  cluster::GossipManager* GetGossipManager() { return gossip_manager_.get(); }
  cluster::ShardManager* GetShardManager() { return shard_manager_.get(); }
  ::astra::replication::ReplicationManager* GetReplicationManager() {
    return replication_manager_.get();
  }

  // Get connection info for CLIENT LIST command
  std::vector<std::tuple<uint64_t, std::string, std::string, int>>
  GetConnectionInfo() {
    std::vector<std::tuple<uint64_t, std::string, std::string, int>> all_info;
    for (auto& worker : workers_) {
      auto worker_info = worker->GetConnectionInfo();
      all_info.insert(all_info.end(), worker_info.begin(), worker_info.end());
    }
    return all_info;
  }

  // Kill a connection by ID (for CLIENT KILL command)
  bool KillConnection(uint64_t conn_id) {
    for (auto& worker : workers_) {
      if (worker->KillConnection(conn_id)) {
        return true;
      }
    }
    return false;
  }

  // Get worker scheduler (for cross-worker task dispatch)

  class WorkerScheduler* GetWorkerScheduler() {
    return worker_scheduler_.get();
  }
  // Update cluster state for all workers (uses DispatchOnAll to avoid deadlock)
  // Implementation is in server.cpp to avoid circular dependency with
  // worker.hpp
  void UpdateClusterState(std::shared_ptr<cluster::ClusterState> new_state);

 private:  // Initialize persistence
  bool InitPersistence() noexcept;

  // Initialize RDB
  bool InitRdb() noexcept;

  // Initialize cluster
  bool InitCluster() noexcept;

  // Handle cluster events (NO SHARING architecture - updates all workers'
  // ClusterState)
  void OnClusterEvent(cluster::ClusterEvent event,
                      const cluster::AstraNodeView& node_view) noexcept;

  // Process cluster event asynchronously (not in libgossip's tick thread)
  void ProcessClusterEventAsync(
      cluster::ClusterEvent event,
      const cluster::AstraNodeView& node_view) noexcept;

  // Initialize ACL
  bool InitACL() noexcept;

  // Initialize metrics

  bool InitMetrics() noexcept;

  // Initialize replication

  bool InitReplication() noexcept;

  // Stats aggregation (NO SHARING architecture - aggregates per-worker stats)
  void StartStatsAggregation();
  void StopStatsAggregation();
  void AggregateStats();
  // Configuration
  ServerConfig config_;

  // Workers (NO SHARING: each worker is completely independent)
  std::vector<std::unique_ptr<Worker>> workers_;

  // Worker scheduler (for cross-worker task dispatch)
  std::unique_ptr<class WorkerScheduler> worker_scheduler_;

  // Server-level managers (shared by all workers via MPSC if needed)
  std::unique_ptr<PersistenceManager> persistence_manager_;
  std::unique_ptr<cluster::ClusterManager> cluster_manager_;
  std::unique_ptr<cluster::GossipManager> gossip_manager_;
  std::unique_ptr<cluster::ShardManager> shard_manager_;
  std::unique_ptr<::astra::security::AclManager> acl_manager_;
  std::unique_ptr<MetricsManager> metrics_manager_;
  std::unique_ptr<::astra::replication::ReplicationManager>
      replication_manager_;

  // Initial ClusterState (set during InitCluster, used by Worker::Start)
  std::shared_ptr<cluster::ClusterState> initial_cluster_state_;

  // Server state
  std::atomic<bool> running_{false};

  // Stats aggregation thread (NO SHARING architecture)
  std::thread stats_aggregation_thread_;
  std::atomic<bool> stats_aggregation_running_{false};

  // Gossip tick thread (NO SHARING architecture)
  std::thread gossip_tick_thread_;

  // Disable copy and move
  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;
  Server(Server&&) = delete;
  Server& operator=(Server&&) = delete;
};

}  // namespace astra::server
