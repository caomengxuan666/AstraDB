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
  ASTRADB_LOG_INFO("New connection created: id={}, addr={}", id_, GetRemoteAddress());
}

Connection::~Connection() {
  ASTRADB_LOG_INFO("Connection destroyed: id={}", id_);
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
    ASTRADB_LOG_ERROR("Error closing socket: id={}, error={}", id_, ec.message());
  }
}

void Connection::Send(const std::string& data) {
  if (closing_ || !socket_.is_open()) {
    ASTRADB_LOG_DEBUG("Send: id={}, skipped (closing={}, open={})", 
                      id_, closing_, socket_.is_open());
    return;
  }
  
  ASTRADB_LOG_DEBUG("Send: id={}, data_len={}, writing_={}", 
                    id_, data.size(), writing_);
  
  write_buffer_ += data;
  DoWrite();
}

void Connection::DoRead() {
  auto self = shared_from_this();
  
  // Read some data into a temporary buffer
  constexpr size_t kBufferSize = 8192;
  auto temp_buffer = std::make_shared<std::array<char, kBufferSize>>();
  
  socket_.async_read_some(
    asio::buffer(*temp_buffer),
    [this, self, temp_buffer](const asio::error_code& ec, size_t bytes_transferred) {
      if (!ec && bytes_transferred > 0) {
        read_buffer_.append(temp_buffer->data(), bytes_transferred);
      }
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
      ASTRADB_LOG_ERROR("Read error: id={}, error={}", id_, ec.message());
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
    ASTRADB_LOG_ERROR("Write error: id={}, error={}, bytes_transferred={}", 
                      id_, ec.message(), bytes_transferred);
    Close();
    return;
  }
  
  ASTRADB_LOG_DEBUG("Write complete: id={}, bytes_transferred={}, buffer_remaining={}", 
                    id_, bytes_transferred, write_buffer_.size());
  
  if (bytes_transferred < write_buffer_.size()) {
    // Partial write, continue with the rest
    write_buffer_.erase(0, bytes_transferred);
    ASTRADB_LOG_DEBUG("Partial write: id={}, continuing with {} bytes", 
                      id_, write_buffer_.size());
    DoWrite();
  } else {
    write_buffer_.clear();
  }
}

void Connection::ProcessData() {
  // Parse RESP commands from read_buffer_
  std::string_view data(read_buffer_);
  
  ASTRADB_LOG_DEBUG("ProcessData: id={}, buffer_size={}, data_size='{}'", 
                    id_, read_buffer_.size(), data.size());
  
  while (!data.empty()) {
    // Check if we have a complete value
    if (!protocol::RespParser::HasCompleteValue(data)) {
      ASTRADB_LOG_DEBUG("ProcessData: id={}, incomplete command, waiting for more data", id_);
      break;
    }
    
    // Parse the value
    auto value = protocol::RespParser::Parse(data);
    if (!value.has_value()) {
      ASTRADB_LOG_ERROR("Parse error: id={}", id_);
      Close();
      return;
    }
    
    // Record data before buffer update
    size_t old_buffer_size = read_buffer_.size();
    size_t data_size_before = data.size();
    
    // Update read buffer - ⚠️ This is the problematic line!
    read_buffer_ = std::string(data);
    
    ASTRADB_LOG_DEBUG("ProcessData: id={}, buffer updated: {} -> {}, data: {} -> {}", 
                      id_, old_buffer_size, read_buffer_.size(), 
                      data_size_before, data.size());
    
    // If it's an array, parse as command
    if (value->IsArray()) {
      auto cmd = protocol::RespParser::ParseCommand(*value);
      if (cmd.has_value() && command_callback_) {
        // ⚠️ value may contain string_views pointing to old buffer!
        ASTRADB_LOG_DEBUG("ProcessData: id={}, processing command: {}, args={}", 
                          id_, cmd->name, cmd->ArgCount());
        ProcessCommand(*cmd);
      }
    }
  }
  
  if (read_buffer_.empty()) {
    ASTRADB_LOG_DEBUG("ProcessData: id={}, buffer is now empty", id_);
  }
}

void Connection::ProcessCommand(const protocol::Command& cmd) {
  ASTRADB_LOG_DEBUG("Received command: id={}, name={}, args={}", 
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