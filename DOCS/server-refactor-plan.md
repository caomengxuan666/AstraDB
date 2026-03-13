# AstraDB Server Architecture Refactoring Plan

## Overview

This document describes the detailed plan for integrating existing AstraDB functionality into the new **NO SHARING architecture**.

**Important Note**: All Redis commands and functional modules have been fully implemented. The current task is to migrate them from the old shared-resource architecture to the new NO SHARING architecture.

## Current Status

### Implemented Features
- ✅ NO SHARING architecture foundation
  - Worker class (containing IO thread + Executor thread)
  - Independent io_context and acceptor for each Worker
  - SO_REUSEPORT support for kernel-level load balancing
  - Private command queues and response queues (no cross-thread communication)
- ✅ Basic connection management
  - Connection acceptance and lifecycle management
  - RESP protocol parsing
  - PING command support

### Current Limitations
- ❌ DataShard only supports PING command (placeholder implementation)
- ❌ Server configuration is simplified (hardcoded, no command-line arguments or config file support)
- ❌ Two client management commands are temporarily disabled (CLIENT LIST/CLIENT KILL)
- ❌ All existing modules not integrated into new architecture:
  - Data persistence (AOF/RDB)
  - Clustering and replication
  - Publish/Subscribe
  - Transaction support
  - ACL authentication
  - Metrics monitoring
  - Lua scripting
  - Blocking commands

## Target Architecture (NO SHARING)

### Core Principles
1. **Complete Isolation**: Each Worker is an independent "mini server"
2. **No Shared Resources**: Workers share no mutable state
3. **Private Data**: Each Worker has independent data shard (Database)
4. **Independent Queues**: Command and response queues are scoped to Worker

### Worker Architecture
```
┌─────────────────────────────────────────────────────────┐
│ Worker N                                                 │
├─────────────────────────────────────────────────────────┤
│  IO Thread                     │  Executor Thread        │
│  ┌──────────────┐             │  ┌──────────────────┐  │
│  │ Acceptor     │             │  │ Command Queue    │  │
│  │ Connections  │             │  │ Response Queue   │  │
│  │ Socket I/O   │─────────────▶│  │ Database        │  │
│  │              │  Commands   │  │ - String/Hash    │  │
│  └──────────────┘  Responses  │  │ - Set/ZSet/List  │  │
│        │                    │  │ - Stream/Bitmap   │  │
│        ▼                    │  │ - HLL/Geo        │  │
│  Response Queue             │  │ - TTL Manager     │  │
│  (Timer-based)               │  │ - Transaction    │  │
│                             │  │ - Blocking Ops    │  │
│                             │  └──────────────────┘  │
└─────────────────────────────────────────────────────────┘
```

## Content Temporarily Removed for Compilation

### 1. Server Class Simplification (server.hpp/server.cpp)

**Removed Configuration Structures**:
- `PersistenceConfig` (persistence configuration)
- `ClusterConfig` (clustering configuration)
- `MetricsConfig` (metrics configuration)
- `AclConfig` (ACL configuration)

**Removed ServerConfig Fields**:
- `thread_count` (number of threads)
- `max_connections` (maximum connections)
- `num_databases` (number of databases)
- `num_shards` (number of shards)
- `persistence` (persistence configuration)
- `cluster` (clustering configuration)
- `metrics` (metrics configuration)
- `acl` (ACL configuration)
- `use_async_commands` (async commands)

**Removed Server Class Members**:
- **Persistence Layer**: `persistence_`, `aof_writer_`, `rdb_writer_`
- **Replication Layer**: `replication_manager_`
- **ACL Layer**: `acl_manager_`
- **Cluster Management**: `cluster_shard_manager_`, `gossip_manager_`
- **Blocking Management**: `blocking_manager_`
- **Core Components**: `executor_`, `buffer_pool_`
- **IO Components**: `io_context_`, `acceptor_`, `connection_pool_`
- **Command Registry**: `registry_`
- **Pub/Sub Management**: `channel_subscribers_`, `pattern_subscribers_`, `conn_channels_`, `conn_patterns_`, `connections_`
- **Various Threads**: `io_threads_`, `expiration_cleaner_thread_`, `gossip_tick_thread_`, `aof_rewrite_thread_`, `rdb_save_thread_`

**Removed Server Class Methods**:
- `Run()` (blocking run)
- `GetConfig()`, `GetRegistry()`, `GetPersistence()`
- `GetClusterShardManager()`, `GetGossipManager()`
- `IsClusterEnabled()`, `IsPersistenceEnabled()`, `IsAofEnabled()`
- `GetAofWriter()`, `GetRdbWriter()`, `GetReplicationManager()`, `GetAclManager()`
- `GetIoContext()`, `ClusterMeet()`, `GetPubSubManager()`
- `GetConnections()`, `GetLocalShardManager()`, `GetBlockingManager()`
- `DoAccept()`, `OnAccept()`, `HandleCommand()`
- `HandleCommandAsync()`, `HandleBatchCommandsAsync()`
- `SendResponse()`, `SendBatchResponses()`
- `StartExpirationCleaner()`, `CleanupExpiredKeys()`
- `StartGossipTick()`, `GossipTickLoop()`
- `StartAofRewriteChecker()`, `AofRewriteCheckerLoop()`, `PerformAofRewrite()`
- `StartRdbSaver()`, `RdbSaverLoop()`, `PerformRdbSave()`
- `AppendToAof()`, `InitPersistence()`, `InitCluster()`

**Current Simplified Implementation**:
- Only保留 `ServerConfig` fields: `host`, `port`, `num_workers`, `use_so_reuseport`
- Only keep `workers_` (Worker pool) and `running_` (running flag)
- `Start()` method only starts Worker threads
- `Stop()` method only stops Worker threads

### 2. Main Function Simplification (main.cpp)

**Removed Features**:
- Command-line argument parsing (cxxopts)
- Configuration file loading (TOML)
- Command-line argument override of configuration
- Version information output (ASTRADB_VERSION_STRING)
- Platform information output (Platform/Architecture/Compiler)
- Feature information output (TLS/ACL/SIMD/io_uring)
- Detailed configuration output (persistence/cluster/metrics)
- `Run()` call (blocking run)

**Current Simplified Implementation**:
- Hardcoded configuration
- Only output basic configuration information
- Call `Start()` (non-blocking)
- Main thread loop waiting

### 3. Client Management Commands Simplification (client_commands.cpp)

#### 3.1 HandleClientList Command

**Removed Implementation**:
- Get all connection lists from server
- Iterate connections and build detailed information string (id, addr, name, age, idle)

**Current Placeholder Implementation**:
```cpp
// Note: Connection tracking not yet implemented in io_uring backend
// Return empty list for now
std::string result = "";
```

**Features to Restore**:
- Worker-level connection tracking
- Cross-Worker connection list aggregation
- Connection information collection (IP, port, name, age, idle time)

#### 3.2 HandleClientKill Command

**Removed Implementation**:
- Get all connection lists from server
- Parse target conditions (ID/TYPE/address)
- Iterate and close matching connections
- Count closed connections

**Current Placeholder Implementation**:
```cpp
// Note: Connection tracking not yet implemented in io_uring backend
// Return 0 (no connections killed) for now
protocol::RespValue resp;
resp.SetInteger(0);
```

**Features to Restore**:
- Worker-level connection management
- Cross-Worker connection lookup and closing
- Multiple closing condition support (ID, address, type)

## Implemented Function Modules (Need Integration)

### 1. Data Type Commands (src/astra/commands/)

**Implemented Command Files**:
- `string_commands.cpp/hpp` - String type commands
- `hash_commands.cpp/hpp` - Hash type commands
- `set_commands.cpp/hpp` - Set type commands
- `zset_commands.cpp/hpp` - ZSet type commands
- `list_commands.cpp/hpp` - List type commands
- `stream_commands.cpp/hpp` - Stream type commands
- `bitmap_commands.cpp/hpp` - Bitmap type commands
- `hyperloglog_commands.cpp/hpp` - HyperLogLog type commands
- `geospatial_commands.cpp/hpp` - Geospatial type commands
- `ttl_commands.cpp/hpp` - TTL commands
- `transaction_commands.cpp/hpp` - Transaction commands
- `admin_commands.cpp/hpp` - Administration commands
- `client_commands.cpp/hpp` - Client management commands
- `acl_commands.cpp/hpp` - ACL authentication commands
- `cluster_commands.cpp/hpp` - Cluster commands
- `replication_commands.cpp/hpp` - Replication commands
- `pubsub_commands.cpp/hpp` - Publish/Subscribe commands
- `script_commands.cpp/hpp` - Lua scripting commands

**Core Classes**:
- `database.hpp` - Database interface (contains all data types)
- `command_handler.hpp` - Command handler
- `command_registry_optimized.hpp` - Command registry
- `blocking_manager.hpp` - Blocking command manager

### 2. Persistence Module (src/astra/persistence/)

**Implemented Files**:
- `leveldb_adapter.hpp` - LevelDB adapter
- `aof_writer.hpp` - AOF writer
- `rdb_writer.hpp` - RDB snapshot writer
- `snapshot_manager.hpp` - Snapshot manager

### 3. Cluster Module (src/astra/cluster/)

**Implemented Files**:
- `cluster_manager.hpp` - Cluster manager
- `gossip_manager.hpp` - Gossip protocol manager
- `shard_manager.hpp` - Shard manager
- `cluster_flatbuffers.hpp` - Cluster message serialization

### 4. Replication Module (src/astra/replication/)

**Implemented Files**:
- `replication_manager.hpp` - Replication manager

### 5. Security Module (src/astra/security/)

**Implemented Files**:
- `acl_manager.hpp` - ACL authentication manager

### 6. Network Module (src/astra/network/)

**Implemented Files**:
- `connection.hpp` - Connection class
- `connection_pool.hpp` - Connection pool

### 7. Async Module (src/astra/core/async/)

**Implemented Files**:
- `executor.hpp` - Coroutine executor
- `future.hpp` - Future/Promise
- `thread_pool.hpp` - Thread pool
- `awaitable_ops.hpp` - Async operations

### 8. Container Module (src/astra/container/)

**Implemented Files**:
- `dash_map.hpp` - Concurrent hash map
- `dash_set.hpp` - Concurrent set
- `linked_list.hpp` - Linked list
- `stream_data.hpp` - Stream data structure
- `zset/btree_zset.hpp` - ZSet implementation

## Feature Integration Plan

### Phase 1: Core Feature Integration (Priority: High)

#### 1.1 Integrate Database into Worker
**Goal**: Integrate the complete Database class into Worker's DataShard

**Task List**:
- [ ] Modify `DataShard` class in `worker.hpp` to integrate `commands::Database`
- [ ] Create a private Database instance for each Worker
- [ ] Implement command routing mechanism (from Command to Database methods)
- [ ] Add command registry integration
- [ ] Test all basic data type commands

**Files Involved**:
- `src/astra/server/worker.hpp` (modify DataShard)
- `src/astra/commands/database.hpp` (already exists, use directly)
- `src/astra/commands/command_handler.hpp` (needs adaptation for NO SHARING)

**Challenges**:
- Database may need access to Server-level components (Pub/Sub, persistence, etc.)
- Need to design CommandContext interface for Worker to provide necessary context

#### 1.2 Restore Command-line Arguments and Configuration File Support
**Goal**: Restore complete configuration system

**Task List**:
- [ ] Restore `cxxopts` command-line argument parsing
- [ ] Restore TOML configuration file loading
- [ ] Restore all configuration options (ServerConfig)
- [ ] Restore version/platform/compiler information output
- [ ] Restore detailed configuration output

**Files Involved**:
- `src/main.cpp` (restore original logic)
- `src/astra/base/config.hpp` (already exists, may need adjustment)
- `src/astra/server/server.hpp` (restore configuration structures)

#### 1.3 Restore Client Management Commands
**Goal**: Restore CLIENT LIST and CLIENT KILL commands

**Task List**:
- [ ] Implement connection tracking in Worker
- [ ] Implement cross-Worker connection aggregation in Server
- [ ] Restore CLIENT LIST command implementation
- [ ] Restore CLIENT KILL command implementation

**Architecture Options**:

**Option A: Server-Level Connection Tracking (Recommended)**
- Each Worker periodically reports connection information to Server
- Server maintains global connection table
- CLIENT LIST/KILL queries Server's connection table
- ✅ Centralized management, simple implementation
- ⚠️ Requires periodic synchronization

**Option B: Client-Level Connection Tracking**
- Server directly holds weak_ptr to all Connections
- CLIENT LIST/KILL iterates through all Connections
- ✅ Real-time and accurate
- ⚠️ Violates NO SHARING principle

**Recommended Approach**: Option A (Server-Level Connection Tracking)

**Files Involved**:
- `src/astra/commands/client_commands.cpp` (restore implementation)
- `src/astra/server/worker.hpp` (add connection reporting)
- `src/astra/server/server.hpp` (add connection table)

---

### Phase 2: Persistence Integration (Priority: High)

#### 2.1 AOF Integration
**Goal**: Integrate AOF Writer into NO SHARING architecture

**Architecture Options**:

**Option A: Independent AOF Writer Thread (Recommended)**
- Server creates independent AOF Writer thread
- Each Worker sends write commands to AOF Writer via queue
- AOF Writer writes to disk sequentially
- ✅ Complies with NO SHARING, good performance
- ⚠️ Requires additional inter-process communication

**Option B: Independent AOF per Worker**
- Each Worker writes to its own AOF file
- Merge multiple AOFs at startup
- ✅ Complete isolation
- ⚠️ Complex file management

**Recommended Approach**: Option A (Independent AOF Writer)

**Task List**:
- [ ] Create Server-level AofManager
- [ ] Add AOF callback to Database in each Worker
- [ ] Implement AOF write queue (moodycamel::ConcurrentQueue)
- [ ] Implement AOF Writer thread
- [ ] Support AOF fsync strategies (always/everysec/no)
- [ ] Implement AOF rewrite mechanism
- [ ] AOF loading and recovery at startup

**Files Involved**:
- `src/astra/persistence/aof_writer.hpp` (already exists, needs adaptation)
- `src/astra/server/server.hpp` (add AofManager)
- `src/astra/server/worker.hpp` (add AOF callback)

#### 2.2 RDB Integration
**Goal**: Integrate RDB Writer into NO SHARING architecture

**Architecture Options**:

**Option A: Global RDB Writer (Recommended)**
- Server creates independent RDB Writer thread
- Periodically collect data snapshots from all Workers
- Write to RDB file sequentially
- ✅ Simple single-file recovery
- ⚠️ Requires pause or copy during snapshot

**Option B: Independent RDB per Worker**
- Each Worker generates its own RDB file
- Load all RDBs at startup
- ✅ Parallel snapshot generation
- ⚠️ Complex recovery logic

**Recommended Approach**: Option A (Global RDB Writer)

**Task List**:
- [ ] Create Server-level RdbManager
- [ ] Implement snapshot generation logic (support fork/copy)
- [ ] Collect data from all Workers
- [ ] Periodic snapshot triggering
- [ ] Manual SAVE/BGSAVE commands
- [ ] RDB loading and recovery at startup

**Files Involved**:
- `src/astra/persistence/rdb_writer.hpp` (already exists, needs adaptation)
- `src/astra/server/server.hpp` (add RdbManager)

---

### Phase 3: Publish/Subscribe Integration (Priority: Medium)

#### 3.1 Pub/Sub Integration
**Goal**: Integrate Pub/Sub functionality into NO SHARING architecture

**Architecture Challenge**:
- Pub/Sub requires **cross-Worker** communication
- Subscribers may be in different Workers

**Solutions**:

**Option A: Gossip Broadcast (Recommended)**
- Each Worker maintains local subscription table
- Broadcast to all Workers on PUBLISH
- Each Worker checks local subscribers and sends messages
- ✅ Complies with NO SHARING principle
- ⚠️ Broadcast overhead

**Option B: Centralized PubSub Broker**
- Server creates independent PubSub Broker thread
- Worker communicates with Broker via queue
- ✅ Complies with NO SHARING principle
- ⚠️ Increases system complexity

**Recommended Approach**: Option A (Gossip Broadcast)

**Task List**:
- [ ] Maintain local subscription table in Worker
- [ ] Implement PUBLISH broadcast mechanism (Worker-to-Worker communication)
- [ ] Implement SUBSCRIBE/UNSUBSCRIBE/PSUBSCRIBE
- [ ] Pattern subscription support
- [ ] Clean up subscriptions when subscriber disconnects

**Files Involved**:
- `src/astra/commands/pubsub_commands.cpp` (already exists, needs adaptation)
- `src/astra/server/worker.hpp` (add PubSub support)
- `src/astra/server/server.hpp` (add Worker-to-Worker communication mechanism)

---

### Phase 4: Cluster and Replication Integration (Priority: Medium)

#### 4.1 Cluster Integration
**Goal**: Integrate cluster functionality into NO SHARING architecture

**Architecture Options**:

**Option A: Server-Level Cluster (Recommended)**
- Server acts as cluster node
- Data sharding within Workers
- Workers share cluster state (read-only)
- ✅ Relatively simple
- ⚠️ Cluster state needs to be shared

**Option B: Each Worker as Independent Node**
- Each Worker is a cluster node
- Requires complete cluster topology implementation
- ❌ High complexity

**Recommended Approach**: Option A (Server-Level Cluster)

**Task List**:
- [ ] Integrate `cluster::ClusterManager` in Server
- [ ] Integrate `cluster::GossipManager` in Server
- [ ] Integrate `cluster::ShardManager` in Server
- [ ] Implement cluster slot management
- [ ] Implement request routing (MOVED/ASK redirection)
- [ ] Implement cluster command support
- [ ] Worker data sharding and cluster slot mapping

**Files Involved**:
- `src/astra/cluster/cluster_manager.hpp` (already exists, use directly)
- `src/astra/cluster/gossip_manager.hpp` (already exists, use directly)
- `src/astra/cluster/shard_manager.hpp` (already exists, use directly)
- `src/astra/commands/cluster_commands.cpp` (already exists, needs adaptation)
- `src/astra/server/server.hpp` (add cluster managers)
- `src/astra/server/worker.hpp` (add slot checking)

#### 4.2 Replication Integration
**Goal**: Integrate replication functionality into NO SHARING architecture

**Architecture Options**:

**Option A: Server-Level Replication (Recommended)**
- Server acts as master/replica node
- Worker data merged and synchronized to replica
- ✅ Centralized replication logic
- ⚠️ High synchronization complexity

**Option B: Worker-Level Replication**
- Each Worker replicates independently
- ✅ Parallel replication
- ⚠️ Difficult to guarantee consistency

**Recommended Approach**: Option A (Server-Level Replication)

**Task List**:
- [ ] Integrate `replication::ReplicationManager` in Server
- [ ] Implement replication protocol (SYNC/PSYNC)
- [ ] Master node data synchronization (collect data from all Workers)
- [ ] Replica incremental synchronization
- [ ] Partial synchronization (PSYNC)
- [ ] Replication offset management

**Files Involved**:
- `src/astra/replication/replication_manager.hpp` (already exists, use directly)
- `src/astra/commands/replication_commands.cpp` (already exists, needs adaptation)
- `src/astra/server/server.hpp` (add replication manager)

---

### Phase 5: Security and Management Integration (Priority: Medium)

#### 5.1 ACL Authentication Integration
**Goal**: Integrate ACL functionality into NO SHARING architecture

**Architecture Options**:

**Option A: Shared ACLManager (Recommended)**
- Server creates globally shared ACLManager
- ACLManager is read-only (no state modification)
- Workers read permission configuration from ACLManager
- ✅ Complies with NO SHARING (read-only sharing)
- ✅ Simple implementation

**Task List**:
- [ ] Integrate `security::AclManager` in Server
- [ ] Add ACL permission checking in Worker
- [ ] Implement AUTH command
- [ ] Implement user management commands (ACL SETUSER/DELUSER)
- [ ] Implement permission checking (command/key/channel)
- [ ] Configuration file loading

**Files Involved**:
- `src/astra/security/acl_manager.hpp` (already exists, use directly)
- `src/astra/commands/acl_commands.cpp` (already exists, needs adaptation)
- `src/astra/server/server.hpp` (add ACL manager)
- `src/astra/server/worker.hpp` (add permission checking)

#### 5.2 Metrics Monitoring Integration
**Goal**: Integrate metrics monitoring into NO SHARING architecture

**Architecture Options**:

**Option A: Independent Metrics Server (Recommended)**
- Server creates independent Metrics Server thread
- Periodically collect metrics from Workers
- Expose HTTP/metrics endpoint
- ✅ Complies with NO SHARING
- ✅ Doesn't affect main flow

**Task List**:
- [ ] Integrate metrics collector in Server
- [ ] Implement Worker metrics reporting mechanism
- [ ] Implement Prometheus metrics format
- [ ] Expose HTTP/metrics endpoint
- [ ] Implement INFO command

**Files Involved**:
- `src/astra/core/metrics.hpp` (already exists, needs adaptation)
- `src/astra/server/server.hpp` (add Metrics Server)
- `src/astra/server/worker.hpp` (add metrics reporting)

---

### Phase 6: Advanced Feature Integration (Priority: Low)

#### 6.1 Transaction Support
**Goal**: Integrate transaction functionality into NO SHARING architecture

**Task List**:
- [ ] Maintain transaction state at Connection level
- [ ] Implement transaction queue (buffer commands in transaction)
- [ ] Execute transaction commands in batch on EXEC
- [ ] WATCH mechanism (optimistic locking)

**Files Involved**:
- `src/astra/commands/transaction_commands.cpp` (already exists, needs adaptation)
- `src/astra/server/worker.hpp` (add transaction state management)

#### 6.2 Blocking Command Support
**Goal**: Integrate blocking commands (BLPOP/BRPOP/XREAD, etc.) into NO SHARING architecture

**Architecture Options**:

**Option A: Worker-Level Blocking (Recommended)**
- Each Worker maintains its own blocking command queue
- Use asio::steady_timer for timeout
- ✅ Complies with NO SHARING
- ✅ Good performance

**Option B: Global Blocking Manager**
- Server creates global BlockingManager
- ❌ Violates NO SHARING principle

**Recommended Approach**: Option A (Worker-Level Blocking)

**Task List**:
- [ ] Integrate blocking command management in Worker
- [ ] Implement blocking queue
- [ ] Implement timeout mechanism
- [ ] Support all blocking commands (BLPOP/BRPOP/BLMOVE/BZPOPMIN/BZPOPMAX/XREAD)

**Files Involved**:
- `src/astra/commands/blocking_manager.hpp` (already exists, needs adaptation)
- `src/astra/server/worker.hpp` (add blocking support)

#### 6.3 Lua Scripting Support
**Goal**: Integrate Lua scripting functionality into NO SHARING architecture

**Task List**:
- [ ] Integrate Lua interpreter in Worker
- [ ] Implement script caching mechanism
- [ ] Implement script execution context
- [ ] Script replication support

**Files Involved**:
- `src/astra/commands/script_commands.cpp` (already exists, needs adaptation)
- `src/astra/server/worker.hpp` (add Lua interpreter)

---

## Implementation Roadmap

### Iteration 1: Core Feature Integration (1-2 weeks)
- [ ] Integrate Database into Worker
- [ ] Restore command-line arguments and configuration file support
- [ ] Restore client management commands
- [ ] Test all basic data type commands

### Iteration 2: Persistence Integration (1-2 weeks)
- [ ] Integrate AOF Writer
- [ ] Integrate RDB Writer
- [ ] Persistence recovery logic

### Iteration 3: Publish/Subscribe Integration (1 week)
- [ ] Implement local Pub/Sub
- [ ] Implement cross-Worker broadcast

### Iteration 4: Cluster and Replication Integration (2-3 weeks)
- [ ] Integrate cluster managers
- [ ] Integrate replication manager
- [ ] Cluster/replication command support

### Iteration 5: Security and Management Integration (1 week)
- [ ] Integrate ACL authentication
- [ ] Integrate metrics monitoring

### Iteration 6: Advanced Feature Integration (1-2 weeks)
- [ ] Integrate transaction support
- [ ] Integrate blocking commands
- [ ] Integrate Lua scripting

---

## Technical Decision Records

### 1. Why Choose NO SHARING Architecture?
- **Performance**: Eliminate lock contention, improve concurrent performance
- **Scalability**: Easy horizontal scaling
- **Simplicity**: Each Worker is independent, easy to understand and maintain
- **Isolation**: Single Worker failure doesn't affect other Workers

### 2. How to Handle Cross-Worker Features?
- **Pub/Sub**: Use Gossip broadcast mechanism
- **Cluster**: Server-level management, Worker-level sharding
- **Replication**: Server-level replication, Worker data merging
- **Persistence**: Independent Writer threads
- **ACL**: Read-only sharing (complies with NO SHARING)

### 3. Data Consistency Guarantees
- **Within Single Worker**: Strong consistency (single-threaded Executor)
- **Across Workers**: Eventual consistency (via Pub/Sub/cluster protocol)
- **Persistence**: Data persistence guaranteed via AOF/RDB

### 4. Performance Optimization Strategies
- **Lock-Free Design**: Use ConcurrentQueue and other lock-free data structures
- **Batch Processing**: Response queue batch processing (max 100 items at a time)
- **Memory Pool**: Use StringPool to reduce memory allocation
- **Connection Reuse**: Keep-Alive connections

---

## Testing Strategy

### Unit Tests
- [ ] Test commands for each data type
- [ ] TTL functionality testing
- [ ] Transaction functionality testing
- [ ] Pub/Sub functionality testing
- [ ] Blocking command testing
- [ ] Lua script testing

### Integration Tests
- [ ] Multi-Worker concurrent testing
- [ ] Persistence recovery testing
- [ ] Cluster functionality testing
- [ ] Replication functionality testing
- [ ] ACL authentication testing

### Performance Tests
- [ ] Benchmark testing (compare with old architecture)
- [ ] Stress testing (high concurrency scenarios)
- [ ] Persistence performance testing
- [ ] Memory usage testing
- [ ] Pub/Sub performance testing

### Compatibility Tests
- [ ] Redis protocol compatibility
- [ ] redis-cli testing
- [ ] Third-party client testing
- [ ] redis-benchmark testing

---

## Risks and Challenges

### Technical Risks
1. **Cross-Worker Communication Performance**: Pub/Sub/cluster communication may become bottleneck
   - Mitigation: Optimize batch sending, use efficient serialization

2. **Data Consistency**: Cross-Worker data consistency difficult to guarantee
   - Mitigation: Clarify consistency model, avoid strong consistency requirements

3. **Memory Usage**: Multiple Workers may increase memory usage
   - Mitigation: Optimize memory allocation, share read-only data

4. **Module Integration Complexity**: Integrating existing modules into new architecture may encounter compatibility issues
   - Mitigation: Gradual integration, thorough testing

### Implementation Risks
1. **Long Development Cycle**: Feature integration requires significant time
   - Mitigation: Phased implementation, prioritize core features

2. **Testing Complexity**: Multi-Worker environment testing is complex
   - Mitigation: Write comprehensive automated tests

3. **Performance Regression**: Refactoring may affect performance
   - Mitigation: Continuous performance testing, timely optimization

---

## Reference Documents

- [AstraDB Design Document](AstraDB_DESIGN.md)
- [Performance Optimization Guide](PERFORMANCE.md)
- [io_uring Architecture Best Practices](DOCS/io-uring-architecture-best-practices.md)
- [Redis Protocol Specification](https://redis.io/docs/reference/protocol-spec/)
- [Redis Cluster Specification](https://redis.io/docs/reference/cluster-spec/)

---

## Appendix

### A. Implemented Command List

#### String Commands
- SET, GET, MGET, MSET, SETNX, SETEX, PSETEX
- INCR, DECR, INCRBY, DECRBY, INCRBYFLOAT
- APPEND, STRLEN, GETRANGE, SETRANGE
- GETSET, BITCOUNT, BITOP, BITPOS

#### Hash Commands
- HSET, HGET, HMGET, HMSET, HSETNX
- HKEYS, HVALS, HGETALL, HEXISTS, HDEL
- HINCRBY, HINCRBYFLOAT, HLEN, HSTRLEN

#### Set Commands
- SADD, SREM, SMEMBERS, SISMEMBER, SCARD
- SPOP, SRANDMEMBER, SMOVE
- SINTER, SINTERSTORE, SUNION, SUNIONSTORE
- SDIFF, SDIFFSTORE

#### ZSet Commands
- ZADD, ZREM, ZSCORE, ZINCRBY
- ZRANGE, ZREVRANGE, ZRANGEBYSCORE, ZREVRANGEBYSCORE
- ZRANK, ZREVRANK, ZCOUNT, ZCARD
- ZRANGEBYLEX, ZLEXCOUNT, ZREMRANGEBYLEX
- ZPOPMAX, ZPOPMIN

#### List Commands
- LPUSH, RPUSH, LPOP, RPOP, LLEN
- LINDEX, LSET, LTRIM
- LRANGE, LINSERT
- RPOPLPUSH, BRPOP, BLPOP

#### Stream Commands
- XADD, XREAD, XREADGROUP, XGROUP
- XACK, XCLAIM, XDEL
- XINFO, XLEN, XRANGE, XREVRANGE

#### Bitmap Commands
- SETBIT, GETBIT, BITCOUNT, BITOP
- BITPOS, BITFIELD

#### HyperLogLog Commands
- PFADD, PFCOUNT, PFMERGE

#### Geospatial Commands
- GEOADD, GEOPOS, GEODIST
- GEORADIUS, GEORADIUSBYMEMBER
- GEOHASH

#### Administration Commands
- PING, ECHO, QUIT
- SELECT, SWAPDB
- DBSIZE, FLUSHDB, FLUSHALL
- INFO, CONFIG
- CLIENT, MONITOR, SLOWLOG

#### Transaction Commands
- MULTI, EXEC, DISCARD, WATCH, UNWATCH

#### Publish/Subscribe Commands
- PUBLISH, SUBSCRIBE, UNSUBSCRIBE
- PSUBSCRIBE, PUNSUBSCRIBE

#### Scripting Commands
- EVAL, EVALSHA, SCRIPT EXISTS
- SCRIPT FLUSH, SCRIPT KILL, SCRIPT LOAD

#### Cluster Commands
- CLUSTER MEET, CLUSTER NODES, CLUSTER INFO
- CLUSTER SLOTS, CLUSTER KEYSLOT
- CLUSTER REPLICATE, CLUSTER FAILOVER

#### Replication Commands
- REPLICAOF, ROLE
- SYNC, PSYNC

#### ACL Commands
- AUTH, ACL SETUSER, ACL DELUSER
- ACL GETUSER, ACL LIST, ACL USERS
- ACL CAT, ACL GENPASS, ACL WHOAMI
- ACL LOAD, ACL SAVE

---

**Document Version**: 2.0
**Created**: 2026-03-13
**Last Updated**: 2026-03-13
**Authors**: AstraDB Team