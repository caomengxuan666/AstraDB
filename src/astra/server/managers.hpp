// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <absl/time/time.h>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include <prometheus/exposer.h>

#include "astra/base/concurrentqueue_wrapper.hpp"
#include "astra/base/logging.hpp"
#include "astra/core/metrics.hpp"
#include "astra/persistence/aof_writer.hpp"
#include "astra/persistence/rdb_writer.hpp"
#include "astra/storage/key_metadata.hpp"

namespace astra::server {

// Persistence Manager - Handles AOF persistence with MPSC queue
// Designed for NO SHARING architecture
class PersistenceManager {
 public:
  PersistenceManager() = default;
  ~PersistenceManager() { Shutdown(); }

  // Initialize persistence manager
  bool Init(const std::string& data_dir, bool aof_enabled = true, bool rdb_enabled = true,
            const std::string& aof_path = "./data/aof/appendonly.aof",
            const std::string& rdb_path = "./data/dump.rdb") {
    data_dir_ = data_dir;
    aof_enabled_ = aof_enabled;
    rdb_enabled_ = rdb_enabled;

    // Initialize AOF writer if enabled
    if (aof_enabled_) {
      persistence::AofOptions options;
      options.aof_path = aof_path;
      options.sync_policy = persistence::AofSyncPolicy::kEverySec;

      if (!aof_writer_.Init(options)) {
        ASTRADB_LOG_ERROR("Failed to initialize AOF writer");
        return false;
      }
    }

    // Initialize RDB writer if enabled
    if (rdb_enabled_) {
      persistence::RdbOptions rdb_options;
      rdb_options.save_path = rdb_path;
      rdb_options.checksum = true;

      if (!rdb_writer_.Init(rdb_options)) {
        ASTRADB_LOG_ERROR("Failed to initialize RDB writer");
        return false;
      }
    }

    // Start background thread for processing AOF commands
    running_.store(true, std::memory_order_release);
    processing_thread_ = std::thread(&PersistenceManager::ProcessCommands, this);

    ASTRADB_LOG_INFO("PersistenceManager: Init with data_dir={}, aof_enabled={}, rdb_enabled={}",
                     data_dir, aof_enabled, rdb_enabled);
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

  // ========== RDB Persistence Operations ==========

  // Save all workers' data to RDB file (blocking)
  template <typename WorkerCollection>
  bool SaveRdb(const WorkerCollection& workers) {
    if (!rdb_enabled_) {
      ASTRADB_LOG_WARN("PersistenceManager: RDB is disabled");
      return false;
    }

    ASTRADB_LOG_INFO("PersistenceManager: Starting RDB save");

    // Collect data from all workers
    std::vector<std::vector<std::tuple<std::string, astra::storage::KeyType,
                                         std::string, int64_t>>> all_data;

    for (const auto& worker : workers) {
      auto worker_data = worker->GetRdbData();
      all_data.push_back(std::move(worker_data));
      ASTRADB_LOG_DEBUG("PersistenceManager: Collected {} keys from worker {}",
                       worker_data.size(), worker->GetWorkerId());
    }

    // Save to RDB file using callback
    auto save_callback = [&all_data](persistence::RdbWriter& writer) {
      size_t db_index = 0;
      size_t db_size = 0;
      size_t expires_size = 0;

      // Count total keys
      for (const auto& worker_data : all_data) {
        db_size += worker_data.size();
        for (const auto& [key, type, value, ttl_ms] : worker_data) {
          if (ttl_ms > 0) {
            expires_size++;
          }
        }
      }

      // Select database and write header
      writer.SelectDb(db_index);
      writer.ResizeDb(db_size, expires_size);

      // Write all key-value pairs
      for (const auto& worker_data : all_data) {
        for (const auto& [key, type, value, ttl_ms] : worker_data) {
          // Calculate absolute expire time
          int64_t expire_ms = -1;
          if (ttl_ms > 0) {
            auto expire_time = absl::Now() + absl::Milliseconds(ttl_ms);
            expire_ms = absl::ToUnixMillis(expire_time);
          }

          // Write key-value pair based on type
          switch (type) {
            case astra::storage::KeyType::kString:
              writer.WriteKv(astra::persistence::RDB_TYPE_STRING, key, value, expire_ms);
              break;
            case astra::storage::KeyType::kHash:
              writer.WriteKv(astra::persistence::RDB_TYPE_HASH, key, value, expire_ms);
              break;
            case astra::storage::KeyType::kList:
              writer.WriteKv(astra::persistence::RDB_TYPE_LIST, key, value, expire_ms);
              break;
            case astra::storage::KeyType::kSet:
              writer.WriteKv(astra::persistence::RDB_TYPE_SET, key, value, expire_ms);
              break;
            case astra::storage::KeyType::kZSet:
              writer.WriteKv(astra::persistence::RDB_TYPE_ZSET, key, value, expire_ms);
              break;
            default:
              ASTRADB_LOG_WARN("PersistenceManager: Unknown type {} for key {}",
                               static_cast<int>(type), key);
              break;
          }
        }
      }
    };

    bool success = rdb_writer_.Save(save_callback);
    if (success) {
      ASTRADB_LOG_INFO("PersistenceManager: RDB save completed");
    } else {
      ASTRADB_LOG_ERROR("PersistenceManager: RDB save failed");
    }

    return success;
  }

  // Start background RDB save (non-blocking)
  template <typename WorkerCollection>
  bool BackgroundSaveRdb(const WorkerCollection& workers) {
    if (bg_save_in_progress_) {
      ASTRADB_LOG_WARN("PersistenceManager: Background RDB save already in progress");
      return false;
    }

    bg_save_in_progress_ = true;

    // Start background save thread
    if (bg_save_thread_.joinable()) {
      bg_save_thread_.join();
    }

    bg_save_thread_ = std::thread([this, workers]() {
      ASTRADB_LOG_INFO("PersistenceManager: Background RDB save started");
      bool success = SaveRdb(workers);
      bg_save_in_progress_ = false;
      ASTRADB_LOG_INFO("PersistenceManager: Background RDB save completed, success={}", success);
    });

    return true;
  }

  // Check if background RDB save is in progress
  bool IsBgSaveInProgress() const { return bg_save_in_progress_; }

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
  bool rdb_enabled_ = true;
  bool initialized_ = false;
  std::atomic<bool> running_{false};

  // AOF writer
  persistence::AofWriter aof_writer_;

  // RDB writer
  persistence::RdbWriter rdb_writer_;

  // MPSC queue for commands from all workers
  moodycamel::ConcurrentQueue<std::string> command_queue_;

  // Background thread for AOF processing
  std::thread processing_thread_;

  // Background RDB save state
  std::atomic<bool> bg_save_in_progress_{false};
  std::thread bg_save_thread_;
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

// Metrics Manager - Prometheus metrics collection and HTTP server
// Uses prometheus-cpp built-in Exposer for NO SHARING architecture
class MetricsManager {
 public:
  MetricsManager() = default;
  ~MetricsManager() { Shutdown(); }

  bool Init(const std::string& bind_addr, uint16_t port) {
    ASTRADB_LOG_INFO("MetricsManager: Init on {}:{}", bind_addr, port);

    try {
      // Initialize metrics registry
      astra::metrics::MetricsConfig config;
      config.enabled = true;
      config.bind_addr = bind_addr;
      config.port = port;

      if (!astra::metrics::MetricsRegistry::Instance().Init(config)) {
        ASTRADB_LOG_ERROR("Failed to initialize metrics registry");
        return false;
      }

      // Create prometheus-cpp Exposer (built-in CivetWeb HTTP server)
      // Use vector constructor for better CivetWeb control
      std::vector<std::string> options = {
        "listening_ports", bind_addr + ":" + std::to_string(port),
        "num_threads", "1",
        "enable_keep_alive", "no"
      };
      exposer_ = std::make_unique<prometheus::Exposer>(options);

      // Register the registry with the exposer
      exposer_->RegisterCollectable(astra::metrics::MetricsRegistry::Instance().GetRegistry());

      ASTRADB_LOG_INFO("MetricsManager: Initialized successfully with built-in Exposer");
      initialized_ = true;
      return true;
    } catch (const std::exception& e) {
      ASTRADB_LOG_ERROR("MetricsManager: Init exception: {}", e.what());
      return false;
    }
  }

  void Shutdown() {
    if (!running_.exchange(false)) {
      return;
    }

    ASTRADB_LOG_INFO("MetricsManager: Shutting down");

    // The Exposer will be automatically destroyed when exposer_ is reset
    // No need to manually stop HTTP server

    ASTRADB_LOG_INFO("MetricsManager: Shutdown complete");
  }

  bool IsInitialized() const { return initialized_; }

 private:
  bool initialized_ = false;
  std::atomic<bool> running_{false};

  // prometheus-cpp built-in Exposer (CivetWeb HTTP server)
  std::unique_ptr<prometheus::Exposer> exposer_;
};

}  // namespace astra::server