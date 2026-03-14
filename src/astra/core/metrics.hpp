// ==============================================================================
// Metrics - Prometheus Integration
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include <absl/container/flat_hash_map.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/string_view.h>
#include <absl/time/time.h>
#include <prometheus/counter.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>
#include <prometheus/registry.h>

#include <asio.hpp>
#include <memory>
#include <mutex>
#include <string>

#include "astra/base/logging.hpp"
#include "astra/core/server_stats.hpp"
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

    // Store enabled state atomically
    enabled_.store(config.enabled, std::memory_order_release);

    if (!config.enabled) {
      ASTRADB_LOG_INFO("Prometheus metrics disabled");
      initialized_.store(true, std::memory_order_release);
      return true;
    }

    try {
      ASTRADB_LOG_INFO("Initializing Prometheus metrics exposer on {}:{}",
                       config.bind_addr, config.port);
      // Note: HTTP server will be started separately with io_context
      initialized_.store(true, std::memory_order_release);
      ASTRADB_LOG_INFO("Prometheus metrics exposer ready");
      return true;
    } catch (const std::exception& e) {
      ASTRADB_LOG_ERROR("Failed to initialize Prometheus metrics: {}",
                        e.what());
      return false;
    }
  }

  // Start HTTP server (must be called with io_context)
  void StartHTTPServer(asio::io_context& io_context,
                       const MetricsConfig& config);

  // Get the prometheus registry
  std::shared_ptr<prometheus::Registry> GetRegistry() const {
    return registry_;
  }

  // Counter operations
  prometheus::Counter& GetCounter(
      const std::string& name,
      const std::map<std::string, std::string>& labels = {}) {
    auto& family = prometheus::BuildCounter().Name(name).Register(*registry_);
    return family.Add(labels);
  }

  // Gauge operations
  prometheus::Gauge& GetGauge(
      const std::string& name,
      const std::map<std::string, std::string>& labels = {}) {
    auto& family = prometheus::BuildGauge().Name(name).Register(*registry_);
    return family.Add(labels);
  }

  // Histogram operations
  prometheus::Histogram& GetHistogram(
      const std::string& name,
      const prometheus::Histogram::BucketBoundaries& buckets,
      const std::map<std::string, std::string>& labels = {}) {
    auto& family = prometheus::BuildHistogram().Name(name).Register(*registry_);
    return family.Add(labels, buckets);
  }

 private:
  MetricsRegistry() : registry_(std::make_shared<prometheus::Registry>()) {}
  ~MetricsRegistry() = default;

  // Helper to check if metrics are enabled (fast path for zero-cost)
  bool IsEnabled() const {
    return enabled_.load(std::memory_order_acquire);
  }

  MetricsRegistry(const MetricsRegistry&) = delete;
  MetricsRegistry& operator=(const MetricsRegistry&) = delete;

  friend class AstraMetrics;  // Allow AstraMetrics to access private members

  std::shared_ptr<prometheus::Registry> registry_;
  std::atomic<bool> initialized_{false};
  std::atomic<bool> enabled_{false};  // Can be toggled at runtime
};

// Pre-defined metrics for AstraDB
class AstraMetrics {
 public:
  static AstraMetrics& Instance() {
    static AstraMetrics instance;
    return instance;
  }

  // Dynamic enable/disable metrics (zero-cost when disabled)
  void Enable() {
    MetricsRegistry::Instance().enabled_.store(true, std::memory_order_release);
  }

  void Disable() {
    MetricsRegistry::Instance().enabled_.store(false, std::memory_order_release);
  }

  bool IsEnabled() const {
    return MetricsRegistry::Instance().IsEnabled();
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
    duration_buckets_ = {0.0001, 0.0005, 0.001, 0.005, 0.01, 0.025, 0.05,
                         0.1,    0.25,   0.5,   1.0,   2.5,  5.0,   10.0};
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

    // Server uptime
    uptime_ = &prometheus::BuildGauge()
                   .Name("astradb_uptime_seconds")
                   .Help("Server uptime in seconds")
                   .Register(*registry.GetRegistry());
    uptime_current_ = &uptime_->Add({});

    // Total connections
    total_connections_ = &prometheus::BuildCounter()
                             .Name("astradb_connections_received_total")
                             .Help("Total connections received")
                             .Register(*registry.GetRegistry());
    total_connections_received_ = &total_connections_->Add({});

    // Total commands
    total_commands_ = &prometheus::BuildCounter()
                          .Name("astradb_commands_total_all")
                          .Help("Total commands processed")
                          .Register(*registry.GetRegistry());
    total_commands_processed_ = &total_commands_->Add({});

    // Keys total
    keys_total_ = &prometheus::BuildGauge()
                      .Name("astradb_keys_total")
                      .Help("Total number of keys")
                      .Register(*registry.GetRegistry());
    keys_total_current_ = &keys_total_->Add({});

    // Network traffic
    net_input_bytes_ = &prometheus::BuildCounter()
                            .Name("astradb_network_input_bytes_total")
                            .Help("Total network input bytes")
                            .Register(*registry.GetRegistry());
    net_input_bytes_current_ = &net_input_bytes_->Add({});

    net_output_bytes_ = &prometheus::BuildCounter()
                             .Name("astradb_network_output_bytes_total")
                             .Help("Total network output bytes")
                             .Register(*registry.GetRegistry());
    net_output_bytes_current_ = &net_output_bytes_->Add({});

    // Slow queries
    slowlog_count_ = &prometheus::BuildCounter()
                         .Name("astradb_slowlog_count_total")
                         .Help("Total number of slow queries (> 10ms)")
                         .Register(*registry.GetRegistry());
    slowlog_count_current_ = &slowlog_count_->Add({});

    // Keyspace stats
    keyspace_hits_ = &prometheus::BuildCounter()
                          .Name("astradb_keyspace_hits_total")
                          .Help("Total keyspace hits")
                          .Register(*registry.GetRegistry());
    keyspace_hits_current_ = &keyspace_hits_->Add({});

    keyspace_misses_ = &prometheus::BuildCounter()
                            .Name("astradb_keyspace_misses_total")
                            .Help("Total keyspace misses")
                            .Register(*registry.GetRegistry());
    keyspace_misses_current_ = &keyspace_misses_->Add({});

    // Error counters
    error_total_ = &prometheus::BuildCounter()
                        .Name("astradb_errors_total")
                        .Help("Total errors")
                        .Register(*registry.GetRegistry());
    error_total_current_ = &error_total_->Add({});

    error_rejected_ = &prometheus::BuildCounter()
                          .Name("astradb_errors_rejected_connections_total")
                          .Help("Total rejected connections")
                          .Register(*registry.GetRegistry());
    error_rejected_current_ = &error_rejected_->Add({});

    error_syntax_ = &prometheus::BuildCounter()
                        .Name("astradb_errors_syntax_total")
                        .Help("Total syntax errors")
                        .Register(*registry.GetRegistry());
    error_syntax_current_ = &error_syntax_->Add({});

    error_protocol_ = &prometheus::BuildCounter()
                          .Name("astradb_errors_protocol_total")
                          .Help("Total protocol errors")
                          .Register(*registry.GetRegistry());
    error_protocol_current_ = &error_protocol_->Add({});

    initialized_ = true;
  }

  // Record command execution (also updates ServerStats)
  void RecordCommand(absl::string_view command, bool success,
                     double duration_seconds) {
    // Always update ServerStats (even when Prometheus metrics are disabled)
    // ServerStats is used by INFO command and other internal purposes
    auto* stats = server::ServerStatsAccessor::Instance().GetStats();
    uint64_t usec = static_cast<uint64_t>(duration_seconds * 1000000);
    stats->RecordCommand(std::string(command), success, usec);

    // Record slow query (> 10ms)
    if (duration_seconds > 0.01) {
      stats->slowlog_count.fetch_add(1, std::memory_order_relaxed);
    }

    // Only update Prometheus metrics if enabled
    if (!IsEnabled()) {
      return;
    }

    std::string cmd(command);
    std::string status = success ? "success" : "error";

    // Update Prometheus metrics
    if (command_counter_) {
      command_counter_->Add({{"command", cmd}, {"status", status}}).Increment();
    }

    if (total_commands_processed_) {
      total_commands_processed_->Increment();
    }

    if (command_duration_) {
      command_duration_->Add({{"command", cmd}}, duration_buckets_)
          .Observe(duration_seconds);
    }
  }

  // Record network traffic
  void RecordNetworkInput(size_t bytes) {
    // Always update ServerStats
    auto* stats = server::ServerStatsAccessor::Instance().GetStats();
    stats->total_net_input_bytes.fetch_add(bytes, std::memory_order_relaxed);

    // Only update Prometheus if enabled
    if (!IsEnabled()) return;
    if (net_input_bytes_current_) {
      net_input_bytes_current_->Increment(bytes);
    }
  }

  void RecordNetworkOutput(size_t bytes) {
    // Always update ServerStats
    auto* stats = server::ServerStatsAccessor::Instance().GetStats();
    stats->total_net_output_bytes.fetch_add(bytes, std::memory_order_relaxed);

    // Only update Prometheus if enabled
    if (!IsEnabled()) return;
    if (net_output_bytes_current_) {
      net_output_bytes_current_->Increment(bytes);
    }
  }

  // Record keyspace hit/miss
  void RecordKeyspaceHit() {
    // Always update ServerStats
    auto* stats = server::ServerStatsAccessor::Instance().GetStats();
    stats->keyspace_hits.fetch_add(1, std::memory_order_relaxed);

    // Only update Prometheus if enabled
    if (!IsEnabled()) return;
    if (keyspace_hits_current_) {
      keyspace_hits_current_->Increment();
    }
  }

  void RecordKeyspaceMiss() {
    // Always update ServerStats
    auto* stats = server::ServerStatsAccessor::Instance().GetStats();
    stats->keyspace_misses.fetch_add(1, std::memory_order_relaxed);

    // Only update Prometheus if enabled
    if (!IsEnabled()) return;
    if (keyspace_misses_current_) {
      keyspace_misses_current_->Increment();
    }
  }

  // Record errors
  void RecordError(const std::string& error_type) {
    // Always update ServerStats
    auto* stats = server::ServerStatsAccessor::Instance().GetStats();
    stats->total_errors.fetch_add(1, std::memory_order_relaxed);

    if (error_type == "rejected_connection") {
      stats->error_rejected_connections.fetch_add(1, std::memory_order_relaxed);
    } else if (error_type == "syntax") {
      stats->error_syntax.fetch_add(1, std::memory_order_relaxed);
    } else if (error_type == "protocol") {
      stats->error_protocol.fetch_add(1, std::memory_order_relaxed);
    }

    // Only update Prometheus if enabled
    if (!IsEnabled()) return;

    if (error_total_current_) {
      error_total_current_->Increment();
    }

    if (error_type == "rejected_connection" && error_rejected_current_) {
      error_rejected_current_->Increment();
    } else if (error_type == "syntax" && error_syntax_current_) {
      error_syntax_current_->Increment();
    } else if (error_type == "protocol" && error_protocol_current_) {
      error_protocol_current_->Increment();
    }
  }

  // Connection tracking (also updates ServerStats)
  void IncrementConnections() {
    // Always update ServerStats
    auto* stats = server::ServerStatsAccessor::Instance().GetStats();
    stats->connected_clients.fetch_add(1, std::memory_order_relaxed);
    stats->total_connections_received.fetch_add(1, std::memory_order_relaxed);

    // Only update Prometheus if enabled
    if (!IsEnabled()) return;
    if (connections_current_) {
      connections_current_->Increment();
    }
  }

  void DecrementConnections() {
    // Always update ServerStats
    auto* stats = server::ServerStatsAccessor::Instance().GetStats();
    stats->connected_clients.fetch_sub(1, std::memory_order_relaxed);

    // Only update Prometheus if enabled
    if (!IsEnabled()) return;
    if (connections_current_) {
      connections_current_->Decrement();
    }
  }

  // Memory tracking (also updates ServerStats)
  void SetMemoryUsed(double bytes) {
    if (!IsEnabled()) return;
    memory_used_->Set(bytes);

    auto* stats = server::ServerStatsAccessor::Instance().GetStats();
    stats->used_memory_bytes.store(static_cast<uint64_t>(bytes), std::memory_order_relaxed);
  }

  void SetMemoryTotal(double bytes) {
    if (!IsEnabled()) return;
    memory_total_->Set(bytes);

    auto* stats = server::ServerStatsAccessor::Instance().GetStats();
    stats->used_memory_rss_bytes.store(static_cast<uint64_t>(bytes), std::memory_order_relaxed);
  }

  // Keys tracking (also updates ServerStats)
  void SetKeys(int db_index, double count) {
    if (!initialized_ || !config_.enabled) return;
    keys_->Add({{"db", absl::StrCat(db_index)}}).Set(count);

    auto* stats = server::ServerStatsAccessor::Instance().GetStats();
    stats->total_keys.store(static_cast<uint64_t>(count), std::memory_order_relaxed);
  }

  // Cluster status
  void SetClusterEnabled(bool enabled) {
    if (!initialized_ || !config_.enabled) return;
    cluster_info_->Add({{"enabled", enabled ? "true" : "false"}})
        .Set(enabled ? 1 : 0);
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
    persistence_info_->Add({{"enabled", enabled ? "true" : "false"}})
        .Set(enabled ? 1 : 0);
  }

  void SetAofSize(double bytes) {
    if (!initialized_ || !config_.enabled) return;
    persistence_info_->Add({{"type", "aof_size"}}).Set(bytes);
  }

  void SetRdbLastSave(double timestamp) {
    if (!initialized_ || !config_.enabled) return;
    persistence_info_->Add({{"type", "rdb_last_save"}}).Set(timestamp);
  }

  // Metrics update methods
  void UpdateMetrics();
  void UpdateMemoryMetrics();
  void UpdateKeysCount();
  void UpdateClusterMetrics();
  void UpdatePersistenceMetrics();

  // Export metrics to FlatBuffers format
  std::vector<uint8_t> ExportToFlatBuffers(
      const std::string& host = "localhost",
      const std::string& instance = "astradb",
      const std::string& job = "astradb") {
    if (!initialized_ || !config_.enabled) {
      return {};
    }

    std::vector<std::vector<uint8_t>> metrics_data;

    // Export connections metric
    if (connections_current_) {
      auto conn_data = core::MetricsSerializer::SerializeGaugeMetric(
          "astradb_connections", connections_current_->Value(), {},
          "Current number of connections");
      metrics_data.push_back(conn_data);
    }

    // Export memory metrics
    if (memory_used_ && memory_total_) {
      auto mem_used_data = core::MetricsSerializer::SerializeGaugeMetric(
          "astradb_memory_bytes", memory_used_->Value(), {{"type", "used"}},
          "Memory usage in bytes");
      metrics_data.push_back(mem_used_data);

      auto mem_total_data = core::MetricsSerializer::SerializeGaugeMetric(
          "astradb_memory_bytes", memory_total_->Value(), {{"type", "total"}},
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
          {}, "Cluster enabled status");
      metrics_data.push_back(cluster_enabled_data);
    }

    // Export persistence metrics
    if (persistence_info_) {
      auto aof_size_data = core::MetricsSerializer::SerializeGaugeMetric(
          "astradb_aof_size_bytes",
          0,  // Placeholder - needs to be tracked
          {{"type", "aof_size"}}, "AOF file size in bytes");
      metrics_data.push_back(aof_size_data);

      auto rdb_save_data = core::MetricsSerializer::SerializeGaugeMetric(
          "astradb_rdb_last_save_timestamp",
          0,  // Placeholder - needs to be tracked
          {{"type", "rdb_last_save"}}, "Last RDB save timestamp");
      metrics_data.push_back(rdb_save_data);
    }

    // Create batch
    return core::MetricsSerializer::SerializeMetricsBatch(metrics_data, host,
                                                          instance, job);
  }

  // Update Prometheus metrics from ServerStats (call this periodically)
  void UpdateFromServerStats() {
    if (!IsEnabled()) return;

    auto* stats = server::ServerStatsAccessor::Instance().GetStats();

    // Update uptime
    if (uptime_current_) {
      uptime_current_->Set(stats->uptime_seconds.load(std::memory_order_relaxed));
    }

    // Update connections
    if (connections_current_) {
      connections_current_->Set(stats->connected_clients.load(std::memory_order_relaxed));
    }
    if (total_connections_received_) {
      total_connections_received_->Increment(
          stats->total_connections_received.load(std::memory_order_relaxed));
    }

    // Update memory
    if (memory_used_) {
      memory_used_->Set(stats->used_memory_bytes.load(std::memory_order_relaxed));
    }
    if (memory_total_) {
      memory_total_->Set(stats->used_memory_rss_bytes.load(std::memory_order_relaxed));
    }

    // Update keys
    if (keys_total_current_) {
      keys_total_current_->Set(stats->total_keys.load(std::memory_order_relaxed));
    }

    // Update total commands (NO SHARING architecture: from aggregated ServerStats)
    if (total_commands_processed_) {
      auto current_value = stats->total_commands_processed.load(std::memory_order_relaxed);
      // Increment by the difference to support aggregation
      total_commands_processed_->Increment(current_value - last_commands_processed_);
      last_commands_processed_ = current_value;
    }

    // Update cluster info

    // Update cluster info
    if (cluster_info_) {
      cluster_info_->Add({{"enabled", stats->cluster_enabled.load() ? "true" : "false"}})
          .Set(stats->cluster_enabled.load() ? 1 : 0);
      cluster_info_->Add({{"metric", "slots_assigned"}})
          .Set(stats->cluster_slots_assigned.load(std::memory_order_relaxed));
    }

    // Update persistence info
    if (persistence_info_) {
      persistence_info_->Add({{"aof_enabled", stats->aof_enabled.load() ? "true" : "false"}})
          .Set(stats->aof_enabled.load() ? 1 : 0);
      persistence_info_->Add({{"rdb_enabled", stats->rdb_enabled.load() ? "true" : "false"}})
          .Set(stats->rdb_enabled.load() ? 1 : 0);
      persistence_info_->Add({{"type", "aof_size"}})
          .Set(stats->aof_size_bytes.load(std::memory_order_relaxed));
      persistence_info_->Add({{"type", "rdb_last_save"}})
          .Set(stats->rdb_last_save_time.load(std::memory_order_relaxed));
    }
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
  prometheus::Family<prometheus::Gauge>* uptime_ = nullptr;
  prometheus::Gauge* uptime_current_ = nullptr;
  prometheus::Family<prometheus::Counter>* total_connections_ = nullptr;
  prometheus::Counter* total_connections_received_ = nullptr;
  prometheus::Family<prometheus::Counter>* total_commands_ = nullptr;
  prometheus::Counter* total_commands_processed_ = nullptr;
  prometheus::Family<prometheus::Gauge>* keys_total_ = nullptr;
  prometheus::Gauge* keys_total_current_ = nullptr;

  // Network traffic
  prometheus::Family<prometheus::Counter>* net_input_bytes_ = nullptr;
  prometheus::Counter* net_input_bytes_current_ = nullptr;
  prometheus::Family<prometheus::Counter>* net_output_bytes_ = nullptr;
  prometheus::Counter* net_output_bytes_current_ = nullptr;

  // Slow queries
  prometheus::Family<prometheus::Counter>* slowlog_count_ = nullptr;
  prometheus::Counter* slowlog_count_current_ = nullptr;

  // Keyspace stats
  prometheus::Family<prometheus::Counter>* keyspace_hits_ = nullptr;
  prometheus::Counter* keyspace_hits_current_ = nullptr;
  prometheus::Family<prometheus::Counter>* keyspace_misses_ = nullptr;
  prometheus::Counter* keyspace_misses_current_ = nullptr;

  // Error counters
  prometheus::Family<prometheus::Counter>* error_total_ = nullptr;
  prometheus::Counter* error_total_current_ = nullptr;
  prometheus::Family<prometheus::Counter>* error_rejected_ = nullptr;
  prometheus::Counter* error_rejected_current_ = nullptr;
  prometheus::Family<prometheus::Counter>* error_syntax_ = nullptr;
  prometheus::Counter* error_syntax_current_ = nullptr;
  prometheus::Family<prometheus::Counter>* error_protocol_ = nullptr;
  prometheus::Counter* error_protocol_current_ = nullptr;

  // Track last command count for incremental updates (NO SHARING architecture)
  uint64_t last_commands_processed_ = 0;
};

}  // namespace astra::metrics
