# Cluster Slot Synchronization Implementation

## Problem Analysis

### Current Status
- MOVED redirection is implemented but not working
- Slot assignment (CLUSTER ADDSLOTS) only updates local cluster state
- Other nodes cannot see slot assignments from other nodes
- Result: Node 1 doesn't know that slot 4 belongs to Node 2, so no MOVED error is returned

### Root Cause
The cluster state is NOT synchronized between nodes. Each node maintains its own `cluster_state` copy:

```cpp
// Node 2 executes: CLUSTER ADDSLOTS 4 5 6 7
// Node 2's cluster_state: slot 4 → node_00000000000000000000000000000002
// Node 1's cluster_state: slot 4 → (unassigned)  // Problem!
```

### Investigation Findings

1. **ClusterSnapshot is defined but never used**:
   - `cluster_message.fbs` defines `ClusterSnapshot` with `slot_assignments`
   - `ClusterFlatbuffers::SerializeClusterSnapshot()` is implemented
   - `ClusterFlatbuffers::DeserializeClusterSnapshot()` is implemented
   - **BUT**: These functions are never called anywhere in the codebase

2. **libgossip's metadata mechanism exists**:
   - `node_view.metadata` is a `std::map<std::string, std::string>`
   - Automatically synchronized via libgossip's gossip protocol
   - Currently unused in AstraDB

3. **No custom message transmission**:
   - No `SendClusterMessage` or `BroadcastClusterSnapshot` implementation
   - No receiver callback for cluster state updates

## Solution Comparison

### Option 1: Metadata-Only Approach

**Implementation**:
```cpp
// Serialize slot assignments as string: "0-8191,100,101,102"
metadata["slots"] = "0-8191,100,101,102";
gossip_manager->BroadcastConfig();
```

**Pros**:
- ✅ Quick to implement
- ✅ Uses existing libgossip mechanism
- ✅ No custom serialization code needed

**Cons**:
- ❌ String format is inefficient for large slot ranges
- ❌ No version control (config_epoch)
- ❌ No support for slot migration state
- ❌ No support for cluster statistics (total_keys, memory_used, etc.)
- ❌ Potential for configuration conflicts
- ❌ Not following Redis Cluster best practices

### Option 2: Full fbs Serialization Approach

**Implementation**:
```cpp
// Serialize complete ClusterSnapshot
auto snapshot = ClusterFlatbuffers::SerializeClusterSnapshot(
    nodes, slot_assignments, config_epoch, total_keys
);
// Send via custom transport mechanism
```

**Pros**:
- ✅ Complete cluster state (nodes, slots, stats, migrations)
- ✅ Version control via config_epoch
- ✅ Support for slot migration states
- ✅ Follows Redis Cluster specification
- ✅ Binary format (FlatBuffers) - efficient parsing
- ✅ Extensible for future features

**Cons**:
- ❌ Requires custom message transmission implementation
- ❌ More complex code
- ❌ Requires integration with libgossip's transport layer

### Option 3: Hybrid Approach (RECOMMENDED)

**Implementation**:
```cpp
// 1. Serialize slot assignments to compact string representation
std::string slots_str = SerializeSlotAssignments(slot_assignments);
// Example: "0-8191" (6 bytes for 8192 slots!)

// 2. Store in metadata
metadata["slots"] = slots_str;
metadata["config_epoch"] = std::to_string(config_epoch);

// 3. Broadcast via libgossip
gossip_manager->BroadcastConfig();

// 4. On receive: parse and update cluster state
if (node.metadata.contains("slots")) {
    auto slot_assignments = DeserializeSlotAssignments(node.metadata["slots"]);
    UpdateClusterState(slot_assignments);
}
```

**Slot Assignment Serialization Format**:
- **Continuous range**: `"0-8191"` → slots [0, 8191]
- **Discrete slots**: `"0,1,2,100,101"` → slots [0, 1, 2, 100, 101]
- **Mixed**: `"0-8191,16382,16383"` → slots [0, 8191] + [16382, 16383]

**Pros**:
- ✅ Leverages existing libgossip metadata synchronization
- ✅ Extremely compact format (6KB for 16384 slots vs 40KB+ for metadata string)
- ✅ Simple to implement
- ✅ Supports version control via metadata["config_epoch"]
- ✅ Easy to extend with base64-encoded full fbs snapshot if needed

**Cons**:
- ❌ Requires custom serialization/deserialization logic for slot ranges
- ❌ Loss of some fbs features (migration states) - can be added later

## Performance Comparison

| Metric | Metadata (Option 1) | Full fbs (Option 2) | Hybrid (Option 3) |
|--------|---------------------|---------------------|-------------------|
| **Data Size** (50% slots) | ~40KB | ~16KB | ~8KB |
| **Serialization Speed** | Slow (string concat) | Fast (FlatBuffers) | Medium |
| **Transmission** | Every gossip | Controlled | Controlled |
| **Version Control** | ❌ No | ✅ Yes | ✅ Yes |
| **Migration Support** | ❌ No | ✅ Yes | ⚠️ Limited |
| **Implementation Complexity** | Low | High | Medium |

## Recommended Solution: Option 3 (Hybrid)

### Why This is the Best Choice

1. **Performance**: Compact string format minimizes transmission overhead
2. **Simplicity**: Uses libgossip's existing metadata mechanism
3. **Extensibility**: Can add base64-encoded fbs snapshot later if needed
4. **Version Control**: Supports config_epoch to prevent conflicts
5. **Pragmatic**: Fits current architecture without major refactoring

### Future Extensibility

When more complex cluster state is needed:
```cpp
// Option A: Add more metadata fields
metadata["slots"] = "0-8191";
metadata["migrations"] = "100->200,300->400";  // slot migrations
metadata["total_keys"] = "1234567";

// Option B: Base64 encode full fbs snapshot
auto snapshot = ClusterFlatbuffers::SerializeClusterSnapshot(...);
metadata["cluster_snapshot"] = Base64Encode(snapshot);
```

## Implementation Plan

### Phase 1: Slot Assignment Serialization (Core)

1. **Add slot serialization utilities** to `cluster_config.hpp`:
   ```cpp
   namespace SlotSerializer {
       // Serialize slot assignments to compact string
       std::string Serialize(const std::vector<std::pair<uint16_t, NodeId>>& assignments);

       // Deserialize compact string to slot assignments
       std::vector<std::pair<uint16_t, NodeId>> Deserialize(
           const std::string& data, const NodeId& owner_id);
   }
   ```

2. **Implement serialization logic**:
   - Group consecutive slots into ranges: `[0,1,2,3]` → `"0-3"`
   - Discrete slots: `[0,100,101]` → `"0,100,101"`
   - Mixed: `[0-3,100,101-103]` → `"0-3,100,101-103"`

3. **Update GossipManager** to support slot metadata:
   ```cpp
   void UpdateSlotMetadata(const std::string& slots_str);
   void BroadcastSlotChanges();
   ```

4. **Update WorkerCommandContext::ClusterAddSlots**:
   ```cpp
   // After updating local cluster_state:
   auto slots_str = SlotSerializer::Serialize(slot_assignments);
   gossip_manager_->UpdateSlotMetadata(slots_str);
   gossip_manager_->BroadcastSlotChanges();
   ```

5. **Update OnClusterEvent** to parse slot metadata:
   ```cpp
   if (node_view.metadata.contains("slots")) {
       auto slots = SlotSerializer::Deserialize(
           node_view.metadata.at("slots"),
           NodeIdFromString(node_view.metadata.at("node_id"))
       );
       UpdateClusterState(slots);
   }
   ```

### Phase 2: Version Control and Conflict Resolution

1. **Add config_epoch to metadata**:
   ```cpp
   metadata["config_epoch"] = std::to_string(config_epoch);
   ```

2. **Implement conflict resolution in OnClusterEvent**:
   ```cpp
   uint64_t remote_epoch = std::stoull(node_view.metadata.at("config_epoch"));
   if (remote_epoch > local_config_epoch) {
       // Remote is newer, accept update
       UpdateClusterState(...);
   } else {
       // Local is newer, reject update
       ASTRADB_LOG_WARN("Ignoring stale cluster state update");
   }
   ```

3. **Increment config_epoch on slot changes**:
   ```cpp
   config_epoch_++;
   ```

### Phase 3: Testing and Validation

1. **Unit tests** for serialization/deserialization:
   - Test continuous ranges: `[0-1000]`
   - Test discrete slots: `[0,100,200]`
   - Test mixed: `[0-100,200,300-400]`
   - Test edge cases: empty, single slot, all slots

2. **Integration tests**:
   - Start 3 nodes
   - Assign slots to different nodes
   - Verify all nodes see the same slot assignments
   - Test MOVED redirection works correctly

3. **Performance tests**:
   - Measure serialization/deserialization speed
   - Measure gossip transmission overhead
   - Verify minimal impact on cluster performance

### Phase 4: Future Enhancements (Optional)

1. **Slot migration support**:
   ```cpp
   metadata["migrations"] = "100->200,300->400";  // source->target
   ```

2. **Cluster statistics**:
   ```cpp
   metadata["total_keys"] = std::to_string(total_keys);
   metadata["memory_used"] = std::to_string(memory_used);
   ```

3. **Full fbs snapshot** (if needed):
   ```cpp
   auto snapshot = ClusterFlatbuffers::SerializeClusterSnapshot(...);
   metadata["cluster_snapshot"] = Base64Encode(snapshot);
   ```

## Code Changes Summary

### Files to Modify

1. **`src/astra/cluster/cluster_config.hpp`**:
   - Add `SlotSerializer` namespace with serialization utilities
   - Add config_epoch tracking

2. **`src/astra/cluster/gossip_manager.hpp`**:
   - Add `UpdateSlotMetadata()` method
   - Add `BroadcastSlotChanges()` method
   - Update `OnNodeEvent()` to parse slot metadata

3. **`src/astra/server/worker.hpp`**:
   - Update `ClusterAddSlots()` to broadcast slot changes
   - Update `ClusterDelSlots()` to broadcast slot changes

4. **`src/astra/server/server.cpp`**:
   - Update `ProcessClusterEventAsync()` to handle slot metadata

### New Dependencies

None - uses existing dependencies:
- `absl::strings` for string manipulation
- `absl::flat_hash_map` for metadata storage
- libgossip for metadata synchronization

## Success Criteria

- ✅ All nodes see consistent slot assignments
- ✅ MOVED redirection works correctly across nodes
- ✅ Slot changes are synchronized within 2 gossip cycles (~200ms)
- ✅ No performance degradation (>10% overhead)
- ✅ Conflict resolution handles concurrent slot changes
- ✅ Cluster state remains consistent after node failures

## Timeline Estimate

- **Phase 1**: 2-3 hours (core serialization and synchronization)
- **Phase 2**: 1-2 hours (version control and conflicts)
- **Phase 3**: 2-3 hours (testing and validation)
- **Phase 4**: Future work (optional enhancements)

**Total**: 5-8 hours for complete implementation

---

## MOVED/ASK Redirection Implementation Plan

### Overview

MOVED/ASK redirection is a core feature of Redis Cluster that allows clients to automatically route requests to the correct node.

- **MOVED Redirection**: Returned when a slot is permanently assigned to another node, client should update local cache
- **ASK Redirection**: Returned when a slot is in the process of migration, client should temporarily redirect without updating cache

### Implementation Approach

#### 1. Command-Level Slot Checking

Before command execution, check if the key's slot is owned by the current node:

```cpp
// Add in Worker::ExecuteCommand or similar location
std::vector<std::string> keys = CommandExtractor::ExtractKeys(command);

if (!keys.empty() && cluster_state->IsEnabled()) {
    for (const auto& key : keys) {
        uint16_t slot = HashSlotCalculator::CalculateWithTag(key);
        auto owner = cluster_state->GetSlotOwner(slot);
        
        if (owner.has_value() && *owner != my_node_id_) {
            // Return MOVED error
            return BuildMovedError(slot, owner);
        }
    }
}
```

#### 2. Existing Helper Functions (To Be Used)

The following functions are already defined in `cluster_commands.cpp` and need to be integrated into the command execution flow:

```cpp
// Calculate hash slot for a key (with hash tag support)
[[maybe_unused]] static uint16_t GetSlotForKey(const std::string& key) noexcept {
    return cluster::HashSlotCalculator::CalculateWithTag(key);
}

// Check if a key needs redirection
[[maybe_unused]] static std::optional<std::string> CheckKeyRedirect(
    const std::string& key, CommandContext* context) {
    return cluster::ClusterStateAccessor::CheckKeyRedirect(key);
}

// Build MOVED error response
[[maybe_unused]] static CommandResult BuildMovedError(
    uint16_t slot, const std::string& target_node) {
    std::string error = "-MOVED " + std::to_string(slot) + " " + target_node;
    protocol::RespValue resp;
    resp.SetString(error, protocol::RespType::kSimpleString);
    return CommandResult(false, error);
}

// Build ASK error response
[[maybe_unused]] static CommandResult BuildAskError(
    uint16_t slot, const std::string& target_node) {
    std::string error = "-ASK " + std::to_string(slot) + " " + target_node;
    protocol::RespValue resp;
    resp.SetString(error, protocol::RespType::kSimpleString);
    return CommandResult(false, error);
}
```

#### 3. Integration Point: Worker::ExecuteCommand

```cpp
CommandResult Worker::ExecuteCommand(const protocol::Command& command) {
    // ... existing code ...
    
    // If cluster mode is enabled and not a cluster command, check for redirection
    if (cluster_state_ && cluster_state_->IsEnabled() && 
        !IsClusterCommand(command.GetCommandName())) {
        
        auto redirect = CheckCommandRedirect(command);
        if (redirect.has_value()) {
            return *redirect; // Return MOVED/ASK error
        }
    }
    
    // ... continue command execution ...
}
```

#### 4. Key Extractor

Need to implement a helper function to extract all keys from a command:

```cpp
// cluster_config.hpp
class KeyExtractor {
public:
    // Extract all affected keys from a command
    static std::vector<std::string> ExtractKeys(const protocol::Command& command);
    
private:
    // Command key position mapping
    static const std::unordered_map<std::string, KeyPosition> kCommandKeyPositions;
};

// Implementation example
std::vector<std::string> KeyExtractor::ExtractKeys(const protocol::Command& command) {
    const auto& cmd_name = command.GetCommandName();
    auto it = kCommandKeyPositions.find(cmd_name);
    
    if (it == kCommandKeyPositions.end()) {
        return {}; // This command doesn't involve keys
    }
    
    std::vector<std::string> keys;
    const auto& args = command.GetArguments();
    
    for (int key_index : it->second.key_indices) {
        if (key_index < args.size()) {
            keys.push_back(args[key_index]);
        }
    }
    
    return keys;
}
```

### Implementation Steps

#### Phase 1: Redirection Check Foundation (Core)

1. **Implement KeyExtractor**:
   - Define key positions for all commands
   - Implement ExtractKeys function
   - Support multi-key commands like MSET/MGET

2. **Implement CheckCommandRedirect**:
   ```cpp
   std::optional<CommandResult> Worker::CheckCommandRedirect(
       const protocol::Command& command) {
       
       auto keys = KeyExtractor::ExtractKeys(command);
       if (keys.empty()) {
           return std::nullopt; // No keys, no check needed
       }
       
       // Check if all keys are in the same slot
       uint16_t first_slot = GetSlotForKey(keys[0]);
       for (const auto& key : keys) {
           if (GetSlotForKey(key) != first_slot) {
               // Keys not in same slot, return error
               return BuildCrossSlotError();
           }
       }
       
       // Check if slot is owned by current node
       auto owner = cluster_state_->GetSlotOwner(first_slot);
       if (!owner.has_value()) {
           return std::nullopt; // Slot not assigned, process locally
       }
       
       if (*owner != my_node_id_) {
           // Get target node's address
           auto target_node = cluster_state_->GetNodeInfo(*owner);
           if (target_node) {
               return BuildMovedError(first_slot, target_node->address);
           }
       }
       
       return std::nullopt;
   }
   ```

3. **Integrate into command execution flow**:
   - Modify `Worker::ExecuteCommand`
   - Add redirection check before execution
   - Skip cluster commands (CLUSTER INFO, CLUSTER NODES, etc.)

#### Phase 2: Multi-Key Command Handling

1. **Cross-slot error**:
   ```cpp
   CommandResult BuildCrossSlotError() {
       std::string error = "-CROSSSLOT Keys in request don't hash to the same slot";
       protocol::RespValue resp;
       resp.SetString(error, protocol::RespType::kSimpleString);
       return CommandResult(false, error);
   }
   ```

2. **Special command support**:
   - MSET/MGET: Allow different slots but need batch processing
   - SUNION/SDIFF: Allow different slots but need cross-node aggregation
   - ZUNION/ZINTER: Allow different slots but need cross-node aggregation

#### Phase 3: ASK Redirection (Optional)

For slot migration scenarios:

```cpp
// Check migration state in CheckCommandRedirect
if (cluster_state_->IsSlotMigrating(first_slot)) {
    auto importing_node = cluster_state_->GetSlotImportingFrom(first_slot);
    if (importing_node.has_value()) {
        return BuildAskError(first_slot, *importing_node);
    }
}
```

#### Phase 4: Testing

1. **Unit tests**:
   - Test GetSlotForKey correctness
   - Test KeyExtractor extracting keys from various commands
   - Test CheckCommandRedirect logic

2. **Integration tests**:
   - Start 3-node cluster
   - Node 2 assigned slots 4-7
   - Access slot 4 key from Node 1
   - Verify MOVED error is returned

3. **Redis client compatibility tests**:
   ```bash
   # Start cluster
   ./astradb --config config/astradb-node1.toml &
   ./astradb --config config/astradb-node2.toml &
   
   # Node 2 assign slot 4
   redis-cli -p 7002 CLUSTER ADDSLOTS 4
   
   # Node 1 try to access slot 4 key
   redis-cli -p 7001 SET test3330 value
   # Expected: -MOVED 4 127.0.0.1:7002
   
   # Use cluster mode client
   redis-cli -c -p 7001 SET test3330 value
   # Expected: Automatically redirect to Node 2, success
   ```

### Code Changes Summary

#### Files to Modify

1. **`src/astra/cluster/cluster_config.hpp`**:
   - Implement `KeyExtractor` class
   - Add command key position mapping

2. **`src/astra/server/worker.hpp`**:
   - Add `CheckCommandRedirect()` method
   - Modify `ExecuteCommand()` to add redirection check

3. **`src/astra/commands/cluster_commands.cpp`**:
   - Remove `[[maybe_unused]]` markers (functions will be officially used)

#### New Dependencies

None - uses existing dependencies

### Success Criteria

- ✅ Correctly identify key's slot
- ✅ Return MOVED error when slot not owned by current node
- ✅ Error format follows Redis specification: `-MOVED <slot> <ip:port>`
- ✅ Redis clients can automatically handle MOVED redirection
- ✅ Cross-slot multi-key commands return CROSSSLOT error
- ✅ Performance impact < 5% (slot checking overhead)

### Estimated Time

- **Phase 1**: 2-3 hours (basic redirection check)
- **Phase 2**: 1-2 hours (multi-key command handling)
- **Phase 3**: 1 hour (ASK redirection, optional)
- **Phase 4**: 2 hours (testing and validation)

**Total**: 6-8 hours for complete implementation