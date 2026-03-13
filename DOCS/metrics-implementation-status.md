# Metrics Implementation Status

## Overview
This document tracks the implementation status of the metrics feature for AstraDB.

## Completed Features

### 1. Infrastructure ✅
- [x] Unified `ServerStats` structure in `src/astra/core/server_stats.hpp`
- [x] `AstraMetrics` class for Prometheus metrics
- [x] Custom ASIO HTTP server (CivetWeb disabled for WSL2 compatibility)
- [x] Coroutine-based HTTP server implementation
- [x] CMake configuration with prometheus-cpp::core only (no pull/push)

### 2. Metrics Collection ✅
- [x] Connection count tracking (via `Connection` lifecycle)
- [x] Command execution tracking (via `CommandTimer` RAII)
- [x] Server uptime metrics
- [x] Memory usage metrics (placeholders)
- [x] Keys count metrics (placeholders)
- [x] Command duration histogram

### 3. Architecture ✅
- [x] NO SHARING compliant design
- [x] Thread-safe implementation (atomic operations + mutexes)
- [x] Dual output support (Prometheus HTTP + Redis INFO)
- [x] Configurable via `MetricsConfig.enabled`

## Current Issues

### Issue 1: Command Count Not Working ❌
**Symptom**:
- `astradb_commands_total_all` always shows 0
- `astradb_commands_total{command="PING",status="success"}` not appearing in metrics output
- `astradb_commands_received_total` always shows 0

**Root Cause**: Under investigation

**Affected Code**:
- `src/astra/server/worker.hpp` - `CommandTimer` usage
- `src/astra/core/metrics.hpp` - `RecordCommand()` method

**Investigation Points**:
- [ ] Check if `CommandTimer` destructor is being called
- [ ] Verify `AstraMetrics::Instance().Init()` was called before commands
- [ ] Check if `command_counter_` pointer is initialized
- [ ] Verify `initialized_` and `config_.enabled` flags
- [ ] Add debug logging to `RecordCommand()` method

### Issue 2: Metrics Not Updated from ServerStats ⚠️
**Symptom**:
- Metrics values are not synchronized from `ServerStats` to Prometheus
- `UpdateFromServerStats()` method exists but may not be called

**Solution**: Need to call `UpdateFromServerStats()` periodically (e.g., every second)

### Issue 3: Memory Usage Not Tracked ⚠️
**Symptom**:
- `astradb_memory_bytes{type="used"}` and `astradb_memory_bytes{type="total"}` always 0

**Solution**: Need to implement memory usage collection and call `SetMemoryUsed()`/`SetMemoryTotal()`

## Prometheus Metrics

### Working ✅
```
# HELP astradb_connections Current number of connections
# TYPE astradb_connections gauge
astradb_connections 2

# HELP astradb_uptime_seconds Server uptime in seconds
# TYPE astradb_uptime_seconds gauge
astradb_uptime_seconds 0

# HELP astradb_keys_total Total number of keys
# TYPE astradb_keys_total gauge
astradb_keys_total 0
```

### Not Working ❌
```
# HELP astradb_commands_total Total number of commands processed
# TYPE astradb_commands_total counter
# Missing from output!

# HELP astradb_command_duration_seconds Command execution duration in seconds
# TYPE astradb_command_duration_seconds histogram
# Missing from output!

# HELP astradb_commands_total_all Total commands processed
# TYPE astradb_commands_total_all counter
astradb_commands_total_all 0  # Should increase when commands are executed

# HELP astradb_connections_received_total Total connections received
# TYPE astradb_connections_received_total counter
astradb_connections_received_total 0  # Should increase when connections are created
```

## Implementation Notes

### Design Decisions
1. **RAII Pattern**: `CommandTimer` automatically records command execution time and status
2. **Minimal Intrusion**: Only 1 line of code needed to track commands: `CommandTimer timer(cmd.command.name);`
3. **Dual Output**: Same `ServerStats` structure supports both Prometheus and Redis INFO
4. **Thread Safety**: All stats use `std::atomic` or `absl::Mutex` for thread safety

### Architecture
```
┌─────────────────────────────────────────────────────────┐
│ Metrics Flow                                            │
├─────────────────────────────────────────────────────────┤
│                                                         │
│  Worker::ExecutorLoop()                                 │
│       │                                                 │
│       ├── CommandTimer timer(cmd)  ← RAII             │
│       │                                                 │
│       └── data_shard_.Execute(cmd)                     │
│                 │                                       │
│                 └── (timer destructor)                 │
│                     │                                   │
│                     ├── AstraMetrics::RecordCommand()  │
│                     │                                   │
│                     ├── Update Prometheus metrics      │
│                     │                                   │
│                     └── Update ServerStats            │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

## Next Steps

### High Priority
1. **Fix command count tracking** - Investigate why `RecordCommand()` is not updating metrics
2. **Add periodic update loop** - Call `UpdateFromServerStats()` every second
3. **Implement memory tracking** - Add system memory usage collection

### Medium Priority
4. **Implement INFO command** - Add `INFO stats`, `INFO memory`, `INFO clients` sections
5. **Add QPS calculation** - Track commands per second
6. **Add latency percentiles** - P50, P95, P99 command latency

### Low Priority
7. **Add persistence metrics** - AOF/RDB size and save time
8. **Add cluster metrics** - Cluster slots, nodes, replication status
9. **Add command stats** - Per-command success/failure rates

## Testing

### Manual Testing Steps
1. Start server: `./build-linux-release-clang/bin/astradb --config astradb.toml`
2. Connect: `nc localhost 6379`
3. Send commands: `PING`, `SET key value`, `GET key`
4. Check metrics: `curl http://localhost:9999/metrics`
5. Verify connection count increases
6. Verify command count increases

### Expected Behavior
```
# Before commands
astradb_connections 0
astradb_commands_total_all 0

# After 1 connection + 3 commands
astradb_connections 1
astradb_commands_total_all 3
astradb_commands_total{command="PING",status="success"} 1
astradb_commands_total{command="SET",status="success"} 1
astradb_commands_total{command="GET",status="success"} 1
```

## Dependencies
- prometheus-cpp 1.2.4 (core library only)
- ASIO (HTTP server)
- Abseil (atomic operations, logging)
- CMake 3.31+

## Configuration
```toml
[metrics]
enabled = true
bind_addr = "0.0.0.0"
port = 9999
```

## Files Modified
- `cmake/Dependencies.cmake` - prometheus-cpp configuration (ENABLE_PULL=OFF)
- `src/astra/core/metrics.hpp` - metrics infrastructure
- `src/astra/core/metrics_http.cpp` - HTTP server implementation
- `src/astra/core/server_stats.hpp` - unified stats structure
- `src/astra/server/managers.hpp` - MetricsManager
- `src/astra/server/worker.hpp` - CommandTimer integration
- `src/astra/commands/CMakeLists.txt` - link prometheus-cpp::core

## Version
- Created: 2026-03-14
- Status: Alpha (basic infrastructure working, issues identified)
- Next Review: After fixing command count tracking