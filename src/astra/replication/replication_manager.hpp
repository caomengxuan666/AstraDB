// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <absl/container/flat_hash_map.h>
#include <absl/functional/any_invocable.h>
#include <absl/strings/str_cat.h>
#include <absl/synchronization/mutex.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "astra/base/logging.hpp"
#include "astra/protocol/resp/resp_types.hpp"

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
  bool Init(const ReplicationConfig& config) noexcept {
    config_ = config;
    role_ = config.role;

    if (role_ == ReplicationRole::kSlave) {
      ASTRADB_LOG_INFO("Replication initialized as slave, master: {}:{}",
                       config_.master_host, config_.master_port);
      // Connect to master asynchronously
      ConnectToMaster();
    } else if (role_ == ReplicationRole::kMaster) {
      ASTRADB_LOG_INFO("Replication initialized as master");
    }

    initialized_.store(true, std::memory_order_release);
    return true;
  }

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

    // Check if partial sync is possible
    // For now, we support partial sync if the offset is within backlog
    absl::MutexLock lock(&backlog_mutex_);
    if (offset < repl_backlog_.size() &&
        offset >= repl_offset_.load(std::memory_order_relaxed) -
                      repl_backlog_.size()) {
      // Partial sync possible
      return "+CONTINUE\r\n";
    } else {
      // Full sync required
      return "+FULLRESYNC " + master_replid_ + " " + absl::StrCat(offset) +
             "\r\n";
    }
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

  absl::flat_hash_map<uint64_t, std::unique_ptr<SlaveInfo>> slaves_;
  mutable absl::Mutex slaves_mutex_;

  std::deque<protocol::Command> repl_backlog_;
  mutable absl::Mutex backlog_mutex_;

  CommandCallback command_callback_;

  // Private methods
  void ConnectToMaster() noexcept {
    ASTRADB_LOG_INFO("Connecting to master at {}:{}...", config_.master_host,
                     config_.master_port);

    // Note: Actual network connection would be implemented here
    // This would use asio to connect to the master and start receiving
    // replication data For now, we just log the attempt
    master_connected_.store(true, std::memory_order_release);

    // After connecting, we would send SYNC or PSYNC command
    // and start receiving updates from master
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
  }

  std::string GenerateRdbSnapshot() noexcept {
    // Note: Actual RDB snapshot generation would be implemented here
    // This would use RdbWriter to create a snapshot of the current database
    // For now, we return a placeholder response
    ASTRADB_LOG_INFO("Generating RDB snapshot...");
    return "+FULLRESYNC 6379a8d1e7a8e7a8e7a8e7a8e7a8e7a8e7a8e 0\r\n";
  }
};

}  // namespace astra::replication
