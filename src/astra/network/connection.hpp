// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <asio.hpp>
#include <memory>
#include <string>
#include <functional>
#include <atomic>

#include <absl/container/flat_hash_set.h>
#include <absl/container/flat_hash_map.h>
#include <absl/container/inlined_vector.h>

#include "astra/protocol/resp/resp_types.hpp"
#include "astra/protocol/resp/resp_parser.hpp"
#include "astra/core/memory/buffer_pool.hpp"

namespace astra::network {

// Forward declaration
class ConnectionPool;

// Connection context
class Connection : public std::enable_shared_from_this<Connection> {
 public:
  using Socket = asio::ip::tcp::socket;
  using Executor = asio::io_context;
  using CommandCallback = std::function<void(const protocol::Command&)>;
  
  explicit Connection(Socket socket, Executor& io_context, 
                    astra::core::memory::BufferPool* buffer_pool = nullptr);
  ~Connection();
  
  // Start the connection
  void Start();
  
  // Set command callback
  void SetCommandCallback(CommandCallback callback) {
    command_callback_ = std::move(callback);
  }
  
  // Get remote address
  std::string GetRemoteAddress() const;
  
  // Get connection ID
  uint64_t GetId() const { return id_; }
  
  // Close the connection
  void Close();
  
  // Check if connected
  bool IsConnected() const { return socket_.is_open(); }
  
  // Send data to client
  void Send(const std::string& data);
  
  // Get buffer pool
  astra::core::memory::BufferPool* GetBufferPool() const { return buffer_pool_; }
  
  // Reset connection state (for object pool reuse)
  void Reset(asio::ip::tcp::socket socket);
  
  // ============== Transaction Support ==============
  
  // Check if in transaction
  bool IsInTransaction() const { return in_transaction_; }
  
  // Begin transaction (MULTI)
  void BeginTransaction();
  
  // Queue a command in transaction
  void QueueCommand(const protocol::Command& cmd);
  
  // Get queued commands (for EXEC)
  absl::InlinedVector<protocol::Command, 16> GetQueuedCommands() const;
  
  // Clear queued commands
  void ClearQueuedCommands();
  
  // Discard transaction
  void DiscardTransaction();
  
  // WATCH key
  void WatchKey(const std::string& key, uint64_t version);
  
  // Get watched keys
  const absl::flat_hash_set<std::string>& GetWatchedKeys() const { return watched_keys_; }
  
  // Check if any watched key was modified
  bool IsWatchedKeyModified(const std::function<uint64_t(const std::string&)>& get_version) const;
  
  // Clear watched keys
  void ClearWatchedKeys();
  
 private:
  void DoRead();
  void DoWrite();
  
  void HandleRead(const asio::error_code& ec, size_t bytes_transferred);
  void HandleWrite(const asio::error_code& ec, size_t bytes_transferred);
  
  void ProcessData();
  void ProcessCommand(const protocol::Command& cmd);
  
  Socket socket_;
  Executor& io_context_;
  
  uint64_t id_;
  static std::atomic<uint64_t> next_id_;
  
  astra::core::memory::BufferPtr read_buffer_;
  astra::core::memory::BufferPtr write_buffer_;
  
  CommandCallback command_callback_;
  
  bool writing_;
  bool closing_;
  
  // Buffer pool reference (not owned)
  astra::core::memory::BufferPool* buffer_pool_;
  
  // ============== Transaction State ==============
  bool in_transaction_ = false;
  absl::InlinedVector<protocol::Command, 16> queued_commands_;
  absl::flat_hash_set<std::string> watched_keys_;
  absl::flat_hash_map<std::string, uint64_t> watched_key_versions_;
};

// Connection pool
class ConnectionPool {
 public:
  explicit ConnectionPool(asio::io_context& io_context, 
                         size_t max_connections = 10000,
                         astra::core::memory::BufferPool* buffer_pool = nullptr);
  ~ConnectionPool();
  
  std::shared_ptr<Connection> Create(asio::ip::tcp::socket socket);
  
  size_t ActiveConnections() const { return active_connections_; }
  size_t TotalConnections() const { return total_connections_; }
  
  void SetMaxConnections(size_t max) { max_connections_ = max; }
  
  // Get buffer pool
  astra::core::memory::BufferPool* GetBufferPool() const { return buffer_pool_; }
  
 private:
  asio::io_context& io_context_;
  size_t max_connections_;
  std::atomic<size_t> active_connections_;
  std::atomic<size_t> total_connections_;
  astra::core::memory::BufferPool* buffer_pool_;
};

}  // namespace astra::network