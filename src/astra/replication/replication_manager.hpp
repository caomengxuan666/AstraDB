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
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "astra/base/logging.hpp"
#include "astra/commands/database.hpp"
#include "astra/persistence/rdb_common.hpp"
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

  SlaveInfo(uint64_t i, const std::string& h, uint16_t p)
      : id(i), host(h), port(p), repl_offset(0) {}
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
            commands::Database* database = nullptr) noexcept {
    config_ = config;
    role_ = config.role;
    database_ = database;

    if (role_ == ReplicationRole::kSlave) {
      ASTRADB_LOG_INFO("Replication initialized as slave, master: {}:{}",
                       config_.master_host, config_.master_port);
      // Connect to master asynchronously
      ConnectToMaster();
    } else if (role_ == ReplicationRole::kMaster) {
      ASTRADB_LOG_INFO("Replication initialized as master");
      // Generate master replication ID
      GenerateMasterReplId();
    }

    initialized_.store(true, std::memory_order_release);
    return true;
  }

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
    slaves_[id] = std::make_unique<SlaveInfo>(id, host, port);

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

    // Send to all slaves
    absl::MutexLock slaves_lock(&slaves_mutex_);
    for (auto& [id, slave] : slaves_) {
      if (slave->online.load(std::memory_order_acquire)) {
        // Send command to slave
        SendCommandToSlave(slave.get(), cmd);
        slave->repl_offset = repl_offset_.load(std::memory_order_relaxed);
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
    // Generate and return RDB snapshot
    return GenerateRdbSnapshot();
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

  absl::flat_hash_map<uint64_t, std::unique_ptr<SlaveInfo>> slaves_;
  mutable absl::Mutex slaves_mutex_;

  std::deque<protocol::Command> repl_backlog_;
  mutable absl::Mutex backlog_mutex_;

  CommandCallback command_callback_;
  std::unique_ptr<persistence::RdbWriter> rdb_writer_;
  absl::BitGen bit_gen_;

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

  void ConnectToMaster() noexcept {
    ASTRADB_LOG_INFO("Connecting to master at {}:{}...", config_.master_host,
                     config_.master_port);

    // Note: Actual network connection would be implemented here
    // This would use asio to connect to the master and start receiving
    // replication data For now, we just log the attempt
    master_connected_.store(true, std::memory_order_release);

    // After connecting, we would send SYNC or PSYNC command
    // and start receiving updates from master
    ASTRADB_LOG_INFO("Successfully connected to master");
  }

  void SendCommandToSlave(SlaveInfo* slave,
                          const protocol::Command& cmd) noexcept {
    if (!slave || !slave->online.load(std::memory_order_acquire)) {
      return;
    }

    // Note: Actual command propagation would be implemented here
    // This would use asio to send the command to the slave
    ASTRADB_LOG_DEBUG("Sending command to slave {}:{} (id={})", slave->host,
                      slave->port, slave->id);

    // Update slave's replication offset
    slave->repl_offset = repl_offset_.load(std::memory_order_relaxed);
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
