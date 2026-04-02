// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include "server.hpp"
#include "worker_scheduler.hpp"

#include "astra/security/acl_manager.hpp"  // For AclManager
#include "absl/time/time.h"

namespace astra::server {

Server::Server(const ServerConfig& config) : config_(config), running_(false) {
  ASTRADB_LOG_INFO("Creating server with NO SHARING architecture");
  // Set server start time for stats
  auto* stats = server::ServerStatsAccessor::Instance().GetStats();
  absl::Time start_time = absl::Now();
  stats->start_time.store(
      absl::ToTimeT(start_time),
      std::memory_order_relaxed);
  ASTRADB_LOG_INFO("Config: host={}, port={}, workers={}", config.host,
                   config.port, config.num_workers);

  // Create all workers (without cross-worker references initially)
  for (size_t i = 0; i < config.num_workers; ++i) {
    workers_.push_back(std::make_unique<Worker>(i, config.host, config.port,
                                                std::vector<Worker*>()));
  }

  // Now, set up cross-worker references
  std::vector<Worker*> worker_ptrs;
  for (auto& worker : workers_) {
    worker_ptrs.push_back(worker.get());
  }

  // Set all workers reference for each worker
  for (auto& worker : workers_) {
    worker->SetAllWorkers(worker_ptrs);
  }

  ASTRADB_LOG_INFO(
      "Server created successfully with {} workers (MPSC cross-worker "
      "communication enabled)",
      workers_.size());
}

Server::~Server() { ASTRADB_LOG_INFO("Server destroyed"); }

void Server::Start() {
  ASTRADB_LOG_INFO("Starting server...");

  // Create unified persistence manager for both AOF and RDB
  if (config_.aof.enabled || config_.rdb.enabled) {
    ASTRADB_LOG_INFO("Initializing persistence manager...");

    persistence_manager_ = std::make_unique<PersistenceManager>();

    std::string data_dir = "./data";
    std::string rdb_path = data_dir + "/dump.rdb";

    if (!persistence_manager_->Init(data_dir, config_.aof.enabled,
                                    config_.rdb.enabled, config_.aof.path,
                                    rdb_path)) {
      ASTRADB_LOG_ERROR("Failed to initialize persistence manager");
      persistence_manager_.reset();
    } else {
      ASTRADB_LOG_INFO("Setting persistence manager for {} workers",
                       workers_.size());
      for (auto& worker : workers_) {
        ASTRADB_LOG_DEBUG("Calling SetPersistenceManager for worker");
        worker->SetPersistenceManager(persistence_manager_.get());
      }
      ASTRADB_LOG_INFO("Persistence manager set for all workers");

      // Set replication manager for all workers
      if (replication_manager_) {
        for (auto& worker : workers_) {
          worker->GetDataShard().GetCommandContext()->SetReplicationManager(
              replication_manager_.get());
        }
        ASTRADB_LOG_INFO("Replication manager set for all workers");
      }

      // Load RDB data if RDB is enabled
      if (config_.rdb.enabled) {
        ASTRADB_LOG_INFO("Loading RDB data if available");
        std::vector<Worker*> worker_ptrs;
        for (auto& worker : workers_) {
          worker_ptrs.push_back(worker.get());
        }
        persistence_manager_->LoadRdb(worker_ptrs);
      }
    }
  }

  // Initialize cluster if enabled
  if (config_.cluster_enabled) {
    if (!InitCluster()) {
      ASTRADB_LOG_WARN(
          "Cluster initialization failed, running in standalone mode");
    }
  }

  // Initialize ACL if enabled
  if (config_.acl_enabled) {
    if (!InitACL()) {
      ASTRADB_LOG_WARN("ACL initialization failed, running without ACL");
    }
  }

  // Initialize metrics if enabled
  if (config_.metrics_enabled) {
    if (!InitMetrics()) {
      ASTRADB_LOG_WARN(
          "Metrics initialization failed, running without metrics");
    }
  }

  // Initialize replication
  if (!InitReplication()) {
    ASTRADB_LOG_WARN(
        "Replication initialization failed, running without replication");
  }

  // Create worker scheduler first (before setting memory config)
  {
    std::vector<Worker*> worker_ptrs;
    worker_ptrs.reserve(workers_.size());
    for (auto& worker : workers_) {
      worker_ptrs.push_back(worker.get());
    }
    worker_scheduler_ = std::make_unique<WorkerScheduler>(worker_ptrs);
    ASTRADB_LOG_INFO("Worker scheduler created with {} workers", workers_.size());
  }

  // Set worker scheduler for all workers
  for (auto& worker : workers_) {
    worker->SetWorkerScheduler(worker_scheduler_.get());
  }
  ASTRADB_LOG_INFO("Worker scheduler set for all workers");

  // Set memory configuration for all workers' data shards
  {
    ASTRADB_LOG_INFO("Memory configuration from file - max_memory={}, eviction_policy={}",
                     config_.memory.max_memory, config_.memory.eviction_policy);
    
    core::memory::MemoryTrackerConfig memory_config;
    memory_config.max_memory_limit = config_.memory.max_memory;
    memory_config.eviction_policy = 
        core::memory::StringToEvictionPolicy(config_.memory.eviction_policy);
    memory_config.eviction_threshold = config_.memory.eviction_threshold;
    memory_config.eviction_samples = config_.memory.eviction_samples;
    memory_config.enable_tracking = config_.memory.enable_tracking;

    ASTRADB_LOG_INFO("Setting memory configuration for {} workers (RocksDB enabled: {})",
                     workers_.size(), config_.rocksdb.enabled ? "yes" : "no");
    for (auto& worker : workers_) {
      // Create callback to get total memory across all workers
      core::memory::GetTotalMemoryCallback get_total_memory_callback;
      if (workers_.size() > 1) {
        get_total_memory_callback = [this]() -> size_t {
          size_t total_memory = 0;
          for (auto& worker : workers_) {
            total_memory += worker->GetDataShard().GetMemoryTracker()->GetCurrentMemory();
          }
          return total_memory;
        };
      }
      
      worker->GetDataShard().SetMemoryConfig(memory_config, std::move(get_total_memory_callback),
                                              config_.rocksdb.enabled);
    }
    ASTRADB_LOG_INFO("Memory configuration set for all workers");
  }

  // Start all workers
  for (auto& worker : workers_) {
    worker->Start();
  }

  running_ = true;
  ASTRADB_LOG_INFO("Server started successfully with {} workers",
                   workers_.size());
  // Start stats aggregation (NO SHARING architecture)
  if (config_.metrics_enabled) {
    StartStatsAggregation();
  }
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

  // Shutdown managers (in reverse order)
  if (metrics_manager_) {
    ASTRADB_LOG_INFO("Shutting down metrics manager...");
    // Stop stats aggregation (NO SHARING architecture)
    StopStatsAggregation();
    metrics_manager_->Shutdown();
    metrics_manager_.reset();
  }

  if (acl_manager_) {
    ASTRADB_LOG_INFO("Shutting down ACL manager...");
    acl_manager_.reset();
  }

  if (cluster_manager_) {
    ASTRADB_LOG_INFO("Shutting down cluster manager...");
    cluster_manager_->Shutdown();
    cluster_manager_.reset();
  }

  if (persistence_manager_) {
    ASTRADB_LOG_INFO("Shutting down persistence manager...");
    persistence_manager_->Shutdown();
    persistence_manager_.reset();
  }

  ASTRADB_LOG_INFO("Server stopped");
}

bool Server::InitPersistence() noexcept {
  try {
    ASTRADB_LOG_INFO("Initializing persistence...");

    // Create persistence manager
    persistence_manager_ = std::make_unique<PersistenceManager>();

    // Initialize with AOF and RDB configuration
    std::string data_dir = "./data";
    std::string rdb_path = data_dir + "/dump.rdb";
    if (!persistence_manager_->Init(data_dir, config_.aof.enabled, true,
                                    config_.aof.path, rdb_path)) {
      ASTRADB_LOG_ERROR("Failed to initialize persistence manager");
      persistence_manager_.reset();
      return false;
    }

    ASTRADB_LOG_INFO("Persistence initialized successfully (AOF enabled: {})",
                     config_.aof.enabled ? "yes" : "no");
    return true;
  } catch (const std::exception& e) {
    ASTRADB_LOG_ERROR("Persistence initialization exception: {}", e.what());
    persistence_manager_.reset();
    return false;
  }
}

bool Server::InitRdb() noexcept {
  try {
    ASTRADB_LOG_INFO("Initializing RDB...");

    // Create persistence manager for RDB only
    persistence_manager_ = std::make_unique<PersistenceManager>();

    // Initialize with RDB configuration (AOF disabled)
    std::string data_dir = "./data";
    std::string rdb_path = data_dir + "/dump.rdb";
    if (!persistence_manager_->Init(data_dir, false, true, "", rdb_path)) {
      ASTRADB_LOG_ERROR("Failed to initialize RDB manager");
      persistence_manager_.reset();
      return false;
    }

    ASTRADB_LOG_INFO("RDB initialized successfully");
    return true;
  } catch (const std::exception& e) {
    ASTRADB_LOG_ERROR("RDB initialization exception: {}", e.what());
    persistence_manager_.reset();
    return false;
  }
}

bool Server::InitCluster() noexcept {
  try {
    ASTRADB_LOG_INFO("Initializing cluster...");

    // Create cluster manager
    cluster_manager_ = std::make_unique<ClusterManager>();

    // Initialize with node ID
    if (!cluster_manager_->Init(config_.cluster_node_id)) {
      ASTRADB_LOG_ERROR("Failed to initialize cluster manager");
      cluster_manager_.reset();
      return false;
    }

    ASTRADB_LOG_INFO("Cluster initialized successfully (enabled: {})",
                     config_.cluster_enabled ? "yes" : "no");
    return true;
  } catch (const std::exception& e) {
    ASTRADB_LOG_ERROR("Cluster initialization exception: {}", e.what());
    cluster_manager_.reset();
    return false;
  }
}

bool Server::InitACL() noexcept {
  try {
    ASTRADB_LOG_INFO("Initializing ACL...");

    // Create ACL manager
    acl_manager_ = std::make_unique<::astra::security::AclManager>();

    // Add default user
    if (!config_.acl_default_user.empty()) {
      acl_manager_->CreateUser(
          config_.acl_default_user, config_.acl_default_password,
          static_cast<uint32_t>(::astra::security::AclPermission::kAdmin),
          true);
      ASTRADB_LOG_INFO("ACL initialized with default user: {}",
                       config_.acl_default_user);
    }

    ASTRADB_LOG_INFO("ACL initialized successfully");
    return true;
  } catch (const std::exception& e) {
    ASTRADB_LOG_ERROR("ACL initialization exception: {}", e.what());
    acl_manager_.reset();
    return false;
  }
}

bool Server::InitMetrics() noexcept {
  try {
    ASTRADB_LOG_INFO("Initializing metrics...");

    // Create metrics manager
    metrics_manager_ = std::make_unique<MetricsManager>();

    // Initialize with bind address and port
    if (!metrics_manager_->Init(config_.metrics_bind_addr,
                                config_.metrics_port)) {
      ASTRADB_LOG_ERROR("Failed to initialize metrics manager");
      metrics_manager_.reset();
      return false;
    }
    return true;
  } catch (const std::exception& e) {
    ASTRADB_LOG_ERROR("Metrics initialization exception: {}", e.what());
    // metrics_manager_.reset();
    return true;  // Continue even if metrics fails
  }
}

bool Server::InitReplication() noexcept {
  try {
    ASTRADB_LOG_INFO("Initializing replication...");

    // Create replication manager
    replication_manager_ =
        std::make_unique<::astra::replication::ReplicationManager>();

    // Initialize with role (default to master)
    ::astra::replication::ReplicationConfig config;
    config.role = ::astra::replication::ReplicationRole::kMaster;
    if (!replication_manager_->Init(config)) {
      ASTRADB_LOG_ERROR("Failed to initialize replication manager");
      replication_manager_.reset();
      return false;
    }

    ASTRADB_LOG_INFO("Replication initialized successfully");
    return true;
  } catch (const std::exception& e) {
    ASTRADB_LOG_ERROR("Replication initialization exception: {}", e.what());
    replication_manager_.reset();
    return false;
  }
}

// Stats aggregation methods (NO SHARING architecture)
void Server::StartStatsAggregation() {
  if (stats_aggregation_running_.exchange(true)) {
    return;  // Already running
  }

  stats_aggregation_thread_ = std::thread([this]() {
    ASTRADB_LOG_DEBUG("Stats aggregation thread started");

    while (stats_aggregation_running_) {
      AggregateStats();
      
      // Use configured stats frequency
      int frequency = config_.stats_frequency_seconds;
      if (frequency <= 0) {
        // Disabled: sleep for a long time (effectively stops stats aggregation)
        ASTRADB_LOG_WARN("Stats aggregation is disabled (frequency <= 0), sleeping for 1 hour");
        absl::SleepFor(absl::Hours(1));
      } else {
        // Use configured frequency
        ASTRADB_LOG_DEBUG("Stats aggregation: sleeping for {} seconds", frequency);
        absl::SleepFor(absl::Seconds(frequency));
      }
    }

    ASTRADB_LOG_DEBUG("Stats aggregation thread exited");
  });
}

void Server::StopStatsAggregation() {
  if (!stats_aggregation_running_.exchange(false)) {
    return;  // Not running
  }

  if (stats_aggregation_thread_.joinable()) {
    stats_aggregation_thread_.join();
  }
}

void Server::AggregateStats() {
  // Reset global stats
  auto* global_stats = server::ServerStatsAccessor::Instance().GetStats();
  global_stats->Reset();

  // Aggregate all worker stats
  for (auto& worker : workers_) {
    global_stats->Merge(worker->GetLocalStats());
  }

  // Update server uptime
  absl::Time now = absl::Now();
  absl::Time start_time = absl::FromTimeT(global_stats->start_time.load());
  absl::Duration uptime_duration = now - start_time;
  int64_t uptime = absl::ToInt64Seconds(uptime_duration);
  
  global_stats->uptime_seconds.store(uptime, std::memory_order_relaxed);

  // Sync to Prometheus
  ::astra::metrics::AstraMetrics::Instance().UpdateFromServerStats();
}

}  // namespace astra::server
