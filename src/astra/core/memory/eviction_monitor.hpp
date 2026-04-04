// Eviction Monitor - Background thread for periodic memory checks
// ==============================================================================
// Based on Dragonfly's cache design for better performance
// ==============================================================================
#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>

#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "eviction_manager.hpp"
#include "memory_tracker.hpp"

namespace astra::core::memory {

// Eviction monitor configuration
struct EvictionMonitorConfig {
  std::chrono::milliseconds check_interval =
      std::chrono::milliseconds(100);  // Check every 100ms
  bool enable_sampling = true;         // Enable sampling for memory estimation
  size_t sample_size = 100;  // Number of keys to sample for estimation
  double probationary_ratio =
      0.067;  // 6.7% for probationary buffer (like Dragonfly)
};

// Eviction monitor - runs in background thread and periodically checks memory
class EvictionMonitor {
 public:
  EvictionMonitor(EvictionManager* eviction_manager,
                  MemoryTracker* memory_tracker,
                  const EvictionMonitorConfig& config = EvictionMonitorConfig())
      : eviction_manager_(eviction_manager),
        memory_tracker_(memory_tracker),
        config_(config),
        running_(false),
        thread_() {}

  ~EvictionMonitor() { Stop(); }

  // Disable copy and move
  EvictionMonitor(const EvictionMonitor&) = delete;
  EvictionMonitor& operator=(const EvictionMonitor&) = delete;
  EvictionMonitor(EvictionMonitor&&) = delete;
  EvictionMonitor& operator=(EvictionMonitor&&) = delete;

  // Start the monitor thread
  void Start() {
    if (running_) return;

    running_ = true;
    thread_ = std::thread(&EvictionMonitor::MonitorLoop, this);
    ASTRADB_LOG_INFO("Eviction monitor started with interval {}ms",
                     config_.check_interval.count());
  }

  // Stop the monitor thread
  void Stop() {
    if (!running_) return;

    running_ = false;
    cv_.notify_all();
    if (thread_.joinable()) {
      thread_.join();
    }
    ASTRADB_LOG_INFO("Eviction monitor stopped");
  }

  // Update configuration
  void SetConfig(const EvictionMonitorConfig& config) {
    absl::MutexLock lock(&mutex_);
    config_ = config;
    cv_.notify_all();
  }

  // Get current configuration
  const EvictionMonitorConfig& GetConfig() const {
    absl::MutexLock lock(&mutex_);
    return config_;
  }

 private:
  // Monitor loop
  void MonitorLoop() {
    while (running_) {
      absl::MutexLock lock(&mutex_);

      // Wait for check interval or stop signal
      absl::Duration timeout =
          absl::Milliseconds(config_.check_interval.count());
      cv_.WaitWithTimeout(&mutex_, timeout, [this]() { return !running_; });

      if (!running_) break;

      lock.Release();

      // Check memory and perform eviction if needed
      if (memory_tracker_->ShouldEvict()) {
        ASTRADB_LOG_DEBUG(
            "Eviction monitor: memory threshold reached, performing eviction");
        eviction_manager_->CheckAndEvict();
      }
    }
  }

  EvictionManager* eviction_manager_;
  MemoryTracker* memory_tracker_;
  EvictionMonitorConfig config_;

  std::atomic<bool> running_;
  std::thread thread_;
  absl::Mutex mutex_;
  absl::CondVar cv_;
};

}  // namespace astra::core::memory
