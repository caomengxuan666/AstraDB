// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <functional>

#include "astra/base/logging.hpp"
#include "astra/protocol/resp/resp_types.hpp"

namespace astra::replication {

// Replication role
enum class ReplicationRole {
  kMaster,
  kSlave,
  kNone
};

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
  bool read_only = true;  // Slaves are read-only by default
  uint64_t repl_backlog_size = 1024 * 1024;  // 1MB
};

// Replication Manager - handles master-slave replication
class ReplicationManager {
 public:
  using CommandCallback = std::function<void(const protocol::Command&)>;
  
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
      // TODO: Connect to master
    } else if (role_ == ReplicationRole::kMaster) {
      ASTRADB_LOG_INFO("Replication initialized as master");
    }
    
    initialized_.store(true, std::memory_order_release);
    return true;
  }
  
  // Stop replication
  void Stop() noexcept {
    running_.store(false, std::memory_order_release);
    std::lock_guard<std::mutex> lock(slaves_mutex_);
    slaves_.clear();
    ASTRADB_LOG_INFO("Replication stopped");
  }
  
  // Get current role
  ReplicationRole GetRole() const noexcept {
    return role_;
  }
  
  // Add slave (master only)
  bool AddSlave(const std::string& host, uint16_t port) noexcept {
    if (role_ != ReplicationRole::kMaster) {
      return false;
    }
    
    std::lock_guard<std::mutex> lock(slaves_mutex_);
    uint64_t id = next_slave_id_.fetch_add(1, std::memory_order_relaxed);
    slaves_[id] = std::make_unique<SlaveInfo>(id, host, port);
    
    ASTRADB_LOG_INFO("Slave registered: {}:{} (id={})", host, port, id);
    return true;
  }
  
  // Remove slave (master only)
  bool RemoveSlave(uint64_t slave_id) noexcept {
    std::lock_guard<std::mutex> lock(slaves_mutex_);
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
    std::lock_guard<std::mutex> lock(backlog_mutex_);
    if (repl_backlog_.size() >= config_.repl_backlog_size) {
      repl_backlog_.pop_front();
    }
    repl_backlog_.push_back(cmd);
    
    // Send to all slaves
    std::lock_guard<std::mutex> slaves_lock(slaves_mutex_);
    for (auto& [id, slave] : slaves_) {
      if (slave->online.load(std::memory_order_acquire)) {
        // TODO: Send command to slave
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
    // TODO: Generate and return RDB snapshot
    return "+FULLRESYNC <master_replid> <repl_offset>\r\n";
  }
  
  // Handle PSYNC command (partial sync)
  std::string HandlePsync(const std::string& replication_id, uint64_t offset) noexcept {
    ASTRADB_LOG_INFO("PSYNC requested: id={}, offset={}", replication_id, offset);
    // TODO: Check if partial sync is possible
    return "+CONTINUE <repl_offset>\r\n";
  }
  
  // Get number of connected slaves
  size_t GetSlaveCount() const noexcept {
    std::lock_guard<std::mutex> lock(slaves_mutex_);
    return slaves_.size();
  }
  
  // Get slave info
  std::vector<std::pair<std::string, uint64_t>> GetSlaveInfo() const noexcept {
    std::vector<std::pair<std::string, uint64_t>> info;
    std::lock_guard<std::mutex> lock(slaves_mutex_);
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
  std::atomic<uint64_t> repl_offset_{0};
  std::atomic<uint64_t> next_slave_id_{1};
  
  std::unordered_map<uint64_t, std::unique_ptr<SlaveInfo>> slaves_;
  mutable std::mutex slaves_mutex_;
  
  std::deque<protocol::Command> repl_backlog_;
  mutable std::mutex backlog_mutex_;
  
  CommandCallback command_callback_;
};

}  // namespace astra::replication