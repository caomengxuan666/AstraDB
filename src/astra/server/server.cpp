// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include <absl/synchronization/mutex.h>
#include <absl/functional/any_invocable.h>
#include "server.hpp"
#include <absl/synchronization/mutex.h>
#include <absl/functional/any_invocable.h>
#include "astra/commands/command_auto_register.hpp"
#include <absl/synchronization/mutex.h>
#include <absl/functional/any_invocable.h>
#include "astra/base/logging.hpp"
#include <absl/synchronization/mutex.h>
#include <absl/functional/any_invocable.h>
#include "astra/core/async/thread_pool.hpp"
#include <absl/synchronization/mutex.h>
#include <absl/functional/any_invocable.h>
#include "astra/protocol/resp/resp_builder.hpp"
#include <absl/synchronization/mutex.h>
#include <absl/functional/any_invocable.h>
#include "astra/persistence/rdb_writer.hpp"
#include <absl/strings/match.h>
#include <absl/strings/string_view.h>
#include <absl/time/time.h>
#include <filesystem>
#include <chrono>

namespace astra::server {

using astra::commands::RoutingStrategy;
using astra::commands::RuntimeCommandRegistry;
using astra::protocol::RespValue;
using astra::protocol::RespType;
using astra::persistence::RdbWriter;

Server::Server(const ServerConfig& config)
    : config_(config),
      local_shard_manager_(config.num_shards, config.num_databases),
      running_(false),
      cleaner_running_(false),
      total_commands_(0) {
  
  // Initialize buffer pool for network I/O
  buffer_pool_ = std::make_unique<astra::core::memory::BufferPool>(
      astra::core::memory::Buffer::kXLargeBufferSize);  // 1MB max buffer size
  
  // Initialize executor for coroutines
  executor_ = std::make_unique<astra::core::async::Executor>(
      config_.thread_count > 0 ? config_.thread_count : std::thread::hardware_concurrency());
  
  // Initialize connection pool with buffer pool
  connection_pool_ = std::make_unique<network::ConnectionPool>(
      io_context_, config.max_connections, buffer_pool_.get());
  
  // Set global function for direct posting to main IO context
  astra::core::async::g_post_to_main_io_context_func = [this](absl::AnyInvocable<void()> work) {
    asio::post(io_context_, std::move(work));
  };
  
  // Auto-register all commands (commands are registered via static initializers)
  RuntimeCommandRegistry::Instance().ApplyToRegistry(registry_);
  
  // Set global registry so that command handlers can access it
  commands::SetGlobalCommandRegistry(&registry_);
  
  // Initialize persistence if enabled
  if (config_.persistence.enabled) {
    if (!InitPersistence()) {
      ASTRADB_LOG_WARN("Persistence initialization failed, running without persistence");
    }
  }
  
  // Initialize cluster if enabled
  if (config_.cluster.enabled) {
    if (!InitCluster()) {
      ASTRADB_LOG_WARN("Cluster initialization failed, running in standalone mode");
    }
  }
  
  // Initialize ACL if enabled
  if (config_.acl.enabled) {
    acl_manager_ = std::make_unique<security::AclManager>();
    // Add default user
    if (!config_.acl.default_user.empty()) {
      acl_manager_->CreateUser(config_.acl.default_user, config_.acl.default_password, 
                               static_cast<uint32_t>(security::AclPermission::kAdmin), true);
      ASTRADB_LOG_INFO("ACL initialized with default user: {}", config_.acl.default_user);
    }
  }
  
  // Initialize metrics
  astra::metrics::MetricsConfig metrics_config;
  metrics_config.enabled = config_.metrics.enabled;
  metrics_config.bind_addr = config_.metrics.bind_addr;
  metrics_config.port = config_.metrics.port;
  astra::metrics::AstraMetrics::Instance().Init(metrics_config);

  // Initialize Pub/Sub manager
  pubsub_manager_ = std::make_unique<ServerPubSubManager>(this);

  // Initialize blocking manager for blocking commands
  blocking_manager_ = std::make_unique<commands::BlockingManager>(io_context_);

  ASTRADB_LOG_INFO("Server configured: host={}, port={}, max_connections={}, databases={}, shards={}, commands={}"
                   "{}, {}{}",
                   config_.host, config_.port,
                   config_.max_connections, config_.num_databases, config_.num_shards,
                   RuntimeCommandRegistry::Instance().GetCommandCount(),
                   config_.persistence.enabled ? ", persistence=enabled" : "",
                   config_.cluster.enabled ? ", cluster=enabled" : "",
                   config_.metrics.enabled ? ", metrics=enabled" : "");
}

Server::~Server() {
  Stop();
  
  // Cleanup cluster
  if (gossip_manager_) {
    gossip_manager_->Stop();
    gossip_manager_.reset();
  }
  cluster_shard_manager_.reset();
  
  // Cleanup persistence
  if (persistence_) {
    persistence_->Close();
    persistence_.reset();
  }
  
  astra::core::async::g_post_to_main_io_context_func = nullptr;
}

void Server::Run() {
  running_ = true;
  
  // Start executor for coroutines
  if (executor_) {
    executor_->Run();
  }
  
  // Global thread pool is already started in GetGlobalThreadPool()
  ASTRADB_LOG_INFO("Using global IO context thread pool with {} threads",
                  astra::core::async::GetGlobalThreadPool().Size());
  
  // Start HTTP server for metrics
  if (config_.metrics.enabled) {
    astra::metrics::MetricsConfig metrics_config;
    metrics_config.enabled = config_.metrics.enabled;
    metrics_config.bind_addr = config_.metrics.bind_addr;
    metrics_config.port = config_.metrics.port;
    astra::metrics::MetricsRegistry::Instance().StartHTTPServer(io_context_, metrics_config);
  }
  
  // Create acceptor
  asio::ip::tcp::endpoint endpoint(asio::ip::make_address(config_.host), config_.port);
  acceptor_ = std::make_unique<asio::ip::tcp::acceptor>(io_context_, endpoint);
  
  acceptor_->listen(asio::socket_base::max_listen_connections);
  
  // Create work guard to keep main IO context running (for accept loop only)
  work_guard_ = std::make_unique<asio::executor_work_guard<asio::io_context::executor_type>>(
      io_context_.get_executor());
  
  // Start gossip tick thread if cluster is enabled
  if (config_.cluster.enabled && gossip_manager_) {
    StartGossipTick();
  }
  
  // Start accepting connections
  DoAccept();
  
  // Run accept loop on main thread (or a dedicated thread)
  ASTRADB_LOG_INFO("Server listening on {}:{}", config_.host, config_.port);
  ASTRADB_LOG_INFO("Accept loop running on main thread");
  io_context_.run();
  
  ASTRADB_LOG_INFO("Server stopped");
}

void Server::Start() {
  running_ = true;
  
  // Start executor for coroutines
  if (executor_) {
    executor_->Run();
  }
  
  // Create acceptor
  asio::ip::tcp::endpoint endpoint(asio::ip::make_address(config_.host), config_.port);
  acceptor_ = std::make_unique<asio::ip::tcp::acceptor>(io_context_, endpoint);
  
  acceptor_->listen(asio::socket_base::max_listen_connections);
  
  ASTRADB_LOG_INFO("Server listening on {}:{}", config_.host, config_.port);
  
  // Start gossip tick thread if cluster is enabled
  if (config_.cluster.enabled && gossip_manager_) {
    StartGossipTick();
  }
  
  // Start accepting connections
  DoAccept();
  
  // Start expiration cleaner thread
  StartExpirationCleaner();
  
  // Start AOF rewrite checker if AOF is enabled
  if (config_.persistence.aof_enabled && aof_writer_) {
    StartAofRewriteChecker();
  }
  
  // Start RDB saver if persistence is enabled
  if (config_.persistence.enabled && rdb_writer_) {
    StartRdbSaver();
  }
}

void Server::Stop() {
  if (!running_) {
    return;
  }
  
  ASTRADB_LOG_INFO("Stopping server...");
  running_ = false;
  
  // Stop executor for coroutines
  if (executor_) {
    executor_->Stop();
  }
  
  // Stop accepting new connections
  if (acceptor_) {
    asio::error_code ec;
    acceptor_->close(ec);
  }
  
  // Destroy work guard to allow io_context to exit
  work_guard_.reset();
  
  // Stop the IO context
  io_context_.stop();
  
  // Stop gossip tick thread
  gossip_running_ = false;
  if (gossip_tick_thread_.joinable()) {
    gossip_tick_thread_.join();
  }
  
  // Stop expiration cleaner thread
  cleaner_running_ = false;
  if (expiration_cleaner_thread_.joinable()) {
    expiration_cleaner_thread_.join();
  }
  
  // Stop AOF rewrite thread
  aof_rewrite_running_ = false;
  if (aof_rewrite_thread_.joinable()) {
    aof_rewrite_thread_.join();
  }
  
  // Stop RDB save thread
  rdb_save_running_ = false;
  if (rdb_save_thread_.joinable()) {
    rdb_save_thread_.join();
  }
  
  // Wait for all threads to finish
  for (auto& thread : io_threads_) {
    if (thread.joinable()) {
      thread.join();
    }
  }
  
  ASTRADB_LOG_INFO("Server stopped, total commands processed: {}", total_commands_.load());
}

void Server::DoAccept() {
  acceptor_->async_accept(
    [this](asio::error_code ec, asio::ip::tcp::socket socket) {
      OnAccept(ec, std::move(socket));
    });
}

void Server::OnAccept(asio::error_code ec, asio::ip::tcp::socket socket) {
  if (ec) {
    if (running_) {
      ASTRADB_LOG_ERROR("Accept error: {}", ec.message());
    }
    return;
  }

  // Acquire connection from pool (reuse if available)
  auto conn = connection_pool_->Acquire(std::move(socket));
  if (!conn) {
    ASTRADB_LOG_WARN("Connection rejected: max connections reached");
    socket.close();
    DoAccept();
    return;
  }

  ASTRADB_LOG_INFO("New client connected: id={}, addr={}",
                   conn->GetId(), conn->GetRemoteAddress());
  
  // Update metrics
  astra::metrics::AstraMetrics::Instance().IncrementConnections();

  // Set batch command callback for Pipeline optimization
  // This processes multiple commands in a single coroutine, reducing overhead
  conn->SetBatchCommandCallback([this, conn](absl::InlinedVector<protocol::Command, 16>&& commands) {
    if (config_.use_async_commands) {
      // Use coroutine-based async handler with Server's io_context
      // All commands in the batch are processed in a single coroutine
      asio::co_spawn(
          io_context_,
          HandleBatchCommandsAsync(std::move(commands), conn),
          [](std::exception_ptr e) {
            if (e) {
              try {
                std::rethrow_exception(e);
              } catch (const std::exception& ex) {
                ASTRADB_LOG_ERROR("Batch coroutine error: {}", ex.what());
              }
            }
          });
    } else {
      // Fallback to synchronous handler (not recommended for production)
      for (const auto& cmd : commands) {
        HandleCommand(cmd, conn);
      }
    }
  });

  // Register for Pub/Sub
  RegisterPubSubConnection(conn->GetId(), conn);

  // Start the connection
  conn->Start();
  
  // Accept next connection
  if (running_) {
    DoAccept();
  }
}

class ServerCommandContext : public commands::CommandContext {
 public:
  ServerCommandContext(commands::Database* db, int db_index = 0,
                       Server* server = nullptr,
                       network::Connection* connection = nullptr)
      : db_(db), db_manager_(nullptr), db_index_(db_index), authenticated_(true), server_(server),
        connection_(connection) {
    // Get database manager from server if available
    if (server_) {
      // For now, get from shard 0 as default (will be improved with per-shard db managers)
      auto* shard = server_->GetLocalShardManager().GetShardByIndex(0);
      if (shard) {
        db_manager_ = shard->GetDatabaseManager();
      }
    }
    // Set AOF callback if AOF is enabled
    if (server_ && server_->IsAofEnabled()) {
      SetAofCallback([this](absl::string_view command, absl::Span<const absl::string_view> args) {
        if (server_ && server_->IsAofEnabled()) {
          // Format as RESP command
          std::string resp_cmd;
          resp_cmd += absl::StrCat("*", args.size() + 1, "\r\n");
          resp_cmd += absl::StrCat("$", command.size(), "\r\n");
          resp_cmd.append(command.data(), command.size());
          resp_cmd += "\r\n";
          for (const auto& arg : args) {
            resp_cmd += absl::StrCat("$", arg.size(), "\r\n");
            resp_cmd.append(arg.data(), arg.size());
            resp_cmd += "\r\n";
          }
          server_->GetAofWriter()->Append(resp_cmd);
        }
      });
    }
  }

  commands::Database* GetDatabase() const override {
    if (db_manager_ && db_manager_->GetDatabase(db_index_)) {
      return db_manager_->GetDatabase(db_index_);
    }
    return db_;  // Fallback to old single database
  }
  int GetDBIndex() const override { return db_index_; }
  bool IsAuthenticated() const override { return authenticated_; }

  void SetDBIndex(int index) override { db_index_ = index; }
  void SetAuthenticated(bool auth) override { authenticated_ = auth; }

  commands::DatabaseManager* GetDatabaseManager() const override { return db_manager_; }

  // Cluster operations
  bool IsClusterEnabled() const override {
    return server_ ? server_->IsClusterEnabled() : false;
  }

  bool ClusterMeet(const std::string& ip, int port) override {
    return server_ ? server_->ClusterMeet(ip, port) : false;
  }

  cluster::GossipManager* GetGossipManager() const override {
    return server_ ? server_->GetGossipManager() : nullptr;
  }

  cluster::GossipManager* GetGossipManagerMutable() override {
    return server_ ? server_->GetGossipManagerMutable() : nullptr;
  }

  cluster::ShardManager* GetClusterShardManager() const override {
    return server_ ? server_->GetClusterShardManager() : nullptr;
  }

  // ACL operations
  security::AclManager* GetAclManager() const override {
    return server_ ? server_->GetAclManager() : nullptr;
  }

  // Persistence operations
  bool IsPersistenceEnabled() const override {
    return server_ ? server_->IsPersistenceEnabled() : false;
  }

  persistence::LevelDBAdapter* GetPersistence() const override {
    return server_ ? server_->GetPersistence() : nullptr;
  }

  // ============== Transaction Support ==============
  bool IsInTransaction() const override {
    return connection_ ? connection_->IsInTransaction() : false;
  }

  void BeginTransaction() override {
    if (connection_) connection_->BeginTransaction();
  }

  void QueueCommand(const protocol::Command& cmd) override {
    if (connection_) connection_->QueueCommand(cmd);
  }

  absl::InlinedVector<protocol::Command, 16> GetQueuedCommands() const override {
    return connection_ ? connection_->GetQueuedCommands() : absl::InlinedVector<protocol::Command, 16>{};
  }

  void ClearQueuedCommands() override {
    if (connection_) connection_->ClearQueuedCommands();
  }

  void DiscardTransaction() override {
    if (connection_) connection_->DiscardTransaction();
  }

  void WatchKey(const std::string& key, uint64_t version) override {
    if (connection_) connection_->WatchKey(key, version);
  }

  const absl::flat_hash_set<std::string>& GetWatchedKeys() const override {
    static absl::flat_hash_set<std::string> empty;
    return connection_ ? connection_->GetWatchedKeys() : empty;
  }

  bool IsWatchedKeyModified(const absl::AnyInvocable<uint64_t(const std::string&) const>& get_version) const override {
    return connection_ ? connection_->IsWatchedKeyModified(get_version) : false;
  }

  void ClearWatchedKeys() override {
    if (connection_) connection_->ClearWatchedKeys();
  }

  // ============== Pub/Sub Support ==============
  commands::PubSubManager* GetPubSubManager() override {
    return server_ ? server_->GetPubSubManager() : nullptr;
  }
  
  replication::ReplicationManager* GetReplicationManager() override {
    return server_ ? server_->GetReplicationManager() : nullptr;
  }

  uint64_t GetConnectionId() const override {
    return connection_ ? connection_->GetId() : 0;
  }

  // Get connection pointer
  network::Connection* GetConnection() const override {
    return connection_;
  }

  // Get server pointer
  void* GetServer() const override {
    return static_cast<void*>(server_);
  }

  // Get blocking manager
  commands::BlockingManager* GetBlockingManager() override {
    return server_ ? server_->GetBlockingManager() : nullptr;
  }

 private:
  commands::Database* db_;
  commands::DatabaseManager* db_manager_;
  int db_index_;
  bool authenticated_;
  Server* server_;
  network::Connection* connection_;
};

void Server::HandleCommand(const protocol::Command& cmd,
                          std::shared_ptr<network::Connection> conn) {
  total_commands_++;

  ASTRADB_LOG_DEBUG("Processing command: id={}, cmd={}, args={}",
                    conn->GetId(), cmd.name, cmd.ArgCount());

  // Start metrics timer (using absl::Time)
  auto start_time = absl::Now();

  // ============== Transaction Handling ==============
  // Check if connection is in transaction mode
  bool in_transaction = conn->IsInTransaction();

  // Commands that are allowed inside MULTI
  static const absl::flat_hash_set<std::string> kTransactionCommands = {
    "MULTI", "EXEC", "DISCARD", "WATCH", "UNWATCH"
  };

  // If in transaction and command is not a transaction control command, queue it
  if (in_transaction && kTransactionCommands.find(cmd.name) == kTransactionCommands.end()) {
    conn->QueueCommand(cmd);

    // Send QUEUED response
    RespValue queued_resp;
    queued_resp.SetString("QUEUED", RespType::kSimpleString);
    SendResponse(conn, commands::CommandResult(queued_resp));
    return;
  }

  // Handle EXEC specially - execute all queued commands
  if (cmd.name == "EXEC") {
    if (!in_transaction) {
      auto error_result = commands::CommandResult(false, "ERR EXEC without MULTI");
      SendResponse(conn, error_result);
      return;
    }

    // Check if any watched keys were modified
    auto* db_for_watch = local_shard_manager_.GetShardByIndex(0);
    if (db_for_watch && conn->IsWatchedKeyModified([db_for_watch](const std::string& key) {
      return db_for_watch->GetDatabase()->GetKeyVersion(key);
    })) {
      conn->DiscardTransaction();
      RespValue null_resp(RespType::kNull);
      auto result = commands::CommandResult(null_resp);
      SendResponse(conn, result);
      return;
    }

    // Get all queued commands
    auto queued_commands = conn->GetQueuedCommands();
    std::vector<RespValue> results;
    results.reserve(queued_commands.size());

    // Execute each queued command
    for (const auto& queued_cmd : queued_commands) {
      // Get shard for the command
      auto cmd_routing = registry_.GetRoutingStrategy(queued_cmd.name);
      Shard* cmd_shard = nullptr;

      if (cmd_routing == RoutingStrategy::kByFirstKey && queued_cmd.ArgCount() > 0) {
        const auto& key_arg = queued_cmd[0];
        if (key_arg.IsBulkString()) {
          cmd_shard = local_shard_manager_.GetShard(key_arg.AsString());
        }
      }
      if (!cmd_shard) {
        cmd_shard = local_shard_manager_.GetShardByIndex(0);
      }

      if (cmd_shard) {
        ServerCommandContext ctx(cmd_shard->GetDatabase(), 0, this, conn.get());
        auto cmd_result = registry_.Execute(queued_cmd, &ctx);
        results.push_back(cmd_result.response);
      } else {
        RespValue err;
        err.SetString("ERR shard not found", RespType::kSimpleString);
        results.push_back(err);
      }
    }

    // Clear transaction state
    conn->ClearQueuedCommands();
    conn->ClearWatchedKeys();
    conn->DiscardTransaction();

    // Return array of results
    RespValue response;
    response.SetArray(std::move(results));
    SendResponse(conn, commands::CommandResult(response));
    return;
  }

  // Get routing strategy for this command
  auto routing = registry_.GetRoutingStrategy(cmd.name);
  
  Shard* shard = nullptr;
  bool need_redirect = false;
  bool ask_redirect = false;
  std::string redirect_addr;
  uint16_t redirect_slot = 0;
  
  // Route based on strategy
  if (routing == RoutingStrategy::kByFirstKey && cmd.ArgCount() > 0) {
    // Route based on first argument (key)
    const auto& key_arg = cmd[0];
    if (key_arg.IsBulkString()) {
      const auto& key = key_arg.AsString();
      
      // In cluster mode, check if key belongs to this node
      if (config_.cluster.enabled && cluster_shard_manager_) {
        auto slot = cluster::HashSlotCalculator::Calculate(key);
        redirect_slot = slot;
        auto shard_id = cluster_shard_manager_->GetShardForSlot(slot);
        
        // Check migration state
        bool is_migrating = cluster_shard_manager_->IsMigrating(shard_id);
        bool is_importing = cluster_shard_manager_->IsImporting(shard_id);
        
        // Check if this slot is served by this node
        auto primary_node = cluster_shard_manager_->GetPrimaryNode(shard_id);
        auto self = gossip_manager_ ? gossip_manager_->GetSelf() : cluster::AstraNodeView{};
        
        if (primary_node != self.id && !is_importing) {
          // Slot is served by another node - need MOVED redirect
          need_redirect = true;
          // Find the node that owns this slot
          if (gossip_manager_) {
            auto owner = gossip_manager_->FindNode(primary_node);
            if (owner) {
              redirect_addr = absl::StrCat(owner->ip, ":", owner->port);
            }
          }
        } else if (is_migrating) {
          // Slot is being migrated from this node
          // Check if key exists locally
          auto* local_shard = local_shard_manager_.GetShardByIndex(shard_id % local_shard_manager_.GetShardCount());
          bool key_exists = false;
          if (local_shard && local_shard->GetDatabase()) {
            key_exists = local_shard->GetDatabase()->Exists(key);
          }
          
          if (!key_exists) {
            // Key already migrated - ASK redirect to target
            ask_redirect = true;
            auto target = cluster_shard_manager_->GetMigrationTarget(shard_id);
            if (gossip_manager_) {
              auto target_node = gossip_manager_->FindNode(target);
              if (target_node) {
                redirect_addr = absl::StrCat(target_node->ip, ":", target_node->port);
              }
            }
          } else {
            // Key still here - serve it
            shard = local_shard;
          }
        } else {
          // Slot is served by this node (stable or importing)
          shard = local_shard_manager_.GetShardByIndex(shard_id % local_shard_manager_.GetShardCount());
        }
      } else {
        // Standalone mode - use local shard manager
        shard = local_shard_manager_.GetShard(key);
      }
    }
  }
  
  // Handle MOVED redirect
  if (need_redirect) {
    std::string moved_error = absl::StrCat("MOVED ", redirect_slot, " ") + redirect_addr;
    auto error_result = commands::CommandResult(false, moved_error);
    SendResponse(conn, error_result);
    return;
  }
  
  // Handle ASK redirect
  if (ask_redirect) {
    std::string ask_error = absl::StrCat("ASK ", redirect_slot, " ") + redirect_addr;
    auto error_result = commands::CommandResult(false, ask_error);
    SendResponse(conn, error_result);
    return;
  }
  
  // Default to shard 0 for commands without routing strategy
  if (!shard) {
    shard = local_shard_manager_.GetShardByIndex(0);
  }
  
  if (!shard) {
    auto error_result = commands::CommandResult(false, "ERR shard not found");
    SendResponse(conn, error_result);
    return;
  }
  
  // Post command execution to the shard's IO context
  shard->Post([this, cmd, conn, shard, start_time]() {
    // Create command context with connection for transaction support
    ServerCommandContext context(shard->GetDatabase(), 0, this, conn.get());

    // Execute command
    auto result = registry_.Execute(cmd, &context);
    
    // Record metrics (using absl::Duration)
    auto duration = absl::Now() - start_time;
    double seconds = absl::ToDoubleSeconds(duration);
    astra::metrics::AstraMetrics::Instance().RecordCommand(cmd.name, result.success, seconds);
    
    // Propagate write commands to slaves
    if (replication_manager_ && result.success) {
      auto* info = registry_.GetInfo(cmd.name);
      if (info && info->is_write) {
        replication_manager_->PropagateCommand(cmd);
      }
    }
    
    // Persist if enabled and command was successful
    if (config_.persistence.enabled && persistence_ && result.success && aof_writer_) {
      AppendToAof(cmd);
    }
    
    // Send response back on connection's thread
    asio::post(io_context_, [this, conn, result]() {
      // Don't send response if command is in blocking state
      // Blocking commands will send their response later when woken up
      if (!result.IsBlocking()) {
        SendResponse(conn, result);
      }
    });
  });
}

// Coroutine-based command handler (async/await)
asio::awaitable<void> Server::HandleCommandAsync(const protocol::Command& cmd,
                                                std::shared_ptr<network::Connection> conn) {
  total_commands_++;

  ASTRADB_LOG_DEBUG("Processing command async: id={}, cmd={}, args={}",
                    conn->GetId(), cmd.name, cmd.ArgCount());

  // Start metrics timer (using absl::Time)
  auto start_time = absl::Now();

  // No explicit yield needed - coroutine scheduling handles it
  ASTRADB_LOG_DEBUG("Executing command body: id={}, cmd={}", conn->GetId(), cmd.name);

  // Helper to send response synchronously
  auto send_response = [this, conn](const commands::CommandResult& result) {
    SendResponse(conn, result);
  };

  // ============== Transaction Handling ==============
  // Check if connection is in transaction mode
  bool in_transaction = conn->IsInTransaction();

  // Commands that are allowed inside MULTI
  static const absl::flat_hash_set<std::string> kTransactionCommands = {
    "MULTI", "EXEC", "DISCARD", "WATCH", "UNWATCH"
  };

  // If in transaction and command is not a transaction control command, queue it
  if (in_transaction && kTransactionCommands.find(cmd.name) == kTransactionCommands.end()) {
    conn->QueueCommand(cmd);

    // Send QUEUED response
    RespValue queued_resp;
    queued_resp.SetString("QUEUED", RespType::kSimpleString);
    
    auto result = commands::CommandResult(queued_resp);
    
    // Record metrics
    auto duration = absl::Now() - start_time;
    double seconds = absl::ToDoubleSeconds(duration);
    astra::metrics::AstraMetrics::Instance().RecordCommand(cmd.name, result.success, seconds);
    
    // Send response
    send_response(result);
    co_return;
  }

  // Handle EXEC specially - execute all queued commands
  if (cmd.name == "EXEC") {
    if (!in_transaction) {
      auto error_result = commands::CommandResult(false, "ERR EXEC without MULTI");
      
      // Record metrics
          auto duration = absl::Now() - start_time;
          double seconds = absl::ToDoubleSeconds(duration);
          astra::metrics::AstraMetrics::Instance().RecordCommand(cmd.name, error_result.success, seconds);
          
          // Send response
          send_response(error_result);
          co_return;    }

    // Check if any watched keys were modified
    auto* db_for_watch = local_shard_manager_.GetShardByIndex(0);
    if (db_for_watch && conn->IsWatchedKeyModified([db_for_watch](const std::string& key) {
      return db_for_watch->GetDatabase()->GetKeyVersion(key);
    })) {
      conn->DiscardTransaction();
      RespValue null_resp(RespType::kNull);
      auto result = commands::CommandResult(null_resp);
      
      // Record metrics
      auto duration = absl::Now() - start_time;
      double seconds = absl::ToDoubleSeconds(duration);
      astra::metrics::AstraMetrics::Instance().RecordCommand(cmd.name, result.success, seconds);
      
      // Send response
      send_response(result);
      co_return;
    }

    // Get all queued commands
    auto queued_commands = conn->GetQueuedCommands();
    std::vector<RespValue> results;
    results.reserve(queued_commands.size());

    // Execute each queued command
    for (const auto& queued_cmd : queued_commands) {
      // Get shard for the command
      auto cmd_routing = registry_.GetRoutingStrategy(queued_cmd.name);
      Shard* cmd_shard = nullptr;

      if (cmd_routing == RoutingStrategy::kByFirstKey && queued_cmd.ArgCount() > 0) {
        const auto& key_arg = queued_cmd[0];
        if (key_arg.IsBulkString()) {
          cmd_shard = local_shard_manager_.GetShard(key_arg.AsString());
        }
      }
      if (!cmd_shard) {
        cmd_shard = local_shard_manager_.GetShardByIndex(0);
      }

      if (cmd_shard) {
        ServerCommandContext ctx(cmd_shard->GetDatabase(), 0, this, conn.get());
        auto cmd_result = registry_.Execute(queued_cmd, &ctx);
        results.push_back(cmd_result.response);
      } else {
        RespValue err;
        err.SetString("ERR shard not found", RespType::kSimpleString);
        results.push_back(err);
      }
    }

    // Clear transaction state
    conn->ClearQueuedCommands();
    conn->ClearWatchedKeys();
    conn->DiscardTransaction();

    // Return array of results
    RespValue response;
    response.SetArray(std::move(results));
    
    // Record metrics
    auto duration = absl::Now() - start_time;
    double seconds = absl::ToDoubleSeconds(duration);
    astra::metrics::AstraMetrics::Instance().RecordCommand(cmd.name, response.IsArray(), seconds);
    
    // Send response
    send_response(commands::CommandResult(response));
    co_return;
  }

  // Get routing strategy for this command
  auto routing = registry_.GetRoutingStrategy(cmd.name);
  
  Shard* shard = nullptr;
  bool need_redirect = false;
  bool ask_redirect = false;
  std::string redirect_addr;
  uint16_t redirect_slot = 0;
  
  // Route based on strategy
  if (routing == RoutingStrategy::kByFirstKey && cmd.ArgCount() > 0) {
    // Route based on first argument (key)
    const auto& key_arg = cmd[0];
    if (key_arg.IsBulkString()) {
      const auto& key = key_arg.AsString();
      
      // In cluster mode, check if key belongs to this node
      if (config_.cluster.enabled && cluster_shard_manager_) {
        auto slot = cluster::HashSlotCalculator::Calculate(key);
        redirect_slot = slot;
        auto shard_id = cluster_shard_manager_->GetShardForSlot(slot);
        
        // Check migration state
        bool is_migrating = cluster_shard_manager_->IsMigrating(shard_id);
        bool is_importing = cluster_shard_manager_->IsImporting(shard_id);
        
        // Check if this slot is served by this node
        auto primary_node = cluster_shard_manager_->GetPrimaryNode(shard_id);
        auto self = gossip_manager_ ? gossip_manager_->GetSelf() : cluster::AstraNodeView{};
        
        if (primary_node != self.id && !is_importing) {
          // Slot is served by another node - need MOVED redirect
          need_redirect = true;
          // Find the node that owns this slot
          if (gossip_manager_) {
            auto owner = gossip_manager_->FindNode(primary_node);
            if (owner) {
              redirect_addr = absl::StrCat(owner->ip, ":", owner->port);
            }
          }
        } else if (is_migrating) {
          // Slot is being migrated from this node
          // Check if key exists locally
          auto* local_shard = local_shard_manager_.GetShardByIndex(shard_id % local_shard_manager_.GetShardCount());
          bool key_exists = false;
          if (local_shard && local_shard->GetDatabase()) {
            key_exists = local_shard->GetDatabase()->Exists(key);
          }
          
          if (!key_exists) {
            // Key already migrated - ASK redirect to target
            ask_redirect = true;
            auto target = cluster_shard_manager_->GetMigrationTarget(shard_id);
            if (gossip_manager_) {
              auto target_node = gossip_manager_->FindNode(target);
              if (target_node) {
                redirect_addr = absl::StrCat(target_node->ip, ":", target_node->port);
              }
            }
          } else {
            // Key still here - serve it
            shard = local_shard;
          }
        } else {
          // Slot is served by this node (stable or importing)
          shard = local_shard_manager_.GetShardByIndex(shard_id % local_shard_manager_.GetShardCount());
        }
      } else {
        // Standalone mode - use local shard manager
        shard = local_shard_manager_.GetShard(key);
      }
    }
  }
  
  // Handle MOVED redirect
  if (need_redirect) {
    std::string moved_error = absl::StrCat("MOVED ", redirect_slot, " ") + redirect_addr;
    auto error_result = commands::CommandResult(false, moved_error);
    
    // Record metrics
    auto duration = absl::Now() - start_time;
    double seconds = absl::ToDoubleSeconds(duration);
    astra::metrics::AstraMetrics::Instance().RecordCommand(cmd.name, error_result.success, seconds);
    
    // Send response
    // Using send_response helper
    co_return;
  }
  
  // Handle ASK redirect
  if (ask_redirect) {
    std::string ask_error = absl::StrCat("ASK ", redirect_slot, " ") + redirect_addr;
    auto error_result = commands::CommandResult(false, ask_error);
    
    // Record metrics
    auto duration = absl::Now() - start_time;
    double seconds = absl::ToDoubleSeconds(duration);
    astra::metrics::AstraMetrics::Instance().RecordCommand(cmd.name, error_result.success, seconds);
    
    // Send response
    // Using send_response helper
    co_return;
  }
  
  // Default to shard 0 for commands without routing strategy
  if (!shard) {
    shard = local_shard_manager_.GetShardByIndex(0);
  }
  
  if (!shard) {
    auto error_result = commands::CommandResult(false, "ERR shard not found");
    
    // Record metrics
    auto duration = absl::Now() - start_time;
    double seconds = absl::ToDoubleSeconds(duration);
    astra::metrics::AstraMetrics::Instance().RecordCommand(cmd.name, error_result.success, seconds);
    
    // Send response
    // Using send_response helper
    co_return;
  }

  // Execute command
  commands::CommandResult result;
  if (shard) {
    ServerCommandContext ctx(shard->GetDatabase(), 0, this, conn.get());
    result = registry_.Execute(cmd, &ctx);
  } else {
    result = commands::CommandResult(false, "ERR shard not found");
  }

  // Record metrics
  auto duration = absl::Now() - start_time;
  double seconds = absl::ToDoubleSeconds(duration);
  astra::metrics::AstraMetrics::Instance().RecordCommand(cmd.name, result.success, seconds);

  // Propagate write commands to slaves
  if (replication_manager_ && result.success) {
    auto* info = registry_.GetInfo(cmd.name);
    if (info && info->is_write) {
      replication_manager_->PropagateCommand(cmd);
    }
  }
  
  // Persist if enabled and command was successful
  if (config_.persistence.enabled && persistence_ && result.success && aof_writer_) {
    AppendToAof(cmd);
  }

  // Send response
  // Don't send response if command is in blocking state
  // Blocking commands will send their response later when woken up
  if (!result.IsBlocking()) {
    send_response(result);
  }
}

void Server::SendResponse(std::shared_ptr<network::Connection> conn,
                         const commands::CommandResult& result) {
  // Convert CommandResult to RESP string
  std::string response;
  
  if (!result.success) {
    // Error response
    response = "-" + result.error + "\r\n";
    ASTRADB_LOG_DEBUG("SendResponse: id={}, error='{}', response='{}'", 
                      conn->GetId(), result.error, response);
  } else {
    // Success response - convert RespValue to string
    if (result.response.IsSimpleString()) {
      response = "+" + result.response.AsString() + "\r\n";
      ASTRADB_LOG_DEBUG("SendResponse: id={}, type=simple_string, value='{}', response='{}'", 
                        conn->GetId(), result.response.AsString(), response);
    } else if (result.response.IsBulkString()) {
      const auto& str = result.response.AsString();
      response = absl::StrCat("$", str.length(), "\r\n", str, "\r\n");
      ASTRADB_LOG_DEBUG("SendResponse: id={}, type=bulk_string, len={}, response='{}'", 
                        conn->GetId(), str.length(), response);
    } else if (result.response.IsInteger()) {
      response = absl::StrCat(":", result.response.AsInteger(), "\r\n");
      ASTRADB_LOG_DEBUG("SendResponse: id={}, type=integer, value={}, response='{}'", 
                        conn->GetId(), result.response.AsInteger(), response);
    } else if (result.response.GetType() == protocol::RespType::kDouble) {
      response = absl::StrCat(",", result.response.AsDouble(), "\r\n");
      ASTRADB_LOG_DEBUG("SendResponse: id={}, type=double, value={}, response='{}'", 
                        conn->GetId(), result.response.AsDouble(), response);
    } else if (result.response.IsArray()) {
      // Use RespBuilder for proper array serialization (including nested arrays)
      response = protocol::RespBuilder::Build(result.response);
      ASTRADB_LOG_DEBUG("SendResponse: id={}, type=array, array_size={}, response_len={}, response='{}'", 
                        conn->GetId(), result.response.AsArray().size(), response.length(), 
                        response.length() > 100 ? response.substr(0, 100) + "..." : response);
    } else if (result.response.IsNull()) {
      response = "$-1\r\n";
      ASTRADB_LOG_DEBUG("SendResponse: id={}, type=null, response='{}'", conn->GetId(), response);
    } else if (result.response.GetType() == protocol::RespType::kMap) {
      // Use RespBuilder for map serialization
      response = protocol::RespBuilder::Build(result.response);
      ASTRADB_LOG_DEBUG("SendResponse: id={}, type=map, map_size={}, response_len={}, response='{}'",
                        conn->GetId(), result.response.AsMap().size(), response.length(),
                        response.length() > 100 ? response.substr(0, 100) + "..." : response);
    } else {
      // For other types, just return OK
      auto type = result.response.GetType();
      ASTRADB_LOG_DEBUG("SendResponse: id={}, type={}, raw_type_int={}, response='{}'",
                        conn->GetId(), "unknown", static_cast<int>(type), response);
      response = "+OK\r\n";
      ASTRADB_LOG_DEBUG("SendResponse: id={}, type=unknown, response='{}'", conn->GetId(), response);
    }
  }
  
  // Send to client
  ASTRADB_LOG_DEBUG("Sending response: id={}, len={}", conn->GetId(), response.length());
  conn->Send(response);
}

void Server::StartExpirationCleaner() {
  cleaner_running_ = true;
  expiration_cleaner_thread_ = std::thread([this]() {
    ASTRADB_LOG_INFO("Expiration cleaner thread started");
    
    while (cleaner_running_) {
      // Clean up expired keys
      CleanupExpiredKeys();
      
      // Sleep for 1 second (1000 milliseconds)
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
    
    ASTRADB_LOG_INFO("Expiration cleaner thread stopped");
  });
}

void Server::CleanupExpiredKeys() {
  // Iterate through all shards and clean up expired keys
  for (size_t shard_index = 0; shard_index < config_.num_shards; ++shard_index) {
    auto* shard = local_shard_manager_.GetShardByIndex(shard_index);
    if (shard) {
      auto* db = shard->GetDatabase();
      if (db) {
        auto expired_keys = db->GetExpiredKeys();
        for (const auto& key : expired_keys) {
          ASTRADB_LOG_DEBUG("Cleaning up expired key: {}", key);
          db->Del(key);
        }
      }
    }
  }
}

bool Server::InitPersistence() noexcept {
  try {
    persistence_ = std::make_unique<persistence::LevelDBAdapter>();
    
    persistence::LevelDBOptions options;
    options.db_path = config_.persistence.data_dir;
    options.write_buffer_size = config_.persistence.write_buffer_size;
    options.cache_size = config_.persistence.cache_size;
    // Note: sync_writes is handled via WriteOptions per-operation, not in LevelDBOptions
    
    if (!persistence_->Open(options)) {
      ASTRADB_LOG_ERROR("Failed to open LevelDB at: {}", options.db_path);
      persistence_.reset();
      return false;
    }
    
    ASTRADB_LOG_INFO("Persistence initialized: path={}, cache_size={}MB",
                     options.db_path, options.cache_size / (1024 * 1024));
    
    // Initialize AOF if enabled
    if (config_.persistence.aof_enabled) {
      aof_writer_ = std::make_unique<persistence::AofWriter>();
      
      persistence::AofOptions aof_options;
      aof_options.aof_path = config_.persistence.aof_path;
      aof_options.sync_policy = config_.persistence.aof_sync_everysec 
          ? persistence::AofSyncPolicy::kEverySec 
          : persistence::AofSyncPolicy::kAlways;
      
      if (!aof_writer_->Init(aof_options)) {
        ASTRADB_LOG_ERROR("Failed to initialize AOF writer at: {}", aof_options.aof_path);
        aof_writer_.reset();
      } else {
        ASTRADB_LOG_INFO("AOF initialized: path={}, sync={}",
                         aof_options.aof_path,
                         config_.persistence.aof_sync_everysec ? "everysec" : "always");
      }
      
      // Initialize RDB writer
      rdb_writer_ = std::make_unique<persistence::RdbWriter>();
      persistence::RdbOptions rdb_options;
      rdb_options.save_path = "./data/dump.rdb";
      
      if (!rdb_writer_->Init(rdb_options)) {
        ASTRADB_LOG_ERROR("Failed to initialize RDB writer");
        rdb_writer_.reset();
      } else {
        ASTRADB_LOG_INFO("RDB writer initialized");
      }
      
      // Initialize replication manager
      replication_manager_ = std::make_unique<replication::ReplicationManager>();
      replication::ReplicationConfig repl_config;
      repl_config.role = replication::ReplicationRole::kMaster;  // Default to master
      
      if (!replication_manager_->Init(repl_config)) {
        ASTRADB_LOG_ERROR("Failed to initialize replication manager");
        replication_manager_.reset();
      } else {
        ASTRADB_LOG_INFO("Replication manager initialized");
      }
    }
    
    return true;
  } catch (const std::exception& e) {
    ASTRADB_LOG_ERROR("Persistence initialization exception: {}", e.what());
    persistence_.reset();
    aof_writer_.reset();
    return false;
  }
}

bool Server::InitCluster() noexcept {
  try {
    // Initialize cluster shard manager
    cluster_shard_manager_ = std::make_unique<cluster::ShardManager>();
    
    // Generate or use provided node ID
    cluster::NodeId node_id{};
    if (!config_.cluster.node_id.empty()) {
      // Parse hex string to NodeId
      cluster::GossipManager::ParseNodeId(config_.cluster.node_id, node_id);
    } else {
      // Generate random node ID
      cluster::GossipManager::GenerateNodeId(node_id);
    }
    
    if (!cluster_shard_manager_->Init(config_.cluster.shard_count, node_id)) {
      ASTRADB_LOG_ERROR("Failed to initialize cluster shard manager");
      cluster_shard_manager_.reset();
      return false;
    }
    
    // Initialize gossip manager
    gossip_manager_ = std::make_unique<cluster::GossipManager>();
    
    cluster::ClusterConfig gossip_config;
    gossip_config.node_id = cluster::GossipManager::NodeIdToString(node_id);
    gossip_config.bind_ip = config_.cluster.bind_addr;
    gossip_config.gossip_port = config_.cluster.gossip_port;
    gossip_config.shard_count = config_.cluster.shard_count;
    
    if (!gossip_manager_->Init(gossip_config)) {
      ASTRADB_LOG_ERROR("Failed to initialize gossip manager");
      gossip_manager_.reset();
      cluster_shard_manager_.reset();
      return false;
    }
    
    // Start the gossip service
    if (!gossip_manager_->Start()) {
      ASTRADB_LOG_ERROR("Failed to start gossip manager");
      gossip_manager_.reset();
      cluster_shard_manager_.reset();
      return false;
    }
    
    // Add seed nodes if provided
    for (const auto& seed : config_.cluster.seeds) {
      // Parse seed format: "ip:port"
      size_t colon_pos = seed.find(':');
      if (colon_pos != std::string::npos) {
        std::string ip = seed.substr(0, colon_pos);
        int port;
        if (!absl::SimpleAtoi(seed.substr(colon_pos + 1), &port)) {
          continue;
        }
        gossip_manager_->MeetNode(ip, port);
      }
    }
    
    ASTRADB_LOG_INFO("Cluster initialized: node_id={}, shards={}, gossip_port={}",
                     gossip_config.node_id.substr(0, 8),
                     config_.cluster.shard_count,
                     config_.cluster.gossip_port);
    return true;
  } catch (const std::exception& e) {
    ASTRADB_LOG_ERROR("Cluster initialization exception: {}", e.what());
    cluster_shard_manager_.reset();
    gossip_manager_.reset();
    return false;
  }
}

void Server::StartGossipTick() {
  if (!config_.cluster.enabled || !gossip_manager_) {
    return;
  }
  
  gossip_running_ = true;
  gossip_tick_thread_ = std::thread([this]() {
    ASTRADB_LOG_INFO("Gossip tick thread started");
    
    while (gossip_running_) {
      // Drive gossip protocol tick
      gossip_manager_->Tick();
      
      // Sleep for 100ms (typical gossip interval)
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    ASTRADB_LOG_INFO("Gossip tick thread stopped");
  });
}

void Server::GossipTickLoop() {
  // This is now handled by StartGossipTick
}

void Server::StartAofRewriteChecker() {
  if (!aof_writer_ || !config_.persistence.aof_enabled) {
    return;
  }

  aof_rewrite_running_.store(true);
  aof_rewrite_thread_ = std::thread([this]() {
    ASTRADB_LOG_INFO("AOF rewrite checker thread started");

    while (aof_rewrite_running_.load(std::memory_order_acquire)) {
      // Check every 60 seconds
      for (int i = 0; i < 60 && aof_rewrite_running_.load(std::memory_order_acquire); ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }

      if (!aof_rewrite_running_.load(std::memory_order_acquire)) {
        break;
      }

      // Check if rewrite is needed
      if (aof_writer_->ShouldRewrite()) {
        ASTRADB_LOG_INFO("AOF rewrite threshold reached, starting rewrite...");
        PerformAofRewrite();
      }
    }

    ASTRADB_LOG_INFO("AOF rewrite checker thread stopped");
  });
}

void Server::AofRewriteCheckerLoop() {
  // This is now handled by StartAofRewriteChecker
}

bool Server::PerformAofRewrite() {
  if (!aof_writer_) {
    return false;
  }

  ASTRADB_LOG_INFO("Starting AOF rewrite...");

  // Callback to serialize all database data
  auto write_callback = [this](std::string& output) {
    // Iterate through all shards and databases
    for (size_t db_idx = 0; db_idx < config_.num_databases; ++db_idx) {
      auto* shard = local_shard_manager_.GetShardByIndex(0);  // For simplicity, use shard 0
      if (!shard) continue;

      auto* db = shard->GetDatabase();
      if (!db) continue;

      // TODO: Serialize all keys and values to RESP commands
      // This requires iterating through all data structures (String, Hash, Set, ZSet, List, Stream)
      // For now, this is a placeholder
      
      ASTRADB_LOG_INFO("Rewriting database {}", db_idx);
    }
  };

  bool success = aof_writer_->Rewrite(write_callback);
  
  if (success) {
    ASTRADB_LOG_INFO("AOF rewrite completed successfully");
  } else {
    ASTRADB_LOG_ERROR("AOF rewrite failed");
  }

  return success;
}

void Server::StartRdbSaver() {
  if (!rdb_writer_ || !config_.persistence.enabled) {
    return;
  }

  rdb_save_running_.store(true);
  rdb_save_thread_ = std::thread([this]() {
    ASTRADB_LOG_INFO("RDB saver thread started");

    while (rdb_save_running_.load(std::memory_order_acquire)) {
      // Save every 300 seconds (5 minutes)
      for (int i = 0; i < 300 && rdb_save_running_.load(std::memory_order_acquire); ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }

      if (!rdb_save_running_.load(std::memory_order_acquire)) {
        break;
      }

      ASTRADB_LOG_INFO("Starting scheduled RDB save...");
      PerformRdbSave();
    }

    ASTRADB_LOG_INFO("RDB saver thread stopped");
  });
}

void Server::AppendToAof(const protocol::Command& cmd) {
  if (!aof_writer_) {
    return;
  }

  const auto& args = cmd.args;
  if (args.empty()) {
    return;
  }

  const auto& command_name = args[0].AsString();

  // Get command info to check if it's a write command
  auto cmd_info = registry_.GetInfo(command_name);
  if (!cmd_info) {
    return;
  }

  // Only persist write commands
  // Check if flags contains "read" or "readonly"
  bool is_readonly = std::find(cmd_info->flags.begin(), cmd_info->flags.end(), "read") != cmd_info->flags.end() ||
                     std::find(cmd_info->flags.begin(), cmd_info->flags.end(), "readonly") != cmd_info->flags.end();
  if (is_readonly) {
    return;
  }

  // Format command as RESP and append to AOF
  std::string resp_cmd;
  resp_cmd += absl::StrCat("*", args.size(), "\r\n");

  for (const auto& arg : args) {
    const auto& str = arg.AsString();
    resp_cmd += absl::StrCat("$", str.length(), "\r\n", str, "\r\n");
  }

  aof_writer_->Append(resp_cmd);
}

void Server::RdbSaverLoop() {
  // This is now handled by StartRdbSaver
}

bool Server::PerformRdbSave() {
  if (!rdb_writer_) {
    return false;
  }

  ASTRADB_LOG_INFO("Starting RDB save...");

  // Callback to serialize database to RDB format
  auto save_callback = [this](RdbWriter& writer) {
    for (size_t db_idx = 0; db_idx < config_.num_databases; ++db_idx) {
      auto* shard = local_shard_manager_.GetShardByIndex(0);
      if (!shard) continue;

      // Get the specific database from DatabaseManager
      auto* db = shard->GetDatabase(static_cast<int>(db_idx));
      if (!db) continue;

      writer.SelectDb(static_cast<int>(db_idx));
      
      // Get actual key count (TODO: implement GetKeyCount() in Database class)
      uint64_t db_size = 0;  // Placeholder
      uint64_t expires_size = 0;  // Placeholder
      writer.ResizeDb(db_size, expires_size);

      // TODO: Serialize all keys and their values
      // For each key, determine its type and write accordingly
      // This requires iterating through all data structures in Database class
      ASTRADB_LOG_INFO("Saving database {} to RDB", db_idx);
    }
  };

  bool success = rdb_writer_->Save(save_callback);
  
  if (success) {
    ASTRADB_LOG_INFO("RDB save completed successfully");
  } else {
    ASTRADB_LOG_ERROR("RDB save failed");
  }

  return success;
}

bool Server::ClusterMeet(const std::string& ip, int port) {
  if (!config_.cluster.enabled || !gossip_manager_) {
    ASTRADB_LOG_WARN("Cluster not enabled, cannot meet node");
    return false;
  }

  ASTRADB_LOG_INFO("CLUSTER MEET: {}:{}", ip, port);
  return gossip_manager_->MeetNode(ip, port);
}

// ============== Pub/Sub Implementation ==============

void Server::RegisterPubSubConnection(uint64_t conn_id, std::shared_ptr<network::Connection> conn) {
  absl::MutexLock lock(&pubsub_mutex_);
  connections_[conn_id] = conn;
}

void Server::UnregisterPubSubConnection(uint64_t conn_id) {
  absl::MutexLock lock(&pubsub_mutex_);

  // Unsubscribe from all channels
  auto it = conn_channels_.find(conn_id);
  if (it != conn_channels_.end()) {
    for (const auto& channel : it->second) {
      auto& subscribers = channel_subscribers_[channel];
      subscribers.erase(conn_id);
      if (subscribers.empty()) {
        channel_subscribers_.erase(channel);
      }
    }
    conn_channels_.erase(conn_id);
  }

  // Unsubscribe from all patterns
  auto pit = conn_patterns_.find(conn_id);
  if (pit != conn_patterns_.end()) {
    for (const auto& pattern : pit->second) {
      auto& subscribers = pattern_subscribers_[pattern];
      subscribers.erase(conn_id);
      if (subscribers.empty()) {
        pattern_subscribers_.erase(pattern);
      }
    }
    conn_patterns_.erase(conn_id);
  }

  connections_.erase(conn_id);
}

// ============== ServerPubSubManager Implementation ==============

void Server::ServerPubSubManager::Subscribe(uint64_t conn_id, const std::vector<std::string>& channels) {
  absl::MutexLock lock(&server_->pubsub_mutex_);

  for (const auto& channel : channels) {
    server_->channel_subscribers_[channel].insert(conn_id);
    server_->conn_channels_[conn_id].insert(channel);

    size_t count = server_->conn_channels_[conn_id].size() + server_->conn_patterns_[conn_id].size();
    SendSubscribeReply(conn_id, channel, count);
  }
}

void Server::ServerPubSubManager::Unsubscribe(uint64_t conn_id, const std::vector<std::string>& channels) {
  absl::MutexLock lock(&server_->pubsub_mutex_);

  auto it = server_->conn_channels_.find(conn_id);
  if (it == server_->conn_channels_.end()) {
    return;
  }

  std::vector<std::string> to_unsub = channels;
  if (to_unsub.empty()) {
    // Unsubscribe from all
    to_unsub = std::vector<std::string>(it->second.begin(), it->second.end());
  }

  for (const auto& channel : to_unsub) {
    auto& subscribers = server_->channel_subscribers_[channel];
    subscribers.erase(conn_id);
    if (subscribers.empty()) {
      server_->channel_subscribers_.erase(channel);
    }

    it->second.erase(channel);

    size_t count = server_->conn_channels_[conn_id].size() + server_->conn_patterns_[conn_id].size();
    SendUnsubscribeReply(conn_id, channel, count);
  }

  if (it->second.empty()) {
    server_->conn_channels_.erase(conn_id);
  }
}

void Server::ServerPubSubManager::PSubscribe(uint64_t conn_id, const std::vector<std::string>& patterns) {
  absl::MutexLock lock(&server_->pubsub_mutex_);

  for (const auto& pattern : patterns) {
    server_->pattern_subscribers_[pattern].insert(conn_id);
    server_->conn_patterns_[conn_id].insert(pattern);

    size_t count = server_->conn_channels_[conn_id].size() + server_->conn_patterns_[conn_id].size();
    SendSubscribeReply(conn_id, pattern, count);
  }
}

void Server::ServerPubSubManager::PUnsubscribe(uint64_t conn_id, const std::vector<std::string>& patterns) {
  absl::MutexLock lock(&server_->pubsub_mutex_);

  auto it = server_->conn_patterns_.find(conn_id);
  if (it == server_->conn_patterns_.end()) {
    return;
  }

  std::vector<std::string> to_unsub = patterns;
  if (to_unsub.empty()) {
    to_unsub = std::vector<std::string>(it->second.begin(), it->second.end());
  }

  for (const auto& pattern : to_unsub) {
    auto& subscribers = server_->pattern_subscribers_[pattern];
    subscribers.erase(conn_id);
    if (subscribers.empty()) {
      server_->pattern_subscribers_.erase(pattern);
    }

    it->second.erase(pattern);

    size_t count = server_->conn_channels_[conn_id].size() + server_->conn_patterns_[conn_id].size();
    SendUnsubscribeReply(conn_id, pattern, count);
  }

  if (it->second.empty()) {
    server_->conn_patterns_.erase(conn_id);
  }
}

size_t Server::ServerPubSubManager::Publish(const std::string& channel, const std::string& message) {
  absl::MutexLock lock(&server_->pubsub_mutex_);

  size_t subscriber_count = 0;

  // Build message: ["message", channel, message]
  std::string resp_msg;
  resp_msg += "*3\r\n";
  resp_msg += "$7\r\nmessage\r\n";
  resp_msg += absl::StrCat("$", channel.size(), "\r\n", channel, "\r\n");
  resp_msg += absl::StrCat("$", message.size(), "\r\n", message, "\r\n");

  // Send to channel subscribers
  auto it = server_->channel_subscribers_.find(channel);
  if (it != server_->channel_subscribers_.end()) {
    for (uint64_t conn_id : it->second) {
      auto conn_it = server_->connections_.find(conn_id);
      if (conn_it != server_->connections_.end()) {
        auto conn = conn_it->second.lock();
        if (conn && conn->IsConnected()) {
          conn->Send(resp_msg);
          subscriber_count++;
        }
      }
    }
  }

  // Send to pattern subscribers (pattern matching)
  for (const auto& [pattern, subscribers] : server_->pattern_subscribers_) {
    // Simple glob pattern matching (supports * and ?)
    bool matches = false;
    if (pattern == "*") {
      matches = true;
    } else if (pattern.find('*') == std::string::npos && pattern.find('?') == std::string::npos) {
      // No wildcards, exact match
      matches = (channel == pattern);
    } else {
      // Simple prefix/suffix matching for common patterns like "news:*"
      size_t star_pos = pattern.find('*');
      if (star_pos != std::string::npos && pattern.find('*', star_pos + 1) == std::string::npos) {
        // Single * wildcard
        std::string prefix = pattern.substr(0, star_pos);
        std::string suffix = pattern.substr(star_pos + 1);
        matches = channel.substr(0, prefix.size()) == prefix &&
                  channel.substr(channel.size() - suffix.size()) == suffix;
      } else {
        // Fallback to simple contains check
        matches = (channel.find(pattern.substr(0, pattern.find_first_of("*?"))) != std::string::npos);
      }
    }

    if (matches) {
      // Build pattern message: ["pmessage", pattern, channel, message]
      std::string pmsg;
      pmsg += "*4\r\n";
      pmsg += "$8\r\npmessage\r\n";
      pmsg += absl::StrCat("$", pattern.size(), "\r\n", pattern, "\r\n");
      pmsg += absl::StrCat("$", channel.size(), "\r\n", channel, "\r\n");
      pmsg += absl::StrCat("$", message.size(), "\r\n", message, "\r\n");

      for (uint64_t conn_id : subscribers) {
        auto conn_it = server_->connections_.find(conn_id);
        if (conn_it != server_->connections_.end()) {
          auto conn = conn_it->second.lock();
          if (conn && conn->IsConnected()) {
            conn->Send(pmsg);
            subscriber_count++;
          }
        }
      }
    }
  }

  return subscriber_count;
}

size_t Server::ServerPubSubManager::GetSubscriptionCount(uint64_t conn_id) const {
  absl::ReaderMutexLock lock(&server_->pubsub_mutex_);

  size_t count = 0;
  auto it = server_->conn_channels_.find(conn_id);
  if (it != server_->conn_channels_.end()) {
    count += it->second.size();
  }
  auto pit = server_->conn_patterns_.find(conn_id);
  if (pit != server_->conn_patterns_.end()) {
    count += pit->second.size();
  }
  return count;
}

bool Server::ServerPubSubManager::IsSubscribed(uint64_t conn_id) const {
  absl::ReaderMutexLock lock(&server_->pubsub_mutex_);
  return server_->conn_channels_.contains(conn_id) || server_->conn_patterns_.contains(conn_id);
}

std::vector<std::string> Server::ServerPubSubManager::GetActiveChannels(const std::string& pattern) const {
  std::vector<std::string> channels;
  
  absl::ReaderMutexLock lock(&server_->pubsub_mutex_);
  
  if (pattern.empty()) {
    // Return all active channels
    for (const auto& [channel, _] : server_->channel_subscribers_) {
      channels.push_back(channel);
    }
  } else {
    // Return channels matching the pattern
    for (const auto& [channel, _] : server_->channel_subscribers_) {
      if (absl::StrContains(channel, pattern)) {
        channels.push_back(channel);
      }
    }
  }
  
  return channels;
}

size_t Server::ServerPubSubManager::GetChannelSubscriberCount(const std::string& channel) const {
  absl::ReaderMutexLock lock(&server_->pubsub_mutex_);
  auto it = server_->channel_subscribers_.find(channel);
  if (it != server_->channel_subscribers_.end()) {
    return it->second.size();
  }
  return 0;
}

size_t Server::ServerPubSubManager::GetPatternSubscriptionCount() const {
  absl::ReaderMutexLock lock(&server_->pubsub_mutex_);
  size_t total = 0;
  for (const auto& [pattern, subscribers] : server_->pattern_subscribers_) {
    total += subscribers.size();
  }
  return total;
}

void Server::ServerPubSubManager::SendSubscribeReply(uint64_t conn_id, const std::string& channel, size_t count) {
  auto conn_it = server_->connections_.find(conn_id);
  if (conn_it != server_->connections_.end()) {
    auto conn = conn_it->second.lock();
    if (conn && conn->IsConnected()) {
      // Send: ["subscribe", channel, count]
      std::string reply;
      reply += "*3\r\n";
      reply += "$9\r\nsubscribe\r\n";
      reply += absl::StrCat("$", channel.size(), "\r\n", channel, "\r\n");
      reply += absl::StrCat(":", count, "\r\n");
      conn->Send(reply);
    }
  }
}

void Server::ServerPubSubManager::SendUnsubscribeReply(uint64_t conn_id, const std::string& channel, size_t count) {
  auto conn_it = server_->connections_.find(conn_id);
  if (conn_it != server_->connections_.end()) {
    auto conn = conn_it->second.lock();
    if (conn && conn->IsConnected()) {
      // Send: ["unsubscribe", channel, count]
      std::string reply;
      reply += "*3\r\n";
      reply += "$11\r\nunsubscribe\r\n";
      reply += absl::StrCat("$", channel.size(), "\r\n", channel, "\r\n");
      reply += absl::StrCat(":", count, "\r\n");
      conn->Send(reply);
    }
  }
}

// ============== Batch Command Processing for Pipeline Optimization ==============

asio::awaitable<void> Server::HandleBatchCommandsAsync(
    absl::InlinedVector<protocol::Command, 16>&& commands,
    std::shared_ptr<network::Connection> conn) {
  if (!conn || !conn->IsConnected()) {
    co_return;
  }

  // Process all commands in a single coroutine
  // This reduces overhead compared to creating one coroutine per command
  absl::InlinedVector<commands::CommandResult, 16> results;
  results.reserve(commands.size());

  ASTRADB_LOG_DEBUG("HandleBatchCommandsAsync: processing {} commands", commands.size());

  for (const auto& cmd : commands) {
    total_commands_++;

    auto start_time = absl::Now();

    // Get routing strategy for this command
    auto routing = registry_.GetRoutingStrategy(cmd.name);

    Shard* shard = nullptr;
    if (routing == RoutingStrategy::kByFirstKey && cmd.ArgCount() > 0) {
      const auto& key_arg = cmd[0];
      if (key_arg.IsBulkString()) {
        shard = local_shard_manager_.GetShard(key_arg.AsString());
      }
    }

    // Default to shard 0 for commands without routing strategy
    if (!shard) {
      shard = local_shard_manager_.GetShardByIndex(0);
    }

    // Execute command
    commands::CommandResult result;
    if (shard) {
      ServerCommandContext ctx(shard->GetDatabase(), 0, this, conn.get());
      result = registry_.Execute(cmd, &ctx);
    } else {
      result = commands::CommandResult(false, "ERR shard not found");
    }

    // Record metrics
    auto duration = absl::Now() - start_time;
    double seconds = absl::ToDoubleSeconds(duration);
    astra::metrics::AstraMetrics::Instance().RecordCommand(cmd.name, result.success, seconds);

    results.push_back(std::move(result));
  }

  // Send all responses in a single write operation
  co_await SendBatchResponses(conn, std::move(results));
}

asio::awaitable<void> Server::SendBatchResponses(
    std::shared_ptr<network::Connection> conn,
    absl::InlinedVector<commands::CommandResult, 16>&& results) {
  if (!conn || !conn->IsConnected()) {
    co_return;
  }

  // Build a single buffer with all responses
  // This reduces the number of system calls from N to 1
  std::string combined_response;
  combined_response.reserve(results.size() * 256);  // Estimate average response size

  for (const auto& result : results) {
    // Serialize response using the same logic as SendResponse
    if (!result.success) {
      combined_response += "-" + result.error + "\r\n";
    } else if (result.response.IsSimpleString()) {
      combined_response += "+" + result.response.AsString() + "\r\n";
    } else if (result.response.IsBulkString()) {
      const auto& str = result.response.AsString();
      combined_response += absl::StrCat("$", str.length(), "\r\n", str, "\r\n");
    } else if (result.response.IsInteger()) {
      combined_response += absl::StrCat(":", result.response.AsInteger(), "\r\n");
    } else if (result.response.GetType() == protocol::RespType::kDouble) {
      combined_response += absl::StrCat(",", result.response.AsDouble(), "\r\n");
    } else if (result.response.IsArray()) {
      combined_response += protocol::RespBuilder::Build(result.response);
    } else if (result.response.IsNull()) {
      combined_response += "$-1\r\n";
    } else if (result.response.GetType() == protocol::RespType::kMap) {
      combined_response += protocol::RespBuilder::Build(result.response);
    } else {
      combined_response += "+OK\r\n";
    }
  }

  // Send all responses in one operation
  conn->Send(combined_response);

  ASTRADB_LOG_DEBUG("SendBatchResponses: sent {} responses in one write", results.size());
  co_return;
}

}  // namespace astra::server
