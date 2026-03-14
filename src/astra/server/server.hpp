// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "astra/base/config.hpp"
#include "astra/base/logging.hpp"
#include "managers.hpp"  // Manager class definitions
#include "worker.hpp"

namespace astra::server {

// Forward declarations
namespace astra::persistence {
class PersistenceManager;
}

namespace astra::cluster {
class ClusterManager;
}

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
  bool metrics_enabled = true;
  std::string metrics_bind_addr = "0.0.0.0";
  uint16_t metrics_port = 9999;

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
    config.metrics = base_config.metrics;

    // Copy AOF configuration
    config.aof = base_config.aof;

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
  class ClusterManager* GetClusterManager() { return cluster_manager_.get(); }
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

 private:
  // Initialize persistence
  bool InitPersistence() noexcept;

  // Initialize RDB
  bool InitRdb() noexcept;

  // Initialize cluster
  bool InitCluster() noexcept;

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

  // Server-level managers (shared by all workers via MPSC if needed)
  std::unique_ptr<PersistenceManager> persistence_manager_;
  std::unique_ptr<ClusterManager> cluster_manager_;
  std::unique_ptr<::astra::security::AclManager> acl_manager_;
  std::unique_ptr<MetricsManager> metrics_manager_;
  std::unique_ptr<::astra::replication::ReplicationManager>
      replication_manager_;

  // Server state
  std::atomic<bool> running_{false};

  // Stats aggregation thread (NO SHARING architecture)
  std::thread stats_aggregation_thread_;
  std::atomic<bool> stats_aggregation_running_{false};

  // Disable copy and move
  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;
  Server(Server&&) = delete;
  Server& operator=(Server&&) = delete;
};

}  // namespace astra::server
