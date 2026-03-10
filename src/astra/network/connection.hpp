// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <absl/container/inlined_vector.h>
#include <absl/functional/any_invocable.h>

#include <asio.hpp>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <queue>
#include <vector>

#include "astra/core/memory/buffer_pool.hpp"
#include "astra/protocol/resp/resp_parser.hpp"
#include "astra/protocol/resp/resp_types.hpp"

namespace astra::network {

// Forward declaration
class ConnectionPool;

// Connection context
class Connection : public std::enable_shared_from_this<Connection> {
 public:
  using Socket = asio::ip::tcp::socket;
  using Executor = asio::io_context;
  using CommandCallback =
      absl::AnyInvocable<void(const protocol::Command&) const>;
  using BatchCommandCallback =
      absl::AnyInvocable<void(absl::InlinedVector<protocol::Command, 16>&&) const>;

  explicit Connection(Socket socket, Executor& io_context,
                      astra::core::memory::BufferPool* buffer_pool = nullptr);
  ~Connection();

  // Start the connection
  void Start();

  // Set command callback
  void SetCommandCallback(CommandCallback callback) {
    command_callback_ = std::move(callback);
  }

  // Set batch command callback for pipeline optimization
  void SetBatchCommandCallback(BatchCommandCallback callback) {
    batch_command_callback_ = std::move(callback);
  }

  // Get remote address
  std::string GetRemoteAddress() const;

  // Get connection ID
  uint64_t GetId() const { return id_; }

  // Close the connection
  void Close();

  // Close socket for reuse (called by ConnectionPool)
  void CloseSocketForReuse();

  // Check if connected
  bool IsConnected() const { return socket_.is_open(); }

  // Send data to client
  void Send(const std::string& data);

  // Get buffer pool
  astra::core::memory::BufferPool* GetBufferPool() const {
    return buffer_pool_;
  }

  // Set client name
  void SetClientName(const std::string& name) { client_name_ = name; }

  // Get client name
  const std::string& GetClientName() const { return client_name_; }

  // ============== RESP Protocol Version ==============
  // Get RESP protocol version (2 or 3)
  int GetProtocolVersion() const { return protocol_version_; }

  // Set RESP protocol version
  void SetProtocolVersion(int version) { protocol_version_ = version; }

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
  const absl::flat_hash_set<std::string>& GetWatchedKeys() const {
    return watched_keys_;
  }

  // Check if any watched key was modified
  bool IsWatchedKeyModified(
      const absl::AnyInvocable<uint64_t(const std::string&) const>& get_version)
      const;

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
  asio::strand<asio::any_io_executor> strand_;

  uint64_t id_;
  static std::atomic<uint64_t> next_id_;

  bool writing_;
  bool closing_;

  astra::core::memory::BufferPtr read_buffer_;
  astra::core::memory::BufferPtr write_buffer_;
  
  // Temporary read buffer (reused across reads to avoid allocations)
  std::array<char, 8192> read_temp_buffer_;

  CommandCallback command_callback_;
  BatchCommandCallback batch_command_callback_;

  // Buffer pool reference (not owned)
  astra::core::memory::BufferPool* buffer_pool_;

  // Client name
  std::string client_name_;

  // ============== RESP Protocol Version ==============
  int protocol_version_ = 2;  // Default to RESP2

  // ============== Transaction State ==============
  bool in_transaction_ = false;
  absl::InlinedVector<protocol::Command, 16> queued_commands_;
  absl::flat_hash_set<std::string> watched_keys_;
  absl::flat_hash_map<std::string, uint64_t> watched_key_versions_;
};

// Connection pool
class ConnectionPool {
 public:
  explicit ConnectionPool(
      asio::io_context& io_context, size_t max_connections = 10000,
      astra::core::memory::BufferPool* buffer_pool = nullptr);
  ~ConnectionPool();

  std::shared_ptr<Connection> Acquire(asio::ip::tcp::socket socket);
  
  void Release(Connection* conn);

  size_t ActiveConnections() const { return counters_.active_connections_.load(std::memory_order_relaxed); }
  size_t TotalConnections() const { return counters_.total_connections_.load(std::memory_order_relaxed); }
  size_t IdleConnections() const { return counters_.idle_connections_.load(std::memory_order_relaxed); }

  void SetMaxConnections(size_t max) { max_connections_ = max; }

  // Get buffer pool
  astra::core::memory::BufferPool* GetBufferPool() const {
    return buffer_pool_;
  }

 private:
  asio::io_context& io_context_;
  
  // Cache line aligned atomic counters to avoid false sharing
  struct alignas(std::hardware_destructive_interference_size) Counters {
    std::atomic<size_t> active_connections_{0};
    std::atomic<size_t> total_connections_{0};
    std::atomic<size_t> idle_connections_{0};
  } counters_;
  
  size_t max_connections_;
  astra::core::memory::BufferPool* buffer_pool_;
  
  // Connection pooling (mutex and queue on separate cache lines)
  alignas(std::hardware_destructive_interference_size) std::mutex pool_mutex_;
  std::queue<Connection*> free_connections_;
  std::vector<std::unique_ptr<Connection>> all_connections_;
};

}  // namespace astra::network