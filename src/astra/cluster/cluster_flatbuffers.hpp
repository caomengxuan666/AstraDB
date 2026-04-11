// ==============================================================================
// FlatBuffers Serializer for Cluster Communication
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#pragma once

#include <absl/time/time.h>

#include <memory>
#include <string>
#include <vector>

#include "astra/base/logging.hpp"
#include "astra/base/version.hpp"
#include "generated/cluster_message_generated.h"
#include "gossip_manager.hpp"
#include "shard_manager.hpp"

namespace astra::cluster {

// Local MigrationAction enum for conversion functions
enum class MigrationAction { kStable, kMigrating, kImporting };

// Local NodeInfo struct for serialization (not the FlatBuffers generated one)
struct NodeInfo {
  NodeId id;
  std::string ip;
  uint16_t port;
  uint16_t gossip_port;
  std::string role;
  libgossip::node_status status;
  uint64_t config_epoch;
  uint64_t heartbeat;
  uint32_t shard_count;
  uint64_t memory_used;
  uint64_t keys_count;
  std::string region;
  uint32_t replication_lag_ms;
};

// FlatBuffers serializer for cluster communication
class ClusterFlatbuffers {
 public:
  // Serialize heartbeat message
  static std::vector<uint8_t> SerializeHeartbeat(const NodeId& node_id,
                                                 uint64_t config_epoch,
                                                 uint64_t sequence) {
    flatbuffers::FlatBufferBuilder builder;

    auto now =
        absl::GetCurrentTimeNanos() / 1000000;  // Convert to milliseconds

    // Convert NodeId to FlatBuffers format
    auto node_id_struct = CreateNodeIdStruct(node_id);

    // Create heartbeat
    auto heartbeat_offset = AstraDB::Cluster::CreateHeartbeat(
        builder, &node_id_struct, config_epoch, sequence, now,
        0  // load_stats (empty for now)
    );

    // Create cluster message
    auto message_offset = AstraDB::Cluster::CreateClusterMessage(
        builder, AstraDB::Cluster::MessageType_Heartbeat, sequence, now,
        &node_id_struct,
        0,                // empty target node for broadcast
        heartbeat_offset  // heartbeat payload
    );

    builder.Finish(message_offset);

    return std::vector<uint8_t>(builder.GetBufferPointer(),
                                builder.GetBufferPointer() + builder.GetSize());
  }

  // Serialize node state update
  static std::vector<uint8_t> SerializeNodeState(const NodeInfo& node_info) {
    flatbuffers::FlatBufferBuilder builder;

    auto now = absl::GetCurrentTimeNanos() / 1000000;

    // Serialize node info
    auto node_id_struct = CreateNodeIdStruct(node_info.id);
    auto ip_offset = builder.CreateString(node_info.ip);
    auto region_offset = builder.CreateString(node_info.region);

    auto node_info_offset = AstraDB::Cluster::CreateNodeInfo(
        builder, &node_id_struct, ip_offset, node_info.port,
        node_info.gossip_port, ConvertNodeRole(node_info.role),
        ConvertNodeStatus(node_info.status), node_info.config_epoch,
        node_info.heartbeat, node_info.shard_count, node_info.memory_used,
        node_info.keys_count, region_offset, node_info.replication_lag_ms);

    // Create cluster message
    auto message_offset = AstraDB::Cluster::CreateClusterMessage(
        builder, AstraDB::Cluster::MessageType_NodeStateUpdate,
        node_info.config_epoch, now, &node_id_struct,
        0,                // empty target node for broadcast
        0,                // heartbeat
        node_info_offset  // node_state payload
    );

    builder.Finish(message_offset);

    return std::vector<uint8_t>(builder.GetBufferPointer(),
                                builder.GetBufferPointer() + builder.GetSize());
  }

  // Serialize slot assignment
  static std::vector<uint8_t> SerializeSlotAssignment(
      uint16_t slot, const NodeId& node_id, MigrationAction action,
      const NodeId& migration_source, const NodeId& migration_target) {
    flatbuffers::FlatBufferBuilder builder;

    auto now = absl::GetCurrentTimeNanos() / 1000000;

    auto node_id_struct = CreateNodeIdStruct(node_id);
    auto source_struct = CreateNodeIdStruct(migration_source);
    auto target_struct = CreateNodeIdStruct(migration_target);

    auto slot_assignment_offset = AstraDB::Cluster::CreateSlotAssignment(
        builder, slot, &node_id_struct, ConvertMigrationAction(action),
        &source_struct, &target_struct);

    // Create cluster message
    auto message_offset = AstraDB::Cluster::CreateClusterMessage(
        builder, AstraDB::Cluster::MessageType_SlotAssignment,
        0,  // sequence
        now, &node_id_struct,
        0,                      // empty target node for broadcast
        0,                      // heartbeat
        0,                      // node_state
        slot_assignment_offset  // slot_assignment payload
    );

    builder.Finish(message_offset);

    return std::vector<uint8_t>(builder.GetBufferPointer(),
                                builder.GetBufferPointer() + builder.GetSize());
  }

  // Serialize data migration
  static std::vector<uint8_t> SerializeDataMigration(
      const NodeId& source, const NodeId& target, uint16_t slot,
      const std::vector<std::pair<std::string, std::string>>& key_values,
      uint64_t batch_id, bool is_last) {
    flatbuffers::FlatBufferBuilder builder;

    auto now = absl::GetCurrentTimeNanos() / 1000000;

    auto source_struct = CreateNodeIdStruct(source);
    auto target_struct = CreateNodeIdStruct(target);

    // Serialize key-values
    std::vector<flatbuffers::Offset<AstraDB::Cluster::KeyValue>> kv_offsets;
    for (const auto& [key, value] : key_values) {
      auto key_offset = builder.CreateString(key);
      auto value_data = builder.CreateVector(
          reinterpret_cast<const uint8_t*>(value.data()), value.size());
      auto kv_offset =
          AstraDB::Cluster::CreateKeyValue(builder, key_offset, value_data,
                                           0,  // ttl_ms
                                           0,  // db_index
                                           0   // value_type (string)
          );
      kv_offsets.push_back(kv_offset);
    }

    auto kvs_offset = builder.CreateVector(kv_offsets);

    auto migration_offset = AstraDB::Cluster::CreateDataMigration(
        builder, &source_struct, &target_struct, slot, kvs_offset, batch_id,
        static_cast<uint32_t>(key_values.size()),
        static_cast<uint32_t>(0),  // current_offset
        is_last);

    // Create cluster message
    auto message_offset = AstraDB::Cluster::CreateClusterMessage(
        builder, AstraDB::Cluster::MessageType_DataMigration,
        0,  // sequence
        now, &source_struct,
        &target_struct,   // target node
        0,                // heartbeat
        0,                // node_state
        0,                // slot_assignment
        migration_offset  // data_migration payload
    );

    builder.Finish(message_offset);

    return std::vector<uint8_t>(builder.GetBufferPointer(),
                                builder.GetBufferPointer() + builder.GetSize());
  }

  // Serialize cluster snapshot
  static std::vector<uint8_t> SerializeClusterSnapshot(
      const std::vector<NodeInfo>& nodes,
      const std::vector<std::pair<uint16_t, NodeId>>& slot_assignments,
      uint64_t config_epoch, uint64_t total_keys) {
    flatbuffers::FlatBufferBuilder builder;

    auto now = absl::GetCurrentTimeNanos() / 1000000;

    // Serialize nodes
    std::vector<flatbuffers::Offset<AstraDB::Cluster::NodeInfo>> node_offsets;
    for (const auto& node : nodes) {
      auto node_id_struct = CreateNodeIdStruct(node.id);
      auto ip_offset = builder.CreateString(node.ip);
      auto region_offset = builder.CreateString(node.region);

      auto node_offset = AstraDB::Cluster::CreateNodeInfo(
          builder, &node_id_struct, ip_offset, node.port, node.gossip_port,
          ConvertNodeRole(node.role), ConvertNodeStatus(node.status),
          node.config_epoch, node.heartbeat, node.shard_count, node.memory_used,
          node.keys_count, region_offset, node.replication_lag_ms);
      node_offsets.push_back(node_offset);
    }
    auto nodes_offset = builder.CreateVector(node_offsets);

    // Serialize slot assignments
    std::vector<flatbuffers::Offset<AstraDB::Cluster::SlotAssignment>>
        slot_offsets;
    for (const auto& [slot, node_id] : slot_assignments) {
      auto node_id_struct = CreateNodeIdStruct(node_id);
      auto empty_source = CreateNodeIdStruct(NodeId{});
      auto empty_target = CreateNodeIdStruct(NodeId{});

      auto slot_offset = AstraDB::Cluster::CreateSlotAssignment(
          builder, slot, &node_id_struct,
          AstraDB::Cluster::MigrationAction_Stable,
          &empty_source,  // empty source
          &empty_target   // empty target
      );
      slot_offsets.push_back(slot_offset);
    }
    auto slots_offset = builder.CreateVector(slot_offsets);

    // Create cluster snapshot
    auto snapshot_offset = AstraDB::Cluster::CreateClusterSnapshot(
        builder, nodes_offset, slots_offset, config_epoch,
        16384,  // total_slots
        total_keys, now);

    // Create cluster message
    auto empty_node_id = CreateNodeIdStruct(NodeId{});
    auto message_offset = AstraDB::Cluster::CreateClusterMessage(
        builder, AstraDB::Cluster::MessageType_SyncResponse,
        0,  // sequence
        now,
        &empty_node_id,  // empty source
        0,               // empty target
        0,               // heartbeat
        0,               // node_state
        0,               // slot_assignment
        0,               // data_migration
        0,               // config_change
        snapshot_offset  // cluster_snapshot payload
    );

    builder.Finish(message_offset);

    return std::vector<uint8_t>(builder.GetBufferPointer(),
                                builder.GetBufferPointer() + builder.GetSize());
  }

  // Deserialize cluster message
  static bool DeserializeMessage(const uint8_t* data, size_t size,
                                 AstraDB::Cluster::MessageType& out_type,
                                 uint64_t& out_sequence, NodeId& out_source) {
    if (!data || size == 0) {
      return false;
    }

    auto message = AstraDB::Cluster::GetClusterMessage(data);
    if (!message) {
      return false;
    }

    out_type =
        static_cast<AstraDB::Cluster::MessageType>(message->message_type());
    out_sequence = message->sequence();

    if (message->source_node()) {
      DeserializeNodeId(message->source_node(), out_source);
    }

    return true;
  }

  // Deserialize heartbeat
  static bool DeserializeHeartbeat(const uint8_t* data, size_t size,
                                   NodeId& node_id, uint64_t& config_epoch,
                                   uint64_t& sequence, uint64_t& timestamp) {
    if (!data || size < sizeof(flatbuffers::uoffset_t)) {
      return false;
    }

    try {
      auto message = AstraDB::Cluster::GetClusterMessage(data);
      if (!message || !message->heartbeat()) {
        return false;
      }

      auto* heartbeat = message->heartbeat();
      if (heartbeat->node_id()) {
        DeserializeNodeId(heartbeat->node_id(), node_id);
      }

      config_epoch = heartbeat->config_epoch();
      sequence = heartbeat->sequence();
      timestamp = heartbeat->timestamp();

      return true;
    } catch (...) {
      // Catch any exceptions from invalid data access
      return false;
    }
  }

  // Deserialize node state
  static bool DeserializeNodeState(const uint8_t* data, size_t size,
                                   NodeInfo& out_node_info) {
    if (!data || size == 0) {
      return false;
    }

    auto message = AstraDB::Cluster::GetClusterMessage(data);
    if (!message || !message->node_state()) {
      return false;
    }

    auto* node_info = message->node_state();

    if (node_info->id()) {
      DeserializeNodeId(node_info->id(), out_node_info.id);
    }

    out_node_info.ip = node_info->ip()->str();
    out_node_info.port = node_info->port();
    out_node_info.gossip_port = node_info->gossip_port();
    out_node_info.role = ConvertFromNodeRole(node_info->role());
    out_node_info.status = ConvertFromNodeStatus(node_info->status());
    out_node_info.config_epoch = node_info->config_epoch();
    out_node_info.heartbeat = node_info->heartbeat();
    out_node_info.shard_count = node_info->shard_count();
    out_node_info.memory_used = node_info->memory_used();
    out_node_info.keys_count = node_info->keys_count();
    out_node_info.region = node_info->region()->str();
    out_node_info.replication_lag_ms = node_info->replication_lag_ms();

    return true;
  }

  // Deserialize data migration
  static bool DeserializeDataMigration(
      const uint8_t* data, size_t size, NodeId& source, NodeId& target,
      uint16_t& slot,
      std::vector<std::pair<std::string, std::string>>& key_values,
      uint64_t& batch_id, bool& is_last) {
    if (!data || size == 0) {
      return false;
    }

    auto message = AstraDB::Cluster::GetClusterMessage(data);
    if (!message || !message->data_migration()) {
      return false;
    }

    auto* migration = message->data_migration();

    if (migration->source_node()) {
      DeserializeNodeId(migration->source_node(), source);
    }
    if (migration->target_node()) {
      DeserializeNodeId(migration->target_node(), target);
    }

    slot = migration->slot();
    batch_id = migration->batch_id();
    is_last = migration->is_last();

    // Deserialize key-values
    key_values.clear();
    if (migration->key_values()) {
      for (const auto* kv : *migration->key_values()) {
        if (kv && kv->key() && kv->value()) {
          std::string key = kv->key()->str();
          std::string value(reinterpret_cast<const char*>(kv->value()->data()),
                            kv->value()->size());
          key_values.emplace_back(std::move(key), std::move(value));
        }
      }
    }

    return true;
  }

  // Get message size (without parsing)
  static size_t GetMessageSize(const uint8_t* data) {
    if (!data) {
      return 0;
    }
    auto message = AstraDB::Cluster::GetClusterMessage(data);
    if (!message) {
      return 0;
    }
    // For FlatBuffers, we can get the size from the buffer
    // The size is stored as a uoffset_t at the beginning of the buffer
    auto size = flatbuffers::GetPrefixedSize(data);
    return size ? size
                : sizeof(flatbuffers::uoffset_t);  // Fallback to minimum size
  }

 private:
  // Helper: Serialize NodeId
  static AstraDB::Cluster::NodeId CreateNodeIdStruct(const NodeId& id) {
    // Create a NodeId struct from the input
    AstraDB::Cluster::NodeId node_id;
    auto bytes_ptr = const_cast<uint8_t*>(node_id.bytes()->data());
    std::copy(id.begin(), id.end(), bytes_ptr);
    return node_id;
  }

  // Helper: Deserialize NodeId
  static void DeserializeNodeId(const AstraDB::Cluster::NodeId* fb_node_id,
                                NodeId& out_id) {
    if (!fb_node_id || !fb_node_id->bytes()) {
      out_id = NodeId{};
      return;
    }

    auto bytes = fb_node_id->bytes();
    if (bytes->size() == 16) {
      std::copy(bytes->begin(), bytes->end(), out_id.begin());
    }
  }

  // Helper: Convert NodeRole
  static AstraDB::Cluster::NodeRole ConvertNodeRole(const std::string& role) {
    if (role == "master") return AstraDB::Cluster::NodeRole_Master;
    if (role == "replica") return AstraDB::Cluster::NodeRole_Replica;
    if (role == "arbiter") return AstraDB::Cluster::NodeRole_Arbiter;
    return AstraDB::Cluster::NodeRole_Master;
  }

  // Helper: Convert from NodeRole
  static std::string ConvertFromNodeRole(AstraDB::Cluster::NodeRole role) {
    switch (role) {
      case AstraDB::Cluster::NodeRole_Master:
        return "master";
      case AstraDB::Cluster::NodeRole_Replica:
        return "replica";
      case AstraDB::Cluster::NodeRole_Arbiter:
        return "arbiter";
      default:
        return "master";
    }
  }

  // Helper: Convert NodeStatus
  static AstraDB::Cluster::NodeStatus ConvertNodeStatus(
      libgossip::node_status status) {
    switch (status) {
      case libgossip::node_status::online:
        return AstraDB::Cluster::NodeStatus_Online;
      case libgossip::node_status::suspect:
        return AstraDB::Cluster::NodeStatus_Suspect;
      case libgossip::node_status::failed:
        return AstraDB::Cluster::NodeStatus_Failed;
      case libgossip::node_status::joining:
        return AstraDB::Cluster::NodeStatus_Online;  // Map joining to online
      case libgossip::node_status::unknown:
        return AstraDB::Cluster::NodeStatus_Failed;  // Map unknown to failed
      default:
        return AstraDB::Cluster::NodeStatus_Online;
    }
  }

  // Helper: Convert from NodeStatus
  static libgossip::node_status ConvertFromNodeStatus(
      AstraDB::Cluster::NodeStatus status) {
    switch (status) {
      case AstraDB::Cluster::NodeStatus_Online:
        return libgossip::node_status::online;
      case AstraDB::Cluster::NodeStatus_Suspect:
        return libgossip::node_status::suspect;
      case AstraDB::Cluster::NodeStatus_Failed:
        return libgossip::node_status::failed;
      case AstraDB::Cluster::NodeStatus_Leaving:
        return libgossip::node_status::failed;  // Map leaving to failed
      default:
        return libgossip::node_status::online;
    }
  }

  // Helper: Convert MigrationAction
  static AstraDB::Cluster::MigrationAction ConvertMigrationAction(
      MigrationAction action) {
    switch (action) {
      case MigrationAction::kImporting:
        return AstraDB::Cluster::MigrationAction_Importing;
      case MigrationAction::kMigrating:
        return AstraDB::Cluster::MigrationAction_Migrating;
      case MigrationAction::kStable:
        return AstraDB::Cluster::MigrationAction_Stable;
      default:
        return AstraDB::Cluster::MigrationAction_Stable;
    }
  }
};

}  // namespace astra::cluster
