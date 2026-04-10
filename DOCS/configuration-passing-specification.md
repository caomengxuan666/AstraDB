# Configuration Passing Specification

## Overview
This document defines the standard process for passing configuration from configuration files to runtime components in AstraDB.

## Configuration Chain

### 1. Configuration File Format
Configuration files use TOML format with structured sections:
- `[server]` - Network and server settings
- `[storage]` - Unified storage mode configuration
- `[memory]` - Memory management settings
- `[cluster]` - Cluster configuration
- `[replication]` - Replication settings

### 2. Configuration Loading Process

#### Step 1: File Parsing (`src/astra/base/config.cpp`)
```cpp
ServerConfig ServerConfig::LoadFromFile(const std::string& config_file)
```
- Parse TOML file
- Extract all configuration sections
- Populate `ServerConfig` structure

#### Step 2: Main Configuration Transfer (`src/main.cpp`)
```cpp
// Copy from parsed config to server config
server_config.storage.mode = config.storage.mode;
server_config.storage.enable_rocksdb_cold_data = config.storage.enable_rocksdb_cold_data;
server_config.storage.enable_compression = config.storage.enable_compression;
server_config.storage.compression_type = config.storage.compression_type;
server_config.storage.redis_mode = config.storage.redis_mode;
server_config.storage.rocksdb_mode = config.storage.rocksdb_mode;
```

#### Step 3: Server Configuration Transfer (`src/astra/server/server.cpp`)
```cpp
// Server constructor or initialization method
config.storage = base_config.storage;  // CRITICAL: Must copy entire storage config
```

#### Step 4: Worker Configuration Transfer (`src/astra/server/server.cpp`)
```cpp
worker->GetDataShard().SetMemoryConfig(
    memory_config, 
    get_total_memory_callback,
    config_.storage.enable_rocksdb_cold_data,  // CRITICAL: Must pass
    config_.storage.mode,                        // CRITICAL: Must pass
    config_.storage.enable_compression,         // CRITICAL: Must pass
    config_.storage.compression_type            // CRITICAL: Must pass
);
```

## Mandatory Configuration Transfers

### Storage Configuration
**CRITICAL**: All storage-related fields must be transferred at each level:

1. **File → Config**: `config.storage.*` must be populated
2. **Main → Server**: `server_config.storage = config.storage` (entire struct)
3. **Server → Worker**: All parameters passed to `SetMemoryConfig()`
4. **Worker → DataShard**: All parameters stored in member variables

### Memory Configuration
- `max_memory`
- `eviction_policy`
- `eviction_threshold`
- `eviction_samples`
- `enable_tracking`

## Common Mistakes

### Mistake 1: Missing Struct Copy
```cpp
// WRONG: Individual field copies
server_config.storage.mode = config.storage.mode;
server_config.storage.enable_compression = config.storage.enable_compression;

// CORRECT: Entire struct copy
server_config.storage = config.storage;
```

### Mistake 2: Missing Parameter in Function Call
```cpp
// WRONG: Missing storage_mode parameter
worker->GetDataShard().SetMemoryConfig(
    memory_config, 
    get_total_memory_callback,
    enable_rocksdb
);

// CORRECT: All parameters present
worker->GetDataShard().SetMemoryConfig(
    memory_config, 
    get_total_memory_callback,
    enable_rocksdb,
    storage_mode,      // CRITICAL
    enable_compression,
    compression_type
);
```

### Mistake 3: Default Parameter Masking
```cpp
// WRONG: Using default parameters
void SetMemoryConfig(
    const MemoryTrackerConfig& config,
    GetTotalMemoryCallback callback = nullptr,
    bool enable_rocksdb = false,              // Default used
    StorageMode storage_mode = StorageMode::kRedis,  // Default used
    bool enable_compression = true,
    const std::string& compression_type = "zlib"
);

// CORRECT: Explicitly pass all parameters
SetMemoryConfig(
    config, 
    callback,
    config_.storage.enable_rocksdb_cold_data,  // Explicit
    config_.storage.mode,                      // Explicit
    config_.storage.enable_compression,       // Explicit
    config_.storage.compression_type          // Explicit
);
```

## Verification Checklist

When adding new configuration:

1. ✅ Define field in `src/astra/base/config.hpp` (Config struct)
2. ✅ Parse field in `src/astra/base/config.cpp` (LoadFromFile)
3. ✅ Copy field in `src/main.cpp` (main → server_config)
4. ✅ Copy field in `src/astra/server/server.cpp` (server → worker)
5. ✅ Pass field in `SetMemoryConfig()` call
6. ✅ Store field in worker member variables
7. ✅ Use field in actual initialization logic
8. ✅ Add logging to verify configuration at each level

## Debug Configuration Issues

### Add Debug Logging
```cpp
// In config.cpp
ASTRADB_LOG_INFO("Parsed storage mode: {}", static_cast<int>(config.storage.mode));

// In main.cpp
ASTRADB_LOG_INFO("Copying storage mode: {}", static_cast<int>(server_config.storage.mode));

// In server.cpp
ASTRADB_LOG_INFO("Passing storage mode: {}", static_cast<int>(config_.storage.mode));

// In worker.hpp
ASTRADB_LOG_INFO("Received storage mode: {}", static_cast<int>(storage_mode));
```

### Verify Configuration Flow
1. Check config file: `grep "storage.mode" config.toml`
2. Check parsing: `grep "storage mode" logs`
3. Check main transfer: `grep "Copying storage" logs`
4. Check server transfer: `grep "Passing storage" logs`
5. Check worker storage: `grep "Received storage" logs`

## Examples

### Adding New Configuration Field

**Step 1**: Define in config.hpp
```cpp
struct StorageConfig {
  StorageMode mode = StorageMode::kRedis;
  bool enable_compression = true;
  std::string compression_type = "zlib";
  int new_field = 42;  // NEW FIELD
};
```

**Step 2**: Parse in config.cpp
```cpp
config.storage.new_field = storage["new_field"].value_or<int>(42);
```

**Step 3**: Copy in main.cpp
```cpp
server_config.storage.new_field = config.storage.new_field;
```

**Step 4**: Copy in server.cpp
```cpp
// This is automatically handled by struct copy
config.storage = base_config.storage;
```

**Step 5**: Pass to worker
```cpp
worker->GetDataShard().SetMemoryConfig(
    // ... other params
    config_.storage.new_field  // NEW PARAMETER
);
```

**Step 6**: Store in worker
```cpp
void SetMemoryConfig(..., int new_field) {
  new_field_ = new_field;
  // Use new_field
}
```

## Critical Rules

1. **NEVER** use default parameters when configuration is available
2. **ALWAYS** copy entire structs when multiple fields need transfer
3. **ALWAYS** add debug logging for new configurations
4. **ALWAYS** verify the entire configuration chain
5. **NEVER** assume configuration is correct without logging

## Testing Configuration

### Unit Test Template
```cpp
TEST(ConfigurationPassing, StorageMode) {
  // 1. Create config file
  WriteConfigFile("test_config.toml", "[storage]\nmode = \"rocksdb\"");
  
  // 2. Load config
  auto config = ServerConfig::LoadFromFile("test_config.toml");
  EXPECT_EQ(config.storage.mode, StorageMode::kRocksDB);
  
  // 3. Create server
  Server server(config);
  EXPECT_EQ(server.GetStorageMode(), StorageMode::kRocksDB);
  
  // 4. Create worker
  Worker worker(0, ...);
  worker.SetMemoryConfig(..., config.storage.mode);
  EXPECT_EQ(worker.GetStorageMode(), StorageMode::kRocksDB);
}
```

## Conclusion

Configuration passing is a critical data flow in AstraDB. Every configuration field must:
1. Be defined in the configuration struct
2. Be parsed from the configuration file
3. Be transferred through all layers (main → server → worker)
4. Be used in the actual initialization
5. Be verified with logging and tests

**REMEMBER**: Missing any step will result in configuration being lost or using defaults.
