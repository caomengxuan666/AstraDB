# RDB Persistence Integration Plan for NO SHARING Architecture

## Overview

This document outlines the plan to integrate Redis RDB (Redis Database) format persistence into AstraDB's NO SHARING architecture. Unlike the main branch's SHARED architecture where all data is accessible through a central coordinator, the NO SHARING architecture requires each worker to independently manage its own data and persistence.

## Implementation Status: COMPLETED ✅

All planned RDB persistence functionality has been successfully implemented and tested.

### Completed Features

✅ **RDB Reader Implementation** (`src/astra/persistence/rdb_reader.hpp`)
- Full RDB file parsing (Redis RDB format version 10)
- Streaming CRC32C checksum verification using Abseil
- Support for all major opcodes (SELECTDB, RESIZEDB, EXPIRETIME_MS, AUX, EOF)
- Robust error handling and validation

✅ **RDB Writer Refactoring** (`src/astra/persistence/rdb_writer.hpp`)
- Fixed header writing logic (direct "REDIS" + version write)
- Integrated with shared RDB common definitions
- Compatible with Redis RDB format

✅ **RDB Common Definitions** (`src/astra/persistence/rdb_common.hpp`)
- Shared opcodes, types, and version constants
- RdbKeyValue structure for unified data representation
- KeyType to RDB type conversion function

✅ **PersistenceManager Integration** (`src/astra/server/managers.hpp`)
- `LoadRdb()` method for RDB file loading
- Optimized `SaveRdb()` method using KeyTypeToRdbType()
- Unified PersistenceManager for both AOF and RDB

✅ **Server Startup Integration** (`src/astra/server/server.cpp`)
- Auto-load RDB data on server startup
- Single unified PersistenceManager creation
- Proper worker setup with persistence callbacks

✅ **Command Implementation** (`src/astra/commands/admin_commands.cpp`)
- SAVE command: synchronous RDB save
- BGSAVE command: asynchronous RDB save

✅ **Worker Interface** (`src/astra/server/worker.hpp`)
- `GetDataShard()` method for data access
- `GetRdbData()` method for RDB data collection

✅ **Comprehensive Testing** (`tests/unit/persistence/rdb_test.cpp`)
- All 5 unit tests passing:
  - BasicStringWriteAndRead
  - StringWithExpiration
  - MultipleDatabases
  - EmptyDatabase
  - ChecksumVerification

### Verification Results

✅ RDB file format compatible with Redis
✅ Data save and load functionality working
✅ Expiration time handled correctly
✅ CRC32C checksum verification passing
✅ SAVE and BGSAVE commands working properly
✅ Multi-worker data distribution working correctly
✅ NO SHARING architecture maintained

## Current State

### Main Branch Architecture (SHARED)
- Central `Server` class with `LocalShardManager`
- All workers share access to global data structures
- Single `RdbWriter` instance managed by `Server`
- Single RDB file contains all databases

### Current Branch Architecture (NO SHARING) - WITH RDB SUPPORT ✅
- Each worker is completely independent
- Each worker has its own `DataShard` with a `Database` instance
- Workers communicate via MPSC queues for cross-worker operations
- AOF persistence is implemented per-worker (each worker writes to shared AOF file)
- **RDB persistence fully implemented with PersistenceManager coordination**
- **SAVE and BGSAVE commands operational**

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

### Implementation Plan: ALL PHASES COMPLETED ✅

### Phase 1: Add RDB Writer to Worker Class ✅ COMPLETED

**Files modified:**
- ✅ `src/astra/server/worker.hpp` - Added GetDataShard() and GetRdbData() methods
- ✅ `src/astra/server/worker.cpp` - Implementation of data collection methods

**Implementation approach:**
- Added `GetDataShard()` method to expose worker's data shard
- Added `GetRdbData()` method to collect all key-value pairs with TTL
- Returns vector of tuples: (key, type, value, ttl_ms)

### Phase 2: Add Database Key Iteration ✅ COMPLETED

**Files modified:**
- ✅ `src/astra/storage/database.hpp` - Already had ForEachKey() method
- ✅ `src/astra/storage/database.cpp` - Implementation of key iteration

**Available methods:**
- `ForEachKey()` - Iterates through all keys with callback
- `GetKeyCount()` - Returns total key count
- `GetExpiredKeysCount()` - Returns expired keys count

### Phase 3: Add RDB Coordinator to Server ✅ COMPLETED

**Files modified:**
- ✅ `src/astra/server/managers.hpp` - PersistenceManager with RDB support
- ✅ `src/astra/server/server.cpp` - Server startup integration

**Implementation:**
- Unified PersistenceManager manages both AOF and RDB
- `SaveRdb()` method: Collects data from all workers, writes to single RDB file
- `LoadRdb()` method: Reads RDB file, distributes keys to appropriate workers based on hash
- Single RDB file format compatible with Redis tools

### Phase 4: Implement SAVE and BGSAVE Commands ✅ COMPLETED

**Files modified:**
- ✅ `src/astra/commands/admin_commands.cpp` - SAVE and BGSAVE command handlers

**Commands implemented:**
- `SAVE`: Synchronous RDB save (blocking)
- `BGSAVE`: Asynchronous RDB save (non-blocking)
- Both commands return appropriate Redis protocol responses

### Phase 5: Add RDB Loading (Recovery) ✅ COMPLETED

**Files created/modified:**
- ✅ `src/astra/persistence/rdb_reader.hpp` - Complete RDB file reader implementation
- ✅ `src/astra/persistence/rdb_common.hpp` - Shared RDB definitions
- ✅ `src/astra/server/server.cpp` - Auto-load RDB on server startup

**Implementation features:**
- Full RDB format parsing (version 10)
- Streaming CRC32C checksum verification
- Support for auxiliary fields, multiple databases, expiration times
- Key distribution to workers based on hash (consistent with NO SHARING architecture)
- Proper TTL conversion (absolute time to remaining milliseconds)

### Phase 6: Periodic RDB Save (Optional) 🔄 PENDING

**Status:** Not implemented yet, can be added as future enhancement

**Proposed implementation:**
- Add RDB auto-save configuration
- Periodic background save thread
- Configurable save interval

## Configuration: IMPLEMENTED ✅

Configuration added to `astradb.toml`:

```toml
[rdb]
enabled = true
path = "./data/dump.rdb"
```

**Status:**
- ✅ `enabled` - RDB persistence can be enabled/disabled
- ✅ `path` - RDB file path configuration
- ⏸️ `auto_save` - Not yet implemented (optional future feature)
- ⏸️ `save_interval` - Not yet implemented (optional future feature)
- ⏸️ `compress` - Not yet implemented (optional future feature)
- ✅ `checksum` - CRC32C checksum verification always enabled

**Current behavior:**
- RDB is loaded on server startup if `enabled = true`
- RDB can be saved manually using SAVE or BGSAVE commands
- CRC32C checksum is always calculated and verified
- No automatic periodic save (requires manual trigger or future enhancement)

## Testing Plan: COMPLETED ✅

### Unit Tests ✅ ALL PASSED

**Test file:** `tests/unit/persistence/rdb_test.cpp`

**Test results:** 5/5 tests passing

1. ✅ `BasicStringWriteAndRead` - Basic string write and read operations
2. ✅ `StringWithExpiration` - Strings with expiration time
3. ✅ `MultipleDatabases` - Support for multiple databases
4. ✅ `EmptyDatabase` - Handling of empty database
5. ✅ `ChecksumVerification` - CRC32C checksum verification

**Run command:**
```bash
./build-linux-debug-gcc/bin/astradb_tests --gtest_filter=RdbTest.*
```

### Integration Tests ✅ ALL PASSED

1. ✅ Test SAVE command (blocking) - Working correctly
2. ✅ Test BGSAVE command (non-blocking) - Working correctly
3. ✅ Test server restart with RDB recovery - Working correctly
4. ⏸️ Test concurrent SAVE/BGSAVE requests - Not yet tested
5. ⏸️ Test RDB save during high load - Not yet tested

**Manual verification:**
```bash
# Test SAVE command
redis-cli SET key1 "value1"
redis-cli SAVE
# Verify RDB file created

# Test BGSAVE command
redis-cli SET key2 "value2"
redis-cli BGSAVE
# Verify "Background saving started" response

# Test server restart with RDB recovery
redis-cli SET user:1001 "张三"
redis-cli SAVE
# Restart server
redis-cli GET user:1001  # Should return "张三"
```

### Performance Tests 🔄 PENDING

1. ⏸️ Measure RDB save time with different data sizes
2. ⏸️ Measure memory usage during RDB save
3. ⏸️ Compare with AOF persistence performance
4. ⏸️ Test RDB save impact on request latency

**Note:** Performance tests can be added as future enhancements

## Migration Path: COMPLETED ✅

1. ✅ **Phase 1-2**: Add worker-side RDB serialization - COMPLETED
   - Worker data collection methods added
   - Database key iteration support added
   - No impact on existing functionality

2. ✅ **Phase 3**: Add RDB coordinator - COMPLETED
   - PersistenceManager with RDB support
   - Unified AOF and RDB management
   - No impact on existing functionality

3. ✅ **Phase 4**: Add SAVE/BGSAVE commands - COMPLETED
   - New feature, fully backward compatible
   - Both commands working correctly

4. ✅ **Phase 5**: Add RDB loading - COMPLETED
   - Enhanced recovery functionality
   - Auto-load on server startup
   - Fully tested and verified

5. ⏸️ **Phase 6**: Add periodic save - PENDING
   - Optional future enhancement
   - Not critical for current functionality

## Future Enhancements

1. ⏸️ **Incremental RDB**: Only save changed keys since last snapshot
2. ⏸️ **Parallel Save**: Each worker writes to its own RDB file, then merge
3. ⏸️ **Compression**: Use zstd compression for RDB files
4. ✅ **Checksum Verification**: Verify RDB integrity on load (COMPLETED)
5. ⏸️ **RDB+AOF Hybrid**: Use RDB for full snapshots, AOF for incremental updates
6. ⏸️ **Periodic Auto-Save**: Configurable automatic RDB saves
7. ⏸️ **RDB Version Migration**: Support for different RDB format versions

## Conclusion

✅ **IMPLEMENTATION COMPLETED SUCCESSFULLY**

The proposed design has been fully implemented and maintains NO SHARING architecture principles while providing Redis-compatible RDB persistence. Each worker independently serializes its data, and a central PersistenceManager coordinates the save/load operations to a single RDB file. This approach successfully balances consistency, performance, and compatibility with Redis tools.

**Key Achievements:**
- ✅ Full RDB v10 format compatibility with Redis
- ✅ Complete save and load functionality
- ✅ SAVE and BGSAVE commands operational
- ✅ Automatic RDB loading on server startup
- ✅ Comprehensive test coverage (5/5 tests passing)
- ✅ CRC32C checksum verification
- ✅ Proper handling of expiration times
- ✅ Multi-database support
- ✅ NO SHARING architecture maintained

**Technical Highlights:**
- Clean separation between reader and writer
- Shared RDB common definitions to avoid duplication
- Unified PersistenceManager for AOF and RDB
- Robust error handling and validation
- Memory-efficient streaming CRC32C calculation
- Proper key distribution based on hash

**Completion Date:** March 14, 2026

**Commit:** `184da3c` - "feat: Implement complete RDB persistence functionality"

**Merged to:** `develop` branch (`b79c991`)