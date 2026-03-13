// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "astra/base/config.hpp"
#include "astra/base/logging.hpp"
#include "worker.hpp"
#include "managers.hpp"  // Manager class definitions

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
}

namespace astra::core {
class MetricsManager;
}

// NO SHARING Server Configuration
// Extends base ServerConfig with NO SHARING architecture specific settings
struct NoSharingServerConfig : public ::astra::base::ServerConfig {
  // NO SHARING architecture settings
  size_t num_workers = 2;  // Number of workers (each has IO + Executor threads)
  bool use_so_reuseport = true;  // Enable SO_REUSEPORT for kernel load balancing
  
  // Persistence (NO SHARING: independent AOF writer thread)
  bool aof_enabled = false;
  std::string aof_path = "./data/aof/appendonly.aof";
  bool aof_sync_everysec = true;  // true = everysec, false = always
  
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
  uint16_t metrics_port = 9090;
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
  class PersistenceManager* GetPersistenceManager() { return persistence_manager_.get(); }
  class ClusterManager* GetClusterManager() { return cluster_manager_.get(); }
  class PubSubManager* GetPubSubManager() { return pubsub_manager_.get(); }

 private:
  // Initialize persistence
  bool InitPersistence() noexcept;

  // Initialize cluster
  bool InitCluster() noexcept;

  // Initialize ACL
  bool InitACL() noexcept;

  // Initialize metrics
  bool InitMetrics() noexcept;

  // Configuration
  ServerConfig config_;

  // Workers (NO SHARING: each worker is completely independent)
  std::vector<std::unique_ptr<Worker>> workers_;

  // Server-level managers (shared by all workers via MPSC if needed)
  std::unique_ptr<PersistenceManager> persistence_manager_;
  std::unique_ptr<ClusterManager> cluster_manager_;
  std::unique_ptr<PubSubManager> pubsub_manager_;
  std::unique_ptr<::astra::security::AclManager> acl_manager_;
  std::unique_ptr<MetricsManager> metrics_manager_;

  // Server state
  std::atomic<bool> running_{false};

  // Disable copy and move
  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;
  Server(Server&&) = delete;
  Server& operator=(Server&&) = delete;
};

}  // namespace astra::server