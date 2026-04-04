// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <asio.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/inlined_vector.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "astra/base/concurrentqueue_wrapper.hpp"
#include "astra/base/logging.hpp"
#include "astra/commands/blocking_manager.hpp"
#include "astra/commands/command_auto_register.hpp"
#include "astra/commands/command_handler.hpp"
#include "astra/commands/database.hpp"
#include "astra/commands/pubsub_commands.hpp"
#include "astra/core/memory/eviction_policy.hpp"
#include "astra/core/metrics.hpp"
#include "astra/core/server_stats.hpp"
#include "astra/persistence/rocksdb_adapter.hpp"
#include "astra/protocol/resp/resp_builder.hpp"
#include "astra/protocol/resp/resp_parser.hpp"
#include "astra/protocol/resp/resp_types.hpp"
#include "managers.hpp"

// Cluster support
#include "astra/cluster/cluster_config.hpp"

namespace astra::server {

// Helper function to convert absl::Duration to std::chrono::duration for asio
inline std::chrono::nanoseconds AbslToChronoNanoseconds(absl::Duration d) {
  return std::chrono::nanoseconds(absl::ToInt64Nanoseconds(d));
}

inline std::chrono::microseconds AbslToChronoMicroseconds(absl::Duration d) {
  return std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::nanoseconds(absl::ToInt64Nanoseconds(d)));
}

inline std::chrono::milliseconds AbslToChronoMilliseconds(absl::Duration d) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::nanoseconds(absl::ToInt64Nanoseconds(d)));
}

// Forward declarations
class Worker;
class PersistenceManager;
class WorkerScheduler;

// Simple CommandContext implementation for Worker
class WorkerCommandContext : public astra::commands::CommandContext {
 public:
  explicit WorkerCommandContext(astra::commands::Database* db) : db_(db) {}

  astra::commands::Database* GetDatabase() const override { return db_; }
  int GetDBIndex() const override { return 0; }
  void SetDBIndex(int index) override { (void)index; }
  bool IsAuthenticated() const override { return true; }
  void SetAuthenticated(bool auth) override { (void)auth; }

  // Get blocking manager (for blocking commands)
  class commands::BlockingManager* GetBlockingManager() override {
    return blocking_manager_;
  }

  // Set blocking manager (called by Worker after blocking manager is
  // initialized)
  void SetBlockingManager(class commands::BlockingManager* blocking_manager) {
    blocking_manager_ = blocking_manager;
  }

  // Get replication manager (for replication commands)
  class replication::ReplicationManager* GetReplicationManager() override {
    return replication_manager_;
  }

  // Set replication manager (called by Worker after replication manager is
  // initialized)
  void SetReplicationManager(
      class replication::ReplicationManager* replication_manager) {
    replication_manager_ = replication_manager;
  }

  // Get pubsub manager (for publish/subscribe commands)
  commands::PubSubManager* GetPubSubManager() override {
    return pubsub_manager_;
  }

  // Set pubsub manager (called by Worker after pubsub manager is initialized)
  void SetPubSubManager(commands::PubSubManager* pubsub_manager) {
    pubsub_manager_ = pubsub_manager;
  }

  // Get command registry (for COMMAND command - NO SHARING architecture)
  class commands::CommandRegistry* GetCommandRegistry() override {
    return command_registry_;
  }

  // Set command registry (called by Worker after command registry is
  // initialized)
  void SetCommandRegistry(class commands::CommandRegistry* command_registry) {
    command_registry_ = command_registry;
  }

  // Get worker scheduler (for SCRIPT KILL - NO SHARING architecture)
  WorkerScheduler* GetWorkerScheduler() const override {
    return worker_scheduler_;
  }

  // Get current worker (for cross-shard operations)
  class Worker* GetWorker() const override { return worker_; }

  // Set worker scheduler (called by Worker after worker scheduler is
  // initialized)
  void SetWorkerScheduler(class WorkerScheduler* worker_scheduler) {
    worker_scheduler_ = worker_scheduler;
  }

  // Set current worker (called by Worker during initialization)
  void SetWorker(class Worker* worker) { worker_ = worker; }

  // Get connection (for async response)
  astra::network::Connection* GetConnection() const override {
    return static_cast<astra::network::Connection*>(connection_);
  }

  // Set connection (called when processing a command)
  void SetConnection(void* connection) { connection_ = connection; }

  // Get connection ID (for blocking manager)
  uint64_t GetConnectionId() const override { return connection_id_; }

  // Set connection ID (called when processing a command)
  void SetConnectionId(uint64_t connection_id) {
    connection_id_ = connection_id;
  }

  // Set AOF callback (for persistence)
  void SetAofCallbackString(std::function<void(const std::string&)> callback) {
    aof_callback_ = std::move(callback);
  }

  // Set RDB save callback (for persistence)
  void SetRdbSaveCallback(std::function<std::string(bool)> callback) {
    rdb_save_callback_ = std::move(callback);
  }

  // Get RDB save callback
  const std::function<std::string(bool)>& GetRdbSaveCallback() const {
    return rdb_save_callback_;
  }

  // Cluster operations (NO SHARING architecture)
  bool IsClusterEnabled() const override { return cluster_enabled_; }
  bool ClusterMeet(const std::string& ip, int port) override {
    if (gossip_manager_) {
      // Directly use the port provided by CLUSTER MEET command
      // User specifies the gossip port directly (e.g., CLUSTER MEET 127.0.0.1
      // 17002) No automatic offset calculation needed
      return gossip_manager_->MeetNode(ip, port);
    }
    return false;
  }
  bool ClusterAddSlots(const std::vector<uint16_t>& slots) override;
  bool ClusterDelSlots(const std::vector<uint16_t>& slots) override;
  cluster::GossipManager* GetGossipManager() const override {
    return gossip_manager_;
  }
  cluster::GossipManager* GetGossipManagerMutable() override {
    return gossip_manager_;
  }
  cluster::ShardManager* GetClusterShardManager() const override {
    return shard_manager_;
  }

  // Get cluster state (for slot management)
  std::shared_ptr<cluster::ClusterState> GetClusterState() const;

  // Set cluster managers (called by Worker after cluster initialization)
  void SetClusterEnabled(bool enabled) { cluster_enabled_ = enabled; }
  void SetGossipManager(cluster::GossipManager* gossip_manager) {
    gossip_manager_ = gossip_manager;
  }
  void SetShardManager(cluster::ShardManager* shard_manager) {
    shard_manager_ = shard_manager;
  }

  // Set worker ID (to avoid deadlock when updating cluster state)
  void SetWorkerId(size_t worker_id) { worker_id_ = worker_id; }

  // Set callback for updating cluster state across all workers
  void SetClusterStateUpdateCallback(
      std::function<void(std::shared_ptr<cluster::ClusterState>)> callback) {
    cluster_state_update_callback_ = std::move(callback);
  }

  // Log command to AOF
  void LogToAof(absl::string_view command,
                absl::Span<const absl::string_view> args) override {
    ASTRADB_LOG_DEBUG("LogToAof called: command={}, args={}", command,
                      args.size());
    if (aof_callback_) {
      // Build RESP command string
      std::string cmd_str;
      cmd_str += "*";
      cmd_str += std::to_string(args.size() + 1);  // +1 for the command name
      cmd_str += "\r\n";
      cmd_str += "$";
      cmd_str += std::to_string(command.size());
      cmd_str += "\r\n";
      cmd_str.append(command.data(), command.size());
      cmd_str += "\r\n";

      for (size_t i = 0; i < args.size(); ++i) {
        cmd_str += "$";
        cmd_str += std::to_string(args[i].size());
        cmd_str += "\r\n";
        cmd_str.append(args[i].data(), args[i].size());
        cmd_str += "\r\n";
      }

      ASTRADB_LOG_DEBUG("LogToAof calling callback, cmd_str size={}",
                        cmd_str.size());
      aof_callback_(cmd_str);
      ASTRADB_LOG_DEBUG("LogToAof callback completed");
    } else {
      ASTRADB_LOG_WARN("LogToAof called but aof_callback_ is null!");
    }
  }

 private:
  astra::commands::Database* db_;
  class commands::BlockingManager* blocking_manager_ = nullptr;
  class commands::CommandRegistry* command_registry_ = nullptr;
  class WorkerScheduler* worker_scheduler_ = nullptr;
  class Worker* worker_ = nullptr;
  class replication::ReplicationManager* replication_manager_ = nullptr;
  commands::PubSubManager* pubsub_manager_ = nullptr;
  void* connection_ = nullptr;
  uint64_t connection_id_ = 0;
  std::function<void(const std::string&)> aof_callback_;
  std::function<std::string(bool)>
      rdb_save_callback_;  // bool = background save

  // Cluster support (NO SHARING architecture)
  bool cluster_enabled_ = false;
  cluster::GossipManager* gossip_manager_ = nullptr;
  cluster::ShardManager* shard_manager_ = nullptr;
  size_t worker_id_ = 0;

  // Callback to update cluster state for all workers (set by Server)
  // Uses WorkerScheduler::DispatchOnAll to avoid deadlock
  std::function<void(std::shared_ptr<cluster::ClusterState>)>
      cluster_state_update_callback_;
};

// Note: WorkerCommandContext method implementations are defined after Worker
// class

// DataShard - Contains a full Database instance
class DataShard {
 public:
  explicit DataShard(size_t shard_id)
      : shard_id_(shard_id), context_(&database_) {
    // Initialize command registry
    auto cmd_count =
        astra::commands::RuntimeCommandRegistry::Instance().GetCommandCount();
    ASTRADB_LOG_DEBUG("Shard {}: RuntimeCommandRegistry has {} commands",
                      shard_id_, cmd_count);

    astra::commands::RuntimeCommandRegistry::Instance().ApplyToRegistry(
        registry_);

    auto registry_size = registry_.Size();
    ASTRADB_LOG_DEBUG("Shard {}: CommandRegistry now has {} commands",
                      shard_id_, registry_size);

    // Set memory tracker for database
    database_.SetMemoryTracker(&memory_tracker_);
    // Note: InitializeEvictionManager() will be called after config is set in
    // SetMemoryConfig()
  }

  // Set memory configuration
  void SetMemoryConfig(
      const core::memory::MemoryTrackerConfig& config,
      core::memory::GetTotalMemoryCallback get_total_memory_callback = nullptr,
      bool enable_rocksdb = false) {
    ASTRADB_LOG_INFO(
        "Shard {}: Setting memory config - max_memory={}, policy={}, "
        "threshold={}, rocksdb={}",
        shard_id_, config.max_memory_limit,
        static_cast<int>(config.eviction_policy), config.eviction_threshold,
        enable_rocksdb);
    memory_tracker_.SetMaxMemory(config.max_memory_limit);
    memory_tracker_.SetEvictionPolicy(config.eviction_policy);
    memory_tracker_.SetEvictionThreshold(config.eviction_threshold);
    memory_tracker_.SetEvictionSamples(config.eviction_samples);
    memory_tracker_.SetTrackingEnabled(config.enable_tracking);

    // Initialize RocksDB if enabled
    if (enable_rocksdb && !rocksdb_adapter_) {
      std::string db_path = "data/rocksdb/shard_" + std::to_string(shard_id_);
      persistence::RocksDBAdapter::Config rocksdb_config;
      rocksdb_config.db_path = db_path;
      rocksdb_config.create_if_missing = true;
      rocksdb_config.enable_wal = true;
      rocksdb_config.cache_size = 256 * 1024 * 1024;  // 256MB cache

      rocksdb_adapter_ =
          std::make_unique<persistence::RocksDBAdapter>(rocksdb_config);
      if (rocksdb_adapter_->IsOpen()) {
        database_.SetRocksDBAdapter(rocksdb_adapter_.get());
        ASTRADB_LOG_INFO("Shard {}: RocksDB initialized at {}", shard_id_,
                         db_path);
      } else {
        ASTRADB_LOG_ERROR("Shard {}: Failed to initialize RocksDB at {}",
                          shard_id_, db_path);
        rocksdb_adapter_.reset();
      }
    }

    // Initialize eviction manager after config is set
    database_.InitializeEvictionManager(std::move(get_total_memory_callback));
  }

  // Check if command should be redirected due to cluster slot ownership
  // Returns empty string if command should be handled locally
  // Returns MOVED error if command should be redirected to another node
  std::string CheckClusterSlot(const astra::protocol::Command& command) {
    // Get the key from command (first argument for most commands)
    if (command.args.empty()) {
      return "";  // No key, handle locally
    }

    // Get key from command
    std::string key;
    if (command.args[0].IsBulkString() || command.args[0].IsSimpleString()) {
      key = command.args[0].AsString();
    } else {
      return "";  // No valid key, handle locally
    }

    // Calculate hash slot
    uint16_t slot = cluster::HashSlotCalculator::CalculateWithTag(key);
    ASTRADB_LOG_DEBUG("Shard {}: CheckClusterSlot - key='{}', slot={}",
                      shard_id_, key, slot);

    // Get cluster state
    auto cluster_state = context_.GetClusterState();
    if (!cluster_state) {
      ASTRADB_LOG_DEBUG("Shard {}: No cluster state, handling locally",
                        shard_id_);
      return "";  // No cluster state, handle locally
    }

    // Get slot owner
    auto slot_owner = cluster_state->GetSlotOwner(slot);
    if (!slot_owner.has_value()) {
      ASTRADB_LOG_WARN("Shard {}: Slot {} not assigned, handling locally",
                       shard_id_, slot);
      return "";  // Slot not assigned, handle locally
    }

    // Get self node ID
    auto* gossip_manager = context_.GetGossipManager();
    if (!gossip_manager) {
      ASTRADB_LOG_WARN("Shard {}: No gossip manager, handling locally",
                       shard_id_);
      return "";  // No gossip manager, handle locally
    }

    auto self = gossip_manager->GetSelf();
    std::string self_id = cluster::GossipManager::NodeIdToString(self.id);

    // Check if slot belongs to this node
    if (slot_owner.value() == self_id) {
      ASTRADB_LOG_DEBUG("Shard {}: Slot {} belongs to this node ({})",
                        shard_id_, slot, self_id);
      return "";  // Slot belongs to this node, handle locally
    }

    // Slot belongs to another node, find target node information
    ASTRADB_LOG_DEBUG(
        "Shard {}: Slot {} belongs to another node ({}), checking target "
        "node...",
        shard_id_, slot, slot_owner.value());

    auto all_nodes = gossip_manager->GetNodes();
    for (const auto& node : all_nodes) {
      std::string node_id = cluster::GossipManager::NodeIdToString(node.id);
      if (node_id == slot_owner.value()) {
        // Return MOVED error with target node information
        auto moved_response =
            astra::protocol::RespBuilder::BuildMoved(slot, node.ip, node.port);
        ASTRADB_LOG_DEBUG(
            "Shard {}: Returning MOVED error - slot={}, target={}:{}",
            shard_id_, slot, node.ip, node.port);
        return moved_response;
      }
    }

    // Slot owner not found in gossip, handle locally
    ASTRADB_LOG_WARN(
        "Shard {}: Slot {} owner {} not found in gossip, handling locally",
        shard_id_, slot, slot_owner.value());
    return "";
  }

  // Execute a command using the command registry
  std::string Execute(const astra::protocol::Command& command) {
    ASTRADB_LOG_DEBUG("Shard {}: Executing command: {}", shard_id_,
                      command.name);
    ASTRADB_LOG_DEBUG("Shard {}: Command args count: {}", shard_id_,
                      command.args.size());

    // Check cluster slot if cluster is enabled
    if (context_.IsClusterEnabled()) {
      ASTRADB_LOG_DEBUG(
          "Shard {}: Cluster enabled, checking slot for command '{}'",
          shard_id_, command.name);
      auto moved_error = CheckClusterSlot(command);
      if (!moved_error.empty()) {
        ASTRADB_LOG_DEBUG("Shard {}: Command '{}' redirected - {}", shard_id_,
                          command.name, moved_error);
        return moved_error;
      }
      ASTRADB_LOG_DEBUG(
          "Shard {}: Command '{}' slot check passed, handling locally",
          shard_id_, command.name);
    }

    auto result = registry_.Execute(command, &context_);
    ASTRADB_LOG_DEBUG("Shard {}: Registry::Execute completed", shard_id_);
    ASTRADB_LOG_DEBUG("Shard {}: Command result success={}, type={}", shard_id_,
                      result.success,
                      static_cast<int>(result.response.GetType()));

    if (!result.success) {
      ASTRADB_LOG_ERROR("Shard {}: Command failed: {}", shard_id_,
                        result.error);
      return astra::protocol::RespBuilder::BuildError(result.error);
    }

    ASTRADB_LOG_DEBUG("Shard {}: Calling RespBuilder::Build", shard_id_);
    auto response = astra::protocol::RespBuilder::Build(result.response);
    ASTRADB_LOG_DEBUG("Shard {}: RespBuilder::Build completed, len={}",
                      shard_id_, response.size());

    return response;
  }

  // Get database reference (for setting callbacks)
  astra::commands::Database& GetDatabase() { return database_; }

  // Get database reference (const version for RDB serialization)
  const astra::commands::Database& GetDatabase() const { return database_; }

  // Get command context (for setting callbacks)
  WorkerCommandContext* GetCommandContext() { return &context_; }

  // Get command registry (for COMMAND command - NO SHARING architecture)
  astra::commands::CommandRegistry* GetCommandRegistry() { return &registry_; }

  // Get memory tracker (for global memory tracking)
  core::memory::MemoryTracker* GetMemoryTracker() { return &memory_tracker_; }

 private:
  size_t shard_id_;
  astra::commands::Database database_;
  core::memory::MemoryTracker memory_tracker_;
  WorkerCommandContext context_;
  astra::commands::CommandRegistry registry_;
  std::unique_ptr<persistence::RocksDBAdapter> rocksdb_adapter_;
};

// Cross-worker request (forwarded from one worker to another)
struct CrossWorkerRequest {
  size_t source_worker_id;  // Which worker sent this request
  uint64_t conn_id;         // Connection ID on source worker
  astra::protocol::Command command;
};

// Cross-worker response (sent back to source worker)
struct CrossWorkerResponse {
  uint64_t conn_id;      // Connection ID on source worker
  std::string response;  // Response data
};

// Client info request (for CLIENT LIST command)
struct ClientInfoRequest {
  size_t source_worker_id;
  uint64_t req_id;  // Unique request ID
};

// Client info response (for CLIENT LIST command)
struct ClientInfoResponse {
  uint64_t req_id;
  std::vector<std::string> client_info_list;  // List of client info strings
};

// Pending client info request (for CLIENT LIST command)
struct PendingClientInfoReq {
  uint64_t req_id;
  uint64_t conn_id;  // Original client connection ID
  size_t responses_received = 0;
  size_t expected_responses = 0;
  std::vector<ClientInfoResponse> responses;
  absl::Time start_time;
};

// Batch cross-worker request for multi-key commands
struct BatchCrossWorkerRequest {
  uint64_t req_id;  // Unique request ID for matching responses
  size_t source_worker_id;
  std::string cmd_type;           // "SINTER", "SUNION", "ZUNIONSTORE", etc.
  std::vector<std::string> keys;  // Keys this worker should process
  std::vector<std::string>
      args;  // Additional arguments (weights, aggregate, etc.)
  std::shared_ptr<std::promise<std::vector<std::string>>>
      result_promise;  // For async response
};

struct BatchCrossWorkerResponse {
  uint64_t req_id;                  // Matching request ID
  std::vector<std::string> result;  // Result from this worker
};

// PubSub message (for cross-worker PUBLISH)
struct PubSubMessage {
  std::string channel;
  std::string message;
};

// PubSub Manager - NO SHARING architecture
// Each worker has its own PubSubManager to manage local subscriptions
// Uses MPSC queues for cross-worker PUBLISH message broadcasting
class PubSubManager : public commands::PubSubManager {
 public:
  PubSubManager(class Worker* worker, size_t worker_id)
      : worker_(worker), worker_id_(worker_id) {}

  void Subscribe(uint64_t conn_id,
                 const std::vector<std::string>& channels) override;
  void Unsubscribe(uint64_t conn_id,
                   const std::vector<std::string>& channels) override;
  void PSubscribe(uint64_t conn_id,
                  const std::vector<std::string>& patterns) override;
  void PUnsubscribe(uint64_t conn_id,
                    const std::vector<std::string>& patterns) override;
  size_t Publish(const std::string& channel,
                 const std::string& message) override;
  size_t GetSubscriptionCount(uint64_t conn_id) const override;
  bool IsSubscribed(uint64_t conn_id) const override;
  std::vector<std::string> GetActiveChannels(
      const std::string& pattern = "") const override;
  size_t GetChannelSubscriberCount(const std::string& channel) const override;
  size_t GetPatternSubscriptionCount() const override;

  // Process incoming pubsub message from other workers
  void ProcessPubSubMessage(const PubSubMessage& msg);

 private:
  class Worker* worker_;
  size_t worker_id_;

  // Local subscriptions (NO SHARING - each worker manages its own)
  absl::flat_hash_map<std::string, absl::flat_hash_set<uint64_t>>
      channel_subscribers_;
  absl::flat_hash_map<std::string, absl::flat_hash_set<uint64_t>>
      pattern_subscribers_;
  absl::flat_hash_map<uint64_t, absl::flat_hash_set<std::string>>
      conn_channels_;
  absl::flat_hash_map<uint64_t, absl::flat_hash_set<std::string>>
      conn_patterns_;

  mutable std::mutex pubsub_mutex_;  // Protect subscription structures

  // Helper methods
  bool PatternMatches(const std::string& pattern,
                      const std::string& channel) const;
  void SendSubscribeReply(uint64_t conn_id, const std::string& channel,
                          size_t count);
  void SendUnsubscribeReply(uint64_t conn_id, const std::string& channel,
                            size_t count);
};

// Command with connection ID
struct CommandWithConnId {
  uint64_t conn_id;
  astra::protocol::Command command;
  bool is_forwarded;  // True if this is a forwarded command from another worker
  size_t source_worker_id;  // Which worker sent this forwarded command (only
                            // valid if is_forwarded=true)
};

// Response with connection ID
struct ResponseWithConnId {
  uint64_t conn_id;
  std::string response;
};

// Worker - NO SHARING architecture with MPSC cross-worker communication
// Each worker is a completely independent "mini server"
class Worker {
 public:
  // Friend classes (for NO SHARING architecture)
  friend class PubSubManager;

  explicit Worker(size_t worker_id, const std::string& host, uint16_t port,
                  std::vector<Worker*> all_workers)
      : worker_id_(worker_id),
        io_context_(),
        acceptor_(io_context_),
        data_shard_(worker_id),
        all_workers_(std::move(all_workers)),
        running_(false) {
    ASTRADB_LOG_INFO("Worker {}: Creating", worker_id_);

    // Setup acceptor
    asio::ip::tcp::endpoint endpoint(asio::ip::make_address(host), port);

    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(asio::socket_base::reuse_address(true));

    // Enable SO_REUSEPORT for kernel-level load balancing
    // This allows multiple acceptors to bind to the same address/port
    // and the kernel will distribute connections evenly across them
#ifdef SO_REUSEPORT
    int reuseport = 1;
    int result = setsockopt(acceptor_.native_handle(), SOL_SOCKET, SO_REUSEPORT,
                            &reuseport, sizeof(reuseport));
    if (result == 0) {
      ASTRADB_LOG_INFO(
          "Worker {}: SO_REUSEPORT enabled - kernel will distribute "
          "connections "
          "evenly across workers",
          worker_id_);
    } else {
      ASTRADB_LOG_WARN(
          "Worker {}: Failed to enable SO_REUSEPORT: {} (falling back to "
          "single acceptor mode)",
          worker_id_, strerror(errno));
    }
#else
    ASTRADB_LOG_WARN(
        "Worker {}: SO_REUSEPORT not supported on this platform (using single "
        "acceptor mode)",
        worker_id_);
#endif

    acceptor_.bind(endpoint);
    acceptor_.listen(asio::socket_base::max_listen_connections);

    ASTRADB_LOG_INFO("Worker {}: Acceptor created on {}:{}", worker_id_, host,
                     port);
  }

  ~Worker() { Stop(); }

  // Start the worker (starts both IO and executor threads)
  void Start(std::shared_ptr<cluster::ClusterState> cluster_state = nullptr) {
    if (running_) {
      ASTRADB_LOG_WARN("Worker {} already running", worker_id_);
      return;
    }

    running_ = true;
    ASTRADB_LOG_INFO("Worker {}: Starting", worker_id_);

    // Initialize cluster state
    if (cluster_state) {
      cluster_state_ = std::move(cluster_state);
      cluster::ClusterStateAccessor::Set(cluster_state_);
      ASTRADB_LOG_INFO("Worker {}: Cluster state initialized", worker_id_);
    }

    // Set batch request callback in database
    data_shard_.GetDatabase().SetBatchRequestCallback(
        [this](
            const std::string& cmd_type, const std::vector<std::string>& keys,
            const std::vector<std::string>& args) -> std::vector<std::string> {
          return SendBatchRequest(cmd_type, keys, args);
        });

    // Initialize blocking manager
    blocking_manager_ =
        std::make_unique<commands::BlockingManager>(io_context_);

    // Initialize pubsub manager
    pubsub_manager_ = std::make_unique<PubSubManager>(this, worker_id_);

    // Set pubsub manager in command context (required for PUBSUB commands)
    data_shard_.GetCommandContext()->SetPubSubManager(pubsub_manager_.get());

    // Set command registry in command context (required for COMMAND command)
    data_shard_.GetCommandContext()->SetCommandRegistry(
        data_shard_.GetCommandRegistry());

    // Set current worker in command context (required for cross-shard
    // operations)
    data_shard_.GetCommandContext()->SetWorker(this);

    // Start IO thread
    io_thread_ = std::thread([this]() {
      ASTRADB_LOG_DEBUG("Worker {}: IO thread started", worker_id_);
      DoAccept();
      ProcessResponseQueue();  // Start response queue processing
      io_context_.run();
      ASTRADB_LOG_DEBUG("Worker {}: IO thread exited", worker_id_);
    });

    // Start executor thread
    exec_thread_ = std::thread([this]() {
      ASTRADB_LOG_DEBUG("Worker {}: Executor thread started", worker_id_);
      ExecutorLoop();
      ASTRADB_LOG_DEBUG("Worker {}: Executor thread exited", worker_id_);
    });
  }

  // Stop the worker
  void Stop() {
    if (!running_) {
      return;
    }

    ASTRADB_LOG_INFO("Worker {}: Stopping", worker_id_);
    running_ = false;

    // Stop acceptor
    asio::error_code ec;
    acceptor_.close(ec);

    // Stop io_context
    io_context_.stop();

    // Wait for threads
    if (io_thread_.joinable()) {
      io_thread_.join();
    }

    // Executor thread will stop when running_ is false
    if (exec_thread_.joinable()) {
      exec_thread_.join();
    }

    ASTRADB_LOG_INFO("Worker {}: Stopped", worker_id_);
  }

  // Get the worker's io_context (for creating connections)
  asio::io_context& GetIOContext() { return io_context_; }

  // Get worker ID
  size_t GetWorkerId() const { return worker_id_; }

  // Add a task to the scheduler queue (called by WorkerScheduler)
  template <typename F>
  void AddTask(F&& func) {
    ASTRADB_LOG_DEBUG("Worker {}: Task added to scheduler queue", worker_id_);
    task_queue_.enqueue(std::function<void()>(std::forward<F>(func)));
    // NOTE: Not calling NotifyExecutorLoop() here to avoid frequent wakeups
    // Scheduler tasks are less latency-sensitive than client commands
    // ExecutorLoop will process them on next iteration or after timeout
  }

  // Notify the executor loop to process pending tasks immediately
  // This should be called after AddTask when immediate processing is needed
  void NotifyTaskProcessing() { NotifyExecutorLoop(); }

  // Get blocking manager (for blocking commands)
  commands::BlockingManager* GetBlockingManager() {
    return blocking_manager_.get();
  }

  // Get pubsub manager (for publish/subscribe commands)
  PubSubManager* GetPubSubManager() { return pubsub_manager_.get(); }

  // Get cluster state (for cluster commands)
  std::shared_ptr<cluster::ClusterState> GetClusterState() const {
    return cluster_state_;
  }

  // Set cluster state (called by Server when cluster configuration changes)
  void SetClusterState(std::shared_ptr<cluster::ClusterState> state) {
    cluster_state_ = std::move(state);
    // Also update thread-local accessor
    cluster::ClusterStateAccessor::Set(cluster_state_);
  }

  // Get local stats (NO SHARING architecture - each worker has its own stats)
  ServerStats& GetLocalStats() { return local_stats_; }
  const ServerStats& GetLocalStats() const { return local_stats_; }

  // Get data shard (for RDB loading)
  DataShard& GetDataShard() { return data_shard_; }
  const DataShard& GetDataShard() const { return data_shard_; }

  // Enqueue a client info response
  void EnqueueClientInfoResponse(const ClientInfoResponse& resp);

  // Enqueue a pubsub message (for receiving PUBLISH from other workers)
  void EnqueuePubSubMessage(const PubSubMessage& msg) {
    pubsub_msg_queue_.enqueue(msg);
    NotifyExecutorLoop();
  }

  // Process pubsub messages (called from ExecutorLoop)
  bool ProcessPubSubMessages() {
    bool has_work = false;
    PubSubMessage msg;
    while (pubsub_msg_queue_.try_dequeue(msg)) {
      has_work = true;
      pubsub_manager_->ProcessPubSubMessage(msg);
    }
    return has_work;
  }

  // Enqueue a client info request (for sending to other workers)
  void EnqueueClientInfoRequest(const ClientInfoRequest& req) {
    client_info_req_queue_.enqueue(req);
    NotifyExecutorLoop();
  }

  // Send client info request to all workers (for CLIENT LIST command)
  uint64_t SendClientInfoRequest(uint64_t conn_id);

  // Get connection info (for CLIENT LIST command)
  std::vector<std::tuple<uint64_t, std::string, std::string, int>>
  GetConnectionInfo() const;

  // Kill a connection by ID (for CLIENT KILL command)
  bool KillConnection(uint64_t conn_id);

  // Process a client info request (for CLIENT LIST command)
  void ProcessClientInfoRequest(const ClientInfoRequest& req);

  // Set all workers reference (called by Server after all workers are created)
  void SetAllWorkers(const std::vector<Worker*>& all_workers) {
    all_workers_ = all_workers;
  }

  // Set persistence manager (called by Server after persistence is initialized)

  void SetPersistenceManager(void* persistence_manager) {
    ASTRADB_LOG_DEBUG("Worker {}: SetPersistenceManager called with ptr={}",
                      worker_id_, persistence_manager);

    if (persistence_manager) {
      auto* pm = static_cast<PersistenceManager*>(persistence_manager);

      // Set AOF callback

      data_shard_.GetCommandContext()->SetAofCallbackString(
          [pm](const std::string& command) {
            ASTRADB_LOG_DEBUG("AOF callback invoked, command size={}",
                              command.size());

            pm->AppendCommand(command);

            ASTRADB_LOG_DEBUG("AOF callback completed");
          });

      // Set RDB save callback

      data_shard_.GetCommandContext()->SetRdbSaveCallback(

          [this, pm](bool background) -> std::string {
            ASTRADB_LOG_DEBUG("Worker {}: RDB save requested, background={}",

                              worker_id_, background);

            if (background) {
              // Background save

              bool success = pm->BackgroundSaveRdb(all_workers_);

              if (success) {
                return "OK";

              } else {
                // Check if already in progress

                if (pm->IsBgSaveInProgress()) {
                  return "ALREADY_IN_PROGRESS";
                }

                return "ERR Background save failed";
              }

            } else {
              // Synchronous save

              bool success = pm->SaveRdb(all_workers_);

              if (success) {
                return "OK";

              } else {
                return "ERR Save failed";
              }
            }
          });

      // Set blocking manager in command context
      data_shard_.GetCommandContext()->SetBlockingManager(
          blocking_manager_.get());

      // Set pubsub manager in command context
      data_shard_.GetCommandContext()->SetPubSubManager(pubsub_manager_.get());

      ASTRADB_LOG_DEBUG("Worker {}: Persistence callbacks set successfully",
                        worker_id_);

    } else {
      ASTRADB_LOG_WARN("Worker {}: SetPersistenceManager called with null ptr",
                       worker_id_);
    }
  }

  // Set worker scheduler (called by Server after worker scheduler is
  // initialized)
  void SetWorkerScheduler(class WorkerScheduler* worker_scheduler) {
    ASTRADB_LOG_DEBUG("Worker {}: SetWorkerScheduler called with ptr={}",
                      worker_id_, static_cast<const void*>(worker_scheduler));
    data_shard_.GetCommandContext()->SetWorkerScheduler(worker_scheduler);
  }

  // Process a cross-worker request (MPSC queue entry point)
  void ProcessCrossWorkerRequest(const CrossWorkerRequest& req) {
    ASTRADB_LOG_DEBUG(
        "Worker {}: Processing cross-worker request from Worker {}", worker_id_,
        req.source_worker_id);

    // Execute command on this worker's data shard
    std::string response = data_shard_.Execute(req.command);

    // Send response back to source worker
    if (req.source_worker_id < all_workers_.size()) {
      CrossWorkerResponse cross_resp{req.conn_id, response};
      all_workers_[req.source_worker_id]->EnqueueCrossWorkerResponse(
          cross_resp);
    }
  }

  // Enqueue a cross-worker response (called by other workers)
  void EnqueueCrossWorkerResponse(const CrossWorkerResponse& resp) {
    cross_worker_resp_queue_.enqueue(resp);
    NotifyExecutorLoop();
  }

  // Enqueue a cross-worker request (called by other workers)
  void EnqueueCrossWorkerRequest(const CrossWorkerRequest& req) {
    // Create a command with connection ID for the request
    CommandWithConnId cmd{req.conn_id, req.command, true,
                          req.source_worker_id};  // Mark as forwarded
    cmd_queue_.enqueue(cmd);
    NotifyExecutorLoop();
  }

  // Send batch request to multiple workers (for multi-key commands)
  std::vector<std::string> SendBatchRequest(
      const std::string& cmd_type, const std::vector<std::string>& keys,
      const std::vector<std::string>& args = {});

  // Enqueue batch response
  void EnqueueBatchResponse(const BatchCrossWorkerResponse& resp) {
    batch_resp_queue_.enqueue(resp);
    NotifyExecutorLoop();
  }

  // Enqueue batch request (called by other workers)
  void EnqueueBatchRequest(const BatchCrossWorkerRequest& req) {
    batch_req_queue_.enqueue(req);
    NotifyExecutorLoop();
  }

  // Process batch request from another worker
  void ProcessBatchRequest(const BatchCrossWorkerRequest& req);

  // Process batch responses (called in executor loop)
  bool ProcessBatchResponses();

  // ========== RDB Persistence Operations ==========

  // Serialize worker's data to RDB format
  // Returns a vector of (key, type, value, ttl_ms) tuples
  std::vector<
      std::tuple<std::string, astra::storage::KeyType, std::string, int64_t>>
  GetRdbData() const {
    std::vector<
        std::tuple<std::string, astra::storage::KeyType, std::string, int64_t>>
        data;

    // Iterate through all keys in the database
    data_shard_.GetDatabase().ForEachKey(
        [&data](const std::string& key, astra::storage::KeyType type,
                const std::string& value, int64_t ttl_ms) {
          data.emplace_back(key, type, value, ttl_ms);
        });

    return data;
  }

  // Get key count for RDB file
  size_t GetRdbKeyCount() const {
    return data_shard_.GetDatabase().GetKeyCount();
  }

  // Get expired keys count for RDB file
  size_t GetRdbExpiredKeysCount() const {
    return data_shard_.GetDatabase().GetExpiredKeysCount();
  }

 private:
  void DoAccept() {
    if (!running_) {
      return;
    }

    acceptor_.async_accept(
        [this](asio::error_code ec, asio::ip::tcp::socket socket) {
          if (!ec && running_) {
            ASTRADB_LOG_DEBUG("Worker {}: Accepted connection", worker_id_);

            // Create connection ID
            uint64_t conn_id = next_conn_id_++;

            // Create connection
            auto conn = std::make_shared<Connection>(
                worker_id_, conn_id, std::move(socket), &cmd_queue_,
                &resp_queue_, [this]() { NotifyExecutorLoop(); },
                [this](uint64_t conn_id) {
                  // Remove connection from map when it's closed
                  std::lock_guard<std::mutex> lock(connections_mutex_);
                  connections_.erase(conn_id);
                  ASTRADB_LOG_DEBUG(
                      "Worker {}: Connection {} removed, total connections: {}",
                      worker_id_, conn_id, connections_.size());
                });

            connections_[conn_id] = conn;
            conn->Start();

            // Record connection in metrics
            astra::metrics::AstraMetrics::Instance().IncrementConnections();

            ASTRADB_LOG_INFO(
                "Worker {}: Connection {} started, total connections: {}",
                worker_id_, conn_id, connections_.size());
          }

          // Continue accepting
          DoAccept();
        });
  }

  // Connection class - belongs to one worker
  class Connection : public std::enable_shared_from_this<Connection> {
   public:
    Connection(size_t worker_id, uint64_t conn_id, asio::ip::tcp::socket socket,
               moodycamel::ConcurrentQueue<CommandWithConnId>* cmd_queue,
               moodycamel::ConcurrentQueue<ResponseWithConnId>* resp_queue,
               std::function<void()> notify_callback,
               std::function<void(uint64_t)> on_close_callback)
        : worker_id_(worker_id),
          conn_id_(conn_id),
          socket_(std::move(socket)),
          cmd_queue_(cmd_queue),
          resp_queue_(resp_queue),
          notify_callback_(std::move(notify_callback)),
          on_close_callback_(std::move(on_close_callback)) {
      // Enable TCP_NODELAY to disable Nagle's algorithm (like Redis)
      asio::ip::tcp::no_delay option(true);
      socket_.set_option(option);
      ASTRADB_LOG_DEBUG("Worker {}: Connection {} created", worker_id_,
                        conn_id_);
    }

    ~Connection() {
      ASTRADB_LOG_DEBUG("Worker {}: Connection {} destroyed", worker_id_,
                        conn_id_);
      // Close socket to avoid CLOSE_WAIT
      Close();
      astra::metrics::AstraMetrics::Instance().DecrementConnections();
    }

    void Start() {
      ASTRADB_LOG_DEBUG("Worker {}: Connection {} starting", worker_id_,
                        conn_id_);
      // Spawn coroutine for reading (similar to Dragonfly's Fiber per
      // connection)
      asio::co_spawn(
          socket_.get_executor(),
          [self = shared_from_this()]() -> asio::awaitable<void> {
            co_await self->DoRead();
          },
          asio::detached);
    }

    asio::ip::tcp::socket& GetSocket() { return socket_; }

    asio::awaitable<void> Send(const std::string& response) {
      ASTRADB_LOG_DEBUG(
          "Worker {}: Connection {} sending response: {} (len={})", worker_id_,
          conn_id_, response, response.size());

      asio::error_code ec;
      size_t bytes_written = co_await asio::async_write(
          socket_, asio::buffer(response),
          asio::redirect_error(asio::use_awaitable, ec));

      if (!ec) {
        ASTRADB_LOG_DEBUG("Worker {}: Connection {} response sent (bytes={})",
                          worker_id_, conn_id_, bytes_written);
        astra::metrics::AstraMetrics::Instance().RecordNetworkOutput(
            bytes_written);
      } else {
        ASTRADB_LOG_ERROR("Worker {}: Connection {} write error: {}",
                          worker_id_, conn_id_, ec.message());
      }
    }

    void Close() {
      asio::error_code ec;
      socket_.close(ec);
      if (ec) {
        ASTRADB_LOG_WARN("Worker {}: Connection {} close error: {}", worker_id_,
                         conn_id_, ec.message());
      }
    }

    bool IsConnected() const { return socket_.is_open(); }

    std::string GetRemoteAddress() const {
      try {
        auto endpoint = socket_.remote_endpoint();
        return endpoint.address().to_string() + ":" +
               std::to_string(endpoint.port());
      } catch (...) {
        return "unknown";
      }
    }

    void SetClientName(const std::string& name) { client_name_ = name; }

    std::string GetClientName() const { return client_name_; }

    int GetProtocolVersion() const {
      return 3;  // Default to RESP3
    }

   private:
    asio::awaitable<void> DoRead() {
      // Get the executor for this coroutine
      auto executor = co_await asio::this_coro::executor;

      while (true) {
        asio::error_code ec;
        size_t bytes_transferred = co_await socket_.async_read_some(
            asio::buffer(buffer_),
            asio::redirect_error(asio::use_awaitable, ec));

        if (ec) {
          // EOF is a normal connection closure, log as debug instead of error
          if (ec == asio::error::eof) {
            ASTRADB_LOG_DEBUG("Worker {}: Connection {} closed by client",
                              worker_id_, conn_id_);
          } else {
            ASTRADB_LOG_ERROR("Worker {}: Connection {} read error: {}",
                              worker_id_, conn_id_, ec.message());
          }
          break;
        }

        ASTRADB_LOG_DEBUG("Worker {}: Connection {} received {} bytes",
                          worker_id_, conn_id_, bytes_transferred);

        // Append to receive buffer
        receive_buffer_.append(buffer_.data(), bytes_transferred);

        astra::metrics::AstraMetrics::Instance().RecordNetworkInput(
            bytes_transferred);

        // Process commands (minimal parsing only)
        ProcessCommands();
      }

      ASTRADB_LOG_DEBUG("Worker {}: Connection {} read loop terminated",
                        worker_id_, conn_id_);

      // Notify Worker to remove this connection from connections_ map
      if (on_close_callback_) {
        on_close_callback_(conn_id_);
      }
    }

    void ProcessCommands() {
      while (true) {
        // Check if we have a complete RESP value
        if (!astra::protocol::RespParser::HasCompleteValue(receive_buffer_)) {
          break;
        }

        // Parse the command
        std::string_view data_view(receive_buffer_);
        auto value_opt = astra::protocol::RespParser::Parse(data_view);

        if (!value_opt) {
          ASTRADB_LOG_ERROR("Worker {}: Connection {} failed to parse RESP",
                            worker_id_, conn_id_);
          astra::metrics::AstraMetrics::Instance().RecordError("protocol");
          // Send error via response queue
          SendResponseViaQueue("ERR invalid RESP protocol");
          break;
        }

        // Calculate how many bytes were consumed
        size_t consumed = receive_buffer_.size() - data_view.size();
        receive_buffer_.erase(0, consumed);

        // Parse command from RESP value
        auto command_opt =
            astra::protocol::RespParser::ParseCommand(*value_opt);
        if (!command_opt) {
          ASTRADB_LOG_ERROR("Worker {}: Connection {} failed to parse command",
                            worker_id_, conn_id_);
          astra::metrics::AstraMetrics::Instance().RecordError("syntax");
          SendResponseViaQueue("ERR invalid command format");
          continue;
        }

        // Enqueue command to executor thread (within same worker!)
        CommandWithConnId cmd{conn_id_, *command_opt, false,
                              worker_id_};  // Not forwarded, source is self
        cmd_queue_->enqueue(cmd);
        // Notify ExecutorLoop that there's work to do
        if (notify_callback_) {
          notify_callback_();
        }
      }
    }

    void SendResponseViaQueue(const std::string& response) {
      // Send response via response queue (from IO thread)
      ResponseWithConnId resp{conn_id_, response};
      resp_queue_->enqueue(resp);
    }

    size_t worker_id_;
    uint64_t conn_id_;
    asio::ip::tcp::socket socket_;
    moodycamel::ConcurrentQueue<CommandWithConnId>* cmd_queue_;
    moodycamel::ConcurrentQueue<ResponseWithConnId>* resp_queue_;
    std::function<void()> notify_callback_;
    std::function<void(uint64_t)> on_close_callback_;

    std::array<char, 1024> buffer_;
    std::string receive_buffer_;
    std::string client_name_;
  };

  // Calculate which worker should handle this key (consistent hashing)
  size_t RouteToWorker(const std::string& key) {
    if (key.empty()) {
      return worker_id_ % all_workers_.size();
    }
    // Simple hash-based routing
    uint64_t hash = std::hash<std::string>{}(key);
    return hash % all_workers_.size();
  }

  void ExecutorLoop() {
    while (running_) {
      bool has_work = false;

      // Process local commands first
      CommandWithConnId cmd;
      if (cmd_queue_.try_dequeue(cmd)) {
        has_work = true;
        ASTRADB_LOG_DEBUG(
            "Worker {}: Executor processing command: {} for conn {} "
            "(forwarded={})",
            worker_id_, cmd.command.name, cmd.conn_id, cmd.is_forwarded);

        if (cmd.is_forwarded) {
          // This is a forwarded command from another worker
          // Execute directly and send response back to source worker
          LocalCommandTimer timer(cmd.command.name, &local_stats_);
          std::string response = data_shard_.Execute(cmd.command);
          CrossWorkerResponse cross_resp{cmd.conn_id, response};
          all_workers_[cmd.source_worker_id]->EnqueueCrossWorkerResponse(
              cross_resp);
        } else {
          // This is a new command from a client
          // Determine if this command should be forwarded to another worker
          // Check if command has a key argument
          size_t target_worker = worker_id_;
          if (!cmd.command.args.empty() &&
              (cmd.command.args[0].IsBulkString() ||
               cmd.command.args[0].IsSimpleString())) {
            std::string key = cmd.command.args[0].AsString();
            target_worker = RouteToWorker(key);
          }

          if (target_worker == worker_id_) {
            // Handle locally
            LocalCommandTimer timer(cmd.command.name, &local_stats_);

            // Set connection and connection ID in command context for blocking
            // commands
            auto conn_it = connections_.find(cmd.conn_id);
            if (conn_it != connections_.end()) {
              data_shard_.GetCommandContext()->SetConnection(
                  conn_it->second.get());
              data_shard_.GetCommandContext()->SetConnectionId(cmd.conn_id);
            }

            std::string response = data_shard_.Execute(cmd.command);
            ResponseWithConnId resp{cmd.conn_id, response};
            resp_queue_.enqueue(resp);

            // OPTIMIZATION: Trigger immediate response processing for low
            // latency This is especially important for single-connection
            // scenarios
            NotifyResponseQueue();
          } else {
            // Forward to target worker (enqueue to avoid blocking)
            ASTRADB_LOG_DEBUG("Worker {}: Forwarding command to Worker {}",
                              worker_id_, target_worker);
            CrossWorkerRequest cross_req{worker_id_,  // source worker
                                         cmd.conn_id, cmd.command};
            // Enqueue to target worker to avoid blocking this worker's
            // ExecutorLoop
            all_workers_[target_worker]->EnqueueCrossWorkerRequest(cross_req);
          }
        }
      }

      // Process cross-worker responses
      CrossWorkerResponse cross_resp;
      while (cross_worker_resp_queue_.try_dequeue(cross_resp)) {
        has_work = true;
        ResponseWithConnId resp{cross_resp.conn_id, cross_resp.response};
        resp_queue_.enqueue(resp);

        // OPTIMIZATION: Trigger immediate response processing
        NotifyResponseQueue();
      }

      // Process client info responses (for CLIENT LIST command)
      ClientInfoResponse client_info_resp;
      while (client_info_resp_queue_.try_dequeue(client_info_resp)) {
        has_work = true;
        // Store the response in pending requests map
        std::lock_guard<std::mutex> lock(batch_mutex_);
        auto it = pending_client_info_reqs_.find(client_info_resp.req_id);
        if (it != pending_client_info_reqs_.end()) {
          it->second->responses.push_back(client_info_resp);
        }
      }

      // Process batch responses
      if (ProcessBatchResponses()) {
        has_work = true;
      }

      // Process pubsub messages from other workers
      if (ProcessPubSubMessages()) {
        has_work = true;
      }

      // Process scheduler tasks (from WorkerScheduler)
      std::function<void()> task;
      while (task_queue_.try_dequeue(task)) {
        has_work = true;
        ASTRADB_LOG_DEBUG("Worker {}: Processing scheduler task", worker_id_);
        try {
          task();
        } catch (const std::exception& e) {
          ASTRADB_LOG_ERROR("Worker {}: Scheduler task failed: {}", worker_id_,
                            e.what());
        }
      }

      // Process batch requests from other workers
      BatchCrossWorkerRequest batch_req;
      while (batch_req_queue_.try_dequeue(batch_req)) {
        has_work = true;
        ProcessBatchRequest(batch_req);
      }

      // Process client info requests from other workers
      ClientInfoRequest client_info_req;
      while (client_info_req_queue_.try_dequeue(client_info_req)) {
        has_work = true;
        ProcessClientInfoRequest(client_info_req);
      }

      // Wait if no work (using absl condition variable for best performance)
      // NOTE: 1ms timeout to handle edge cases where NotifyExecutorLoop() might
      // be missed This provides a balance between:
      // - Zero latency (when notification works correctly)
      // - Fallback timeout (when notification is missed, prevents deadlock)
      // Spurious wakeups are handled by the outer while loop
      if (!has_work) {
        absl::MutexLock lock(&executor_mutex_);
        // Wait for notification with 1ms timeout (prevents potential deadlock)
        executor_cv_.WaitWithTimeout(&executor_mutex_, absl::Milliseconds(1));
      }
    }
  }

  // Trigger immediate response processing using asio::post()
  // This eliminates timer delays and follows asio best practices
  // ProcessResponseQueue() will run in the io_context_ thread
  void NotifyResponseQueue() {
    asio::post(io_context_, [this]() { ProcessResponseQueue(); });
  }

  void ProcessResponseQueue() {
    if (!running_) {
      return;
    }

    // Collect all available responses (not limited to 100)
    // This increases batching opportunities (Dragonfly best practice)
    absl::flat_hash_map<uint64_t, std::vector<std::string>> conn_responses;

    ResponseWithConnId resp;
    while (resp_queue_.try_dequeue(resp)) {
      conn_responses[resp.conn_id].push_back(std::move(resp.response));

      // Limit total responses per batch to avoid memory bloat
      static constexpr size_t kMaxBatchSize = 1000;
      if (conn_responses.size() > kMaxBatchSize) {
        break;
      }
    }

    // Send responses per connection (batched and merged)
    for (const auto& [conn_id, responses] : conn_responses) {
      auto it = connections_.find(conn_id);
      if (it != connections_.end() && !responses.empty()) {
        // Merge all responses into a single buffer for true batch sending
        asio::co_spawn(
            it->second->GetSocket().get_executor(),
            [conn = it->second,
             responses = std::move(responses)]() -> asio::awaitable<void> {
              // Calculate total size and pre-allocate
              size_t total_size = 0;
              for (const auto& response : responses) {
                total_size += response.size();
              }

              std::string batch;
              batch.reserve(total_size);

              // Merge all responses (use append to avoid reallocations)
              for (const auto& response : responses) {
                batch.append(response);
              }

              // Send all responses in a single system call
              co_await conn->Send(batch);
            },
            asio::detached);
      }
    }

    // OPTIMIZATION: No periodic timer needed
    // ProcessResponseQueue() is triggered by asio::post() when responses are
    // ready This eliminates timer delays and follows Redis/Dragonfly best
    // practices
  }

  size_t worker_id_;
  asio::io_context io_context_;
  asio::ip::tcp::acceptor acceptor_;
  DataShard data_shard_;

  // Worker's private queues (no sharing!)
  moodycamel::ConcurrentQueue<CommandWithConnId> cmd_queue_;
  moodycamel::ConcurrentQueue<ResponseWithConnId> resp_queue_;

  // Cross-worker communication queues (MPSC)
  moodycamel::ConcurrentQueue<CrossWorkerResponse> cross_worker_resp_queue_;
  moodycamel::ConcurrentQueue<ClientInfoRequest> client_info_req_queue_;
  moodycamel::ConcurrentQueue<ClientInfoResponse> client_info_resp_queue_;

  // Scheduler task queue (for WorkerScheduler cross-worker task dispatch)
  moodycamel::ConcurrentQueue<std::function<void()>> task_queue_;

  // Batch request queues (for multi-key commands)
  moodycamel::ConcurrentQueue<BatchCrossWorkerRequest> batch_req_queue_;
  moodycamel::ConcurrentQueue<BatchCrossWorkerResponse> batch_resp_queue_;

  // Pending batch requests (for matching responses)
  std::unordered_map<uint64_t, std::shared_ptr<BatchCrossWorkerRequest>>
      pending_batch_reqs_;
  std::unordered_map<uint64_t, std::shared_ptr<PendingClientInfoReq>>
      pending_client_info_reqs_;
  std::atomic<uint64_t> next_batch_req_id_{1};
  std::atomic<uint64_t> next_client_info_req_id_{1};

  // Mutex for protecting connections_ map (NO SHARING architecture)
  std::mutex connections_mutex_;

  absl::flat_hash_map<uint64_t, std::shared_ptr<Connection>> connections_;

  // Reference to all workers for cross-worker communication
  std::vector<Worker*> all_workers_;

  std::thread io_thread_;
  std::thread exec_thread_;
  std::atomic<bool> running_{false};
  std::atomic<uint64_t> next_conn_id_{0};
  std::mutex batch_mutex_;  // For pending_batch_reqs_ access

  // Condition variable and mutex for ExecutorLoop (absl for better performance)
  absl::Mutex executor_mutex_;
  absl::CondVar executor_cv_;
  [[maybe_unused]] bool has_work_{
      false};  // Flag to indicate if there's work to do

  // Condition variable and mutex for batch response notification
  absl::Mutex batch_response_mutex_;
  absl::CondVar batch_response_cv_;
  std::atomic<size_t> pending_batch_responses_{0};

  // Notify ExecutorLoop that there's work to do
  inline void NotifyExecutorLoop() {
    absl::MutexLock lock(&executor_mutex_);
    executor_cv_.Signal();
  }

  // Notify that a batch response is ready
  inline void NotifyBatchResponseReady() {
    absl::MutexLock lock(&batch_response_mutex_);
    batch_response_cv_.Signal();
  }

  // Blocking manager for blocking commands (BLPOP, BRPOP, etc.)
  std::unique_ptr<commands::BlockingManager> blocking_manager_;

  // PubSub manager for publish/subscribe commands (NO SHARING architecture)
  std::unique_ptr<PubSubManager> pubsub_manager_;

  // PubSub message queue for receiving PUBLISH messages from other workers
  moodycamel::ConcurrentQueue<PubSubMessage> pubsub_msg_queue_;

  // Local stats (NO SHARING architecture - each worker has its own stats)
  ServerStats local_stats_;

  // Cluster state (NO SHARING architecture - each worker has its own
  // thread-local snapshot) Following Dragonfly's pattern: zero-copy updates via
  // shared_ptr
  std::shared_ptr<cluster::ClusterState> cluster_state_;

  // RAII timer for command duration (NO SHARING architecture - uses local
  // stats)
  class LocalCommandTimer {
   public:
    explicit LocalCommandTimer(absl::string_view command, ServerStats* stats)
        : command_(command), start_(absl::Now()), stats_(stats) {}

    ~LocalCommandTimer() {
      if (!stats_) return;
      auto duration = absl::Now() - start_;
      double seconds = absl::ToDoubleSeconds(duration);
      uint64_t usec = static_cast<uint64_t>(seconds * 1000000);
      stats_->RecordCommand(command_, success_, usec);
    }

    void SetError() { success_ = false; }

   private:
    std::string command_;
    absl::Time start_;
    bool success_ = true;
    ServerStats* stats_;
  };
};

// ==============================================================================
// WorkerCommandContext Method Implementations
// ==============================================================================

inline std::shared_ptr<cluster::ClusterState>
WorkerCommandContext::GetClusterState() const {
  if (worker_) {
    return worker_->GetClusterState();
  }
  return nullptr;
}

inline bool WorkerCommandContext::ClusterAddSlots(
    const std::vector<uint16_t>& slots) {
  ASTRADB_LOG_INFO("ClusterAddSlots: Adding {} slots to this node",
                   slots.size());

  if (!worker_) {
    ASTRADB_LOG_ERROR("ClusterAddSlots: worker_ is null");
    return false;
  }
  auto current_state = worker_->GetClusterState();
  if (!current_state) {
    ASTRADB_LOG_ERROR("ClusterAddSlots: current_state is null");
    return false;
  }
  // Get self node ID from gossip manager
  if (!gossip_manager_) {
    ASTRADB_LOG_ERROR("ClusterAddSlots: gossip_manager_ is null");
    return false;
  }
  auto self = gossip_manager_->GetSelf();
  std::string self_id = cluster::GossipManager::NodeIdToString(self.id);
  ASTRADB_LOG_DEBUG("ClusterAddSlots: self_id={}, slots={}", self_id,
                    slots.size());

  // Create new state with slots assigned
  auto new_state = current_state->WithSlotsAssigned(self_id, slots);

  ASTRADB_LOG_INFO("ClusterAddSlots: After assignment, checking slot owners:");
  for (auto slot : slots) {
    auto owner = new_state->GetSlotOwner(slot);
    if (owner.has_value()) {
      ASTRADB_LOG_DEBUG("  Slot {} owner: {}", static_cast<int>(slot),
                        owner.value());
    } else {
      ASTRADB_LOG_ERROR("  Slot {} has no owner!", static_cast<int>(slot));
    }
  }

  // Serialize slot assignments to compact string
  auto slots_str = cluster::SlotSerializer::Serialize(slots);
  ASTRADB_LOG_INFO("ClusterAddSlots: Serialized {} slots to {} bytes: {}",
                   slots.size(), slots_str.size(), slots_str);

  // Update gossip manager's slot metadata
  if (gossip_manager_) {
    gossip_manager_->IncrementConfigEpoch();
    gossip_manager_->UpdateSlotMetadata(slots_str);
    gossip_manager_->BroadcastConfig();
    ASTRADB_LOG_INFO("ClusterAddSlots: Broadcasted slot metadata to cluster");
  } else {
    ASTRADB_LOG_ERROR(
        "ClusterAddSlots: gossip_manager_ is null, cannot broadcast!");
  }

  // Update all workers' cluster state via callback (set by Server)
  // This avoids circular dependency with WorkerScheduler
  if (cluster_state_update_callback_) {
    ASTRADB_LOG_DEBUG(
        "ClusterAddSlots: Calling cluster_state_update_callback_");
    cluster_state_update_callback_(new_state);
    ASTRADB_LOG_DEBUG(
        "ClusterAddSlots: cluster_state_update_callback_ completed");
  } else {
    ASTRADB_LOG_ERROR(
        "ClusterAddSlots: cluster_state_update_callback_ is null!");
  }

  return true;
}

inline bool WorkerCommandContext::ClusterDelSlots(
    const std::vector<uint16_t>& slots) {
  if (!worker_) {
    return false;
  }
  auto current_state = worker_->GetClusterState();
  if (!current_state) {
    return false;
  }
  // Create new state with slots removed
  auto new_state = current_state->WithSlotsRemoved(slots);

  // Update gossip manager's slot metadata
  if (gossip_manager_) {
    gossip_manager_->IncrementConfigEpoch();

    // Get remaining slots for this node
    auto self = gossip_manager_->GetSelf();
    std::string self_id = cluster::GossipManager::NodeIdToString(self.id);
    std::vector<uint16_t> remaining_slots;

    // Iterate through all slots to find ones still owned by this node
    // Note: This is inefficient for large slot counts, but OK for now
    for (uint16_t slot = 0; slot < 16384; ++slot) {
      auto owner = current_state->GetSlotOwner(slot);
      if (owner.has_value() && owner.value() == self_id) {
        bool is_removed =
            std::find(slots.begin(), slots.end(), slot) != slots.end();
        if (!is_removed) {
          remaining_slots.push_back(slot);
        }
      }
    }

    // Serialize and broadcast remaining slots
    auto slots_str = cluster::SlotSerializer::Serialize(remaining_slots);
    gossip_manager_->UpdateSlotMetadata(slots_str);
    gossip_manager_->BroadcastConfig();
    ASTRADB_LOG_INFO(
        "ClusterDelSlots: Broadcasted slot metadata ({} remaining slots)",
        remaining_slots.size());
  }

  // Update all workers' cluster state via callback (set by Server)
  // This avoids circular dependency with WorkerScheduler
  if (cluster_state_update_callback_) {
    cluster_state_update_callback_(new_state);
  }

  return true;
}

// ==============================================================================
// Batch Request Implementation (Inline)
// ==============================================================================

inline std::vector<std::string> Worker::SendBatchRequest(
    const std::string& cmd_type, const std::vector<std::string>& keys,
    const std::vector<std::string>& args) {
  // Group keys by worker ID
  absl::flat_hash_map<size_t, std::vector<std::string>> worker_keys;
  for (const auto& key : keys) {
    size_t target_worker = RouteToWorker(key);
    worker_keys[target_worker].push_back(key);
  }

  // If all keys are on this worker, process locally
  if (worker_keys.size() == 1 && worker_keys.begin()->first == worker_id_) {
    if (cmd_type == "SINTER") {
      return data_shard_.GetDatabase().SInterLocal(keys);
    }
    return {};
  }

  // For cross-worker requests, send requests and wait for responses
  absl::InlinedVector<std::vector<std::string>, 4> all_results;
  absl::InlinedVector<std::future<std::vector<std::string>>, 4> futures;

  // Send requests to all workers
  for (const auto& [target_worker_id, sub_keys] : worker_keys) {
    if (target_worker_id == worker_id_) {
      // Local processing
      if (cmd_type == "SINTER") {
        all_results.push_back(data_shard_.GetDatabase().SInterLocal(sub_keys));
      }
    } else {
      // Cross-worker request
      uint64_t req_id = next_batch_req_id_++;
      auto req = std::make_shared<BatchCrossWorkerRequest>();
      req->req_id = req_id;
      req->source_worker_id = worker_id_;
      req->cmd_type = cmd_type;
      req->keys = sub_keys;
      req->args = args;
      req->result_promise =
          std::make_shared<std::promise<std::vector<std::string>>>();

      // Store the request in pending map
      {
        std::lock_guard<std::mutex> lock(batch_mutex_);
        pending_batch_reqs_[req_id] = req;
      }

      // Get future before sending request
      futures.push_back(req->result_promise->get_future());

      // Send request to target worker
      ASTRADB_LOG_DEBUG(
          "Worker {}: Enqueueing batch request {} to Worker {} (keys: {})",
          worker_id_, req_id, target_worker_id, sub_keys.size());
      all_workers_[target_worker_id]->EnqueueBatchRequest(*req);
    }
  }

  // Wait for all cross-worker responses with timeout
  // OPTIMIZATION: Use condition variable instead of sleep to avoid 1ms delay
  auto start_time = absl::Now();
  size_t completed = 0;

  // Register pending responses for notification
  pending_batch_responses_.fetch_add(futures.size());

  while (completed < futures.size()) {
    // Process batch responses (this is called from ExecutorLoop, so we need to
    // do it here)
    ProcessBatchResponses();

    // Check if any future is ready (with short timeout to avoid blocking too
    // long)
    bool any_ready = false;
    for (size_t i = 0; i < futures.size(); ++i) {
      if (futures[i].valid()) {
        auto status = futures[i].wait_for(AbslToChronoMicroseconds(
            absl::Microseconds(100)));  // 100us instead of 1ms
        if (status == std::future_status::ready) {
          auto result = futures[i].get();
          all_results.push_back(std::move(result));
          completed++;
          any_ready = true;
        }
      }
    }

    // If no futures are ready, wait for batch response notification
    if (!any_ready && completed < futures.size()) {
      absl::MutexLock lock(&batch_response_mutex_);
      // Wait for notification or timeout (100us)
      batch_response_cv_.WaitWithTimeout(&batch_response_mutex_,
                                         absl::Microseconds(100));
    }

    // Check timeout
    auto elapsed = absl::Now() - start_time;
    if (elapsed > absl::Seconds(5)) {
      ASTRADB_LOG_ERROR("Worker {}: Batch request timeout after 5 seconds",
                        worker_id_);
      // Add empty results for any remaining futures
      for (size_t i = 0; i < futures.size(); ++i) {
        if (futures[i].valid()) {
          all_results.push_back({});
        }
      }
      break;
    }
  }

  // Unregister pending responses
  pending_batch_responses_.fetch_sub(futures.size());

  // Aggregate results based on command type
  absl::InlinedVector<std::string, 8> final_result;
  if (cmd_type == "SINTER") {
    if (!all_results.empty()) {
      // Compute intersection of all results
      final_result = absl::InlinedVector<std::string, 8>(all_results[0].begin(),
                                                         all_results[0].end());
      for (size_t i = 1; i < all_results.size(); ++i) {
        absl::flat_hash_set<std::string> current_set(all_results[i].begin(),
                                                     all_results[i].end());
        absl::InlinedVector<std::string, 8> temp;
        for (const auto& member : final_result) {
          if (current_set.find(member) != current_set.end()) {
            temp.push_back(member);
          }
        }
        final_result = std::move(temp);
        if (final_result.empty()) break;
      }
    }
  }
  // TODO: Add other command types (SUNION, ZUNIONSTORE, ZINTERSTORE)

  // Convert absl::InlinedVector to std::vector for return
  return std::vector<std::string>(final_result.begin(), final_result.end());
}

inline void Worker::ProcessBatchRequest(const BatchCrossWorkerRequest& req) {
  ASTRADB_LOG_DEBUG("Worker {}: Processing batch request {} from Worker {}",
                    worker_id_, req.req_id, req.source_worker_id);

  std::vector<std::string> result;

  // Execute command on this worker's database
  if (req.cmd_type == "SINTER") {
    ASTRADB_LOG_DEBUG("Worker {}: Calling SInterLocal with {} keys", worker_id_,
                      req.keys.size());
    result = data_shard_.GetDatabase().SInterLocal(req.keys);
    ASTRADB_LOG_DEBUG("Worker {}: SInterLocal returned {} results", worker_id_,
                      result.size());
  }
  // Add other command types as needed

  // Send response back
  ASTRADB_LOG_DEBUG("Worker {}: Creating batch response for request {}",
                    worker_id_, req.req_id);
  BatchCrossWorkerResponse resp{req.req_id, std::move(result)};
  if (req.source_worker_id < all_workers_.size()) {
    ASTRADB_LOG_DEBUG("Worker {}: Enqueueing batch response to Worker {}",
                      worker_id_, req.source_worker_id);
    all_workers_[req.source_worker_id]->EnqueueBatchResponse(resp);
    // OPTIMIZATION: Notify source worker that batch response is ready
    all_workers_[req.source_worker_id]->NotifyBatchResponseReady();
    ASTRADB_LOG_DEBUG("Worker {}: Batch response enqueued successfully",
                      worker_id_);
  } else {
    ASTRADB_LOG_ERROR("Worker {}: Invalid source worker ID: {}", worker_id_,
                      req.source_worker_id);
  }
}

inline bool Worker::ProcessBatchResponses() {
  bool has_work = false;
  BatchCrossWorkerResponse resp;
  while (batch_resp_queue_.try_dequeue(resp)) {
    has_work = true;
    ASTRADB_LOG_DEBUG("Worker {}: Received batch response for request {}",
                      worker_id_, resp.req_id);
    std::lock_guard<std::mutex> lock(batch_mutex_);
    auto it = pending_batch_reqs_.find(resp.req_id);
    if (it != pending_batch_reqs_.end()) {
      ASTRADB_LOG_DEBUG(
          "Worker {}: Setting promise for request {} with {} results",
          worker_id_, resp.req_id, resp.result.size());
      // Set the promise value with the received result
      it->second->result_promise->set_value(std::move(resp.result));
      // Remove from pending map
      pending_batch_reqs_.erase(it);
      ASTRADB_LOG_DEBUG("Worker {}: Request {} removed from pending map",
                        worker_id_, resp.req_id);
    } else {
      ASTRADB_LOG_ERROR("Worker {}: Received response for unknown request {}",
                        worker_id_, resp.req_id);
    }
  }
  return has_work;
}

// Enqueue a client info response
inline void Worker::EnqueueClientInfoResponse(const ClientInfoResponse& resp) {
  client_info_resp_queue_.enqueue(resp);
  NotifyExecutorLoop();
}

// Send client info request to all workers (for CLIENT LIST command)
inline uint64_t Worker::SendClientInfoRequest(uint64_t conn_id) {
  uint64_t req_id =
      next_client_info_req_id_.fetch_add(1, std::memory_order_relaxed);

  // Create pending request
  auto pending_req = std::make_shared<PendingClientInfoReq>();
  pending_req->req_id = req_id;
  pending_req->conn_id = conn_id;
  pending_req->responses_received = 0;
  pending_req->expected_responses = all_workers_.size();
  pending_req->start_time = absl::Now();

  {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    pending_client_info_reqs_[req_id] = pending_req;
  }

  // Send request to all workers (including self)
  for (auto& worker : all_workers_) {
    ClientInfoRequest req;
    req.source_worker_id = worker_id_;
    req.req_id = req_id;
    worker->EnqueueClientInfoRequest(req);
  }

  return req_id;
}

// Get connection info for CLIENT LIST command
inline std::vector<std::tuple<uint64_t, std::string, std::string, int>>
Worker::GetConnectionInfo() const {
  std::vector<std::tuple<uint64_t, std::string, std::string, int>> info;
  for (const auto& [id, conn] : connections_) {
    if (conn && conn->IsConnected()) {
      info.emplace_back(id, conn->GetRemoteAddress(), conn->GetClientName(),
                        conn->GetProtocolVersion());
    }
  }
  return info;
}

// Kill a connection by ID (for CLIENT KILL command)
inline bool Worker::KillConnection(uint64_t conn_id) {
  auto it = connections_.find(conn_id);
  if (it != connections_.end() && it->second) {
    it->second->Close();
    connections_.erase(it);
    return true;
  }
  return false;
}

// Process a client info request (for CLIENT LIST command)
inline void Worker::ProcessClientInfoRequest(const ClientInfoRequest& req) {
  // Get connection info from this worker
  auto info = GetConnectionInfo();

  // Build response
  ClientInfoResponse resp;
  resp.req_id = req.req_id;

  // Convert tuple to string format
  for (const auto& [id, addr, name, resp_version] : info) {
    // Parse address to get IP and port
    std::string ip = "unknown";
    uint16_t port = 0;
    size_t colon_pos = addr.find(':');
    if (colon_pos != std::string::npos) {
      ip = addr.substr(0, colon_pos);
      try {
        port = static_cast<uint16_t>(std::stoul(addr.substr(colon_pos + 1)));
      } catch (...) {
        port = 0;
      }
    }

    // Build client info string (Redis format)
    std::ostringstream info_str;
    info_str << "id=" << id << " " << "addr=" << ip << ":" << port << " "
             << "fd=-1 "                            // Socket fd not tracked
             << "name=" << name << " " << "age=0 "  // Age not tracked
             << "idle=0 "                           // Idle time not tracked
             << "db=0 "                             // Current database
             << "sub=0 "                            // Number of subscriptions
             << "psub=0 "                // Number of pattern subscriptions
             << "multi=-1 "              // Multi flag
             << "qbuf=0 "                // Query buffer size
             << "qbuf-free=0 "           // Query buffer free space
             << "obl=0 "                 // Output buffer size
             << "oll=0 "                 // Output list length
             << "omem=0 "                // Output memory
             << "events=r "              // Events
             << "cmd=client "            // Last command
             << "user=default "          // User name
             << "redir=-1 "              // Redirect
             << "resp=" << resp_version  // RESP version
             << "\n";

    resp.client_info_list.push_back(info_str.str());
  }

  // Send response back to source worker
  if (req.source_worker_id < all_workers_.size()) {
    all_workers_[req.source_worker_id]->EnqueueClientInfoResponse(resp);
  }
}

// ==============================================================================
// PubSubManager Implementation (Inline)
// ==============================================================================

inline void PubSubManager::Subscribe(uint64_t conn_id,
                                     const std::vector<std::string>& channels) {
  std::lock_guard<std::mutex> lock(pubsub_mutex_);

  for (const auto& channel : channels) {
    channel_subscribers_[channel].insert(conn_id);
    conn_channels_[conn_id].insert(channel);

    size_t count =
        conn_channels_[conn_id].size() + conn_patterns_[conn_id].size();
    SendSubscribeReply(conn_id, channel, count);
  }
}

inline void PubSubManager::Unsubscribe(
    uint64_t conn_id, const std::vector<std::string>& channels) {
  std::lock_guard<std::mutex> lock(pubsub_mutex_);

  auto it = conn_channels_.find(conn_id);
  if (it == conn_channels_.end()) {
    return;
  }

  std::vector<std::string> to_unsub = channels;
  if (to_unsub.empty()) {
    // Unsubscribe from all
    to_unsub = std::vector<std::string>(it->second.begin(), it->second.end());
  }

  for (const auto& channel : to_unsub) {
    auto& subscribers = channel_subscribers_[channel];
    subscribers.erase(conn_id);
    if (subscribers.empty()) {
      channel_subscribers_.erase(channel);
    }

    it->second.erase(channel);

    size_t count =
        conn_channels_[conn_id].size() + conn_patterns_[conn_id].size();
    SendUnsubscribeReply(conn_id, channel, count);
  }

  if (it->second.empty()) {
    conn_channels_.erase(conn_id);
  }
}

inline void PubSubManager::PSubscribe(
    uint64_t conn_id, const std::vector<std::string>& patterns) {
  std::lock_guard<std::mutex> lock(pubsub_mutex_);

  for (const auto& pattern : patterns) {
    pattern_subscribers_[pattern].insert(conn_id);
    conn_patterns_[conn_id].insert(pattern);

    size_t count =
        conn_channels_[conn_id].size() + conn_patterns_[conn_id].size();
    SendSubscribeReply(conn_id, pattern, count);
  }
}

inline void PubSubManager::PUnsubscribe(
    uint64_t conn_id, const std::vector<std::string>& patterns) {
  std::lock_guard<std::mutex> lock(pubsub_mutex_);

  auto it = conn_patterns_.find(conn_id);
  if (it == conn_patterns_.end()) {
    return;
  }

  std::vector<std::string> to_unsub = patterns;
  if (to_unsub.empty()) {
    to_unsub = std::vector<std::string>(it->second.begin(), it->second.end());
  }

  for (const auto& pattern : to_unsub) {
    auto& subscribers = pattern_subscribers_[pattern];
    subscribers.erase(conn_id);
    if (subscribers.empty()) {
      pattern_subscribers_.erase(pattern);
    }

    it->second.erase(pattern);

    size_t count =
        conn_channels_[conn_id].size() + conn_patterns_[conn_id].size();
    SendUnsubscribeReply(conn_id, pattern, count);
  }

  if (it->second.empty()) {
    conn_patterns_.erase(conn_id);
  }
}

inline size_t PubSubManager::Publish(const std::string& channel,
                                     const std::string& message) {
  size_t subscriber_count = 0;

  // Send to local channel subscribers
  {
    std::lock_guard<std::mutex> lock(pubsub_mutex_);
    auto it = channel_subscribers_.find(channel);
    if (it != channel_subscribers_.end()) {
      // Build message: ["message", channel, message]
      std::string resp_msg;
      resp_msg += "*3\r\n";
      resp_msg += "$7\r\nmessage\r\n";
      resp_msg +=
          "$" + std::to_string(channel.size()) + "\r\n" + channel + "\r\n";
      resp_msg +=
          "$" + std::to_string(message.size()) + "\r\n" + message + "\r\n";

      for (uint64_t conn_id : it->second) {
        // Send via worker's response queue
        ResponseWithConnId resp{conn_id, resp_msg};
        worker_->resp_queue_.enqueue(resp);
        subscriber_count++;
      }
    }
  }

  // Send to local pattern subscribers
  {
    std::lock_guard<std::mutex> lock(pubsub_mutex_);
    for (const auto& [pattern, subscribers] : pattern_subscribers_) {
      if (PatternMatches(pattern, channel)) {
        // Build pattern message: ["pmessage", pattern, channel, message]
        std::string pmsg;
        pmsg += "*4\r\n";
        pmsg += "$8\r\npmessage\r\n";
        pmsg +=
            "$" + std::to_string(pattern.size()) + "\r\n" + pattern + "\r\n";
        pmsg +=
            "$" + std::to_string(channel.size()) + "\r\n" + channel + "\r\n";
        pmsg +=
            "$" + std::to_string(message.size()) + "\r\n" + message + "\r\n";

        for (uint64_t conn_id : subscribers) {
          ResponseWithConnId resp{conn_id, pmsg};
          worker_->resp_queue_.enqueue(resp);
          subscriber_count++;
        }
      }
    }
  }

  // Broadcast to all other workers (NO SHARING architecture)
  PubSubMessage pubsub_msg{channel, message};
  for (auto* other_worker : worker_->all_workers_) {
    if (other_worker->worker_id_ != worker_id_) {
      other_worker->EnqueuePubSubMessage(pubsub_msg);
    }
  }

  return subscriber_count;
}

inline void PubSubManager::ProcessPubSubMessage(const PubSubMessage& msg) {
  // Send to local channel subscribers
  {
    std::lock_guard<std::mutex> lock(pubsub_mutex_);
    auto it = channel_subscribers_.find(msg.channel);
    if (it != channel_subscribers_.end()) {
      // Build message: ["message", channel, message]
      std::string resp_msg;
      resp_msg += "*3\r\n";
      resp_msg += "$7\r\nmessage\r\n";
      resp_msg += "$" + std::to_string(msg.channel.size()) + "\r\n" +
                  msg.channel + "\r\n";
      resp_msg += "$" + std::to_string(msg.message.size()) + "\r\n" +
                  msg.message + "\r\n";

      for (uint64_t conn_id : it->second) {
        ResponseWithConnId resp{conn_id, resp_msg};
        worker_->resp_queue_.enqueue(resp);
      }
    }
  }

  // Send to local pattern subscribers
  {
    std::lock_guard<std::mutex> lock(pubsub_mutex_);
    for (const auto& [pattern, subscribers] : pattern_subscribers_) {
      if (PatternMatches(pattern, msg.channel)) {
        // Build pattern message: ["pmessage", pattern, channel, message]
        std::string pmsg;
        pmsg += "*4\r\n";
        pmsg += "$8\r\npmessage\r\n";
        pmsg +=
            "$" + std::to_string(pattern.size()) + "\r\n" + pattern + "\r\n";
        pmsg += "$" + std::to_string(msg.channel.size()) + "\r\n" +
                msg.channel + "\r\n";
        pmsg += "$" + std::to_string(msg.message.size()) + "\r\n" +
                msg.message + "\r\n";

        for (uint64_t conn_id : subscribers) {
          ResponseWithConnId resp{conn_id, pmsg};
          worker_->resp_queue_.enqueue(resp);
        }
      }
    }
  }
}

inline size_t PubSubManager::GetSubscriptionCount(uint64_t conn_id) const {
  std::lock_guard<std::mutex> lock(pubsub_mutex_);
  size_t count = 0;
  auto it_channels = conn_channels_.find(conn_id);
  if (it_channels != conn_channels_.end()) {
    count += it_channels->second.size();
  }
  auto it_patterns = conn_patterns_.find(conn_id);
  if (it_patterns != conn_patterns_.end()) {
    count += it_patterns->second.size();
  }
  return count;
}

inline bool PubSubManager::IsSubscribed(uint64_t conn_id) const {
  std::lock_guard<std::mutex> lock(pubsub_mutex_);
  return conn_channels_.find(conn_id) != conn_channels_.end() ||
         conn_patterns_.find(conn_id) != conn_patterns_.end();
}

inline std::vector<std::string> PubSubManager::GetActiveChannels(
    const std::string& pattern) const {
  std::lock_guard<std::mutex> lock(pubsub_mutex_);
  std::vector<std::string> channels;
  for (const auto& [channel, _] : channel_subscribers_) {
    if (pattern.empty() || PatternMatches(pattern, channel)) {
      channels.push_back(channel);
    }
  }
  return channels;
}

inline size_t PubSubManager::GetChannelSubscriberCount(
    const std::string& channel) const {
  std::lock_guard<std::mutex> lock(pubsub_mutex_);
  auto it = channel_subscribers_.find(channel);
  if (it != channel_subscribers_.end()) {
    return it->second.size();
  }
  return 0;
}

inline size_t PubSubManager::GetPatternSubscriptionCount() const {
  std::lock_guard<std::mutex> lock(pubsub_mutex_);
  return pattern_subscribers_.size();
}

inline bool PubSubManager::PatternMatches(const std::string& pattern,
                                          const std::string& channel) const {
  // Simple glob pattern matching (supports * and ?)
  if (pattern == "*") {
    return true;
  } else if (pattern.find('*') == std::string::npos &&
             pattern.find('?') == std::string::npos) {
    // No wildcards, exact match
    return channel == pattern;
  } else {
    // Simple prefix/suffix matching for common patterns like "news:*"
    size_t star_pos = pattern.find('*');
    if (star_pos != std::string::npos &&
        pattern.find('*', star_pos + 1) == std::string::npos) {
      // Single * wildcard
      std::string prefix = pattern.substr(0, star_pos);
      std::string suffix = pattern.substr(star_pos + 1);
      if (star_pos == 0) {
        // Pattern like "*suffix"
        return channel.size() >= suffix.size() &&
               channel.substr(channel.size() - suffix.size()) == suffix;
      } else if (star_pos == pattern.size() - 1) {
        // Pattern like "prefix*"
        return channel.substr(0, prefix.size()) == prefix;
      } else {
        // Pattern like "prefix*suffix"
        return channel.size() >= prefix.size() + suffix.size() &&
               channel.substr(0, prefix.size()) == prefix &&
               channel.substr(channel.size() - suffix.size()) == suffix;
      }
    } else {
      // Fallback to simple contains check
      return channel.find(pattern.substr(0, pattern.find_first_of("*?"))) !=
             std::string::npos;
    }
  }
}

inline void PubSubManager::SendSubscribeReply(uint64_t conn_id,
                                              const std::string& channel,
                                              size_t count) {
  // Build subscribe reply: ["subscribe", channel, count]
  std::string reply;
  reply += "*3\r\n";
  reply += "$9\r\nsubscribe\r\n";
  reply += "$" + std::to_string(channel.size()) + "\r\n" + channel + "\r\n";
  reply += ":" + std::to_string(count) + "\r\n";

  ResponseWithConnId resp{conn_id, reply};
  worker_->resp_queue_.enqueue(resp);
}

inline void PubSubManager::SendUnsubscribeReply(uint64_t conn_id,
                                                const std::string& channel,
                                                size_t count) {
  // Build unsubscribe reply: ["unsubscribe", channel, count]
  std::string reply;
  reply += "*3\r\n";
  reply += "$11\r\nunsubscribe\r\n";
  reply += "$" + std::to_string(channel.size()) + "\r\n" + channel + "\r\n";
  reply += ":" + std::to_string(count) + "\r\n";

  ResponseWithConnId resp{conn_id, reply};
  worker_->resp_queue_.enqueue(resp);
}

}  // namespace astra::server
