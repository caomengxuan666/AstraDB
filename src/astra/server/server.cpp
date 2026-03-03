// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include "server.hpp"
#include "astra/commands/command_auto_register.hpp"
#include "astra/base/logging.hpp"
#include "astra/core/async/thread_pool.hpp"

namespace astra::server {

using astra::commands::RoutingStrategy;
using astra::commands::RuntimeCommandRegistry;

Server::Server(const ServerConfig& config)
    : config_(config),
      local_shard_manager_(config.num_shards),
      connection_pool_(io_context_, config.max_connections),
      running_(false),
      cleaner_running_(false),
      total_commands_(0) {
  
  // Set global function for direct posting to main IO context
  astra::core::async::g_post_to_main_io_context_func = [this](std::function<void()> work) {
    asio::post(io_context_, std::move(work));
  };
  
  // Auto-register all commands (commands are registered via static initializers)
  RuntimeCommandRegistry::Instance().ApplyToRegistry(registry_);
  
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
  
  ASTRADB_LOG_INFO("Server configured: host={}, port={}, max_connections={}, databases={}, shards={}, commands={}"
                   "{}, {}",
                   config_.host, config_.port,
                   config_.max_connections, config_.num_databases, config_.num_shards,
                   RuntimeCommandRegistry::Instance().GetCommandCount(),
                   config_.persistence.enabled ? ", persistence=enabled" : "",
                   config_.cluster.enabled ? ", cluster=enabled" : "");
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
  
  // Global thread pool is already started in GetGlobalThreadPool()
  ASTRADB_LOG_INFO("Using global IO context thread pool with {} threads",
                  astra::core::async::GetGlobalThreadPool().Size());
  
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
}

void Server::Stop() {
  if (!running_) {
    return;
  }
  
  ASTRADB_LOG_INFO("Stopping server...");
  running_ = false;
  
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
  
  // Create new connection
  auto conn = connection_pool_.Create(std::move(socket));
  if (!conn) {
    ASTRADB_LOG_WARN("Connection rejected: max connections reached");
    socket.close();
    DoAccept();
    return;
  }
  
  ASTRADB_LOG_INFO("New client connected: id={}, addr={}", 
                   conn->GetId(), conn->GetRemoteAddress());
  
  // Set command callback
  conn->SetCommandCallback([this, conn](const protocol::Command& cmd) {
    HandleCommand(cmd, conn);
  });
  
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
                       Server* server = nullptr)
      : db_(db), db_index_(db_index), authenticated_(true), server_(server) {
    // Set AOF callback if AOF is enabled
    if (server_ && server_->IsAofEnabled()) {
      SetAofCallback([this](const std::string& command, const std::vector<std::string>& args) {
        if (server_ && server_->IsAofEnabled()) {
          // Format as RESP command
          std::string resp_cmd;
          resp_cmd += "*" + std::to_string(args.size() + 1) + "\r\n";
          resp_cmd += "$" + std::to_string(command.size()) + "\r\n" + command + "\r\n";
          for (const auto& arg : args) {
            resp_cmd += "$" + std::to_string(arg.size()) + "\r\n" + arg + "\r\n";
          }
          server_->GetAofWriter()->Append(resp_cmd);
        }
      });
    }
  }
  
  commands::Database* GetDatabase() const override { return db_; }
  int GetDBIndex() const override { return db_index_; }
  bool IsAuthenticated() const override { return authenticated_; }
  
  void SetDBIndex(int index) override { db_index_ = index; }
  void SetAuthenticated(bool auth) override { authenticated_ = auth; }
  
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
  
  // Persistence operations
  bool IsPersistenceEnabled() const override {
    return server_ ? server_->IsPersistenceEnabled() : false;
  }
  
  persistence::LevelDBAdapter* GetPersistence() const override {
    return server_ ? server_->GetPersistence() : nullptr;
  }
  
 private:
  commands::Database* db_;
  int db_index_;
  bool authenticated_;
  Server* server_;
};

void Server::HandleCommand(const protocol::Command& cmd,
                          std::shared_ptr<network::Connection> conn) {
  total_commands_++;
  
  ASTRADB_LOG_DEBUG("Processing command: id={}, cmd={}, args={}", 
                    conn->GetId(), cmd.name, cmd.ArgCount());
  
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
              redirect_addr = owner->ip + ":" + std::to_string(owner->port);
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
                redirect_addr = target_node->ip + ":" + std::to_string(target_node->port);
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
    std::string moved_error = "MOVED " + std::to_string(redirect_slot) + " " + redirect_addr;
    auto error_result = commands::CommandResult(false, moved_error);
    SendResponse(conn, error_result);
    return;
  }
  
  // Handle ASK redirect
  if (ask_redirect) {
    std::string ask_error = "ASK " + std::to_string(redirect_slot) + " " + redirect_addr;
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
  shard->Post([this, cmd, conn, shard]() {
    // Create command context
    ServerCommandContext context(shard->GetDatabase(), 0, this);
    
    // Execute command
    auto result = registry_.Execute(cmd, &context);
    
    // Persist if enabled and command was successful
    if (config_.persistence.enabled && persistence_ && result.success) {
      // TODO: Add persistence logic for write commands
    }
    
    // Send response back on connection's thread
    asio::post(io_context_, [this, conn, result]() {
      SendResponse(conn, result);
    });
  });
}

void Server::SendResponse(std::shared_ptr<network::Connection> conn,
                         const commands::CommandResult& result) {
  // Convert CommandResult to RESP string
  std::string response;
  
  if (!result.success) {
    // Error response
    response = "-" + result.error + "\r\n";
  } else {
    // Success response - convert RespValue to string
    if (result.response.IsSimpleString()) {
      response = "+" + result.response.AsString() + "\r\n";
    } else if (result.response.IsBulkString()) {
      const auto& str = result.response.AsString();
      response = "$" + std::to_string(str.length()) + "\r\n" + str + "\r\n";
    } else if (result.response.IsInteger()) {
      response = ":" + std::to_string(result.response.AsInteger()) + "\r\n";
    } else if (result.response.GetType() == protocol::RespType::kDouble) {
      response = "," + std::to_string(result.response.AsDouble()) + "\r\n";
    } else if (result.response.IsArray()) {
      const auto& arr = result.response.AsArray();
      response = "*" + std::to_string(arr.size()) + "\r\n";
      for (const auto& elem : arr) {
        if (elem.IsBulkString()) {
          const auto& str = elem.AsString();
          response += "$" + std::to_string(str.length()) + "\r\n" + str + "\r\n";
        } else if (elem.IsInteger()) {
          response += ":" + std::to_string(elem.AsInteger()) + "\r\n";
        } else if (elem.IsNull()) {
          response += "$-1\r\n";
        } else {
          // Fallback to simple string
          response += "+" + elem.AsString() + "\r\n";
        }
      }
    } else if (result.response.IsNull()) {
      response = "$-1\r\n";
    } else {
      // For other types, just return OK
      response = "+OK\r\n";
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
        int port = std::stoi(seed.substr(colon_pos + 1));
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

bool Server::ClusterMeet(const std::string& ip, int port) {
  if (!config_.cluster.enabled || !gossip_manager_) {
    ASTRADB_LOG_WARN("Cluster not enabled, cannot meet node");
    return false;
  }
  
  ASTRADB_LOG_INFO("CLUSTER MEET: {}:{}", ip, port);
  return gossip_manager_->MeetNode(ip, port);
}

}  // namespace astra::server
