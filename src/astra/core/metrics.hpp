// ==============================================================================
// Metrics - Prometheus Integration
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include <prometheus/counter.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>
#include <prometheus/registry.h>
#include <prometheus/exposer.h>

#include <absl/container/flat_hash_map.h>
#include <absl/strings/string_view.h>
#include <absl/time/time.h>

#include <memory>
#include <string>
#include <mutex>

#include "metrics_flatbuffers.hpp"

namespace astra::metrics {

// Metrics configuration
struct MetricsConfig {
  bool enabled = true;
  std::string bind_addr = "0.0.0.0";
  uint16_t port = 9090;  // Prometheus metrics port
  std::string endpoint = "/metrics";
};

// Singleton metrics registry wrapper
class MetricsRegistry {
 public:
  static MetricsRegistry& Instance() {
    static MetricsRegistry instance;
    return instance;
  }

  // Initialize metrics exposer
  bool Init(const MetricsConfig& config) {
    if (initialized_.load(std::memory_order_acquire)) {
      return true;
    }

    if (!config.enabled) {
      initialized_.store(true, std::memory_order_release);
      return true;
    }

    try {
      exposer_ = std::make_unique<prometheus::Exposer>(config.bind_addr, config.port);
      exposer_->RegisterCollectable(registry_);
      initialized_.store(true, std::memory_order_release);
      return true;
    } catch (const std::exception& e) {
      return false;
    }
  }

  // Get the prometheus registry
  std::shared_ptr<prometheus::Registry> GetRegistry() const { return registry_; }

  // Counter operations
  prometheus::Counter& GetCounter(const std::string& name, 
                                   const std::map<std::string, std::string>& labels = {}) {
    auto& family = prometheus::BuildCounter()
        .Name(name)
        .Register(*registry_);
    return family.Add(labels);
  }

  // Gauge operations
  prometheus::Gauge& GetGauge(const std::string& name,
                               const std::map<std::string, std::string>& labels = {}) {
    auto& family = prometheus::BuildGauge()
        .Name(name)
        .Register(*registry_);
    return family.Add(labels);
  }

  // Histogram operations
  prometheus::Histogram& GetHistogram(const std::string& name,
                                       const prometheus::Histogram::BucketBoundaries& buckets,
                                       const std::map<std::string, std::string>& labels = {}) {
    auto& family = prometheus::BuildHistogram()
        .Name(name)
        .Register(*registry_);
    return family.Add(labels, buckets);
  }

 private:
  MetricsRegistry() : registry_(std::make_shared<prometheus::Registry>()) {}
  ~MetricsRegistry() = default;

  MetricsRegistry(const MetricsRegistry&) = delete;
  MetricsRegistry& operator=(const MetricsRegistry&) = delete;

  std::shared_ptr<prometheus::Registry> registry_;
  std::unique_ptr<prometheus::Exposer> exposer_;
  std::atomic<bool> initialized_{false};
};

// Pre-defined metrics for AstraDB
class AstraMetrics {
 public:
  static AstraMetrics& Instance() {
    static AstraMetrics instance;
    return instance;
  }

  void Init(const MetricsConfig& config) {
    config_ = config;
    MetricsRegistry::Instance().Init(config);
    
    if (!config.enabled) {
      return;
    }

    // Initialize command metrics
    auto& registry = MetricsRegistry::Instance();
    
    // Command counter: astradb_commands_total{command="GET", status="success"}
    command_counter_ = &prometheus::BuildCounter()
        .Name("astradb_commands_total")
        .Help("Total number of commands processed")
        .Register(*registry.GetRegistry());

    // Command duration histogram buckets
    duration_buckets_ = {0.0001, 0.0005, 0.001, 0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0};
    command_duration_ = &prometheus::BuildHistogram()
        .Name("astradb_command_duration_seconds")
        .Help("Command execution duration in seconds")
        .Register(*registry.GetRegistry());

    // Connections gauge
    connections_ = &prometheus::BuildGauge()
        .Name("astradb_connections")
        .Help("Current number of connections")
        .Register(*registry.GetRegistry());
    connections_current_ = &connections_->Add({});

    // Memory gauge
    memory_ = &prometheus::BuildGauge()
        .Name("astradb_memory_bytes")
        .Help("Memory usage in bytes")
        .Register(*registry.GetRegistry());
    memory_used_ = &memory_->Add({{"type", "used"}});
    memory_total_ = &memory_->Add({{"type", "total"}});

    // Keys gauge
    keys_ = &prometheus::BuildGauge()
        .Name("astradb_keys_total")
        .Help("Total number of keys per database")
        .Register(*registry.GetRegistry());

    // Cluster info
    cluster_info_ = &prometheus::BuildGauge()
        .Name("astradb_cluster_info")
        .Help("Cluster information")
        .Register(*registry.GetRegistry());

    // Persistence info
    persistence_info_ = &prometheus::BuildGauge()
        .Name("astradb_persistence_info")
        .Help("Persistence information")
        .Register(*registry.GetRegistry());

    initialized_ = true;
  }

  // Record command execution
  void RecordCommand(absl::string_view command, bool success, double duration_seconds) {
    if (!initialized_ || !config_.enabled) return;

    std::string cmd(command);
    std::string status = success ? "success" : "error";
    
    command_counter_->Add({{"command", cmd}, {"status", status}}).Increment();
    command_duration_->Add({{"command", cmd}}, duration_buckets_).Observe(duration_seconds);
  }

  // Connection tracking
  void IncrementConnections() {
    if (!initialized_ || !config_.enabled) return;
    connections_current_->Increment();
  }

  void DecrementConnections() {
    if (!initialized_ || !config_.enabled) return;
    connections_current_->Decrement();
  }

  // Memory tracking
  void SetMemoryUsed(double bytes) {
    if (!initialized_ || !config_.enabled) return;
    memory_used_->Set(bytes);
  }

  void SetMemoryTotal(double bytes) {
    if (!initialized_ || !config_.enabled) return;
    memory_total_->Set(bytes);
  }

  // Keys tracking
  void SetKeys(int db_index, double count) {
    if (!initialized_ || !config_.enabled) return;
    keys_->Add({{"db", absl::StrCat(db_index)}}).Set(count);
  }

  // Cluster status
  void SetClusterEnabled(bool enabled) {
    if (!initialized_ || !config_.enabled) return;
    cluster_info_->Add({{"enabled", enabled ? "true" : "false"}}).Set(enabled ? 1 : 0);
  }

  void SetClusterNodes(double count) {
    if (!initialized_ || !config_.enabled) return;
    cluster_info_->Add({{"metric", "nodes"}}).Set(count);
  }

  void SetClusterSlotsOwned(double count) {
    if (!initialized_ || !config_.enabled) return;
    cluster_info_->Add({{"metric", "slots_owned"}}).Set(count);
  }

  // Persistence status
  void SetPersistenceEnabled(bool enabled) {
    if (!initialized_ || !config_.enabled) return;
    persistence_info_->Add({{"enabled", enabled ? "true" : "false"}}).Set(enabled ? 1 : 0);
  }

  void SetAofSize(double bytes) {
    if (!initialized_ || !config_.enabled) return;
    persistence_info_->Add({{"type", "aof_size"}}).Set(bytes);
  }

  void SetRdbLastSave(double timestamp) {
    if (!initialized_ || !config_.enabled) return;
    persistence_info_->Add({{"type", "rdb_last_save"}}).Set(timestamp);
  }

  // Export metrics to FlatBuffers format
  std::vector<uint8_t> ExportToFlatBuffers(const std::string& host = "localhost",
                                             const std::string& instance = "astradb",
                                             const std::string& job = "astradb") {
    if (!initialized_ || !config_.enabled) {
      return {};
    }

    std::vector<std::vector<uint8_t>> metrics_data;
    
    // Export connections metric
    if (connections_current_) {
      auto conn_data = core::MetricsSerializer::SerializeGaugeMetric(
        "astradb_connections",
        connections_current_->Value(),
        {},
        "Current number of connections");
      metrics_data.push_back(conn_data);
    }
    
    // Export memory metrics
    if (memory_used_ && memory_total_) {
      auto mem_used_data = core::MetricsSerializer::SerializeGaugeMetric(
        "astradb_memory_bytes",
        memory_used_->Value(),
        {{"type", "used"}},
        "Memory usage in bytes");
      metrics_data.push_back(mem_used_data);
      
      auto mem_total_data = core::MetricsSerializer::SerializeGaugeMetric(
        "astradb_memory_bytes",
        memory_total_->Value(),
        {{"type", "total"}},
        "Memory usage in bytes");
      metrics_data.push_back(mem_total_data);
    }
    
    // Export keys metric
    if (keys_) {
      // Get current keys count for each database
      for (int db = 0; db < 16; db++) {
        // This would need to be tracked separately
        // For now, just create placeholder
      }
    }
    
    // Export cluster metrics
    if (cluster_info_) {
      auto cluster_enabled_data = core::MetricsSerializer::SerializeGaugeMetric(
        "astradb_cluster_enabled",
        0,  // Placeholder - needs to be tracked
        {},
        "Cluster enabled status");
      metrics_data.push_back(cluster_enabled_data);
    }
    
    // Export persistence metrics
    if (persistence_info_) {
      auto aof_size_data = core::MetricsSerializer::SerializeGaugeMetric(
        "astradb_aof_size_bytes",
        0,  // Placeholder - needs to be tracked
        {{"type", "aof_size"}},
        "AOF file size in bytes");
      metrics_data.push_back(aof_size_data);
      
      auto rdb_save_data = core::MetricsSerializer::SerializeGaugeMetric(
        "astradb_rdb_last_save_timestamp",
        0,  // Placeholder - needs to be tracked
        {{"type", "rdb_last_save"}},
        "Last RDB save timestamp");
      metrics_data.push_back(rdb_save_data);
    }
    
    // Create batch
    return core::MetricsSerializer::SerializeMetricsBatch(metrics_data, host, instance, job);
  }

 private:
  AstraMetrics() = default;
  ~AstraMetrics() = default;

  AstraMetrics(const AstraMetrics&) = delete;
  AstraMetrics& operator=(const AstraMetrics&) = delete;

  MetricsConfig config_;
  bool initialized_ = false;

  prometheus::Histogram::BucketBoundaries duration_buckets_;
  prometheus::Family<prometheus::Counter>* command_counter_ = nullptr;
  prometheus::Family<prometheus::Histogram>* command_duration_ = nullptr;
  prometheus::Family<prometheus::Gauge>* connections_ = nullptr;
  prometheus::Gauge* connections_current_ = nullptr;
  prometheus::Family<prometheus::Gauge>* memory_ = nullptr;
  prometheus::Gauge* memory_used_ = nullptr;
  prometheus::Gauge* memory_total_ = nullptr;
  prometheus::Family<prometheus::Gauge>* keys_ = nullptr;
  prometheus::Family<prometheus::Gauge>* cluster_info_ = nullptr;
  prometheus::Family<prometheus::Gauge>* persistence_info_ = nullptr;
};

// RAII timer for command duration
class CommandTimer {
 public:
  explicit CommandTimer(absl::string_view command)
      : command_(command), start_(absl::Now()) {}

  ~CommandTimer() {
    auto duration = absl::Now() - start_;
    double seconds = absl::ToDoubleSeconds(duration);
    AstraMetrics::Instance().RecordCommand(command_, true, seconds);
  }

  void SetError() { success_ = false; }

 private:
  std::string command_;
  absl::Time start_;
  bool success_ = true;
};

}  // namespace astra::metrics
