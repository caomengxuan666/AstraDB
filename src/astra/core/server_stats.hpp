// ==============================================================================
// Server Statistics - Unified stats for Prometheus and Redis INFO
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include <absl/container/flat_hash_map.h>
#include <absl/synchronization/mutex.h>

#include <atomic>
#include <string>

namespace astra::server {

// Server statistics (thread-safe, shared by Prometheus and INFO command)
struct ServerStats {
  // ===== Server Information =====
  std::atomic<uint64_t> uptime_seconds{0};  // Server uptime
  std::atomic<uint64_t> start_time{0};  // Server start time (Unix timestamp)

  // ===== Client Information =====
  std::atomic<uint64_t> connected_clients{0};  // Current connected clients
  std::atomic<uint64_t> total_connections_received{
      0};  // Total connections since start
  std::atomic<uint64_t> total_connections_rejected{
      0};  // Total rejected connections

  // ===== Memory Information =====
  std::atomic<uint64_t> used_memory_bytes{0};       // Used memory in bytes
  std::atomic<uint64_t> used_memory_peak_bytes{0};  // Peak used memory
  std::atomic<uint64_t> used_memory_rss_bytes{0};   // RSS memory
  std::atomic<uint64_t> used_memory_fragmentation_ratio{
      0};  // Fragmentation ratio

  // ===== Command Statistics =====
  std::atomic<uint64_t> total_commands_processed{
      0};                                          // Total commands processed
  std::atomic<uint64_t> total_commands_failed{0};  // Total commands failed
  std::atomic<uint64_t> instantaneous_ops_per_sec{0};  // Current ops/sec
  std::atomic<uint64_t> slowlog_count{0};  // Number of slow queries (> 10ms)

  // ===== Keys Information =====
  std::atomic<uint64_t> total_keys{0};  // Total keys across all databases
  std::atomic<uint64_t> keys_with_expiration{0};  // Keys with TTL
  std::atomic<uint64_t> keyspace_hits{0};  // Keyspace hits (GET key found)
  std::atomic<uint64_t> keyspace_misses{
      0};  // Keyspace misses (GET key not found)

  // ===== Network Statistics =====
  std::atomic<uint64_t> total_net_input_bytes{0};  // Total network input bytes
  std::atomic<uint64_t> total_net_output_bytes{
      0};  // Total network output bytes
  std::atomic<uint64_t> instantaneous_input_kbps{0};   // Current input Kbps
  std::atomic<uint64_t> instantaneous_output_kbps{0};  // Current output Kbps

  // ===== Error Statistics =====
  std::atomic<uint64_t> total_errors{0};                // Total errors
  std::atomic<uint64_t> error_rejected_connections{0};  // Rejected connections
  std::atomic<uint64_t> error_syntax{0};                // Syntax errors
  std::atomic<uint64_t> error_protocol{0};              // Protocol errors

  // ===== Persistence Information =====
  std::atomic<uint64_t> aof_size_bytes{0};  // AOF file size
  std::atomic<uint64_t> aof_rewrite_time_seconds{
      0};  // Last AOF rewrite duration
  std::atomic<uint64_t> rdb_last_save_time{
      0};  // Last RDB save time (Unix timestamp)
  std::atomic<uint64_t> rdb_last_save_duration_ms{0};  // Last RDB save duration
  std::atomic<bool> aof_enabled{false};                // AOF enabled flag
  std::atomic<bool> rdb_enabled{false};                // RDB enabled flag

  // ===== Cluster Information =====
  std::atomic<bool> cluster_enabled{false};         // Cluster enabled flag
  std::atomic<uint64_t> cluster_slots_assigned{0};  // Assigned slots
  std::atomic<uint64_t> cluster_slots_ok{0};        // Slots in OK state
  std::atomic<uint64_t> cluster_slots_pfail{0};  // Slots in probable fail state
  std::atomic<uint64_t> cluster_slots_fail{0};   // Slots in fail state

  // ===== Replication Information =====
  std::atomic<bool> role_master{true};  // Role: master (true) / replica (false)
  std::atomic<uint64_t> connected_replicas{0};  // Connected replicas
  std::atomic<uint64_t> master_repl_offset{0};  // Master replication offset
  std::atomic<uint64_t> master_repl_backlog_active{0};  // Is backlog active
  std::atomic<uint64_t> master_repl_backlog_size{
      0};  // Replication backlog size

  // ===== Command Counters (per command type) =====
  struct CommandStats {
    std::atomic<uint64_t> calls{0};
    std::atomic<uint64_t> errors{0};
    std::atomic<uint64_t> usec{0};  // Total microseconds spent
  };

  // Thread-safe access to command stats
  absl::Mutex command_stats_mutex_;
  absl::flat_hash_map<std::string, std::unique_ptr<CommandStats>>
      command_stats_;

  // Get or create command stats (with global lock - performance bottleneck!)
  // TODO: This violates NO SHARING architecture - should be per-worker
  CommandStats* GetCommandStats(const std::string& command) {
    absl::MutexLock lock(&command_stats_mutex_);
    auto& stats = command_stats_[command];
    if (!stats) {
      stats = std::make_unique<CommandStats>();
    }
    return stats.get();
  }

  // Fast path: record command without creating stats (lock-free)
  // Returns nullptr if stats don't exist, to avoid lock contention
  CommandStats* TryGetCommandStats(const std::string& command) {
    absl::ReaderMutexLock lock(&command_stats_mutex_);
    auto it = command_stats_.find(command);
    if (it != command_stats_.end()) {
      return it->second.get();
    }
    return nullptr;
  }

  // Record command execution
  void RecordCommand(const std::string& command, bool success, uint64_t usec) {
    total_commands_processed.fetch_add(1, std::memory_order_relaxed);
    if (!success) {
      total_commands_failed.fetch_add(1, std::memory_order_relaxed);
    }

    // Try fast path first (lock-free)
    auto* stats = TryGetCommandStats(command);
    if (!stats) {
      // Fall back to slow path with lock (only happens for new commands)
      stats = GetCommandStats(command);
    }

    stats->calls.fetch_add(1, std::memory_order_relaxed);
    stats->usec.fetch_add(usec, std::memory_order_relaxed);
    if (!success) {
      stats->errors.fetch_add(1, std::memory_order_relaxed);
    }
  }

  // Get command stats for INFO command
  std::string GetCommandStatsInfo() {
    absl::MutexLock lock(&command_stats_mutex_);
    std::string result;
    for (const auto& [cmd, stats] : command_stats_) {
      result += "cmdstat_" + cmd +
                ":calls=" + std::to_string(stats->calls.load()) +
                ",usec=" + std::to_string(stats->usec.load());
      if (stats->errors.load() > 0) {
        result += ",rejected_calls=" + std::to_string(stats->errors.load());
      }
      result += "\r\n";
    }
    return result;
  }

  // Merge stats from another ServerStats (for NO SHARING architecture)
  // This is used to aggregate per-worker stats into global stats
  void Merge(ServerStats& other) {
    // Merge all atomic counters
    uptime_seconds.store(other.uptime_seconds.load(std::memory_order_relaxed),
                         std::memory_order_relaxed);
    start_time.store(other.start_time.load(std::memory_order_relaxed),
                     std::memory_order_relaxed);
    connected_clients.fetch_add(
        other.connected_clients.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    total_connections_received.fetch_add(
        other.total_connections_received.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    total_connections_rejected.fetch_add(
        other.total_connections_rejected.load(std::memory_order_relaxed),
        std::memory_order_relaxed);

    used_memory_bytes.store(
        other.used_memory_bytes.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    used_memory_peak_bytes.store(
        other.used_memory_peak_bytes.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    used_memory_rss_bytes.store(
        other.used_memory_rss_bytes.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    used_memory_fragmentation_ratio.store(
        other.used_memory_fragmentation_ratio.load(std::memory_order_relaxed),
        std::memory_order_relaxed);

    total_commands_processed.fetch_add(
        other.total_commands_processed.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    total_commands_failed.fetch_add(
        other.total_commands_failed.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    instantaneous_ops_per_sec.fetch_add(
        other.instantaneous_ops_per_sec.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    slowlog_count.fetch_add(other.slowlog_count.load(std::memory_order_relaxed),
                            std::memory_order_relaxed);

    total_keys.fetch_add(other.total_keys.load(std::memory_order_relaxed),
                         std::memory_order_relaxed);
    keys_with_expiration.fetch_add(
        other.keys_with_expiration.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    keyspace_hits.fetch_add(other.keyspace_hits.load(std::memory_order_relaxed),
                            std::memory_order_relaxed);
    keyspace_misses.fetch_add(
        other.keyspace_misses.load(std::memory_order_relaxed),
        std::memory_order_relaxed);

    total_net_input_bytes.fetch_add(
        other.total_net_input_bytes.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    total_net_output_bytes.fetch_add(
        other.total_net_output_bytes.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    instantaneous_input_kbps.store(
        other.instantaneous_input_kbps.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    instantaneous_output_kbps.store(
        other.instantaneous_output_kbps.load(std::memory_order_relaxed),
        std::memory_order_relaxed);

    total_errors.fetch_add(other.total_errors.load(std::memory_order_relaxed),
                           std::memory_order_relaxed);
    error_rejected_connections.fetch_add(
        other.error_rejected_connections.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    error_syntax.fetch_add(other.error_syntax.load(std::memory_order_relaxed),
                           std::memory_order_relaxed);
    error_protocol.fetch_add(
        other.error_protocol.load(std::memory_order_relaxed),
        std::memory_order_relaxed);

    aof_size_bytes.store(other.aof_size_bytes.load(std::memory_order_relaxed),
                         std::memory_order_relaxed);
    aof_rewrite_time_seconds.store(
        other.aof_rewrite_time_seconds.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    rdb_last_save_time.store(
        other.rdb_last_save_time.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    rdb_last_save_duration_ms.store(
        other.rdb_last_save_duration_ms.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    aof_enabled.store(other.aof_enabled.load(std::memory_order_relaxed),
                      std::memory_order_relaxed);
    rdb_enabled.store(other.rdb_enabled.load(std::memory_order_relaxed),
                      std::memory_order_relaxed);

    cluster_enabled.store(other.cluster_enabled.load(std::memory_order_relaxed),
                          std::memory_order_relaxed);
    cluster_slots_assigned.fetch_add(
        other.cluster_slots_assigned.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    cluster_slots_ok.fetch_add(
        other.cluster_slots_ok.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    cluster_slots_pfail.fetch_add(
        other.cluster_slots_pfail.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    cluster_slots_fail.fetch_add(
        other.cluster_slots_fail.load(std::memory_order_relaxed),
        std::memory_order_relaxed);

    role_master.store(other.role_master.load(std::memory_order_relaxed),
                      std::memory_order_relaxed);
    connected_replicas.fetch_add(
        other.connected_replicas.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    master_repl_offset.fetch_add(
        other.master_repl_offset.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    master_repl_backlog_active.store(
        other.master_repl_backlog_active.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    master_repl_backlog_size.store(
        other.master_repl_backlog_size.load(std::memory_order_relaxed),
        std::memory_order_relaxed);

    // Merge command stats
    {
      absl::MutexLock lock(&command_stats_mutex_);
      absl::MutexLock lock_other(&other.command_stats_mutex_);

      for (const auto& [cmd, other_stats] : other.command_stats_) {
        auto& stats = command_stats_[cmd];
        if (!stats) {
          stats = std::make_unique<CommandStats>();
        }
        stats->calls.fetch_add(
            other_stats->calls.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        stats->errors.fetch_add(
            other_stats->errors.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        stats->usec.fetch_add(other_stats->usec.load(std::memory_order_relaxed),
                              std::memory_order_relaxed);
      }
    }
  }

  // Reset all stats to zero
  void Reset() {
    uptime_seconds.store(0, std::memory_order_relaxed);
    start_time.store(0, std::memory_order_relaxed);
    connected_clients.store(0, std::memory_order_relaxed);
    total_connections_received.store(0, std::memory_order_relaxed);
    total_connections_rejected.store(0, std::memory_order_relaxed);

    used_memory_bytes.store(0, std::memory_order_relaxed);
    used_memory_peak_bytes.store(0, std::memory_order_relaxed);
    used_memory_rss_bytes.store(0, std::memory_order_relaxed);
    used_memory_fragmentation_ratio.store(0, std::memory_order_relaxed);

    total_commands_processed.store(0, std::memory_order_relaxed);
    total_commands_failed.store(0, std::memory_order_relaxed);
    instantaneous_ops_per_sec.store(0, std::memory_order_relaxed);
    slowlog_count.store(0, std::memory_order_relaxed);

    total_keys.store(0, std::memory_order_relaxed);
    keys_with_expiration.store(0, std::memory_order_relaxed);
    keyspace_hits.store(0, std::memory_order_relaxed);
    keyspace_misses.store(0, std::memory_order_relaxed);

    total_net_input_bytes.store(0, std::memory_order_relaxed);
    total_net_output_bytes.store(0, std::memory_order_relaxed);
    instantaneous_input_kbps.store(0, std::memory_order_relaxed);
    instantaneous_output_kbps.store(0, std::memory_order_relaxed);

    total_errors.store(0, std::memory_order_relaxed);
    error_rejected_connections.store(0, std::memory_order_relaxed);
    error_syntax.store(0, std::memory_order_relaxed);
    error_protocol.store(0, std::memory_order_relaxed);

    aof_size_bytes.store(0, std::memory_order_relaxed);
    aof_rewrite_time_seconds.store(0, std::memory_order_relaxed);
    rdb_last_save_time.store(0, std::memory_order_relaxed);
    rdb_last_save_duration_ms.store(0, std::memory_order_relaxed);
    aof_enabled.store(false, std::memory_order_relaxed);
    rdb_enabled.store(false, std::memory_order_relaxed);

    cluster_enabled.store(false, std::memory_order_relaxed);
    cluster_slots_assigned.store(0, std::memory_order_relaxed);
    cluster_slots_ok.store(0, std::memory_order_relaxed);
    cluster_slots_pfail.store(0, std::memory_order_relaxed);
    cluster_slots_fail.store(0, std::memory_order_relaxed);

    role_master.store(true, std::memory_order_relaxed);
    connected_replicas.store(0, std::memory_order_relaxed);
    master_repl_offset.store(0, std::memory_order_relaxed);
    master_repl_backlog_active.store(0, std::memory_order_relaxed);
    master_repl_backlog_size.store(0, std::memory_order_relaxed);

    absl::MutexLock lock(&command_stats_mutex_);
    command_stats_.clear();
  }
};

// Global server stats accessor
class ServerStatsAccessor {
 public:
  static ServerStatsAccessor& Instance() {
    static ServerStatsAccessor instance;
    return instance;
  }

  ServerStats* GetStats() { return &stats_; }
  const ServerStats* GetStats() const { return &stats_; }

 private:
  ServerStatsAccessor() = default;
  ~ServerStatsAccessor() = default;

  ServerStatsAccessor(const ServerStatsAccessor&) = delete;
  ServerStatsAccessor& operator=(const ServerStatsAccessor&) = delete;

  ServerStats stats_;
};

}  // namespace astra::server
