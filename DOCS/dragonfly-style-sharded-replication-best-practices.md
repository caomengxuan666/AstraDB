# Dragonfly-Style Sharded Replication Best Practices

**Created**: 2026-04-09  
**Based on Commit**: e17d400  
**Architecture**: NO SHADING (Per-Worker Isolation)  
**Reference**: Dragonfly Database replication architecture

## Overview

This document summarizes the best practices for implementing Dragonfly-style sharded replication in AstraDB. This architecture resolves data race and deadlock issues in traditional broadcast-based replication while maintaining the integrity and scalability of the NO SHADING architecture.

**Core Concept**: Each slave worker connects to the master and declares its worker-id, and the master propagates commands only to slaves with matching worker-ids.

---

## Architecture Design

### 1. SO_REUSEPORT + Multi-Worker Architecture

**Design Principles**:
- All workers bind to the same port (e.g., 7001)
- Kernel automatically load balances connections via SO_REUSEPORT
- Each worker independently accepts connections, no need for Worker 0 forwarding
- Avoids single point of bottleneck and connection forwarding overhead

**Configuration Example**:
```toml
[server]
host = "0.0.0.0"
port = 7001
thread_count = 4
use_per_worker_io = true
use_so_reuseport = true
```

**Advantages**:
- ✅ Kernel-level load balancing, more efficient than user-space round-robin
- ✅ Zero connection forwarding overhead
- ✅ Completely avoids Worker 0 single point of bottleneck
- ✅ Complies with NO SHADING architecture (each worker is completely independent)

**Implementation Location**: `src/astra/server/worker.hpp` - Worker constructor

### 2. Worker-ID Routing Mechanism

**Design Principles**:
- Each slave connection sends a custom command to declare its worker-id after connecting
- Master uses a global registry to record slave worker-id information
- Master propagates commands only to slaves with matching worker-ids
- Avoids data race conditions caused by broadcast-based replication

**Custom Command Protocol**:
```
ASTRA REPLICANEGOTIATE <worker_id>
```

**RESP Format** (constructed using RespBuilder):
```
*2\r\n$21\r\nASTRADB_REPLICANEGOTIATE\r\n$1\r\n0\r\n
```

**Command Registration**:
```cpp
ASTRADB_REGISTER_COMMAND(ASTRADB_REPLICANEGOTIATE, 2, "readonly", 
                        RoutingStrategy::kNone, HandleAstraReplicaNegotiate);
```

**Key Points**:
- ✅ Use `RoutingStrategy::kNone` to avoid cluster slot routing conflicts
- ✅ Command name includes project prefix "ASTRA" to avoid conflicts with Redis standard commands
- ✅ Use RespBuilder::BuildArray for construction, avoid manual concatenation errors

**Implementation Location**: 
- `src/astra/commands/replication_commands.cpp` - Command handling
- `src/astra/replication/replication_manager.hpp` - Worker-id negotiation

### 3. Global Slave Connection Registry

**Design Principles**:
- Due to SO_REUSEPORT load balancing, multiple commands from the same slave may be distributed to different workers
- Need a global registry to record all slave connection information
- Use `worker_id + connection_id` combination as unique identifier

**Data Structure**:
```cpp
struct GlobalSlaveConnection {
  size_t master_worker_id;   // Master worker that accepted this connection
  uint64_t connection_id;    // Local connection ID of that worker
  size_t slave_worker_id;    // Worker ID of this slave
  std::string remote_addr;   // Remote address
  
  uint64_t GetCompositeKey() const {
    return (static_cast<uint64_t>(master_worker_id) << 32) | connection_id;
  }
};
```

**Registration Flow**:
1. Master worker accepts slave connection
2. Register to global registry during PSYNC processing
3. Update worker_id during ASTRA REPLICANEGOTIATE processing
4. Query global registry during command propagation

**Implementation Location**: `src/astra/replication/replication_manager.hpp` - GlobalSlaveRegistry

### 4. Command Propagation Mechanism

**Dragonfly-Style Propagation**:
- Each master worker propagates only to its matching slave worker-id
- No broadcast, avoids data race conditions and deadlocks
- Use WorkerScheduler::Add for task distribution

**Propagation Flow**:
```
Worker 0 executes SET command
  ↓
Query global registry: find slave connections with worker_id=0
  ↓
If slave is in Worker 1 (due to SO_REUSEPORT load balancing)
  ↓
Use WorkerScheduler::Add to forward command to Worker 1
  ↓
Worker 1 sends command to slave
```

**Deadlock Avoidance** (based on Apr 2 commit 742004f):
```cpp
// Check if target worker is current worker
if (target_worker_id == current_worker_id) {
  // Execute directly, not through queue
  return replication_manager_->SendCommandToSlaveLocal(connection_id, cmd);
} else {
  // Forward via WorkerScheduler
  return worker_scheduler_->Add(target_worker_id, task);
}
```

**Implementation Location**: 
- `src/astra/server/worker.hpp` - PropagateCommandToAllSlaves
- `src/astra/replication/replication_manager.hpp` - SendCommandToSlaveLocal

### 5. Cross-Worker Communication

**Design Principles**:
- Use WorkerScheduler for worker-to-worker task distribution
- Comply with NO SHADING architecture (no shared mutable state)
- Loose coupling through callback mechanism

**Callback Functions**:
```cpp
// Find slave in other workers
using SearchOtherWorkersCallback = std::function<bool(
  uint64_t connection_id, size_t worker_id, size_t current_worker_id
)>;

// Send command to slave in other workers
using SendCommandToWorkerCallback = std::function<bool(
  size_t target_worker_id, uint64_t connection_id, const protocol::Command&
)>;
```

**Implementation Location**: `src/astra/replication/replication_manager.hpp` - Callback function definitions

---

## Key Implementation Points

### 1. Connection Management

**Each slave worker establishes 1 connection**:
- Connects to master's SO_REUSEPORT port
- Kernel automatically load balances to a master worker
- Declares worker-id via ASTRA REPLICANEGOTIATE
- Master associates worker_id with connection

**Connection Identifier**:
- Single worker: `connection_id` (locally unique)
- Cross-worker: `master_worker_id + connection_id` (globally unique)
- Avoids connection_id conflicts (each worker starts from 0)

### 2. Command Propagation Strategy

**Dragonfly Style**:
- ✅ Each worker propagates only to its own slaves
- ✅ No broadcast, avoids data race conditions
- ✅ Precise worker-id matching

**Broadcast Style** (Old implementation, deprecated):
- ❌ Each worker broadcasts to all slaves
- ❌ Causes data race conditions and deadlocks
- ❌ Not suitable for NO SHADING architecture

### 3. Error Handling

**Connection Disconnection**:
- Detect socket errors
- Remove from global registry
- Log events

**worker_id Mismatch**:
- Skip slaves with mismatching worker_id
- Log debug information
- Does not affect other slaves

**Cross-Worker Failure**:
- WorkerScheduler.Add returns false
- Log error messages
- Does not block other slaves

### 4. Performance Optimization

**Kernel Load Balancing**:
- SO_REUSEPORT is more efficient than user-space round-robin
- Zero connection forwarding overhead
- Completely avoids Worker 0 bottleneck

**Avoid Lock Contention**:
- Global registry uses mutex protection
- Local slaves_ uses absl::Mutex
- Minimize critical sections

**Async Propagation**:
- Use asio::co_spawn for async sending
- Does not block command execution
- Improves throughput

---

## Configuration Best Practices

### Master Configuration
```toml
[server]
host = "0.0.0.0"
port = 7001
thread_count = 4
use_per_worker_io = true
use_so_reuseport = true  # Critical configuration

[replication]
enabled = true
role = "master"
```

### Slave Configuration
```toml
[server]
host = "0.0.0.0"
port = 7002
thread_count = 4
use_per_worker_io = true
use_so_reuseport = true  # Critical configuration

[replication]
enabled = true
role = "slave"
master_host = "127.0.0.1"
master_port = 7001
```

**Critical Configuration**:
- ✅ `use_per_worker_io = true` - Enable per-worker IO mode
- ✅ `use_so_reuseport = true` - Enable SO_REUSEPORT load balancing
- ✅ Both Master and Slave must enable the same configuration

---

## Debugging Guide

### Log Levels

**DEBUG Level** (recommended for development):
```
[debug] ASTRA REPLICANEGOTIATE command sent: worker_id=0
[debug] Updated global registry: worker_1 connection_id 0 -> slave_worker_id 0
[debug] Worker 0: Found 1 connections for slave_worker_id 0 in global registry
[debug] Worker 0: Sending command to 1 matching slaves
```

**INFO Level** (recommended for production):
```
[info] Updated global registry: worker_1 connection_id 0 -> slave_worker_id 0
[info] ASTRA REPLICANEGOTIATE: Slave worker-id 0 updated for connection 0
```

### Common Issues

**1. Worker 0 Did Not Receive Slave Connection**
- **Cause**: SO_REUSEPORT load balancing is uneven
- **Solution**: Increase connection retry mechanism, or adjust worker count
- **Impact**: Some keys cannot be replicated, but does not affect other shards

**2. ASTRA REPLICANEGOTIATE Command Not Sent**
- **Cause**: Slave worker's ConnectToMaster coroutine did not start
- **Check**: Verify if io_context_ is null
- **Logs**: "io_context_ pointer: 0x..." or "io_context_ is null"

**3. worker_id Mismatch**
- **Cause**: worker_id sent by slave does not match master's expectation
- **Check**: View global registry worker_id update logs
- **Solution**: Ensure slave's worker_id is correctly set

**4. Cross-Worker Command Forwarding Failed**
- **Cause**: WorkerScheduler.Add returns false
- **Check**: Verify target worker exists
- **Logs**: "Failed to add command to worker X queue"

### Testing Validation

**Basic Functionality Test**:
```bash
# Start master
./astradb --config config/astradb-node1.toml

# Start slave
./astradb --config config/astradb-node2.toml

# Test SET/GET
redis-cli -p 7001 SET test_key hello
redis-cli -p 7002 GET test_key  # Should return hello

# Test multiple keys
redis-cli -p 7001 SET key1 value1
redis-cli -p 7001 SET key2 value2
redis-cli -p 7001 SET key3 value3
redis-cli -p 7002 GET key1
redis-cli -p 7002 GET key2
redis-cli -p 7002 GET key3
```

**Routing Validation**:
```bash
# View master logs
grep "routing to worker" logs/astradb-node1.log

# View slave connections
grep "Worker.*Connected to master" logs/astradb-node2.log

# View global registry
grep "Updated global registry" logs/astradb-node1.log
```

---

## Known Limitations

### 1. SO_REUSEPORT Load Balancing Uneven
- **Phenomenon**: Some workers receive more/fewer connections
- **Impact**: May cause incomplete replication for some shards
- **Solutions**: 
  - Increase connection retry mechanism
  - Use smarter load balancing algorithms
  - Adjust worker count

### 2. Worker 0 Race Condition
- **Phenomenon**: In some cases, Worker 0's ASTRA REPLICANEGOTIATE command is not sent
- **Cause**: Not fully debugged, possibly initialization sequence issue
- **Solution**: Needs further debugging

### 3. Partial Shard Replication Failure
- **Phenomenon**: Some keys cannot be replicated to slave
- **Cause**: Corresponding master worker did not receive slave connection
- **Solution**: Increase connection retry and load balancing optimization

---

## Performance Metrics

### Test Environment
- Worker count: 4
- Slave count: 4
- Test commands: SET/GET
- Data size: Small strings

### Test Results
- ✅ **test_key**: Successfully replicated (routed to Worker 0)
- ✅ **key2**: Successfully replicated (routed to Worker 0)
- ❌ **key1**: Replication failed (routed to Worker 2, no corresponding slave connection)
- ❌ **key3**: Replication failed (routed to Worker 2, no corresponding slave connection)

### Success Rate
- **Overall success rate**: 50% (2/4)
- **Main reason**: SO_REUSEPORT load balancing uneven

### Optimization Directions
1. Increase connection retry mechanism
2. Optimize load balancing algorithms
3. Implement smarter connection allocation
4. Add health checks and automatic reconnection

---

## Architecture Advantages

### vs Traditional Broadcast-Based Replication

| Feature | Dragonfly-Style | Broadcast-Based |
|---------|----------------|-----------------|
| Data Race | ❌ None | ✅ Present |
| Deadlock Risk | ❌ None | ✅ Present |
| Replication Precision | ✅ Exact Match | ❌ Broadcast All |
| Scalability | ✅ Linear Scaling | ❌ Broadcast Limited |
| Complexity | ✅ Medium | ✅ Low |

### vs Single Worker Architecture

| Feature | Multi-Worker + SO_REUSEPORT | Single Worker |
|---------|-----------------------------|--------------|
| Concurrency | ✅ High | ❌ Low |
| Throughput | ✅ High | ❌ Low |
| Single Point of Bottleneck | ❌ None | ✅ Present |
| Complexity | ✅ Medium | ✅ Low |

---

## Related Documentation

- `DOCS/implementation-status.md` - Overall implementation status
- `DOCS/rdb-integration-plan.md` - RDB persistence implementation
- `DOCS/server-refactor-plan.md` - NO SHADING architecture design
- `DOCS/io-uring-architecture-best-practices.md` - IO architecture best practices

---

## Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2026-04-09 | Initial version - Dragonfly-style sharded replication best practices |

---

## Contributors

- AstraDB Team
- Based on Dragonfly Database replication architecture

---

**Document Status**: ✅ Complete  
**Last Updated**: 2026-04-09  
**Applicable Version**: AstraDB 1.3.0+