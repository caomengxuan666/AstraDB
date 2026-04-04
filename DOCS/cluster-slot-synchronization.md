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

## MOVED/ASK 重定向实现计划

### 概述

MOVED/ASK 重定向是 Redis Cluster 的核心功能，允许客户端自动将请求路由到正确的节点。

- **MOVED 重定向**: 当 slot 永久分配到其他节点时返回，客户端应更新本地缓存
- **ASK 重定向**: 当 slot 正在迁移过程中时返回，客户端应临时重定向，不更新缓存

### 实现方案

#### 1. 命令级别的 Slot 检查

在命令执行前，检查 key 所属的 slot 是否由当前节点负责：

```cpp
// 在 Worker::ExecuteCommand 或类似位置添加
std::vector<std::string> keys = CommandExtractor::ExtractKeys(command);

if (!keys.empty() && cluster_state->IsEnabled()) {
    for (const auto& key : keys) {
        uint16_t slot = HashSlotCalculator::CalculateWithTag(key);
        auto owner = cluster_state->GetSlotOwner(slot);
        
        if (owner.has_value() && *owner != my_node_id_) {
            // 返回 MOVED 错误
            return BuildMovedError(slot, owner);
        }
    }
}
```

#### 2. 已存在的辅助函数（待使用）

以下函数已经在 `cluster_commands.cpp` 中定义，需要集成到命令执行流程：

```cpp
// 计算 key 的 hash slot（支持 hash tag）
[[maybe_unused]] static uint16_t GetSlotForKey(const std::string& key) noexcept {
    return cluster::HashSlotCalculator::CalculateWithTag(key);
}

// 检查 key 是否需要重定向
[[maybe_unused]] static std::optional<std::string> CheckKeyRedirect(
    const std::string& key, CommandContext* context) {
    return cluster::ClusterStateAccessor::CheckKeyRedirect(key);
}

// 构建 MOVED 错误响应
[[maybe_unused]] static CommandResult BuildMovedError(
    uint16_t slot, const std::string& target_node) {
    std::string error = "-MOVED " + std::to_string(slot) + " " + target_node;
    protocol::RespValue resp;
    resp.SetString(error, protocol::RespType::kSimpleString);
    return CommandResult(false, error);
}

// 构建 ASK 错误响应
[[maybe_unused]] static CommandResult BuildAskError(
    uint16_t slot, const std::string& target_node) {
    std::string error = "-ASK " + std::to_string(slot) + " " + target_node;
    protocol::RespValue resp;
    resp.SetString(error, protocol::RespType::kSimpleString);
    return CommandResult(false, error);
}
```

#### 3. 集成点：Worker::ExecuteCommand

```cpp
CommandResult Worker::ExecuteCommand(const protocol::Command& command) {
    // ... 现有代码 ...
    
    // 如果启用集群模式且不是集群命令，检查重定向
    if (cluster_state_ && cluster_state_->IsEnabled() && 
        !IsClusterCommand(command.GetCommandName())) {
        
        auto redirect = CheckCommandRedirect(command);
        if (redirect.has_value()) {
            return *redirect; // 返回 MOVED/ASK 错误
        }
    }
    
    // ... 继续执行命令 ...
}
```

#### 4. Key 提取器

需要实现一个辅助函数来从命令中提取所有 key：

```cpp
// cluster_config.hpp
class KeyExtractor {
public:
    // 从命令中提取所有受影响的 key
    static std::vector<std::string> ExtractKeys(const protocol::Command& command);
    
private:
    // 命令 key 位置映射
    static const std::unordered_map<std::string, KeyPosition> kCommandKeyPositions;
};

// 实现示例
std::vector<std::string> KeyExtractor::ExtractKeys(const protocol::Command& command) {
    const auto& cmd_name = command.GetCommandName();
    auto it = kCommandKeyPositions.find(cmd_name);
    
    if (it == kCommandKeyPositions.end()) {
        return {}; // 该命令不涉及 key
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

### 实现步骤

#### Phase 1: 重定向检查基础（核心）

1. **实现 KeyExtractor**:
   - 定义所有命令的 key 位置
   - 实现 ExtractKeys 函数
   - 支持 MSET/MGET 等多 key 命令

2. **实现 CheckCommandRedirect**:
   ```cpp
   std::optional<CommandResult> Worker::CheckCommandRedirect(
       const protocol::Command& command) {
       
       auto keys = KeyExtractor::ExtractKeys(command);
       if (keys.empty()) {
           return std::nullopt; // 无 key，无需检查
       }
       
       // 检查所有 key 是否在同一个 slot
       uint16_t first_slot = GetSlotForKey(keys[0]);
       for (const auto& key : keys) {
           if (GetSlotForKey(key) != first_slot) {
               // keys 不在同一 slot，返回错误
               return BuildCrossSlotError();
           }
       }
       
       // 检查 slot 是否由当前节点负责
       auto owner = cluster_state_->GetSlotOwner(first_slot);
       if (!owner.has_value()) {
           return std::nullopt; // slot 未分配，本地处理
       }
       
       if (*owner != my_node_id_) {
           // 获取目标节点的地址
           auto target_node = cluster_state_->GetNodeInfo(*owner);
           if (target_node) {
               return BuildMovedError(first_slot, target_node->address);
           }
       }
       
       return std::nullopt;
   }
   ```

3. **集成到命令执行流程**:
   - 修改 `Worker::ExecuteCommand`
   - 在执行前添加重定向检查
   - 跳过集群命令（CLUSTER INFO, CLUSTER NODES 等）

#### Phase 2: 多 key 命令处理

1. **验证跨 slot 错误**:
   ```cpp
   CommandResult BuildCrossSlotError() {
       std::string error = "-CROSSSLOT Keys in request don't hash to the same slot";
       protocol::RespValue resp;
       resp.SetString(error, protocol::RespType::kSimpleString);
       return CommandResult(false, error);
   }
   ```

2. **支持的特殊命令**:
   - MSET/MGET: 允许不同 slot，但需要分批处理
   - SUNION/SDIFF: 允许不同 slot，需要跨节点聚合
   - ZUNION/ZINTER: 允许不同 slot，需要跨节点聚合

#### Phase 3: ASK 重定向（可选）

用于 slot 迁移场景：

```cpp
// 在 CheckCommandRedirect 中检查迁移状态
if (cluster_state_->IsSlotMigrating(first_slot)) {
    auto importing_node = cluster_state_->GetSlotImportingFrom(first_slot);
    if (importing_node.has_value()) {
        return BuildAskError(first_slot, *importing_node);
    }
}
```

#### Phase 4: 测试

1. **单元测试**:
   - 测试 GetSlotForKey 正确性
   - 测试 KeyExtractor 提取各种命令的 key
   - 测试 CheckCommandRedirect 逻辑

2. **集成测试**:
   - 启动 3 节点集群
   - Node 2 分配 slots 4-7
   - 从 Node 1 访问 slot 4 的 key
   - 验证返回 MOVED 错误

3. **Redis 客户端兼容性测试**:
   ```bash
   # 启动集群
   ./astradb --config config/astradb-node1.toml &
   ./astradb --config config/astradb-node2.toml &
   
   # Node 2 分配 slot 4
   redis-cli -p 7002 CLUSTER ADDSLOTS 4
   
   # Node 1 尝试访问 slot 4 的 key
   redis-cli -p 7001 SET test3330 value
   # 期望: -MOVED 4 127.0.0.1:7002
   
   # 使用集群模式客户端
   redis-cli -c -p 7001 SET test3330 value
   # 期望: 自动重定向到 Node 2，成功
   ```

### 代码变更总结

#### 需要修改的文件

1. **`src/astra/cluster/cluster_config.hpp`**:
   - 实现 `KeyExtractor` 类
   - 添加命令 key 位置映射

2. **`src/astra/server/worker.hpp`**:
   - 添加 `CheckCommandRedirect()` 方法
   - 修改 `ExecuteCommand()` 添加重定向检查

3. **`src/astra/commands/cluster_commands.cpp`**:
   - 移除 `[[maybe_unused]]` 标记（函数将正式使用）

#### 新增依赖

无 - 使用现有依赖

### 成功标准

- ✅ 正确识别 key 所属 slot
- ✅ slot 不属于当前节点时返回 MOVED 错误
- ✅ 错误格式符合 Redis 规范: `-MOVED <slot> <ip:port>`
- ✅ Redis 客户端能够自动处理 MOVED 重定向
- ✅ 跨 slot 多 key 命令返回 CROSSSLOT 错误
- ✅ 性能影响 < 5%（slot 检查开销）

### 预计时间

- **Phase 1**: 2-3 小时（基础重定向检查）
- **Phase 2**: 1-2 小时（多 key 命令处理）
- **Phase 3**: 1 小时（ASK 重定向，可选）
- **Phase 4**: 2 小时（测试和验证）

**总计**: 6-8 小时完成完整实现

### 参考资料

- Redis Cluster 重定向规范: https://redis.io/docs/manual/scaling/
- Redis CRC16 算法: https://redis.io/docs/reference/cluster-spec/#key-distribution-model
- ASK vs MOVED: https://redis.io/docs/manual/scaling/#moved-redirection

## References

- Redis Cluster Specification: https://redis.io/docs/manual/scaling/
- libgossip Documentation: https://github.com/libgossip/libgossip
- FlatBuffers Documentation: https://google.github.io/flatbuffers/