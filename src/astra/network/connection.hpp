// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <asio.hpp>
#include <memory>
#include <string>
#include <functional>

#include "astra/protocol/resp/resp_types.hpp"
#include "astra/protocol/resp/resp_parser.hpp"

namespace astra::network {

// Connection context
class Connection : public std::enable_shared_from_this<Connection> {
 public:
  using Socket = asio::ip::tcp::socket;
  using Executor = asio::io_context;
  using CommandCallback = std::function<void(const protocol::Command&)>;
  
  explicit Connection(Socket socket, Executor& io_context);
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
  
  std::string read_buffer_;
  std::string write_buffer_;
  
  CommandCallback command_callback_;
  
  bool writing_;
  bool closing_;
};

// Connection pool
class ConnectionPool {
 public:
  explicit ConnectionPool(asio::io_context& io_context, size_t max_connections = 10000);
  ~ConnectionPool();
  
  std::shared_ptr<Connection> Create(asio::ip::tcp::socket socket);
  
  size_t ActiveConnections() const { return active_connections_; }
  size_t TotalConnections() const { return total_connections_; }
  
  void SetMaxConnections(size_t max) { max_connections_ = max; }
  
 private:
  asio::io_context& io_context_;
  size_t max_connections_;
  std::atomic<size_t> active_connections_;
  std::atomic<size_t> total_connections_;
};

}  // namespace astra::network