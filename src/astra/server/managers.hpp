// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <absl/time/time.h>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "astra/base/concurrentqueue_wrapper.hpp"
#include "astra/base/logging.hpp"
#include "astra/persistence/aof_writer.hpp"

namespace astra::server {

// Persistence Manager - Handles AOF persistence with MPSC queue
// Designed for NO SHARING architecture
class PersistenceManager {
 public:
  PersistenceManager() = default;
  ~PersistenceManager() { Shutdown(); }

  // Initialize persistence manager
  bool Init(const std::string& data_dir, bool aof_enabled = true,
            const std::string& aof_path = "./data/aof/appendonly.aof") {
    data_dir_ = data_dir;
    aof_enabled_ = aof_enabled;

    if (!aof_enabled_) {
      ASTRADB_LOG_INFO("PersistenceManager: Init (AOF disabled)");
      return true;
    }

    // Initialize AOF writer
    persistence::AofOptions options;
    options.aof_path = aof_path;
    options.sync_policy = persistence::AofSyncPolicy::kEverySec;

    if (!aof_writer_.Init(options)) {
      ASTRADB_LOG_ERROR("Failed to initialize AOF writer");
      return false;
    }

    // Start background thread for processing commands
    running_.store(true, std::memory_order_release);
    processing_thread_ = std::thread(&PersistenceManager::ProcessCommands, this);

    ASTRADB_LOG_INFO("PersistenceManager: Init with data_dir={}, aof_path={}",
                     data_dir, aof_path);
    initialized_ = true;
    return true;
  }

  // Shutdown persistence manager
  void Shutdown() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
      return;
    }

    if (processing_thread_.joinable()) {
      processing_thread_.join();
    }

    if (aof_writer_.IsInitialized()) {
      aof_writer_.Stop();
    }

    ASTRADB_LOG_INFO("PersistenceManager: Shutdown");
  }

  // Append a write command to AOF (thread-safe, non-blocking)
  void AppendCommand(const std::string& command) {
    if (!aof_enabled_ || !initialized_) {
      return;
    }
    command_queue_.enqueue(command);
  }

  // Check if AOF is enabled
  bool IsAofEnabled() const { return aof_enabled_; }

  // Get AOF writer (for advanced operations like rewrite)
  persistence::AofWriter* GetAofWriter() { return &aof_writer_; }

 private:
  // Background thread to process commands and write to AOF
  void ProcessCommands() {
    ASTRADB_LOG_INFO("PersistenceManager: Command processing thread started");

    while (running_.load(std::memory_order_acquire)) {
      // Process commands in batch
      const size_t kBatchSize = 100;
      std::string rcmd;
      size_t processed = 0;

      while (processed < kBatchSize &&
             command_queue_.try_dequeue(rcmd)) {
        aof_writer_.Append(rcmd);
        rcmd.clear();
        processed++;
      }

      // Flush if we processed commands
      if (processed > 0) {
        aof_writer_.Flush();
      }

      // Small sleep if no commands
      if (processed == 0) {
        absl::SleepFor(absl::Milliseconds(1));
      }
    }

    // Process remaining commands before shutdown
    std::string rcmd;
    while (command_queue_.try_dequeue(rcmd)) {
      aof_writer_.Append(rcmd);
      rcmd.clear();
    }
    aof_writer_.Flush();

    ASTRADB_LOG_INFO("PersistenceManager: Command processing thread stopped");
  }

  std::string data_dir_;
  bool aof_enabled_ = true;
  bool initialized_ = false;
  std::atomic<bool> running_{false};

  // AOF writer
  persistence::AofWriter aof_writer_;

  // MPSC queue for commands from all workers
  moodycamel::ConcurrentQueue<std::string> command_queue_;

  // Background thread
  std::thread processing_thread_;
};

// Simple cluster manager stub
// TODO: Implement full cluster with Gossip, ShardManager
class ClusterManager {
 public:
  ClusterManager() = default;
  ~ClusterManager() = default;

  bool Init(const std::string& node_id) {
    ASTRADB_LOG_INFO("ClusterManager: Init with node_id={}", node_id);
    return true;
  }

  void Shutdown() {
    ASTRADB_LOG_INFO("ClusterManager: Shutdown");
  }
};

// Simple Pub/Sub manager stub
// TODO: Implement full Pub/Sub with cross-worker communication
class PubSubManager {
 public:
  PubSubManager() = default;
  ~PubSubManager() = default;

  bool Init() {
    ASTRADB_LOG_INFO("PubSubManager: Init");
    return true;
  }

  void Shutdown() {
    ASTRADB_LOG_INFO("PubSubManager: Shutdown");
  }
};

// Simple metrics manager stub
// TODO: Implement full metrics with Prometheus
class MetricsManager {
 public:
  MetricsManager() = default;
  ~MetricsManager() = default;

  bool Init(const std::string& bind_addr, uint16_t port) {
    ASTRADB_LOG_INFO("MetricsManager: Init on {}:{}", bind_addr, port);
    return true;
  }

  void Shutdown() {
    ASTRADB_LOG_INFO("MetricsManager: Shutdown");
  }
};

}  // namespace astra::server