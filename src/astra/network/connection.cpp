// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include "connection.hpp"
#include "astra/base/logging.hpp"
#include <sstream>

namespace astra::network {

std::atomic<uint64_t> Connection::next_id_(1);

Connection::Connection(Socket socket, Executor& io_context)
    : socket_(std::move(socket)),
      io_context_(io_context),
      id_(next_id_++),
      writing_(false),
      closing_(false) {
  LOG_INFO("New connection created: id={}, addr={}", id_, GetRemoteAddress());
}

Connection::~Connection() {
  LOG_INFO("Connection destroyed: id={}", id_);
}

void Connection::Start() {
  DoRead();
}

std::string Connection::GetRemoteAddress() const {
  if (!socket_.is_open()) {
    return "unknown";
  }
  
  try {
    return socket_.remote_endpoint().address().to_string() + ":" + 
           std::to_string(socket_.remote_endpoint().port());
  } catch (...) {
    return "unknown";
  }
}

void Connection::Close() {
  if (closing_) return;
  closing_ = true;
  
  asio::error_code ec;
  socket_.close(ec);
  
  if (ec) {
    LOG_ERROR("Error closing socket: id={}, error={}", id_, ec.message());
  }
}

void Connection::DoRead() {
  auto self = shared_from_this();
  
  // Read some data
  socket_.async_read_some(
    asio::buffer(read_buffer_),
    [this, self](const asio::error_code& ec, size_t bytes_transferred) {
      HandleRead(ec, bytes_transferred);
    });
}

void Connection::DoWrite() {
  if (writing_ || write_buffer_.empty()) {
    return;
  }
  
  writing_ = true;
  auto self = shared_from_this();
  
  asio::async_write(
    socket_,
    asio::buffer(write_buffer_),
    [this, self](const asio::error_code& ec, size_t bytes_transferred) {
      HandleWrite(ec, bytes_transferred);
    });
}

void Connection::HandleRead(const asio::error_code& ec, size_t bytes_transferred) {
  if (ec) {
    if (ec != asio::error::eof) {
      LOG_ERROR("Read error: id={}, error={}", id_, ec.message());
    }
    Close();
    return;
  }
  
  if (bytes_transferred == 0) {
    return;
  }
  
  ProcessData();
  
  if (!closing_) {
    DoRead();
  }
}

void Connection::HandleWrite(const asio::error_code& ec, size_t bytes_transferred) {
  writing_ = false;
  
  if (ec) {
    LOG_ERROR("Write error: id={}, error={}", id_, ec.message());
    Close();
    return;
  }
  
  if (bytes_transferred < write_buffer_.size()) {
    // Partial write, continue with the rest
    write_buffer_.erase(0, bytes_transferred);
    DoWrite();
  } else {
    write_buffer_.clear();
  }
}

void Connection::ProcessData() {
  // Parse RESP commands from read_buffer_
  std::string_view data(read_buffer_);
  
  while (!data.empty()) {
    // Check if we have a complete value
    if (!protocol::RespParser::HasCompleteValue(data)) {
      break;
    }
    
    // Parse the value
    auto value = protocol::RespParser::Parse(data);
    if (!value.has_value()) {
      LOG_ERROR("Parse error: id={}", id_);
      Close();
      return;
    }
    
    // Update read buffer
    read_buffer_ = std::string(data);
    
    // If it's an array, parse as command
    if (value->IsArray()) {
      auto cmd = protocol::RespParser::ParseCommand(*value);
      if (cmd.has_value() && command_callback_) {
        ProcessCommand(*cmd);
      }
    }
  }
}

void Connection::ProcessCommand(const protocol::Command& cmd) {
  LOG_DEBUG("Received command: id={}, name={}, args={}", 
             id_, cmd.name, cmd.ArgCount());
  
  // Call the command callback
  if (command_callback_) {
    command_callback_(cmd);
  }
}

// ConnectionPool Implementation
ConnectionPool::ConnectionPool(asio::io_context& io_context, size_t max_connections)
    : io_context_(io_context),
      max_connections_(max_connections),
      active_connections_(0),
      total_connections_(0) {
}

ConnectionPool::~ConnectionPool() = default;

std::shared_ptr<Connection> ConnectionPool::Create(asio::ip::tcp::socket socket) {
  if (active_connections_ >= max_connections_) {
    return nullptr;
  }
  
  auto conn = std::make_shared<Connection>(std::move(socket), io_context_);
  active_connections_++;
  total_connections_++;
  
  return conn;
}

}  // namespace astra::network