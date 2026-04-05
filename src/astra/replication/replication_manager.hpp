// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <absl/container/flat_hash_map.h>
#include <absl/functional/any_invocable.h>
#include <absl/random/random.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/escaping.h>
#include <absl/synchronization/mutex.h>

#include <atomic>
#include <deque>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <asio.hpp>

#include "astra/base/logging.hpp"
#include "astra/commands/database.hpp"
#include "astra/persistence/rdb_common.hpp"
#include "astra/persistence/rdb_reader.hpp"
#include "astra/persistence/rdb_writer.hpp"
#include "astra/protocol/resp/resp_types.hpp"
#include "astra/storage/key_metadata.hpp"

namespace astra::replication {

// Replication role
enum class ReplicationRole { kMaster, kSlave, kNone };

// Slave connection info
struct SlaveInfo {
  uint64_t id;
  std::string host;
  uint16_t port;
  uint64_t repl_offset;  // Replication offset
  std::atomic<bool> online{true};
  std::unique_ptr<asio::ip::tcp::socket> socket;  // Socket for sending commands

  SlaveInfo(uint64_t i, const std::string& h, uint16_t p,
            asio::io_context* io_ctx = nullptr)
      : id(i), host(h), port(p), repl_offset(0) {
    if (io_ctx) {
      socket = std::make_unique<asio::ip::tcp::socket>(*io_ctx);
    }
  }
};

// Replication configuration
struct ReplicationConfig {
  ReplicationRole role = ReplicationRole::kNone;
  std::string master_host = "";
  uint16_t master_port = 6379;
  std::string master_auth = "";
  bool read_only = true;                     // Slaves are read-only by default
  uint64_t repl_backlog_size = 1024 * 1024;  // 1MB
};

// Replication Manager - handles master-slave replication
class ReplicationManager {
 public:
  using CommandCallback = absl::AnyInvocable<void(const protocol::Command&)>;

  ReplicationManager() noexcept = default;
  ~ReplicationManager() noexcept { Stop(); }

  // Non-copyable, non-movable
  ReplicationManager(const ReplicationManager&) = delete;
  ReplicationManager& operator=(const ReplicationManager&) = delete;
  ReplicationManager(ReplicationManager&&) = delete;
  ReplicationManager& operator=(ReplicationManager&&) = delete;

  // Initialize replication manager
  bool Init(const ReplicationConfig& config,
            commands::Database* database = nullptr,
            asio::io_context* io_context = nullptr) noexcept {
    config_ = config;
    role_ = config.role;
    database_ = database;
    io_context_ = io_context;

    if (role_ == ReplicationRole::kSlave) {
      ASTRADB_LOG_INFO("Replication initialized as slave, master: {}:{}",
                       config_.master_host, config_.master_port);
      // Connect to master asynchronously
      if (io_context_) {
        asio::co_spawn(
            *io_context_,
            [this]() -> asio::awaitable<void> { co_await ConnectToMaster(); },
            asio::detached);
      }
    } else if (role_ == ReplicationRole::kMaster) {
      ASTRADB_LOG_INFO("Replication initialized as master");
      // Generate master replication ID
      GenerateMasterReplId();
    }

    initialized_.store(true, std::memory_order_release);
    return true;
  }

  // Set io_context (for late initialization)
  void SetIOContext(asio::io_context* io_context) noexcept {
    io_context_ = io_context;
  }

  // Get io_context
  asio::io_context* GetIOContext() const noexcept { return io_context_; }

  // Set database pointer (for late initialization)
  void SetDatabase(commands::Database* database) noexcept {
    database_ = database;
  }

  // Get database pointer
  commands::Database* GetDatabase() const noexcept { return database_; }

  // Stop replication
  void Stop() noexcept {
    running_.store(false, std::memory_order_release);
    absl::MutexLock lock(&slaves_mutex_);
    slaves_.clear();
    ASTRADB_LOG_INFO("Replication stopped");
  }

  // Get current role
  ReplicationRole GetRole() const noexcept { return role_; }

  // Add slave (master only)
  bool AddSlave(const std::string& host, uint16_t port) noexcept {
    if (role_ != ReplicationRole::kMaster) {
      return false;
    }

    absl::MutexLock lock(&slaves_mutex_);
    uint64_t id = next_slave_id_.fetch_add(1, std::memory_order_relaxed);
    slaves_[id] = std::make_unique<SlaveInfo>(id, host, port, io_context_);

    ASTRADB_LOG_INFO("Slave registered: {}:{} (id={})", host, port, id);
    return true;
  }

  // Remove slave (master only)
  bool RemoveSlave(uint64_t slave_id) noexcept {
    absl::MutexLock lock(&slaves_mutex_);
    auto it = slaves_.find(slave_id);
    if (it != slaves_.end()) {
      slaves_.erase(it);
      ASTRADB_LOG_INFO("Slave removed: id={}", slave_id);
      return true;
    }
    return false;
  }

  // Propagate command to slaves (master only)
  void PropagateCommand(const protocol::Command& cmd) noexcept {
    if (role_ != ReplicationRole::kMaster) {
      return;
    }

    // Increment replication offset
    repl_offset_.fetch_add(1, std::memory_order_relaxed);

    // Store in backlog
    absl::MutexLock lock(&backlog_mutex_);
    if (repl_backlog_.size() >= config_.repl_backlog_size) {
      repl_backlog_.pop_front();
    }
    repl_backlog_.push_back(cmd);

    // Send to all slaves asynchronously using coroutines
    if (io_context_) {
      absl::MutexLock slaves_lock(&slaves_mutex_);
      for (auto& [id, slave] : slaves_) {
        if (slave->online.load(std::memory_order_acquire)) {
          // Spawn coroutine to send command to slave
          asio::co_spawn(
              *io_context_,
              [this, slave_ptr = slave.get(),
               cmd]() -> asio::awaitable<void> {
                co_await this->SendCommandToSlave(slave_ptr, cmd);
              },
              asio::detached);
        }
      }
    }
  }

  // Get current replication offset
  uint64_t GetReplOffset() const noexcept {
    return repl_offset_.load(std::memory_order_relaxed);
  }

  // Set command callback (for slaves to propagate commands)
  void SetCommandCallback(CommandCallback callback) noexcept {
    command_callback_ = std::move(callback);
  }

  // Handle SYNC command (slave request)
  std::string HandleSync(const std::string& replication_id) noexcept {
    ASTRADB_LOG_INFO("SYNC requested by slave: {}", replication_id);
    // Return full sync response header
    uint64_t current_offset = repl_offset_.load(std::memory_order_relaxed);
    return "+FULLRESYNC " + master_replid_ + " " + absl::StrCat(current_offset) +
           "\r\n";
  }

  // Send RDB snapshot to slave (coroutine)
  asio::awaitable<void> SendRdbSnapshot(
      asio::ip::tcp::socket* socket) noexcept {
    if (!database_) {
      ASTRADB_LOG_ERROR("Database not set, cannot send RDB snapshot");
      co_return;
    }

    ASTRADB_LOG_INFO("Starting to send RDB snapshot to slave");

    // Initialize RDB writer if not already initialized
    if (!rdb_writer_) {
      rdb_writer_ = std::make_unique<persistence::RdbWriter>();
      persistence::RdbOptions options;
      options.save_path = "./data/dump_sync.rdb";  // Temporary file for sync
      options.checksum = true;
      if (!rdb_writer_->Init(options)) {
        ASTRADB_LOG_ERROR("Failed to initialize RDB writer");
        co_return;
      }
    }

    // Save snapshot to a temporary buffer
    bool success = rdb_writer_->Save([this](persistence::RdbWriter& writer) {
      // Note: This is a simplified implementation
      // In production, we would write to a file and then stream it

      // Select database 0
      writer.SelectDb(0);

      // Get database size
      size_t db_size = database_->DbSize();
      writer.ResizeDb(db_size, 0);

      // Iterate through all keys and write to RDB
      database_->ForEachKey([&writer](const std::string& key,
                                      astra::storage::KeyType type,
                                      const std::string& value, int64_t ttl_ms) {
        // Convert storage::KeyType to RDB type using the correct mapping
        uint8_t rdb_type = persistence::KeyTypeToRdbType(type);

        // Calculate expire time (ttl_ms is absolute time in milliseconds)
        int64_t expire_ms = (ttl_ms > 0) ? ttl_ms : -1;

        // Write key-value pair to RDB
        writer.WriteKv(rdb_type, key, value, expire_ms);
      });
    });

    if (!success) {
      ASTRADB_LOG_ERROR("Failed to generate RDB snapshot");
      co_return;
    }

    // Read the generated RDB file
    std::ifstream rdb_file("./data/dump_sync.rdb", std::ios::binary);
    if (!rdb_file.is_open()) {
      ASTRADB_LOG_ERROR("Failed to open RDB file for reading");
      co_return;
    }

    // Send RDB file data
    asio::error_code ec;
    std::array<char, 65536> buf;
    size_t total_bytes = 0;

    while (!rdb_file.eof()) {
      rdb_file.read(buf.data(), buf.size());
      size_t bytes_read = rdb_file.gcount();

      if (bytes_read > 0) {
        size_t bytes_sent = co_await asio::async_write(
            *socket, asio::buffer(buf.data(), bytes_read),
            asio::redirect_error(asio::use_awaitable, ec));

        if (ec) {
          ASTRADB_LOG_ERROR("Failed to send RDB data: {}", ec.message());
          co_return;
        }

        total_bytes += bytes_sent;
      }
    }

    rdb_file.close();

    ASTRADB_LOG_INFO("RDB snapshot sent successfully ({} bytes)", total_bytes);
  }

  // Handle PSYNC command (partial sync)
  std::string HandlePsync(const std::string& replication_id,
                          uint64_t offset) noexcept {
    ASTRADB_LOG_INFO("PSYNC requested: id={}, offset={}", replication_id,
                     offset);

    uint64_t current_offset = repl_offset_.load(std::memory_order_relaxed);

    // Check if partial sync is possible
    // We support partial sync if the offset is within backlog range
    absl::MutexLock lock(&backlog_mutex_);
    uint64_t backlog_start_offset =
        current_offset >= repl_backlog_.size()
            ? current_offset - repl_backlog_.size()
            : 0;

    if (offset >= backlog_start_offset && offset <= current_offset) {
      // Partial sync possible
      ASTRADB_LOG_INFO("Partial sync accepted: offset={}, current_offset={}",
                       offset, current_offset);
      return "+CONTINUE\r\n";
    } else {
      // Full sync required
      ASTRADB_LOG_INFO("Full sync required: offset={}, current_offset={}",
                       offset, current_offset);
      return "+FULLRESYNC " + master_replid_ + " " + absl::StrCat(current_offset) +
             "\r\n";
    }
  }

  // Get backlog data for partial sync
  std::vector<protocol::Command> GetBacklogData(uint64_t start_offset,
                                                 uint64_t end_offset) noexcept {
    std::vector<protocol::Command> result;
    absl::MutexLock lock(&backlog_mutex_);

    uint64_t current_offset = repl_offset_.load(std::memory_order_relaxed);
    uint64_t backlog_start_offset =
        current_offset >= repl_backlog_.size()
            ? current_offset - repl_backlog_.size()
            : 0;

    // Validate offset range
    if (start_offset < backlog_start_offset || end_offset > current_offset) {
      ASTRADB_LOG_WARN(
          "Invalid backlog offset range: start={}, end={}, "
          "backlog_start={}, current={}",
          start_offset, end_offset, backlog_start_offset, current_offset);
      return result;
    }

    // Extract commands from backlog
    size_t start_idx = start_offset - backlog_start_offset;
    size_t end_idx = end_offset - backlog_start_offset;

    for (size_t i = start_idx; i < end_idx && i < repl_backlog_.size(); ++i) {
      result.push_back(repl_backlog_[i]);
    }

    return result;
  }

  // Handle REPLCONF ACK (master only)
  void HandleReplconfAck(uint64_t slave_id, uint64_t offset) noexcept {
    if (role_ != ReplicationRole::kMaster) {
      return;
    }

    absl::MutexLock lock(&slaves_mutex_);
    auto it = slaves_.find(slave_id);
    if (it != slaves_.end()) {
      it->second->repl_offset = offset;
      ASTRADB_LOG_DEBUG("Received ACK from slave {} (id={}): offset={}",
                       it->second->host, slave_id, offset);
    }
  }

  // Get master replication ID
  const std::string& GetMasterReplId() const noexcept {
    return master_replid_;
  }

  // Get number of connected slaves
  size_t GetSlaveCount() const noexcept {
    absl::MutexLock lock(&slaves_mutex_);
    return slaves_.size();
  }

  // Get slave info
  std::vector<std::pair<std::string, uint64_t>> GetSlaveInfo() const noexcept {
    std::vector<std::pair<std::string, uint64_t>> info;
    absl::MutexLock lock(&slaves_mutex_);
    for (const auto& [id, slave] : slaves_) {
      info.emplace_back(slave->host, slave->repl_offset);
    }
    return info;
  }

 private:
  ReplicationConfig config_;
  ReplicationRole role_{ReplicationRole::kNone};
  std::atomic<bool> initialized_{false};
  std::atomic<bool> running_{true};
  std::atomic<bool> master_connected_{false};
  std::string master_replid_ = "?";
  std::atomic<uint64_t> repl_offset_{0};
  std::atomic<uint64_t> next_slave_id_{1};
  commands::Database* database_{nullptr};
  asio::io_context* io_context_{nullptr};

  absl::flat_hash_map<uint64_t, std::unique_ptr<SlaveInfo>> slaves_;
  mutable absl::Mutex slaves_mutex_;

  std::deque<protocol::Command> repl_backlog_;
  mutable absl::Mutex backlog_mutex_;

  CommandCallback command_callback_;
  std::unique_ptr<persistence::RdbWriter> rdb_writer_;
  absl::BitGen bit_gen_;

  // Network connections (NO SHARING - each ReplicationManager has its own connections)
  std::unique_ptr<asio::ip::tcp::socket> master_socket_;  // Slave: connection to master

  // Private methods
  void GenerateMasterReplId() noexcept {
    // Generate a 40-character hex string as master replication ID
    const char hex_chars[] = "0123456789abcdef";
    master_replid_.resize(40);
    for (int i = 0; i < 40; ++i) {
      master_replid_[i] = hex_chars[absl::Uniform(bit_gen_, 0, 16)];
    }
    ASTRADB_LOG_INFO("Generated master replication ID: {}", master_replid_);
  }

  // Connect to master (slave only) - coroutine-based
  asio::awaitable<void> ConnectToMaster() noexcept {
    ASTRADB_LOG_INFO("Connecting to master at {}:{}...", config_.master_host,
                     config_.master_port);

    if (!io_context_) {
      ASTRADB_LOG_ERROR("IO context not set, cannot connect to master");
      co_return;
    }

    // Create socket for master connection
    master_socket_ = std::make_unique<asio::ip::tcp::socket>(*io_context_);

    asio::error_code ec;
    auto executor = co_await asio::this_coro::executor;

    // Resolve master address
    asio::ip::tcp::resolver resolver(executor);
    auto endpoints = co_await resolver.async_resolve(
        config_.master_host, std::to_string(config_.master_port),
        asio::redirect_error(asio::use_awaitable, ec));

    if (ec) {
      ASTRADB_LOG_ERROR("Failed to resolve master address: {}", ec.message());
      co_return;
    }

    // Connect to master
    co_await asio::async_connect(
        *master_socket_, endpoints,
        asio::redirect_error(asio::use_awaitable, ec));

    if (ec) {
      ASTRADB_LOG_ERROR("Failed to connect to master: {}", ec.message());
      co_return;
    }

    master_connected_.store(true, std::memory_order_release);
    ASTRADB_LOG_INFO("Successfully connected to master at {}:{}",
                     config_.master_host, config_.master_port);

    // Send PSYNC command
    std::string psync_cmd =
        "*3\r\n$5\r\nPSYNC\r\n$1\r\n?\r\n$1\r\n0\r\n";  // PSYNC ? 0
    size_t bytes_sent = co_await asio::async_write(
        *master_socket_, asio::buffer(psync_cmd),
        asio::redirect_error(asio::use_awaitable, ec));

    if (ec) {
      ASTRADB_LOG_ERROR("Failed to send PSYNC command: {}", ec.message());
      co_return;
    }

    ASTRADB_LOG_INFO("PSYNC command sent ({} bytes)", bytes_sent);

    // Receive master response
    std::array<char, 256> response_buf;
    size_t bytes_received = co_await master_socket_->async_read_some(
        asio::buffer(response_buf),
        asio::redirect_error(asio::use_awaitable, ec));

    if (ec) {
      ASTRADB_LOG_ERROR("Failed to receive master response: {}", ec.message());
      co_return;
    }

    std::string response(response_buf.data(), bytes_received);
    ASTRADB_LOG_INFO("Received master response: {}", response);

    // Parse response
    if (response.find("+FULLRESYNC") == 0) {
      // Full sync required - receive RDB snapshot
      ASTRADB_LOG_INFO("Full sync required, receiving RDB snapshot");
      co_await ReceiveRdbSnapshot();
    } else if (response.find("+CONTINUE") == 0) {
      // Partial sync - skip RDB and receive commands directly
      ASTRADB_LOG_INFO("Partial sync, skipping RDB");
    } else {
      ASTRADB_LOG_ERROR("Unexpected response from master: {}", response);
      co_return;
    }

    // Start receiving replication data (command stream)
    co_await ReceiveReplicationData();
  }

  // Receive RDB snapshot from master (slave only)
  asio::awaitable<void> ReceiveRdbSnapshot() noexcept {
    if (!master_socket_ || !master_socket_->is_open()) {
      co_return;
    }

    ASTRADB_LOG_INFO("Starting to receive RDB snapshot from master");

    // Create temporary file for RDB data
    std::string rdb_path = "./data/dump_sync_received.rdb";
    std::ofstream rdb_file(rdb_path, std::ios::binary);
    if (!rdb_file.is_open()) {
      ASTRADB_LOG_ERROR("Failed to create RDB file for writing");
      co_return;
    }

    // Receive RDB data
    asio::error_code ec;
    std::array<char, 65536> buf;
    size_t total_bytes = 0;

    // Read until we have enough data or EOF
    // For simplicity, we read a fixed amount for now
    // In production, we would read until RDB EOF marker
    while (running_.load(std::memory_order_acquire)) {
      size_t bytes_received = co_await master_socket_->async_read_some(
          asio::buffer(buf),
          asio::redirect_error(asio::use_awaitable, ec));

      if (ec) {
        if (ec == asio::error::eof) {
          ASTRADB_LOG_INFO("Master closed RDB transfer (EOF)");
        } else {
          ASTRADB_LOG_ERROR("Error receiving RDB data: {}", ec.message());
        }
        break;
      }

      // Write to file
      rdb_file.write(buf.data(), bytes_received);
      total_bytes += bytes_received;

      // Check for RDB EOF marker (0xFF)
      // This is a simplified check - in production, we would parse the RDB properly
      if (bytes_received > 0 && static_cast<uint8_t>(buf[bytes_received - 1]) == 0xFF) {
        ASTRADB_LOG_INFO("RDB EOF marker received");
        break;
      }

      // Stop if we've received a reasonable amount of data (for testing)
      if (total_bytes > 1024 * 1024) {  // 1MB limit for testing
        ASTRADB_LOG_INFO("RDB data limit reached, stopping");
        break;
      }
    }

    rdb_file.close();

    ASTRADB_LOG_INFO("RDB snapshot received ({} bytes)", total_bytes);

    // TODO: Load RDB data into database using RdbReader
    // This would involve:
    // 1. Open the RDB file
    // 2. Parse it using RdbReader
    // 3. Load data into database_
    ASTRADB_LOG_INFO("RDB snapshot loading not yet implemented");
  }

  // Send command to slave (master only) - coroutine-based
  asio::awaitable<void> SendCommandToSlave(SlaveInfo* slave,
                                            const protocol::Command& cmd) noexcept {
    if (!slave || !slave->online.load(std::memory_order_acquire)) {
      co_return;
    }

    if (!slave->socket || !slave->socket->is_open()) {
      ASTRADB_LOG_WARN("Slave {}:{} socket not connected", slave->host,
                       slave->port);
      co_return;
    }

    // Convert command to RESP format
    std::string resp_cmd = ReplicationManager::CommandToResp(cmd);

    asio::error_code ec;
    size_t bytes_sent = co_await asio::async_write(
        *slave->socket, asio::buffer(resp_cmd),
        asio::redirect_error(asio::use_awaitable, ec));

    if (ec) {
      ASTRADB_LOG_ERROR("Failed to send command to slave {}:{}: {}",
                        slave->host, slave->port, ec.message());
      slave->online.store(false, std::memory_order_release);
      co_return;
    }

    ASTRADB_LOG_DEBUG("Sent command to slave {}:{} ({} bytes)", slave->host,
                      slave->port, bytes_sent);

    // Update slave's replication offset
    slave->repl_offset = repl_offset_.load(std::memory_order_relaxed);
  }

  // Receive replication data from master (slave only)
  asio::awaitable<void> ReceiveReplicationData() noexcept {
    if (!master_socket_ || !master_socket_->is_open()) {
      co_return;
    }

    ASTRADB_LOG_INFO("Starting to receive replication data from master");

    std::array<char, 65536> buf;
    asio::error_code ec;
    std::string recv_buffer;  // Buffer for incomplete RESP data

    while (running_.load(std::memory_order_acquire)) {
      size_t bytes_received = co_await master_socket_->async_read_some(
          asio::buffer(buf),
          asio::redirect_error(asio::use_awaitable, ec));

      if (ec) {
        if (ec == asio::error::eof) {
          ASTRADB_LOG_WARN("Master closed connection");
        } else {
          ASTRADB_LOG_ERROR("Error receiving replication data: {}",
                            ec.message());
        }
        break;
      }

      // Append received data to buffer
      recv_buffer.append(buf.data(), bytes_received);
      ASTRADB_LOG_DEBUG("Received {} bytes of replication data", bytes_received);

      // Process RESP commands from buffer
      while (!recv_buffer.empty()) {
        // Try to parse a complete RESP command
        // This is a simplified implementation - in production, we would use a proper RESP parser
        size_t cmd_end = FindRespCommandEnd(recv_buffer);
        if (cmd_end == std::string::npos) {
          // Incomplete command, wait for more data
          break;
        }

        // Extract complete command
        std::string cmd_data = recv_buffer.substr(0, cmd_end);
        recv_buffer.erase(0, cmd_end);

        // Parse and execute command
        ProcessReplicationCommand(cmd_data);
      }
    }

    master_connected_.store(false, std::memory_order_release);
    ASTRADB_LOG_INFO("Stopped receiving replication data");
  }

  // Find the end of a RESP command (simplified)
  size_t FindRespCommandEnd(const std::string& buffer) const {
    // This is a simplified implementation
    // In production, we would properly parse RESP protocol
    // For now, we look for \r\n at the end
    size_t pos = buffer.find("\r\n");
    if (pos != std::string::npos) {
      // Check if this is an array (starts with *)
      if (buffer[0] == '*') {
        // For arrays, we need to find the end of all elements
        // This is complex, so we'll just return the first \r\n for now
        return pos + 2;
      }
      return pos + 2;
    }
    return std::string::npos;
  }

  // Process a replication command (simplified)
  void ProcessReplicationCommand(const std::string& cmd_data) noexcept {
    // TODO: Parse RESP command and execute it
    // This would involve:
    // 1. Parse the RESP protocol
    // 2. Create a Command object
    // 3. Execute the command on the database
    // 4. Update replication offset

    ASTRADB_LOG_DEBUG("Processing replication command: {}", cmd_data);

    // Increment replication offset
    repl_offset_.fetch_add(1, std::memory_order_relaxed);

    // Send ACK to master (periodically)
    // In production, we would send ACK every N commands or every T seconds
    uint64_t current_offset = repl_offset_.load(std::memory_order_relaxed);
    if (current_offset % 100 == 0) {  // Send ACK every 100 commands
      SendAckToMaster(current_offset);
    }

    // For now, we just log the command
    // In production, we would execute it using the command callback
    if (command_callback_) {
      // Parse command and call callback
      // This is a placeholder - actual parsing would be more complex
      protocol::Command cmd;
      cmd.name = "SET";  // Placeholder
      command_callback_(cmd);
    }
  }

  // Send ACK to master (slave only)
  void SendAckToMaster(uint64_t offset) noexcept {
    if (role_ != ReplicationRole::kSlave || !master_socket_ || !master_socket_->is_open()) {
      return;
    }

    // Send REPLCONF ACK command
    std::string ack_cmd = "*3\r\n$8\r\nREPLCONF\r\n$3\r\nACK\r\n$" +
                         std::to_string(std::to_string(offset).size()) + "\r\n" +
                         std::to_string(offset) + "\r\n";

    // Note: This is a synchronous send for simplicity
    // In production, we would use async send
    asio::error_code ec;
    size_t bytes_sent = asio::write(*master_socket_, asio::buffer(ack_cmd), ec);

    if (ec) {
      ASTRADB_LOG_ERROR("Failed to send ACK to master: {}", ec.message());
    } else {
      ASTRADB_LOG_DEBUG("Sent ACK to master: offset={} ({} bytes)", offset, bytes_sent);
    }
  }

  // Convert Command to RESP format (static helper)
  static std::string CommandToResp(const protocol::Command& cmd) {
    std::ostringstream oss;

    // Number of arguments (including command name)
    int arg_count = cmd.ArgCount() + 1;
    oss << "*" << arg_count << "\r\n";

    // Command name
    oss << "$" << cmd.name.size() << "\r\n";
    oss << cmd.name << "\r\n";

    // Arguments
    for (size_t i = 0; i < cmd.ArgCount(); ++i) {
      const auto& arg = cmd[i];
      oss << "$" << arg.AsString().size() << "\r\n";
      oss << arg.AsString() << "\r\n";
    }

    return oss.str();
  }

  std::string GenerateRdbSnapshot() noexcept {
    ASTRADB_LOG_INFO("Generating RDB snapshot...");

    if (!database_) {
      ASTRADB_LOG_ERROR("Database not set, cannot generate RDB snapshot");
      return "+FULLRESYNC " + master_replid_ + " 0\r\n";
    }

    // Initialize RDB writer if not already initialized
    if (!rdb_writer_) {
      rdb_writer_ = std::make_unique<persistence::RdbWriter>();
      persistence::RdbOptions options;
      options.save_path = "./data/dump.rdb";
      options.checksum = true;
      if (!rdb_writer_->Init(options)) {
        ASTRADB_LOG_ERROR("Failed to initialize RDB writer");
        return "+FULLRESYNC " + master_replid_ + " 0\r\n";
      }
    }

    // Save snapshot using RdbWriter
    bool success = rdb_writer_->Save([this](persistence::RdbWriter& writer) {
      // Select database 0
      writer.SelectDb(0);

      // Get database size
      size_t db_size = database_->DbSize();
      writer.ResizeDb(db_size, 0);

      // Iterate through all keys and write to RDB
      database_->ForEachKey([&writer](const std::string& key,
                                      astra::storage::KeyType type,
                                      const std::string& value, int64_t ttl_ms) {
        // Convert storage::KeyType to RDB type using the correct mapping
        uint8_t rdb_type = persistence::KeyTypeToRdbType(type);

        // Calculate expire time (ttl_ms is absolute time in milliseconds)
        int64_t expire_ms = (ttl_ms > 0) ? ttl_ms : -1;

        // Write key-value pair to RDB
        writer.WriteKv(rdb_type, key, value, expire_ms);
      });
    });

    if (!success) {
      ASTRADB_LOG_ERROR("Failed to save RDB snapshot");
      return "+FULLRESYNC " + master_replid_ + " 0\r\n";
    }

    ASTRADB_LOG_INFO("RDB snapshot generated successfully");
    return "+FULLRESYNC " + master_replid_ + " " +
           absl::StrCat(repl_offset_.load(std::memory_order_relaxed)) + "\r\n";
  }
};

}  // namespace astra::replication
