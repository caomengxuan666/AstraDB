# RDB Persistence Integration Plan for NO SHARING Architecture

## Overview

This document outlines the plan to integrate Redis RDB (Redis Database) format persistence into AstraDB's NO SHARING architecture. Unlike the main branch's SHARED architecture where all data is accessible through a central coordinator, the NO SHARING architecture requires each worker to independently manage its own data and persistence.

## Current State

### Main Branch Architecture (SHARED)
- Central `Server` class with `LocalShardManager`
- All workers share access to global data structures
- Single `RdbWriter` instance managed by `Server`
- Single RDB file contains all databases

### Current Branch Architecture (NO SHARING)
- Each worker is completely independent
- Each worker has its own `DataShard` with a `Database` instance
- Workers communicate via MPSC queues for cross-worker operations
- AOF persistence is implemented per-worker (each worker writes to shared AOF file)
- No RDB persistence implementation yet

## Analysis: Main Branch RDB Implementation

### Key Components

1. **RdbWriter** (`src/astra/persistence/rdb_writer.hpp`)
   - Streaming CRC32C checksum calculation
   - Redis RDB v10 compatible format
   - Callback-based serialization via `absl::AnyInvocable`
   - Support for: SELECTDB, RESIZEDB, EXPIRETIME_MS, AUX fields

2. **RDB Save Flow** (from `server.cpp`)
   ```cpp
   auto save_callback = [this](RdbWriter& writer) {
     for (size_t db_idx = 0; db_idx < config_.num_databases; ++db_idx) {
       auto* shard = local_shard_manager_.GetShardByIndex(0);
       auto* db = shard->GetDatabase(static_cast<int>(db_idx));
       writer.SelectDb(static_cast<int>(db_idx));
       writer.ResizeDb(db_size, expires_size);
       // TODO: Serialize all keys and their values
     }
   };
   bool success = rdb_writer_->Save(save_callback);
   ```

3. **RDB Format Structure**
   - Header: "REDIS" + version
   - Auxiliary fields: redis-ver, redis-bits, ctime
   - Database sections: SELECTDB + RESIZEDB + key-value pairs
   - EOF opcode + checksum

## Integration Challenges for NO SHARING Architecture

### Challenge 1: Multiple Data Sources
- Each worker has independent `Database` instances
- Keys are distributed across workers via hashing
- No central view of all data

### Challenge 2: Coordination
- All workers must save their data atomically (as much as possible)
- Need to ensure consistent snapshot across all workers
- Handle worker failures during save

### Challenge 3: File Management
- Option A: Single RDB file with data from all workers
  - Pros: Compatible with Redis tools, simpler recovery
  - Cons: Requires coordination, potential lock contention

- Option B: One RDB file per worker
  - Pros: No coordination needed, true independence
  - Cons: More complex recovery, not Redis-compatible

## Proposed Solution: Coordinated Multi-Worker RDB Save

### Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        Server                               │
│  ┌─────────────────────────────────────────────────────┐   │
│  │              RdbCoordinator                          │   │
│  │  - Coordinate save operations across workers        │   │
│  │  - Merge worker data into single RDB file          │   │
│  │  - Handle save triggers (BGSAVE, SAVE)             │   │
│  └─────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
                           │
           ┌───────────────┼───────────────┐
           │               │               │
           ▼               ▼               ▼
    ┌────────────┐  ┌────────────┐  ┌────────────┐
    │  Worker 0  │  │  Worker 1  │  │  Worker N  │
    │  ┌──────┐  │  │  ┌──────┐  │  │  ┌──────┐  │
    │  │ Shard│  │  │  │ Shard│  │  │  │ Shard│  │
    │  │ DB   │  │  │  │ DB   │  │  │  │ DB   │  │
    │  └──────┘  │  │  └──────┘  │  │  └──────┘  │
    │  ┌──────┐  │  │  ┌──────┐  │  │  ┌──────┐  │
    │  │RDB   │  │  │  │RDB   │  │  │  │RDB   │  │
    │  │Save  │  │  │  │Save  │  │  │  │Save  │  │
    │  │CB    │  │  │  │CB    │  │  │  │CB    │  │
    │  └──────┘  │  │  └──────┘  │  │  └──────┘  │
    └────────────┘  └────────────┘  └────────────┘
```

### Design Principles

1. **Per-Worker Data Serialization**: Each worker serializes its own data to a memory buffer
2. **Central Coordinator**: Server coordinates the save operation and merges data
3. **Atomic Snapshot**: Use two-phase save to ensure consistency
4. **Redis Compatible**: Output single RDB file compatible with Redis tools

### Implementation Plan

#### Phase 1: Add RDB Writer to Worker Class

**Files to modify:**
- `src/astra/server/worker.hpp`
- `src/astra/server/worker.cpp`

**Changes:**

```cpp
// In Worker class
class Worker {
 public:
  // New methods
  std::vector<uint8_t> SerializeToRdbBuffer(int db_num);
  void SetRdbSaveCallback(std::function<void(RdbWriter&)> callback);
  
 private:
  // New members
  std::function<void(RdbWriter&)> rdb_save_callback_;
};
```

**Implementation:**

```cpp
std::vector<uint8_t> Worker::SerializeToRdbBuffer(int db_num) {
  std::vector<uint8_t> buffer;
  
  // Use stringstream as intermediate buffer
  std::stringstream ss;
  std::unique_ptr<std::ofstream> file(nullptr);
  
  // Custom writer that writes to stringstream instead of file
  auto write_func = [&ss](const void* data, size_t len) {
    ss.write(static_cast<const char*>(data), len);
  };
  
  // Serialize all keys from database
  auto& db = data_shard_.GetDatabase();
  // TODO: Iterate through all keys and write them
  // Need to add key iteration to Database class
  
  // Convert stringstream to byte vector
  std::string str = ss.str();
  buffer.assign(str.begin(), str.end());
  
  return buffer;
}
```

#### Phase 2: Add Database Key Iteration

**Files to modify:**
- `src/astra/commands/database.hpp`
- `src/astra/commands/database.cpp`

**New methods:**

```cpp
class Database {
 public:
  // Iterate through all keys in database
  template <typename Func>
  void ForEachKey(Func&& func) const {
    for (const auto& pair : strings_) {
      func(pair.first, pair.second.value, pair.second.ttl);
    }
    // Repeat for other data types...
  }
  
  // Get total key count
  size_t GetKeyCount() const {
    return strings_.size() + hashes_.size() + lists_.size() + sets_.size();
  }
  
  // Get expired keys count
  size_t GetExpiredKeysCount() const;
};
```

#### Phase 3: Add RDB Coordinator to Server

**Files to modify:**
- `src/astra/server/server.hpp`
- `src/astra/server/server.cpp`

**New class:**

```cpp
class RdbCoordinator {
 public:
  bool Init(const std::string& save_path, size_t num_workers);
  bool Save(const std::vector<Worker*>& workers);
  bool StartBackgroundSave(const std::vector<Worker*>& workers);
  
 private:
  bool SaveToRdbFile(const std::vector<std::vector<uint8_t>>& worker_data);
  std::string save_path_;
  RdbWriter rdb_writer_;
  std::atomic<bool> bg_save_in_progress_{false};
};
```

**Server integration:**

```cpp
class Server {
 private:
  std::unique_ptr<RdbCoordinator> rdb_coordinator_;
  
 public:
  bool SaveRdb() {
    std::vector<Worker*> worker_ptrs;
    for (auto& worker : workers_) {
      worker_ptrs.push_back(worker.get());
    }
    return rdb_coordinator_->Save(worker_ptrs);
  }
  
  bool BackgroundSaveRdb() {
    std::vector<Worker*> worker_ptrs;
    for (auto& worker : workers_) {
      worker_ptrs.push_back(worker.get());
    }
    return rdb_coordinator_->StartBackgroundSave(worker_ptrs);
  }
};
```

#### Phase 4: Implement SAVE and BGSAVE Commands

**Files to modify:**
- `src/astra/commands/server_commands.cpp`

**New commands:**

```cpp
bool HandleSave(CommandContext* context, const Command& cmd) {
  auto* worker_ctx = static_cast<WorkerCommandContext*>(context);
  auto& db = worker_ctx->GetDatabase();
  
  // Request server to trigger RDB save
  // This requires worker to communicate with server
  // Use MPSC queue to send SAVE request to server
  
  return RespBuilder::BuildSimpleString("OK");
}

bool HandleBgsave(CommandContext* context, const Command& cmd) {
  auto* worker_ctx = static_cast<WorkerCommandContext*>(context);
  auto& db = worker_ctx->GetDatabase();
  
  // Request server to trigger background RDB save
  // This requires worker to communicate with server
  // Use MPSC queue to send BGSAVE request to server
  
  return RespBuilder::BuildSimpleString("Background saving started");
}
```

#### Phase 5: Add RDB Loading (Recovery)

**Files to modify:**
- `src/astra/persistence/rdb_writer.hpp` (rename/add rdb_reader.hpp)
- `src/astra/server/server.cpp`

**Implementation:**

```cpp
bool LoadRdb(const std::string& rdb_path, const std::vector<Worker*>& workers) {
  // Read RDB file
  std::ifstream file(rdb_path, std::ios::binary);
  
  // Parse RDB header
  // Parse auxiliary fields
  
  // For each database in RDB:
  //   Read SELECTDB opcode
  //   Read RESIZEDB opcode
  //   For each key-value pair:
  //     Calculate target worker based on key hash
  //     Forward key-value to target worker
  //     Worker loads key-value into its database
  
  return true;
}
```

#### Phase 6: Periodic RDB Save (Optional)

**Files to modify:**
- `src/astra/server/server.hpp`
- `src/astra/server/server.cpp`

**Implementation:**

```cpp
class Server {
 private:
  void StartRdbSaveScheduler() {
    std::thread([this]() {
      while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(300)); // 5 minutes
        if (config_.rdb.auto_save) {
          BackgroundSaveRdb();
        }
      }
    }).detach();
  }
};
```

## Configuration

Add to `astradb.toml`:

```toml
[rdb]
enabled = true
path = "./data/dump.rdb"
auto_save = true
save_interval = 300  # seconds
compress = true
checksum = true
```

## Testing Plan

### Unit Tests
1. Test RDB serialization of individual worker data
2. Test RDB coordinator merging of multiple worker buffers
3. Test RDB file format compatibility with Redis tools

### Integration Tests
1. Test SAVE command (blocking)
2. Test BGSAVE command (non-blocking)
3. Test server restart with RDB recovery
4. Test concurrent SAVE/BGSAVE requests
5. Test RDB save during high load

### Performance Tests
1. Measure RDB save time with different data sizes
2. Measure memory usage during RDB save
3. Compare with AOF persistence performance
4. Test RDB save impact on request latency

## Migration Path

1. **Phase 1-2**: Add worker-side RDB serialization (no impact on existing functionality)
2. **Phase 3**: Add RDB coordinator (no impact on existing functionality)
3. **Phase 4**: Add SAVE/BGSAVE commands (new feature, backward compatible)
4. **Phase 5**: Add RDB loading (enhanced recovery, optional)
5. **Phase 6**: Add periodic save (optional enhancement)

## Future Enhancements

1. **Incremental RDB**: Only save changed keys since last snapshot
2. **Parallel Save**: Each worker writes to its own RDB file, then merge
3. **Compression**: Use zstd compression for RDB files
4. **Checksum Verification**: Verify RDB integrity on load
5. **RDB+AOF Hybrid**: Use RDB for full snapshots, AOF for incremental updates

## Conclusion

The proposed design maintains NO SHARING architecture principles while providing Redis-compatible RDB persistence. Each worker independently serializes its data, and a central coordinator merges the data into a single RDB file. This approach balances consistency, performance, and compatibility with Redis tools.

The implementation is incremental and can be done in phases, with each phase adding functionality without breaking existing code.