// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <absl/time/time.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "astra/base/concurrentqueue_wrapper.hpp"
#include "astra/base/logging.hpp"
#include "astra/core/metrics.hpp"
#include "astra/persistence/aof_writer.hpp"
#include "astra/persistence/rdb_reader.hpp"
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
  bool Init(const std::string& data_dir, bool aof_enabled = true,
            bool rdb_enabled = true,
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
      options_.save_path = rdb_path;
      options_.compress = true;
      options_.compression_level = 6;
      options_.checksum = true;

      if (!rdb_writer_.Init(options_)) {
        ASTRADB_LOG_ERROR("Failed to initialize RDB writer");
        return false;
      }
    }

    // Start background thread for processing AOF commands
    running_.store(true, std::memory_order_release);
    processing_thread_ =
        std::thread(&PersistenceManager::ProcessCommands, this);

    ASTRADB_LOG_INFO(
        "PersistenceManager: Init with data_dir={}, aof_enabled={}, "
        "rdb_enabled={}",
        data_dir, aof_enabled, rdb_enabled);
    initialized_ = true;
    return true;
  }

  // Shutdown persistence manager
  void Shutdown() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
      return;
    }

    // Signal the processing thread to wake up
    command_cv_.Signal();

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
    // Notify processing thread that there's new data
    command_cv_.Signal();
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
    std::vector<std::vector<
        std::tuple<std::string, astra::storage::KeyType, std::string, int64_t>>>
        all_data;

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
          uint8_t rdb_type = astra::persistence::KeyTypeToRdbType(type);
          writer.WriteKv(rdb_type, key, value, expire_ms);
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
      ASTRADB_LOG_WARN(
          "PersistenceManager: Background RDB save already in progress");
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
      ASTRADB_LOG_INFO(
          "PersistenceManager: Background RDB save completed, success={}",
          success);
    });

    return true;
  }

  // Check if background RDB save is in progress
  bool IsBgSaveInProgress() const { return bg_save_in_progress_; }

  // Load RDB file and distribute data to workers
  template <typename WorkerCollection>
  bool LoadRdb(const WorkerCollection& workers) {
    if (!rdb_enabled_) {
      ASTRADB_LOG_WARN("PersistenceManager: RDB is disabled");
      return false;
    }

    ASTRADB_LOG_INFO("PersistenceManager: Starting RDB load");

    // Check if RDB file exists - if not, this is normal (first run)
    if (!std::filesystem::exists(options_.save_path)) {
      ASTRADB_LOG_INFO(
          "PersistenceManager: RDB file not found (first run), skipping load: "
          "{}",
          options_.save_path);
      return true;  // File not found is normal, not an error
    }

    if (!rdb_reader_.Init(options_.save_path, true)) {
      ASTRADB_LOG_ERROR("PersistenceManager: Failed to initialize RDB reader");
      return false;
    }

    size_t num_workers = 0;
    for (const auto& w : workers) {
      (void)w;  // Suppress unused warning
      num_workers++;
    }

    // Load key-value pairs from RDB file into workers
    auto load_callback = [&workers, num_workers](
                             int db_num, const persistence::RdbKeyValue& kv) {
      // Calculate target worker based on key hash (consistent with NO SHARING
      // architecture)
      size_t hash = std::hash<std::string>{}(kv.key);
      size_t target_worker = hash % num_workers;

      // Get target worker
      size_t idx = 0;
      typename std::decay_t<decltype(workers)>::value_type target_worker_ptr =
          nullptr;
      for (const auto& w : workers) {
        if (idx == target_worker) {
          target_worker_ptr = w;
          break;
        }
        idx++;
      }

      if (target_worker_ptr) {
        // Load key-value into target worker's database
        auto& db = target_worker_ptr->GetDataShard().GetDatabase();

        // Calculate TTL
        int64_t ttl_ms = -1;
        if (kv.expire_ms > 0) {
          auto now = absl::Now();
          auto expire_time = absl::FromUnixMillis(kv.expire_ms);
          auto ttl = expire_time - now;
          ttl_ms = absl::ToInt64Milliseconds(ttl);

          // Skip expired keys
          if (ttl_ms <= 0) {
            return;
          }
        }

        // Load based on type
        switch (kv.type) {
          case persistence::RDB_TYPE_STRING:
            db.Set(kv.key, kv.value);
            if (ttl_ms > 0) {
              db.SetExpireMs(kv.key, ttl_ms);
            }
            break;
          case persistence::RDB_TYPE_HASH:
            // TODO: Parse field-value pairs from serialized value
            ASTRADB_LOG_WARN(
                "PersistenceManager: Hash type not fully supported yet for key "
                "{}",
                kv.key);
            break;
          case persistence::RDB_TYPE_LIST:
            db.LPush(kv.key, kv.value);  // Simplified: single value
            if (ttl_ms > 0) {
              db.SetExpireMs(kv.key, ttl_ms);
            }
            break;
          case persistence::RDB_TYPE_SET:
            db.SAdd(kv.key, kv.value);  // Simplified: single member
            if (ttl_ms > 0) {
              db.SetExpireMs(kv.key, ttl_ms);
            }
            break;
          case persistence::RDB_TYPE_ZSET:
            db.ZAdd(kv.key, 1.0, kv.value);  // Simplified: score=1.0
            if (ttl_ms > 0) {
              db.SetExpireMs(kv.key, ttl_ms);
            }
            break;
          default:
            ASTRADB_LOG_WARN("PersistenceManager: Unknown type {} for key {}",
                             static_cast<int>(kv.type), kv.key);
            break;
        }
      }
    };

    bool success = rdb_reader_.Load(load_callback);
    if (success) {
      ASTRADB_LOG_INFO("PersistenceManager: RDB load completed");
    } else {
      ASTRADB_LOG_ERROR("PersistenceManager: RDB load failed");
    }

    return success;
  }

 private:
  // Background thread to process commands and write to AOF
  void ProcessCommands() {
    ASTRADB_LOG_DEBUG("PersistenceManager: Command processing thread started");

    while (running_.load(std::memory_order_acquire)) {
      // Process commands in batch
      const size_t kBatchSize = 100;
      std::string rcmd;
      size_t processed = 0;

      while (processed < kBatchSize && command_queue_.try_dequeue(rcmd)) {
        aof_writer_.Append(rcmd);
        rcmd.clear();
        processed++;
      }

      // Flush if we processed commands
      if (processed > 0) {
        aof_writer_.Flush();
      }

      // Wait for new commands or shutdown signal
      if (processed == 0) {
        // Use condition variable to wait efficiently instead of sleeping
        absl::MutexLock lock(&command_mutex_);
        command_cv_.Wait(&command_mutex_);
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

  // RDB reader
  persistence::RdbReader rdb_reader_;

  // RDB options
  persistence::RdbOptions options_;

  // MPSC queue for commands from all workers
  moodycamel::ConcurrentQueue<std::string> command_queue_;

  // Condition variable and mutex for efficient waiting
  absl::CondVar command_cv_;
  absl::Mutex command_mutex_;

  // Background thread for AOF processing
  std::thread processing_thread_;

  // Background RDB save state
  std::atomic<bool> bg_save_in_progress_{false};
  std::thread bg_save_thread_;
};

// Note: ClusterManager has been moved to astra/cluster/cluster_manager.hpp
// This stub has been removed to avoid naming conflicts

// Metrics Manager - Prometheus metrics collection and HTTP server
// Uses prometheus-cpp built-in Exposer for NO SHARING architecture
// Metrics Manager - Prometheus metrics collection with custom ASIO HTTP server
// Simplified HTTP server for NO SHARING architecture
// Metrics Manager - Prometheus metrics collection with custom ASIO HTTP server
// Simplified HTTP server for NO SHARING architecture
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

      // Initialize AstraMetrics (creates Prometheus metrics)
      astra::metrics::AstraMetrics::Instance().Init(config);

      // Create dedicated io_context for metrics HTTP server
      metrics_io_context_ = std::make_unique<asio::io_context>();

      // Start HTTP server in background thread
      running_ = true;
      metrics_thread_ = std::thread([this, bind_addr, config]() {
        ASTRADB_LOG_INFO("MetricsManager: Starting HTTP server thread");

        // Start HTTP server
        astra::metrics::MetricsRegistry::Instance().StartHTTPServer(
            *metrics_io_context_, config);

        // Run io_context
        metrics_io_context_->run();

        ASTRADB_LOG_DEBUG("MetricsManager: HTTP server thread exited");
      });

      // Start periodic update thread
      update_thread_ = std::thread([this]() {
        ASTRADB_LOG_DEBUG("MetricsManager: Starting periodic update thread");

        auto start_time = std::chrono::steady_clock::now();
        while (running_.load()) {
          std::this_thread::sleep_for(std::chrono::seconds(1));

          if (!running_.load()) break;

          // Update uptime
          auto now = std::chrono::steady_clock::now();
          auto uptime =
              std::chrono::duration_cast<std::chrono::seconds>(now - start_time)
                  .count();
          astra::server::ServerStatsAccessor::Instance()
              .GetStats()
              ->uptime_seconds.store(uptime, std::memory_order_relaxed);

          // Sync ServerStats to Prometheus
          astra::metrics::AstraMetrics::Instance().UpdateFromServerStats();

          // Update memory usage (placeholder)
          // TODO: Implement actual memory tracking
          // astra::metrics::AstraMetrics::Instance().SetMemoryUsed(...);
        }

        ASTRADB_LOG_DEBUG("MetricsManager: Periodic update thread exited");
      });

      ASTRADB_LOG_INFO(
          "MetricsManager: Initialized successfully with custom HTTP server");
      initialized_ = true;
      return true;
    } catch (const std::exception& e) {
      ASTRADB_LOG_ERROR("MetricsManager: Init exception: {}", e.what());
      running_ = false;
      return false;
    }
  }

  void Shutdown() {
    if (!running_.exchange(false)) {
      return;
    }

    ASTRADB_LOG_INFO("MetricsManager: Shutting down");

    // Stop HTTP server first
    astra::metrics::MetricsRegistry::Instance().StopHTTPServer();

    // Stop io_context
    if (metrics_io_context_) {
      metrics_io_context_->stop();
    }

    // Wait for HTTP server thread
    if (metrics_thread_.joinable()) {
      metrics_thread_.join();
    }

    // Wait for periodic update thread
    if (update_thread_.joinable()) {
      update_thread_.join();
    }

    ASTRADB_LOG_INFO("MetricsManager: Shutdown complete");
  }

  bool IsInitialized() const { return initialized_; }

 private:
  bool initialized_ = false;
  std::atomic<bool> running_{false};

  // Dedicated io_context for metrics HTTP server
  std::unique_ptr<asio::io_context> metrics_io_context_;

  // Thread for running metrics io_context
  std::thread metrics_thread_;

  // Thread for periodic updates
  std::thread update_thread_;
};

}  // namespace astra::server
