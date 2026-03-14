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
  std::atomic<uint64_t> uptime_seconds{0};           // Server uptime
  std::atomic<uint64_t> start_time{0};               // Server start time (Unix timestamp)

  // ===== Client Information =====
  std::atomic<uint64_t> connected_clients{0};        // Current connected clients
  std::atomic<uint64_t> total_connections_received{0}; // Total connections since start
  std::atomic<uint64_t> total_connections_rejected{0}; // Total rejected connections

  // ===== Memory Information =====
  std::atomic<uint64_t> used_memory_bytes{0};        // Used memory in bytes
  std::atomic<uint64_t> used_memory_peak_bytes{0};   // Peak used memory
  std::atomic<uint64_t> used_memory_rss_bytes{0};    // RSS memory
  std::atomic<uint64_t> used_memory_fragmentation_ratio{0}; // Fragmentation ratio

  // ===== Command Statistics =====
  std::atomic<uint64_t> total_commands_processed{0}; // Total commands processed
  std::atomic<uint64_t> total_commands_failed{0};    // Total commands failed
  std::atomic<uint64_t> instantaneous_ops_per_sec{0}; // Current ops/sec
  std::atomic<uint64_t> slowlog_count{0};            // Number of slow queries (> 10ms)

  // ===== Keys Information =====
  std::atomic<uint64_t> total_keys{0};               // Total keys across all databases
  std::atomic<uint64_t> keys_with_expiration{0};     // Keys with TTL
  std::atomic<uint64_t> keyspace_hits{0};            // Keyspace hits (GET key found)
  std::atomic<uint64_t> keyspace_misses{0};          // Keyspace misses (GET key not found)

  // ===== Network Statistics =====
  std::atomic<uint64_t> total_net_input_bytes{0};    // Total network input bytes
  std::atomic<uint64_t> total_net_output_bytes{0};   // Total network output bytes
  std::atomic<uint64_t> instantaneous_input_kbps{0}; // Current input Kbps
  std::atomic<uint64_t> instantaneous_output_kbps{0}; // Current output Kbps

  // ===== Error Statistics =====
  std::atomic<uint64_t> total_errors{0};             // Total errors
  std::atomic<uint64_t> error_rejected_connections{0}; // Rejected connections
  std::atomic<uint64_t> error_syntax{0};              // Syntax errors
  std::atomic<uint64_t> error_protocol{0};            // Protocol errors

  // ===== Persistence Information =====
  std::atomic<uint64_t> aof_size_bytes{0};           // AOF file size
  std::atomic<uint64_t> aof_rewrite_time_seconds{0}; // Last AOF rewrite duration
  std::atomic<uint64_t> rdb_last_save_time{0};       // Last RDB save time (Unix timestamp)
  std::atomic<uint64_t> rdb_last_save_duration_ms{0}; // Last RDB save duration
  std::atomic<bool> aof_enabled{false};              // AOF enabled flag
  std::atomic<bool> rdb_enabled{false};              // RDB enabled flag

  // ===== Cluster Information =====
  std::atomic<bool> cluster_enabled{false};          // Cluster enabled flag
  std::atomic<uint64_t> cluster_slots_assigned{0};   // Assigned slots
  std::atomic<uint64_t> cluster_slots_ok{0};         // Slots in OK state
  std::atomic<uint64_t> cluster_slots_pfail{0};      // Slots in probable fail state
  std::atomic<uint64_t> cluster_slots_fail{0};       // Slots in fail state

  // ===== Replication Information =====
  std::atomic<bool> role_master{true};               // Role: master (true) / replica (false)
  std::atomic<uint64_t> connected_replicas{0};       // Connected replicas
  std::atomic<uint64_t> master_repl_offset{0};       // Master replication offset
  std::atomic<uint64_t> master_repl_backlog_active{0}; // Is backlog active
  std::atomic<uint64_t> master_repl_backlog_size{0};  // Replication backlog size

  // ===== Command Counters (per command type) =====
  struct CommandStats {
    std::atomic<uint64_t> calls{0};
    std::atomic<uint64_t> errors{0};
    std::atomic<uint64_t> usec{0}; // Total microseconds spent
  };

  // Thread-safe access to command stats
  absl::Mutex command_stats_mutex_;
  absl::flat_hash_map<std::string, std::unique_ptr<CommandStats>> command_stats_;

  // Get or create command stats
  CommandStats* GetCommandStats(const std::string& command) {
    absl::MutexLock lock(&command_stats_mutex_);
    auto& stats = command_stats_[command];
    if (!stats) {
      stats = std::make_unique<CommandStats>();
    }
    return stats.get();
  }

  // Record command execution
  void RecordCommand(const std::string& command, bool success, uint64_t usec) {
    total_commands_processed.fetch_add(1, std::memory_order_relaxed);
    if (!success) {
      total_commands_failed.fetch_add(1, std::memory_order_relaxed);
    }

    auto* stats = GetCommandStats(command);
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
      result += "cmdstat_" + cmd + ":calls=" + std::to_string(stats->calls.load()) +
                ",usec=" + std::to_string(stats->usec.load());
      if (stats->errors.load() > 0) {
        result += ",rejected_calls=" + std::to_string(stats->errors.load());
      }
      result += "\r\n";
    }
    return result;
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