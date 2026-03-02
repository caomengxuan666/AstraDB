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
      shard_manager_(config.num_shards),
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
  
  ASTRADB_LOG_INFO("Server configured: host={}, port={}, max_connections={}, databases={}, shards={}, commands={}",
                   config_.host, config_.port,
                   config_.max_connections, config_.num_databases, config_.num_shards,
                   RuntimeCommandRegistry::Instance().GetCommandCount());
}

Server::~Server() {
  Stop();
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
  ServerCommandContext(commands::Database* db, int db_index = 0)
      : db_(db), db_index_(db_index), authenticated_(true) {}
  
  commands::Database* GetDatabase() const override { return db_; }
  int GetDBIndex() const override { return db_index_; }
  bool IsAuthenticated() const override { return authenticated_; }
  
  void SetDBIndex(int index) override { db_index_ = index; }
  void SetAuthenticated(bool auth) override { authenticated_ = auth; }
  
 private:
  commands::Database* db_;
  int db_index_;
  bool authenticated_;
};

void Server::HandleCommand(const protocol::Command& cmd,
                          std::shared_ptr<network::Connection> conn) {
  total_commands_++;
  
  ASTRADB_LOG_DEBUG("Processing command: id={}, cmd={}, args={}", 
                    conn->GetId(), cmd.name, cmd.ArgCount());
  
  // Get routing strategy for this command
  auto routing = registry_.GetRoutingStrategy(cmd.name);
  
  Shard* shard = nullptr;
  
  // Route based on strategy
  if (routing == RoutingStrategy::kByFirstKey && cmd.ArgCount() > 0) {
    // Route based on first argument (key)
    const auto& key_arg = cmd[0];
    if (key_arg.IsBulkString()) {
      shard = shard_manager_.GetShard(key_arg.AsString());
    }
  }
  
  // Default to shard 0 for commands without routing strategy
  if (!shard) {
    shard = shard_manager_.GetShardByIndex(0);
  }
  
  if (!shard) {
    auto error_result = commands::CommandResult(false, "ERR shard not found");
    SendResponse(conn, error_result);
    return;
  }
  
  // Post command execution to the shard's IO context
  shard->Post([this, cmd, conn, shard]() {
    // Create command context
    ServerCommandContext context(shard->GetDatabase(), 0);
    
    // Execute command
    auto result = registry_.Execute(cmd, &context);
    
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
    auto* shard = shard_manager_.GetShardByIndex(shard_index);
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

}  // namespace astra::server
