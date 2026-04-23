# AstraDB Performance Analysis & Optimization

## Executive Summary

This document captures performance analysis, profiling results, and optimization decisions for AstraDB.

**Current baseline (2026-04-23)**: we use one consistent benchmark method, `redis-benchmark -t set,get`, and compare pipeline levels (`-P`) under the same server/config to avoid split-test bias.

---

## Performance Benchmarks

### Test Environment (Current Baseline)
- **OS**: Native Linux (Ubuntu 25.04)
- **Binary**: `build-linux-release-debuginfo-noasan/bin/astradb`
- **Config**: `config/astradb-benchmark.toml` (`thread_count=2`, `num_shards=2`, persistence disabled)
- **Compiler**: Clang 19.1, C++23, optimized build
- **Client**: `redis-benchmark`
- **Benchmark mode**: mixed commands in one run (`-t set,get`)

### Latest Results (2026-04-23, `-n 1000000 -c 256`)

| Pipeline (`-P`) | SET QPS | GET QPS |
|-----------------|---------|---------|
| **1** | 209,424.08 | 209,117.52 |
| **16** | 1,381,215.50 | 1,805,054.12 |
| **64** | 1,612,903.25 | 2,145,922.75 |

Repeatability (same machine/config):
- `P=1`: SET `210,837.02`, GET `209,511.84`
- `P=64`: SET `1,600,000.00`, GET `2,127,659.50`

### Methodology Rules

1. Always run `set,get` together in one benchmark process.
2. Keep server binary and config fixed when comparing `-P` values.
3. Report both SET and GET from the same run output.
4. Treat historical WSL results as archived only, not release baselines.

---

## Network Performance Optimization (2026-03-20)

### Problem Statement

Initial performance showed:
- Multi-connection QPS limited to 50K-70K (vs target 150K+)
- Single connection QPS limited to ~300 (vs Redis 109K)
- CLOSE_WAIT connections causing memory leaks

### Root Causes Identified

1. **1ms Timer Bottleneck**
   - `ProcessResponseQueue()` triggered every 1ms
   - Limited single connection QPS to ~1000
   - Solution: Dynamic timer (0.1ms when queue has data, 1ms when empty)

2. **CLOSE_WAIT Leaks**
   - Connection read loop terminated without closing socket
   - Connections not removed from `connections_` map
   - Solution: Added `on_close_callback_` to properly cleanup

3. **Inefficient Batch Sending**
   - Initial implementation was serial sending (not true batch)
   - String concatenation using `+=` caused reallocations
   - Solution: Merge all responses, use `append()` for zero reallocation

### Optimizations Implemented

#### 1. TCP_NODELAY (Disable Nagle's Algorithm)

```cpp
// Connection constructor
asio::ip::tcp::no_delay option(true);
socket_.set_option(option);
```

**Impact**: Reduced latency for small packets (like Redis protocol)

#### 2. True Batch Response Sending

**Before** (Serial):
```cpp
for (const auto& response : responses) {
  co_await conn->Send(response);  // Multiple system calls
}
```

**After** (Merged):
```cpp
std::string batch;
batch.reserve(total_size);
for (const auto& response : responses) {
  batch.append(response);  // Zero reallocation
}
co_await conn->Send(batch);  // Single system call
```

**Impact**: Reduced system calls from N to 1 per batch

#### 3. Dynamic Timer Interval

**Before** (Fixed 1ms):
```cpp
response_timer_.expires_after(std::chrono::milliseconds(1));
```

**After** (Dynamic):
```cpp
if (!conn_responses.empty()) {
  response_timer_.expires_after(std::chrono::microseconds(100));
} else {
  response_timer_.expires_after(std::chrono::milliseconds(1));
}
```

**Impact**: 
- Single connection QPS: 300 → 9,700 (**32x improvement**)
- Multi connection QPS: 50K → 216K (**4.3x improvement**)

#### 4. Connection Cleanup

**Before** (Leak):
```cpp
~Connection() {
  ASTRADB_LOG_DEBUG("Connection destroyed");
}
```

**After** (Proper cleanup):
```cpp
~Connection() {
  Close();  // Close socket to prevent CLOSE_WAIT
}

// In DoRead() when connection closes
if (on_close_callback_) {
  on_close_callback_(conn_id_);  // Remove from map
}
```

**Impact**: Eliminated CLOSE_WAIT leaks

### Performance Evolution

| Phase | SET QPS | GET QPS | Issues |
|-------|---------|---------|---------|
| Initial | ~50K | ~50K | High CPU (204%), 100ms latency |
| + CPU Optimization | ~50K | ~50K | CPU 4.8%, still 100ms latency |
| + Notification | ~50K | ~50K | Latency eliminated |
| + TCP_NODELAY | ~50K | ~50K | No improvement yet |
| + Batch Sending | ~50K | ~50K | Serial sending issue |
| + True Batch | ~50K | ~50K | CLOSE_WAIT leaks |
| + Connection Cleanup | ~54K | ~54K | Clean, but still slow |
| + Dynamic Timer | **216K** | **220K** | ✅ **Goal Achieved!** |

### Configuration for Optimal Performance

```toml
[server]
thread_count = 2  # 2 workers for optimal performance
use_per_worker_io = true
use_so_reuseport = true

[performance]
enable_pipeline = true
```

### Lessons Learned

1. **CLOSE_WAIT is a silent killer**
   - Always close sockets when connection terminates
   - Remove connections from tracking maps
   - Monitor with `ss -antp | grep CLOSE-WAIT`

2. **Batch sending must be true batch**
   - Merging responses is essential
   - Single system call is key
   - Use `append()` not `+=` for strings

3. **Timer interval matters**
   - 1ms timer limits QPS to ~1000
   - Dynamic timer adapts to load
   - Balance performance vs CPU usage

4. **Testing methodology**
   - Test both single and multi connection
   - Monitor for leaks
   - Compare with Redis baseline

---

## Architectural Evolution

### Initial Architecture (Problematic)

```
┌─────────────────────────────────────────────────────────┐
│                    Client Request                     │
└────────────────────┬────────────────────────────────┘
                     │
                     ▼
          ┌────────────────────────┐
          │  Connection Handler   │
          └────────────┬───────────┘
                       │
                       ▼
          ┌────────────────────────┐
          │  Shard Router        │
          └────────────┬───────────┘
                       │
                       ▼
          ┌────────────────────────┐
          │  Thread Pool          │
          │  ┌──────────────────┐  │
          │  │ ConcurrentQueue   │  │
          │  │ + Worker Threads │  │
          │  └──────────────────┘  │
          └────────────┬───────────┘
                       │
                       ▼
          ┌────────────────────────┐
          │  Command Execution  │
          └────────────────────────┘
```

**Problems**:
- Task queue overhead (std::function construction, atomic operations)
- Worker thread management overhead
- Context switching between threads
- Unnecessary complexity for the architecture

### Current Architecture (Optimized)

```
┌─────────────────────────────────────────────────────────┐
│                    Client Request                     │
└────────────────────┬────────────────────────────────┘
                     │
                     ▼
          ┌────────────────────────┐
          │  Connection Handler   │
          └────────────┬───────────┘
                       │
                       ▼
          ┌────────────────────────┐
          │  Shard Router        │
          └────────────┬───────────┘
                       │
                       ▼
          ┌────────────────────────┐
          │  asio::post()        │
          │  (Direct to IO Context)│
          └────────────┬───────────┘
                       │
                       ▼
          ┌────────────────────────┐
          │  Command Execution  │
          └────────────────────────┘
```

**Benefits**:
- Zero task queue overhead
- No thread switching (runs in event loop)
- Simplified codebase
- Better performance in WSL environment

---

## Performance Analysis

### Methodology

We use `redis-benchmark` in mixed mode (`set,get`) to measure throughput:

```bash
# Non-pipeline baseline
redis-benchmark -h 127.0.0.1 -p 6379 -t set,get -n 1000000 -c 256 -P 1 -q

# Pipeline comparison points
redis-benchmark -h 127.0.0.1 -p 6379 -t set,get -n 1000000 -c 256 -P 16 -q
redis-benchmark -h 127.0.0.1 -p 6379 -t set,get -n 1000000 -c 256 -P 64 -q
```

### Key Findings

1. **Task Scheduling is NOT the Bottleneck**
   - Both TBB+concurrentqueue and direct asio::post performed identically
   - Lock-free queues don't help in WSL environment (virtualization overhead dominates)

2. **Scaling is Limited in WSL**
   - Only 1.64x improvement from 1 to 8 threads
   - Expected: ~8x in native Linux
   - Actual: 1.64x (shows WSL virtualization overhead)

3. **Comparison with DragonflyDB**
   - AstraDB slightly outperforms DragonflyDB in our tests
   - Confirms our implementation is solid
   - Both are limited by WSL environment

### Bottleneck Analysis

Using `strace`, we identified the system call overhead distribution:

| System Call | Percentage | Time (s) |
|-------------|------------|---------|
| `futex` | 35.45% | 2.12s |
| `recvfrom` | 34.86% | 2.09s |
| `sendto` | 22.13% | 1.32s |
| `epoll_wait` | 7.54% | 0.45s |

**Interpretation**:
- Network I/O accounts for ~60% of time
- Lock contention (futex) accounts for ~35% of time
- Only ~7.5% is actual processing time

---

## Dependencies Added

### Intel TBB (Threading Building Blocks)
- **Version**: v1.0.4
- **Purpose**: ConcurrentQueue for lock-free task queue
- **Usage**: Integrated but ultimately not used in final design

### moodycamel/concurrentqueue
- **Version**: v1.0.4
- **Purpose**: Alternative lock-free queue implementation
- **Usage**: Integrated but ultimately not used in final design

### Abseil
- **Purpose**: Efficient data structures (flat_hash_map, strings, containers)
- **Impact**: Significant improvement in hash table performance

---

## Configuration

### astradb.toml (Current Production Config)

```toml
[server]
host = "0.0.0.0"
port = 6379
max_connections = 10000
thread_count = 2  # 2 workers for optimal performance

# Network Architecture
use_per_worker_io = true
use_so_reuseport = true

[database]
num_databases = 16
num_shards = 16

[logging]
level = "error"  # Minimal logging for production
file = "astradb.log"
async = true
queue_size = 8192

[performance]
enable_pipeline = true
enable_compression = false

[memory]
max_memory = 2147483648  # 2GB
eviction_policy = "2q"
eviction_threshold = 0.9
enable_tracking = true
```

### Recommended Settings for Different Scenarios

**High Performance (Production)**:
```toml
thread_count = 2
use_per_worker_io = true
use_so_reuseport = true
logging.level = "error"
```

**Development**:
```toml
thread_count = 1
use_per_worker_io = true
use_so_reuseport = false
logging.level = "trace"
```

**Single Connection Testing**:
```toml
thread_count = 1
use_per_worker_io = false
use_so_reuseport = false
```

---

## Lessons Learned

### 1. Don't Optimize What Isn't Broken
- Task scheduling optimization didn't improve performance
- The bottleneck was elsewhere (WSL virtualization)
- Simple architecture often beats complex solutions

### 2. Environment Matters
- WSL2 has significant virtualization overhead
- Network I/O performance is ~2-3x worse than native Linux
- Thread switching is more expensive in virtualized environments

### 3. Complexity vs Performance
- Complex thread pool didn't provide benefits
- Simpler direct post architecture is faster and more maintainable
- "Premature optimization is the root of all evil"

### 4. Benchmark Against Baselines
- DragonflyDB served as an excellent baseline
- Performance parity confirmed our implementation quality
- Without DragonflyDB comparison, we would have pursued wrong optimizations

---

## Future Optimization Directions

### High Priority (Based on Analysis)

1. **Network I/O Optimization**
   - Implement TCP_CORK for batched sending
   - Increase TCP buffer sizes
   - Consider io_uring if available (Linux 5.1+)

2. **Command Batching**
   - Pipeline support (already enabled in config)
   - Multi-command parsing
   - Batch response construction

3. **Zero-Copy Buffers**
   - Reduce memory copies in request/response path
   - Use shared buffers where possible

### Medium Priority

4. **NUMA Optimization**
   - CPU affinity for worker threads
   - Memory locality optimizations
   - Thread-local caches

5. **Connection Pool**
   - Reuse connection objects
   - Reduce connection setup overhead

### Low Priority (Environment Dependent)

6. **Work-Stealing Thread Pool**
   - May help in native Linux with many cores
   - Unlikely to help in WSL environment

7. **MCS Locks**
   - Replace standard mutexes if needed
   - Only beneficial with high contention

8. **Thread-Local Caching**
   - Cache frequently accessed data
   - Reduce shared memory access

---

## Memory Management Performance (Updated 2026-03-20)

### Memory Tracking Optimization

AstraDB implements advanced memory tracking and eviction strategies inspired by DragonflyDB's design:

#### Performance Optimizations

| Optimization | Impact | Implementation |
|--------------|--------|----------------|
| **Background Monitoring** | ~90% CPU reduction | EvictionMonitor runs every 100ms in background thread |
| **Sampling Estimation** | ~80% CPU reduction | Randomly sample 100 keys for memory estimation |
| **80% Check Threshold** | ~50% check reduction | Only check eviction when memory ≥ 80% of limit |
| **2Q Algorithm** | ~10-15% better hit rate | Dragonfly-style dual-buffer design |

#### Memory Usage Tracking

```cpp
// Exact calculation (keys ≤ sample_size)
if (all_keys.size() <= sample_size_) {
  for (const auto& key : all_keys) {
    total += get_key_size(key);
  }
}

// Sampling estimation (keys > sample_size)
for (size_t i = 0; i < sample_size_; ++i) {
  size_t idx = static_cast<size_t>(dist_(rng_) * all_keys.size());
  sample_total += get_key_size(all_keys[idx]);
}
double avg_size = static_cast<double>(sample_total) / sample_size_;
return static_cast<uint64_t>(avg_size * all_keys.size());
```

#### 2Q Algorithm Performance

The 2Q algorithm (probationary + protected buffers) provides better cache hit rates than traditional LRU:

```
Probationary Buffer (6.7%): FIFO for new keys
  ↓ (accessed once)
Protected Buffer (93.3%): LRU for accessed keys
```

**Performance Characteristics**:
- O(1) time complexity for access
- O(1) time complexity for eviction
- Zero additional metadata overhead (uses existing key metadata)
- Higher hit rate than LRU in most workloads

#### Configuration Example

```toml
[memory]
max_memory = 1073741824        # 1GB (0 = no limit)
eviction_policy = "2q"         # Recommended: 2Q algorithm
eviction_threshold = 0.9       # Trigger eviction at 90% of max_memory
eviction_samples = 5           # Number of samples for LRU/LFU
enable_tracking = true         # Enable memory tracking
```

#### Performance Comparison

| Strategy | Hit Rate | CPU Overhead | Memory Overhead |
|----------|----------|--------------|-----------------|
| LRU | 60-70% | High | Low |
| LFU | 65-75% | High | Low |
| **2Q** | **75-85%** | **Low** | **Zero** |

For detailed information, see `DOCS/eviction-strategy-optimization.md`.

### Global Memory Tracking in NO SHARING Architecture

In NO SHARING architecture, each worker has independent memory tracking, but eviction uses global memory:

```
Worker 0                Worker 1
  ├─ MemoryTracker 0      ├─ MemoryTracker 1
  ├─ Database 0          ├─ Database 1
  └─ Keys: 49            └─ Keys: 51

Global Memory Check:
  Total = 20000 + 20000 = 40000
  Max = 1048576
  Threshold = 943718 (90%)
  Should Evict = No
```

**Implementation**:
```cpp
// Get total memory across all workers
core::memory::GetTotalMemoryCallback get_total_memory_callback;
get_total_memory_callback = [this]() -> size_t {
  size_t total_memory = 0;
  for (auto& worker : workers_) {
    total_memory += worker->GetDataShard().GetMemoryTracker()->GetCurrentMemory();
  }
  return total_memory;
};
```

### Memory Usage Best Practices

1. **Set Appropriate Memory Limits**
   - Don't set too low (frequent eviction)
   - Don't set too high (risk of OOM)
   - Monitor eviction rate with Prometheus

2. **Choose Right Eviction Policy**
   - Use `2q` for best overall performance
   - Use `volatile-*` policies if TTL is important
   - Use `noeviction` only if you have infinite memory

3. **Monitor Memory Metrics**
   ```bash
   # Check current memory usage
   INFO memory
   
   # Monitor eviction rate
   # Metrics: eviction_key_total, eviction_operation_total
   ```

4. **Avoid Memory Leaks**
   - All keys with TTL are automatically cleaned up
   - Eviction prevents unbounded memory growth
   - Use memory tracking to identify issues

---

## Build Instructions

### Release Build (Stripped, Optimized)

```bash
cd build-release
cmake -DCMAKE_BUILD_TYPE=Release ..
ninja
```

### RelWithDebInfo Build (Profiling)

```bash
cd build-release-debuginfo
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS="-O2 -g -fno-omit-frame-pointer" ..
ninja
```

### Running

```bash
# Start server
./build-linux-release-debuginfo-noasan/bin/astradb --config config/astradb-benchmark.toml

# Run mixed benchmark
redis-benchmark -h 127.0.0.1 -p 6379 -t set,get -n 1000000 -c 256 -P 16 -q
```

---

## Performance Testing Checklist

When testing performance, ensure:

- [ ] Use release build (not debug)
- [ ] Fix binary/config before comparison runs
- [ ] Run `set,get` in one benchmark process (no split set/get runs)
- [ ] Compare multiple pipeline levels (`-P 1/16/64`)
- [ ] Report both SET and GET from the same output
- [ ] Profile with perf if bottlenecks appear
- [ ] Check CPU utilization
- [ ] Monitor memory usage

---

## Conclusion

With the current benchmark baseline, AstraDB reaches about `~209k` QPS in non-pipeline mode (`P=1`) and up to `~1.6M SET / ~2.1M GET` in pipeline mode (`P=64`) on the reference machine.

The main takeaway for optimization work is to keep measuring with consistent mixed-load methodology and focus on pipeline hot paths (batch parse, dispatch, and response write-back) where the remaining gap to Redis is concentrated.

---

## Appendix: Testing Commands

### Basic Performance Test

```bash
# Canonical mixed run
redis-benchmark -h 127.0.0.1 -p 6379 -t set,get -n 1000000 -c 256 -P 1 -q
redis-benchmark -h 127.0.0.1 -p 6379 -t set,get -n 1000000 -c 256 -P 16 -q
redis-benchmark -h 127.0.0.1 -p 6379 -t set,get -n 1000000 -c 256 -P 64 -q
```

### Profiling

```bash
# System call profiling
strace -c -T redis-benchmark -h 127.0.0.1 -p 6379 -t set,get -n 1000000 -c 256 -P 16

# Futex contention analysis
strace -f -e trace=futex -p <pid> 2>&1 | grep futex
```

### Comparison Testing

```bash
# DragonflyDB
cd ~/codespace/dragonfly/bin
./dragonfly-x86_64 &
redis-benchmark -h 127.0.0.1 -p 6379 -t set,get -n 1000000 -c 256 -P 16 -q

# Redis
redis-server &
redis-benchmark -h 127.0.0.1 -p 6379 -t set,get -n 1000000 -c 256 -P 16 -q
```

---

**Document Version**: 2.1  
**Last Updated**: 2026-04-23  
**Tested Environment**: Ubuntu 25.04 (native Linux), Clang 19.1, C++23, persistence disabled
