// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include "connection.hpp"

#include <absl/functional/any_invocable.h>
#include <absl/strings/str_cat.h>

#include <sstream>

#include "astra/base/logging.hpp"

namespace astra::network {

std::atomic<uint64_t> Connection::next_id_(1);

Connection::Connection(Socket socket, Executor& io_context,
                       astra::core::memory::BufferPool* buffer_pool)
    : socket_(std::move(socket)),
      io_context_(io_context),
      strand_(asio::make_strand(io_context)),
      id_(next_id_++),
      writing_(false),
      closing_(false),
      buffer_pool_(buffer_pool) {
  ASTRADB_LOG_INFO("New connection created: id={}, addr={}", id_,
                   GetRemoteAddress());
}
Connection::~Connection() {
  ASTRADB_LOG_INFO("Connection destroyed: id={}", id_);
}

void Connection::Reset(asio::ip::tcp::socket socket) {
  // Close old socket if still open
  if (socket_.is_open()) {
    asio::error_code ec;
    socket_.close(ec);
    if (ec) {
      ASTRADB_LOG_WARN("Failed to close old socket: id={}, error={}", id_, ec.message());
    }
  }

  // Replace with new socket
  socket_ = std::move(socket);

  // Reset strand with new socket's executor
  strand_ = asio::make_strand(io_context_);

  // Reset state
  read_buffer_.Reset();
  write_buffer_.Reset();
  writing_ = false;
  closing_ = false;
  in_transaction_ = false;
  protocol_version_ = 2;
  client_name_.clear();
  watched_keys_.clear();
  watched_key_versions_.clear();
  queued_commands_.clear();

  // Clear temp buffer
  read_temp_buffer_.fill(0);

  ASTRADB_LOG_INFO("Connection reset: id={}, new_addr={}", id_, GetRemoteAddress());
}

void Connection::Start() { DoRead(); }

std::string Connection::GetRemoteAddress() const {
  if (!socket_.is_open()) {
    return "unknown";
  }

  try {
    return socket_.remote_endpoint().address().to_string() + ":" +
           absl::StrCat(socket_.remote_endpoint().port());
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
    ASTRADB_LOG_ERROR("Error closing socket: id={}, error={}", id_,
                      ec.message());
  }
}

void Connection::CloseSocketForReuse() {
  // Close socket without setting closing_ flag (for pool reuse)
  asio::error_code ec;
  socket_.close(ec);

  if (ec) {
    ASTRADB_LOG_WARN("Failed to close socket for reuse: id={}, error={}", id_, ec.message());
  }
}

void Connection::Send(const std::string& data) {
  if (closing_ || !socket_.is_open()) {
    ASTRADB_LOG_DEBUG("Send: id={}, skipped (closing={}, open={})", id_,
                      closing_, socket_.is_open());
    return;
  }

  ASTRADB_LOG_DEBUG("Send: id={}, data_len={}, writing_={}", id_, data.size(),
                    writing_);

  // Append data to write buffer
  if (!write_buffer_) {
    if (buffer_pool_) {
      write_buffer_ = buffer_pool_->Acquire(data.size());
    } else {
      write_buffer_ = astra::core::memory::BufferPtr(
          new astra::core::memory::Buffer(data.size()));
    }
  }

  write_buffer_->Append(data.data(), data.size());
  DoWrite();
}

void Connection::DoRead() {
  auto self = shared_from_this();

  // Reuse the member temp buffer instead of allocating a new one
  socket_.async_read_some(
      asio::buffer(read_temp_buffer_),
      asio::bind_executor(
          strand_, [this, self](const asio::error_code& ec,
                               size_t bytes_transferred) {
            if (!ec && bytes_transferred > 0) {
              // Initialize or append to read buffer
              if (!read_buffer_) {
                if (buffer_pool_) {
                  read_buffer_ = buffer_pool_->Acquire(bytes_transferred);
                } else {
                  read_buffer_ = astra::core::memory::BufferPtr(
                      new astra::core::memory::Buffer(bytes_transferred));
                }
              }

              // Ensure buffer has enough capacity
              if (read_buffer_->size() + bytes_transferred >
                  read_buffer_->capacity()) {
                read_buffer_->Reserve(read_buffer_->size() + bytes_transferred);
              }

              read_buffer_->Append(read_temp_buffer_.data(), bytes_transferred);
            }
            HandleRead(ec, bytes_transferred);
          }));
}

void Connection::DoWrite() {
  if (writing_ || !write_buffer_ || write_buffer_->empty()) {
    return;
  }

  writing_ = true;
  auto self = shared_from_this();

  // Capture the buffer size at the time of the write request
  // This ensures we only write the data that was in the buffer when DoWrite was
  // called
  size_t write_size = write_buffer_->size();

  asio::async_write(
      socket_, asio::buffer(write_buffer_->data(), write_size),
      asio::bind_executor(strand_, [this, self, write_size](
                                       const asio::error_code& ec,
                                       size_t bytes_transferred) {
        writing_ = false;

        if (ec) {
          ASTRADB_LOG_ERROR(
              "Write error: id={}, error={}, bytes_transferred={}", id_,
              ec.message(), bytes_transferred);
          Close();
          return;
        }

        ASTRADB_LOG_DEBUG(
            "Write complete: id={}, bytes_transferred={}, buffer_remaining={}",
            id_, bytes_transferred, write_buffer_ ? write_buffer_->size() : 0);

        if (!write_buffer_) {
          return;
        }

        if (bytes_transferred < write_size) {
          // Partial write, remove written bytes from buffer
          size_t remaining = write_buffer_->size() - bytes_transferred;
          std::memmove(write_buffer_->data(),
                       write_buffer_->data() + bytes_transferred, remaining);
          write_buffer_->Resize(remaining);
          ASTRADB_LOG_DEBUG("Partial write: id={}, continuing with {} bytes",
                            id_, write_buffer_->size());
          DoWrite();
        } else {
          // All data written, remove written bytes from buffer
          // Note: Don't clear the entire buffer, as new data may have been added while we were writing
          size_t buffer_size_before = write_buffer_->size();
          if (buffer_size_before > write_size) {
            // Remove only the bytes that were written, keep any new data that was added
            size_t remaining = buffer_size_before - write_size;
            std::memmove(write_buffer_->data(),
                         write_buffer_->data() + write_size, remaining);
            write_buffer_->Resize(remaining);
            ASTRADB_LOG_DEBUG("Write complete: id={}, removed {} bytes, {} bytes remaining",
                              id_, write_size, remaining);
            
            // Continue writing if there's more data
            if (!write_buffer_->empty()) {
              DoWrite();
            }
          } else {
            // Buffer size equals write size, clear the buffer
            write_buffer_->Clear();
            ASTRADB_LOG_DEBUG("Write complete: id={}, buffer cleared", id_);
          }
        }
      }));
}

void Connection::HandleRead(const asio::error_code& ec,
                            size_t bytes_transferred) {
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

void Connection::HandleWrite(const asio::error_code& ec,
                             size_t bytes_transferred) {
  writing_ = false;

  if (ec) {
    ASTRADB_LOG_ERROR("Write error: id={}, error={}, bytes_transferred={}", id_,
                      ec.message(), bytes_transferred);
    Close();
    return;
  }

  ASTRADB_LOG_DEBUG(
      "Write complete: id={}, bytes_transferred={}, buffer_remaining={}", id_,
      bytes_transferred, write_buffer_ ? write_buffer_->size() : 0);

  if (!write_buffer_) {
    return;
  }

  if (bytes_transferred < write_buffer_->size()) {
    // Partial write, remove written bytes from buffer
    size_t remaining = write_buffer_->size() - bytes_transferred;
    std::memmove(write_buffer_->data(),
                 write_buffer_->data() + bytes_transferred, remaining);
    write_buffer_->Resize(remaining);
    ASTRADB_LOG_DEBUG("Partial write: id={}, continuing with {} bytes", id_,
                      write_buffer_->size());
    DoWrite();
  } else {
    // All data written, clear buffer
    write_buffer_->Clear();
  }
}

void Connection::ProcessData() {
  if (!read_buffer_ || read_buffer_->empty()) {
    return;
  }

  ASTRADB_LOG_DEBUG("ProcessData: id={}, buffer_size={}", id_,
                    read_buffer_->size());

  // Parse RESP commands from read_buffer_
  std::string_view data(read_buffer_->data(), read_buffer_->size());
  size_t consumed = 0;

  while (!data.empty()) {
    // Check if we have a complete value
    if (!protocol::RespParser::HasCompleteValue(data)) {
      ASTRADB_LOG_DEBUG(
          "ProcessData: id={}, incomplete command, waiting for more data", id_);
      break;
    }

    // Parse the value - this modifies the data reference
    size_t data_size_before = data.size();
    auto value = protocol::RespParser::Parse(data);
    if (!value.has_value()) {
      ASTRADB_LOG_ERROR("Parse error: id={}", id_);
      Close();
      return;
    }

    // Calculate bytes consumed by this parse
    size_t bytes_consumed = data_size_before - data.size();

    // If it's an array, parse as command
    if (value->IsArray()) {
      auto cmd = protocol::RespParser::ParseCommand(*value);
      if (cmd.has_value() && command_callback_) {
        ASTRADB_LOG_DEBUG("ProcessData: id={}, processing command: {}, args={}",
                          id_, cmd->name, cmd->ArgCount());
        ProcessCommand(*cmd);
      }
    }

    // Update total consumed bytes
    consumed += bytes_consumed;

    // Update data view for next iteration
    data = std::string_view(read_buffer_->data() + consumed,
                            read_buffer_->size() - consumed);
  }

  // Remove consumed data from read buffer
  if (consumed > 0) {
    size_t remaining = read_buffer_->size() - consumed;
    if (remaining > 0) {
      std::memmove(read_buffer_->data(), read_buffer_->data() + consumed,
                   remaining);
      read_buffer_->Resize(remaining);
    } else {
      read_buffer_->Clear();
    }
  }

  if (!read_buffer_ || read_buffer_->empty()) {
    ASTRADB_LOG_DEBUG("ProcessData: id={}, buffer is now empty", id_);
  }
}

void Connection::ProcessCommand(const protocol::Command& cmd) {
  ASTRADB_LOG_DEBUG("Received command: id={}, name={}, args={}", id_, cmd.name,
                    cmd.ArgCount());

  // Print command arguments
  if (cmd.ArgCount() > 0) {
    std::ostringstream oss;
    oss << " args=[";
    for (size_t i = 0; i < cmd.ArgCount(); ++i) {
      if (i > 0) oss << ", ";
      const auto& arg = cmd[i];
      if (arg.IsBulkString()) {
        oss << "'" << arg.AsString() << "'";
      } else if (arg.IsSimpleString()) {
        oss << "'" << arg.AsString() << "'";
      } else if (arg.IsInteger()) {
        oss << arg.AsInteger();
      } else {
        oss << "?";
      }
    }
    oss << "]";
    ASTRADB_LOG_DEBUG("Received command: id={}, name={}{}", id_, cmd.name,
                      oss.str());
  }

  // Call the command callback
  if (command_callback_) {
    command_callback_(cmd);
  }
}

// ============== Transaction Support Implementation ==============

void Connection::BeginTransaction() {
  in_transaction_ = true;
  queued_commands_.clear();
  ASTRADB_LOG_DEBUG("Transaction started: id={}", id_);
}

void Connection::QueueCommand(const protocol::Command& cmd) {
  queued_commands_.push_back(cmd);
  ASTRADB_LOG_DEBUG("Command queued: id={}, name={}, queue_size={}", id_,
                    cmd.name, queued_commands_.size());
}

absl::InlinedVector<protocol::Command, 16> Connection::GetQueuedCommands()
    const {
  return queued_commands_;
}

void Connection::ClearQueuedCommands() { queued_commands_.clear(); }

void Connection::DiscardTransaction() {
  in_transaction_ = false;
  queued_commands_.clear();
  watched_keys_.clear();
  watched_key_versions_.clear();
  ASTRADB_LOG_DEBUG("Transaction discarded: id={}", id_);
}

void Connection::WatchKey(const std::string& key, uint64_t version) {
  watched_keys_.insert(key);
  watched_key_versions_[key] = version;
  ASTRADB_LOG_DEBUG("WATCH key: id={}, key={}, version={}", id_, key, version);
}

bool Connection::IsWatchedKeyModified(
    const absl::AnyInvocable<uint64_t(const std::string&) const>& get_version)
    const {
  for (const auto& key : watched_keys_) {
    auto it = watched_key_versions_.find(key);
    if (it != watched_key_versions_.end()) {
      uint64_t current_version = get_version(key);
      if (current_version != it->second) {
        ASTRADB_LOG_DEBUG("WATCH key modified: id={}, key={}, old={}, new={}",
                          id_, key, it->second, current_version);
        return true;
      }
    }
  }
  return false;
}

void Connection::ClearWatchedKeys() {
  watched_keys_.clear();
  watched_key_versions_.clear();
}

// ConnectionPool Implementation
ConnectionPool::ConnectionPool(asio::io_context& io_context,
                               size_t max_connections,
                               astra::core::memory::BufferPool* buffer_pool)
    : io_context_(io_context),
      max_connections_(max_connections),
      buffer_pool_(buffer_pool) {
  ASTRADB_LOG_INFO("ConnectionPool initialized: max_connections={}",
                   max_connections);
}

ConnectionPool::~ConnectionPool() = default;

std::shared_ptr<Connection> ConnectionPool::Acquire(
    asio::ip::tcp::socket socket) {
  std::lock_guard<std::mutex> lock(pool_mutex_);
  
  Connection* conn_ptr = nullptr;

  // 1. Try to reuse from free connections
  if (!free_connections_.empty()) {
    conn_ptr = free_connections_.front();
    free_connections_.pop();
    counters_.idle_connections_.fetch_sub(1, std::memory_order_relaxed);

    // Reinitialize connection with new socket
    conn_ptr->Reset(std::move(socket));

    ASTRADB_LOG_DEBUG("Connection reused: id={}, active={}, idle={}",
                      conn_ptr->GetId(), counters_.active_connections_.load(std::memory_order_relaxed),
                      counters_.idle_connections_.load(std::memory_order_relaxed));
  }
  // 2. Create new connection if under limit
  else if (all_connections_.size() < max_connections_) {
    auto new_conn = std::make_unique<Connection>(std::move(socket),
                                                  io_context_,
                                                  buffer_pool_);
    conn_ptr = new_conn.get();
    all_connections_.push_back(std::move(new_conn));

    ASTRADB_LOG_DEBUG("New connection created: id={}, total={}",
                      conn_ptr->GetId(), all_connections_.size());
  }
  // 3. Pool is full
  else {
    ASTRADB_LOG_WARN("Connection pool full: active={}, max={}",
                     counters_.active_connections_.load(std::memory_order_relaxed), max_connections_);
    return nullptr;
  }

  counters_.active_connections_.fetch_add(1, std::memory_order_relaxed);

  // Return shared_ptr with custom deleter that releases back to pool
  return std::shared_ptr<Connection>(conn_ptr, [this](Connection* conn) {
    Release(conn);
  });
}

void ConnectionPool::Release(Connection* conn) {
  if (!conn) return;

  std::lock_guard<std::mutex> lock(pool_mutex_);

  // Prepare connection for reuse
  conn->CloseSocketForReuse();

  // Add back to free queue
  free_connections_.push(conn);
  counters_.idle_connections_.fetch_add(1, std::memory_order_relaxed);
  counters_.active_connections_.fetch_sub(1, std::memory_order_relaxed);

  ASTRADB_LOG_DEBUG("Connection released: id={}, active={}, idle={}",
                    conn->GetId(), counters_.active_connections_.load(std::memory_order_relaxed),
                    counters_.idle_connections_.load(std::memory_order_relaxed));
}

}  // namespace astra::network