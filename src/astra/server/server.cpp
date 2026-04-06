// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include "server.hpp"

#include "absl/time/time.h"
#include "astra/cluster/cluster_manager.hpp"  // For ClusterManager
#include "astra/cluster/gossip_manager.hpp"   // For GossipManager
#include "astra/cluster/shard_manager.hpp"    // For ShardManager
#include "astra/security/acl_manager.hpp"     // For AclManager
#include "worker_scheduler.hpp"

namespace astra::server {

Server::Server(const ServerConfig& config) : config_(config), running_(false) {
  ASTRADB_LOG_INFO("Creating server with NO SHARING architecture");
  // Set server start time for stats
  auto* stats = server::ServerStatsAccessor::Instance().GetStats();
  absl::Time start_time = absl::Now();
  stats->start_time.store(absl::ToTimeT(start_time), std::memory_order_relaxed);
  ASTRADB_LOG_INFO("Config: host={}, port={}, workers={}", config.host,
                   config.port, config.num_workers);

  // Create all workers (without cross-worker references initially)
  for (size_t i = 0; i < config.num_workers; ++i) {
    workers_.push_back(std::make_unique<Worker>(
        i, config.host, config.port, std::vector<Worker*>(),
        config.replication));
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
    // Note: initial_cluster_state_ is set in InitCluster()

    // Set cluster managers for all workers
    if (cluster_manager_ && gossip_manager_ && shard_manager_) {
      for (auto& worker : workers_) {
        worker->GetDataShard().GetCommandContext()->SetClusterEnabled(true);
        worker->GetDataShard().GetCommandContext()->SetGossipManager(
            gossip_manager_.get());
        worker->GetDataShard().GetCommandContext()->SetShardManager(
            shard_manager_.get());
        worker->GetDataShard().GetCommandContext()->SetWorkerId(
            worker->GetWorkerId());
        // Set callback for updating cluster state across all workers
        worker->GetDataShard()
            .GetCommandContext()
            ->SetClusterStateUpdateCallback(
                [this](std::shared_ptr<cluster::ClusterState> new_state) {
                  this->UpdateClusterState(new_state);
                });
      }
      ASTRADB_LOG_INFO("Cluster managers set for all workers");
    }
  }

  // Initialize ACL if enabled
  if (config_.acl_enabled) {
    if (!InitACL()) {
      ASTRADB_LOG_WARN("ACL initialization failed, running without ACL");
    }
  }

  // Initialize metrics if enabled
  ASTRADB_LOG_INFO("Metrics enabled: {}", config_.metrics_enabled);
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
    ASTRADB_LOG_INFO("Worker scheduler created with {} workers",
                     workers_.size());
  }

  // Set worker scheduler for all workers
  for (auto& worker : workers_) {
    worker->SetWorkerScheduler(worker_scheduler_.get());
  }
  ASTRADB_LOG_INFO("Worker scheduler set for all workers");

  // Set memory configuration for all workers' data shards
  {
    ASTRADB_LOG_INFO(
        "Memory configuration from file - max_memory={}, eviction_policy={}",
        config_.memory.max_memory, config_.memory.eviction_policy);

    core::memory::MemoryTrackerConfig memory_config;
    memory_config.max_memory_limit = config_.memory.max_memory;
    memory_config.eviction_policy =
        core::memory::StringToEvictionPolicy(config_.memory.eviction_policy);
    memory_config.eviction_threshold = config_.memory.eviction_threshold;
    memory_config.eviction_samples = config_.memory.eviction_samples;
    memory_config.enable_tracking = config_.memory.enable_tracking;

    ASTRADB_LOG_INFO(
        "Setting memory configuration for {} workers (RocksDB enabled: {})",
        workers_.size(), config_.rocksdb.enabled ? "yes" : "no");
    for (auto& worker : workers_) {
      // Create callback to get total memory across all workers
      core::memory::GetTotalMemoryCallback get_total_memory_callback;
      if (workers_.size() > 1) {
        get_total_memory_callback = [this]() -> size_t {
          size_t total_memory = 0;
          for (auto& worker : workers_) {
            total_memory +=
                worker->GetDataShard().GetMemoryTracker()->GetCurrentMemory();
          }
          return total_memory;
        };
      }

      worker->GetDataShard().SetMemoryConfig(
          memory_config, std::move(get_total_memory_callback),
          config_.rocksdb.enabled);
    }
    ASTRADB_LOG_INFO("Memory configuration set for all workers");
  }

  // Start all workers
  for (auto& worker : workers_) {
    worker->Start(initial_cluster_state_);
  }

  running_ = true;
  ASTRADB_LOG_INFO("Server started successfully with {} workers",
                   workers_.size());

  // Start stats aggregation (NO SHARING architecture)
  if (config_.metrics_enabled) {
    StartStatsAggregation();
  }

  // Start gossip tick thread (NO SHARING architecture)
  if (gossip_manager_ && config_.cluster_enabled) {
    gossip_tick_thread_ = std::thread([this]() {
      while (running_) {
        gossip_manager_->Tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    });
    ASTRADB_LOG_INFO("Gossip tick thread started");
  }
}

void Server::Stop() {
  if (!running_) {
    return;
  }

  ASTRADB_LOG_INFO("Stopping server...");
  running_ = false;

  // IMPORTANT: Stop gossip manager BEFORE joining tick thread
  // This prevents race conditions where Tick() might trigger callbacks
  // while the Server object is being destroyed
  if (gossip_manager_) {
    gossip_manager_->Stop();
  }

  // Join gossip tick thread AFTER stopping gossip manager
  if (gossip_tick_thread_.joinable()) {
    gossip_tick_thread_.join();
    ASTRADB_LOG_INFO("Gossip tick thread stopped");
  }

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
    // Stop gossip manager first
    if (gossip_manager_) {
      gossip_manager_->Stop();
      gossip_manager_.reset();
    }
    // Reset shard manager
    if (shard_manager_) {
      shard_manager_.reset();
    }
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

    if (!config_.cluster_enabled) {
      ASTRADB_LOG_INFO("Cluster is disabled in configuration");
      return true;
    }

    // Create and initialize cluster manager
    cluster_manager_ = std::make_unique<cluster::ClusterManager>();
    if (!cluster_manager_->Init(config_.cluster_node_id, config_.host,
                                config_.port, config_.cluster_gossip_port)) {
      ASTRADB_LOG_ERROR("Failed to initialize cluster manager");
      cluster_manager_.reset();
      return false;
    }

    // Create and initialize gossip manager
    gossip_manager_ = std::make_unique<cluster::GossipManager>();
    cluster::ClusterConfig gossip_config;
    gossip_config.node_id = config_.cluster_node_id;
    gossip_config.bind_ip = config_.cluster_bind_addr;
    gossip_config.gossip_port = config_.cluster_gossip_port;
    gossip_config.data_port = config_.port;
    gossip_config.shard_count = config_.cluster_shard_count;
    gossip_config.use_tcp = config_.cluster.use_tcp;

    if (!gossip_manager_->Init(gossip_config)) {
      ASTRADB_LOG_ERROR("Failed to initialize gossip manager");
      gossip_manager_.reset();
      cluster_manager_.reset();
      return false;
    }

    // Start gossip manager
    if (!gossip_manager_->Start()) {
      ASTRADB_LOG_ERROR("Failed to start gossip manager");
      gossip_manager_.reset();
      cluster_manager_.reset();
      return false;
    }

    // Set event callback to update ClusterState for all workers (NO SHARING
    // architecture) Use lambda with [this] capture (std::function should
    // correctly store the capture)
    ASTRADB_LOG_INFO("Setting event callback: this={}", (void*)this);
    gossip_manager_->SetEventCallback(
        [this](cluster::ClusterEvent event,
               const cluster::AstraNodeView& node_view) {
          OnClusterEvent(event, node_view);
        });

    // Create and initialize shard manager
    shard_manager_ = std::make_unique<cluster::ShardManager>();
    cluster::NodeId self_id{};
    cluster::GossipManager::ParseNodeId(config_.cluster_node_id, self_id);

    if (!shard_manager_->Init(config_.cluster_shard_count, self_id)) {
      ASTRADB_LOG_ERROR("Failed to initialize shard manager");
      shard_manager_.reset();
      gossip_manager_.reset();
      cluster_manager_.reset();
      return false;
    }

    ASTRADB_LOG_INFO("ShardManager initialized: {} shards",
                     config_.cluster_shard_count);

    // Create initial ClusterState for all workers (NO SHARING architecture)
    auto initial_cluster_state = std::make_shared<cluster::ClusterState>(
        config_.cluster_node_id, config_.host, config_.port,
        config_.cluster_gossip_port);

    // Meet seed nodes if configured
    for (const auto& seed : config_.cluster_seeds) {
      // Parse seed address (format: "ip:port")
      size_t colon_pos = seed.find(':');
      if (colon_pos != std::string::npos) {
        std::string seed_ip = seed.substr(0, colon_pos);
        int seed_port = std::stoi(seed.substr(colon_pos + 1));
        gossip_manager_->MeetNode(seed_ip, seed_port);
        ASTRADB_LOG_INFO("Meeting seed node: {}", seed);
      }
    }

    // Set initial ClusterState for all workers (will be set during
    // Worker::Start) Store it in a temporary variable and set it when workers
    // start
    initial_cluster_state_ = initial_cluster_state;

    ASTRADB_LOG_INFO(
        "Cluster initialized successfully (enabled: yes, node_id: {})",
        config_.cluster_node_id);
    return true;
  } catch (const std::exception& e) {
    ASTRADB_LOG_ERROR("Cluster initialization exception: {}", e.what());
    cluster_manager_.reset();
    gossip_manager_.reset();
    return false;
  }
}

void Server::OnClusterEvent(cluster::ClusterEvent event,
                            const cluster::AstraNodeView& node_view) noexcept {
  // NO SHARING architecture: Update all workers' ClusterState
  // This method is called from GossipManager's event callback
  // CRITICAL: This runs in libgossip's tick thread, which holds gossip_core's
  // mutex DO NOT call any function that might acquire gossip_core's mutex

  ASTRADB_LOG_INFO(
      "OnClusterEvent: event={}, node_id={}, ip={}:{}, this={}, "
      "workers_.size()={}",
      static_cast<int>(event),
      cluster::GossipManager::NodeIdToString(node_view.id), node_view.ip,
      node_view.port, (void*)this, workers_.size());

  // IMPORTANT: Defer cluster state update to avoid holding libgossip's lock
  // Post the event to worker thread for async processing
  if (!workers_.empty()) {
    ASTRADB_LOG_INFO("OnClusterEvent: Calling AddTask on worker 0");
    workers_[0]->AddTask([this, event, node_view]() {
      ASTRADB_LOG_INFO("ProcessClusterEventAsync: Starting");
      ProcessClusterEventAsync(event, node_view);
    });
  }
}

// Process cluster event asynchronously (not in libgossip's tick thread)
void Server::ProcessClusterEventAsync(
    cluster::ClusterEvent event,
    const cluster::AstraNodeView& node_view) noexcept {
  try {
    ASTRADB_LOG_DEBUG(
        "ProcessClusterEventAsync: event={}, node_id={}, ip={}:{}, "
        "has_slots={}",
        static_cast<int>(event),
        cluster::GossipManager::NodeIdToString(node_view.id), node_view.ip,
        node_view.port, node_view.metadata.contains("slots"));

    // Get current cluster state from any worker (they should be in sync)
    std::shared_ptr<cluster::ClusterState> new_state;
    if (!workers_.empty() && workers_[0]->GetClusterState()) {
      new_state = workers_[0]->GetClusterState();
      ASTRADB_LOG_DEBUG("ProcessClusterEventAsync: Got existing ClusterState");
    } else {
      // Create initial state if not exists
      new_state = std::make_shared<cluster::ClusterState>(
          config_.cluster_node_id, config_.host, config_.port,
          config_.cluster_gossip_port);
      ASTRADB_LOG_DEBUG("ProcessClusterEventAsync: Created new ClusterState");
    }

    // Add/update node in cluster state based on event
    switch (event) {
      case cluster::ClusterEvent::kNodeJoined: {
        // Add new node to cluster state
        cluster::ClusterNodeInfo node_info;
        node_info.id = cluster::GossipManager::NodeIdToString(node_view.id);
        node_info.ip = node_view.ip;
        // Convert gossip port to data port: data_port = gossip_port - 10000
        // E.g., gossip_port 17002 -> data_port 7002
        node_info.port = node_view.port - 10000;
        // Bus port is for Redis Cluster internal communication
        // Using gossip port as bus port for simplicity
        node_info.bus_port = node_view.port;
        node_info.role = (node_view.role == "master")
                             ? cluster::ClusterRole::kMaster
                             : cluster::ClusterRole::kSlave;
        node_info.config_epoch = node_view.config_epoch;
        new_state = new_state->WithNodeAdded(node_info);
        ASTRADB_LOG_INFO("Added node {} to ClusterState: {}:{}@{}",
                         node_info.id, node_info.ip, node_info.port,
                         node_info.bus_port);
        break;
      }
      case cluster::ClusterEvent::kNodeLeft:
      case cluster::ClusterEvent::kNodeFailed: {
        // Remove node from cluster state
        std::string node_id =
            cluster::GossipManager::NodeIdToString(node_view.id);
        new_state = new_state->WithNodeRemoved(node_id);
        ASTRADB_LOG_INFO("Removed node {} from ClusterState", node_id);
        break;
      }
      case cluster::ClusterEvent::kNodeRecovered: {
        // Node recovered - add back to cluster
        cluster::ClusterNodeInfo node_info;
        node_info.id = cluster::GossipManager::NodeIdToString(node_view.id);
        node_info.ip = node_view.ip;
        // Convert gossip port to data port: data_port = gossip_port - 10000
        node_info.port = node_view.port - 10000;
        // Bus port is for Redis Cluster internal communication
        node_info.bus_port = node_view.port;
        node_info.role = (node_view.role == "master")
                             ? cluster::ClusterRole::kMaster
                             : cluster::ClusterRole::kSlave;
        node_info.config_epoch = node_view.config_epoch;
        new_state = new_state->WithNodeAdded(node_info);
        ASTRADB_LOG_INFO("Recovered node {} in ClusterState", node_info.id);
        break;
      }
      default:
        // Other events (kConfigChanged, kLeaderChanged) - update metadata and
        // slots For kConfigChanged, ensure node is in cluster state before
        // processing metadata
        if (event == cluster::ClusterEvent::kConfigChanged) {
          // Add or update node in cluster state
          cluster::ClusterNodeInfo node_info;
          node_info.id = cluster::GossipManager::NodeIdToString(node_view.id);
          node_info.ip = node_view.ip;
          // Convert gossip port to data port: data_port = gossip_port - 10000
          node_info.port = node_view.port - 10000;
          // Bus port is for Redis Cluster internal communication
          node_info.bus_port = node_view.port;
          node_info.role = (node_view.role == "master")
                               ? cluster::ClusterRole::kMaster
                               : cluster::ClusterRole::kSlave;
          node_info.config_epoch = node_view.config_epoch;
          new_state = new_state->WithNodeAdded(node_info);
          ASTRADB_LOG_INFO(
              "ConfigChanged: Added/updated node {} in ClusterState: {}:{}@{}",
              node_info.id, node_info.ip, node_info.port, node_info.bus_port);
        }
        break;
    }

    // Process slot metadata if present
    if (node_view.metadata.contains("slots")) {
      std::string node_id =
          cluster::GossipManager::NodeIdToString(node_view.id);
      const std::string& slots_str = node_view.metadata.at("slots");

      ASTRADB_LOG_DEBUG(
          "ProcessClusterEventAsync: Node {} has slot metadata: '{}', size={} "
          "bytes",
          node_id, slots_str, slots_str.size());

      // Check config epoch for conflict resolution
      uint64_t remote_epoch = 0;
      if (node_view.metadata.contains("config_epoch")) {
        remote_epoch = std::stoull(node_view.metadata.at("config_epoch"));
      }

      ASTRADB_LOG_DEBUG(
          "ProcessClusterEventAsync: Remote config epoch={}, node_id={}",
          remote_epoch, node_id);

      // Check if remote config is newer
      uint64_t local_epoch = gossip_manager_->GetConfigEpoch();
      ASTRADB_LOG_DEBUG(
          "ProcessClusterEventAsync: Local config epoch={}, node_id={}",
          local_epoch, node_id);

      if (remote_epoch > local_epoch) {
        ASTRADB_LOG_DEBUG(
            "ProcessClusterEventAsync: Remote node {} has newer config epoch "
            "({} > {}), accepting slot assignments",
            node_id, remote_epoch, local_epoch);

        // Deserialize slot assignments
        auto slots = cluster::SlotSerializer::Deserialize(slots_str);
        ASTRADB_LOG_DEBUG(
            "ProcessClusterEventAsync: Deserialized {} slots from metadata: "
            "'{}'",
            slots.size(), slots_str);

        if (!slots.empty()) {
          ASTRADB_LOG_DEBUG(
              "ProcessClusterEventAsync: Updating slot assignments for node "
              "{}: {} slots",
              node_id, slots.size());

          // Update cluster state with slot assignments
          for (uint16_t slot : slots) {
            new_state = new_state->WithSlotAssigned(node_id, slot);
            ASTRADB_LOG_DEBUG(
                "ProcessClusterEventAsync: Assigned slot {} to node {}", slot,
                node_id);
          }

          ASTRADB_LOG_DEBUG(
              "ProcessClusterEventAsync: Updated cluster state with {} slot "
              "assignments",
              slots.size());
        }
      } else {
        ASTRADB_LOG_DEBUG(
            "ProcessClusterEventAsync: Ignoring slot metadata from node {} "
            "(config_epoch {} <= local {})",
            node_id, remote_epoch, local_epoch);
      }
    } else {
      ASTRADB_LOG_DEBUG(
          "ProcessClusterEventAsync: Node {} has no slot metadata",
          cluster::GossipManager::NodeIdToString(node_view.id));
    }

    // Update all workers' ClusterState (zero-copy via shared_ptr)
    ASTRADB_LOG_DEBUG(
        "ProcessClusterEventAsync: Updating cluster state in all workers");
    UpdateClusterState(new_state);

  } catch (const std::exception& e) {
    ASTRADB_LOG_ERROR("ProcessClusterEventAsync exception: {}", e.what());
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
        ASTRADB_LOG_WARN(
            "Stats aggregation is disabled (frequency <= 0), sleeping for 1 "
            "hour");
        absl::SleepFor(absl::Hours(1));
      } else {
        // Use configured frequency
        ASTRADB_LOG_DEBUG("Stats aggregation: sleeping for {} seconds",
                          frequency);
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

// Update cluster state for all workers (NO SHARING architecture)
void Server::UpdateClusterState(
    std::shared_ptr<cluster::ClusterState> new_state) {
  if (!worker_scheduler_) {
    ASTRADB_LOG_ERROR("WorkerScheduler not initialized");
    return;
  }
  // Use DispatchOnAll (non-blocking) to avoid deadlock
  // The cluster state update is idempotent and safe to process asynchronously
  // We need to create a wrapper lambda that captures the worker pointer
  for (size_t i = 0; i < workers_.size(); ++i) {
    auto* worker = workers_[i].get();
    worker->AddTask(
        [worker, new_state]() { worker->SetClusterState(new_state); });
  }
  ASTRADB_LOG_INFO("Dispatched cluster state update to all workers");
}

}  // namespace astra::server
