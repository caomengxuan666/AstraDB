// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include "cluster_commands.hpp"

#include <set>
#include <vector>

#include "astra/base/logging.hpp"
#include "astra/cluster/cluster_config.hpp"
#include "astra/cluster/gossip_manager.hpp"
#include "astra/cluster/shard_manager.hpp"
#include "astra/server/server.hpp"
#include "core/gossip_core.hpp"  // For libgossip types

namespace astra::commands {

// Get cluster slot for a key with hash tag support
[[maybe_unused]] static uint16_t GetSlotForKey(
    const std::string& key) noexcept {
  return cluster::HashSlotCalculator::CalculateWithTag(key);
}

// Check if a key should be redirected to another node
// Returns std::nullopt if the key can be processed locally, otherwise returns
// the target node ID
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

// CLUSTER INFO - Get cluster information
CommandResult HandleClusterInfo(const protocol::Command& command,
                                CommandContext* context) {
  if (command.ArgCount() != 0) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'cluster info' command");
  }

  // Use ClusterStateAccessor for NO SHARING architecture
  auto* cluster_state = cluster::ClusterStateAccessor::Get();

  // Check if cluster is enabled
  if (!cluster_state || !cluster_state->IsEnabled()) {
    std::string info = "cluster_state:fail\n";
    protocol::RespValue resp;
    resp.SetString(info, protocol::RespType::kBulkString);
    return CommandResult(resp);
  }

  // Build cluster info string
  std::string info;

  // Cluster state
  info += "cluster_state:ok\n";

  // Slots information
  uint32_t assigned_slots = 0;
  for (uint16_t slot = 0; slot < 16384; ++slot) {
    if (cluster_state->GetSlotOwner(slot).has_value()) {
      assigned_slots++;
    }
  }
  info += "cluster_slots_assigned:" + std::to_string(assigned_slots) + "\n";
  info += "cluster_slots_ok:" + std::to_string(assigned_slots) + "\n";
  info += "cluster_slots_pfail:0\n";
  info += "cluster_slots_fail:0\n";

  // Nodes information
  const auto& nodes = cluster_state->GetNodes();
  info += "cluster_known_nodes:" + std::to_string(nodes.size()) + "\n";
  info += "cluster_size:" + std::to_string(nodes.size()) + "\n";

  // Epoch information (default values for now)
  info += "cluster_current_epoch:1\n";
  info += "cluster_my_epoch:1\n";

  // Gossip statistics (not available in NO SHARING mode)
  info += "cluster_stats_messages_sent:0\n";
  info += "cluster_stats_messages_received:0\n";

  protocol::RespValue resp;
  resp.SetString(info, protocol::RespType::kBulkString);
  return CommandResult(resp);
}

// CLUSTER NODES - List all nodes in the cluster (NO SHARING architecture)
CommandResult HandleClusterNodes(const protocol::Command& command,
                                 CommandContext* context) {
  if (command.ArgCount() != 0) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'cluster nodes' command");
  }

  // Use ClusterStateAccessor for NO SHARING architecture
  auto* cluster_state = cluster::ClusterStateAccessor::Get();

  // Check if cluster is enabled
  if (!cluster_state || !cluster_state->IsEnabled()) {
    std::string result = "";
    protocol::RespValue resp;
    resp.SetString(result, protocol::RespType::kBulkString);
    return CommandResult(resp);
  }

  // Get all nodes from cluster state
  const auto& nodes = cluster_state->GetNodes();

  ASTRADB_LOG_DEBUG("CLUSTER NODES: {} nodes in cluster state", nodes.size());

  // Build nodes string in Redis cluster format
  std::string result;
  for (const auto& [node_id, node] : nodes) {
    ASTRADB_LOG_DEBUG("CLUSTER NODES: processing node_id={}", node_id);

    // Format: <id> <ip>:<port@bus-port> <flags> <master> <ping-sent>
    // <pong-recv> <config-epoch> <link-state> <slot> <slot> ... <slot>
    std::string node_str = node_id;
    node_str += " " + node.ip + ":" + std::to_string(node.port) + "@" +
                std::to_string(node.bus_port);

    // Flags
    uint32_t flags = 0;
    if (node.role == cluster::ClusterRole::kMaster) {
      flags |= 0x1000;  // master
    } else if (node.role == cluster::ClusterRole::kSlave) {
      flags |= 0x2000;  // slave
    }
    node_str += " " + std::to_string(flags);

    // Master ID (self for master nodes)
    if (node.role == cluster::ClusterRole::kSlave) {
      node_str += " " + node.master_id;
    } else {
      node_str += " -";
    }

    // Ping and pong timestamps
    node_str += " " + std::to_string(0) + " " + std::to_string(0);

    // Config epoch
    node_str += " " + std::to_string(node.config_epoch);

    // Link state
    node_str += " connected";

    // Slots (find all slots owned by this node)
    std::string slots_str;
    uint16_t slot_start = 0;
    bool in_range = false;
    int owned_slots = 0;
    for (uint16_t slot = 0; slot < 16384; ++slot) {
      auto owner = cluster_state->GetSlotOwner(slot);
      if (owner.has_value()) {
        if (slot < 10 || (slot >= 4 && slot <= 7)) {
          ASTRADB_LOG_DEBUG("CLUSTER NODES: slot {} owner={}", slot,
                            owner.value());
        }
      }
      if (owner == node_id) {
        owned_slots++;
        if (!in_range) {
          slot_start = slot;
          in_range = true;
        }
      } else {
        if (in_range) {
          if (slot_start == slot - 1) {
            slots_str += " " + std::to_string(slot_start);
          } else {
            slots_str += " " + std::to_string(slot_start) + "-" +
                         std::to_string(slot - 1);
          }
          in_range = false;
        }
      }
    }
    // Handle last range
    if (in_range) {
      slots_str += " " + std::to_string(slot_start) + "-16383";
    }

    ASTRADB_LOG_DEBUG("CLUSTER NODES: node_id={} owns {} slots, slots_str='{}'",
                      node_id, owned_slots, slots_str);

    node_str += slots_str;

    if (!result.empty()) {
      result += "\n";
    }
    result += node_str;
  }

  protocol::RespValue resp;
  resp.SetString(result, protocol::RespType::kBulkString);
  return CommandResult(resp);
}

// CLUSTER MEET - Add a node to the cluster (NO SHARING architecture)
CommandResult HandleClusterMeet(const protocol::Command& command,
                                CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'cluster meet' command");
  }

  auto* server = static_cast<server::Server*>(context->GetServer());
  auto* gossip_manager = server->GetGossipManager();

  if (!gossip_manager) {
    return CommandResult(false,
                         "ERR This instance has cluster support disabled");
  }

  std::string ip = command[0].AsString();
  std::string port_str = command[1].AsString();

  try {
    int port = std::stoi(port_str);
    if (gossip_manager->MeetNode(ip, port)) {
      return CommandResult(true, "OK");
    } else {
      return CommandResult(false, "ERR Failed to meet node");
    }
  } catch (const std::exception& e) {
    return CommandResult(false, "ERR Invalid port number");
  }
}

// CLUSTER FORGET - Remove a node from the cluster
CommandResult HandleClusterForget(const protocol::Command& command,
                                  CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'cluster forget' command");
  }

  return CommandResult(false, "ERR This instance has cluster support disabled");
}

// CLUSTER SLOTS - Get cluster slots mapping (NO SHARING architecture)
CommandResult HandleClusterSlots(const protocol::Command& command,
                                 CommandContext* context) {
  if (command.ArgCount() != 0) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'cluster slots' command");
  }

  // Use ClusterStateAccessor for NO SHARING architecture
  auto* cluster_state = cluster::ClusterStateAccessor::Get();

  // Check if cluster is enabled
  if (!cluster_state || !cluster_state->IsEnabled()) {
    // Return empty array for disabled cluster
    protocol::RespValue resp;
    resp.SetArray({});
    return CommandResult(resp);
  }

  // Build slots array in Redis Cluster format
  // Format: [[start, end, [ip, port, node_id], [ip, port, node_id]], ...]
  std::vector<protocol::RespValue> slots_array;

  // Group slots by node
  absl::flat_hash_map<std::string, std::vector<std::pair<uint16_t, uint16_t>>>
      node_slots;
  uint16_t slot_start = 0;
  std::string current_owner;
  bool in_range = false;

  for (uint16_t slot = 0; slot < 16384; ++slot) {
    auto owner = cluster_state->GetSlotOwner(slot);
    if (owner) {
      if (owner != current_owner) {
        // End previous range
        if (in_range && !current_owner.empty()) {
          node_slots[current_owner].push_back({slot_start, slot - 1});
        }
        // Start new range
        current_owner = *owner;
        slot_start = slot;
        in_range = true;
      }
    } else {
      // End current range
      if (in_range && !current_owner.empty()) {
        node_slots[current_owner].push_back({slot_start, slot - 1});
        current_owner.clear();
        in_range = false;
      }
    }
  }

  // Handle last range
  if (in_range && !current_owner.empty()) {
    node_slots[current_owner].push_back({slot_start, 16383});
  }

  // Build response array
  for (const auto& [node_id, ranges] : node_slots) {
    auto node = cluster_state->GetNode(node_id);
    if (!node) continue;

    for (const auto& [start, end] : ranges) {
      std::vector<protocol::RespValue> slot_info;

      // Slot range
      slot_info.push_back(protocol::RespValue(static_cast<int64_t>(start)));
      slot_info.push_back(protocol::RespValue(static_cast<int64_t>(end)));

      // Master node info
      std::vector<protocol::RespValue> master_info;
      master_info.push_back(protocol::RespValue(node->ip));
      master_info.push_back(
          protocol::RespValue(static_cast<int64_t>(node->port)));
      master_info.push_back(protocol::RespValue(node_id));
      slot_info.push_back(protocol::RespValue(master_info));

      // Replica node info (for now, empty)
      // In the future, we'll add replicas here

      slots_array.push_back(protocol::RespValue(slot_info));
    }
  }

  protocol::RespValue resp;
  resp.SetArray(slots_array);
  return CommandResult(resp);
}

// CLUSTER REPLICAS - List replicas of a node
CommandResult HandleClusterReplicas(const protocol::Command& command,
                                    CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'cluster replicas' command");
  }

  protocol::RespValue resp;
  resp.SetArray({});

  return CommandResult(resp);
}

// CLUSTER ADDSLOTS - Assign slots to a node
CommandResult HandleClusterAddSlots(const protocol::Command& command,
                                    CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'cluster addslots' command");
  }

  // Parse slot numbers
  std::vector<uint16_t> slots;
  for (size_t i = 0; i < command.ArgCount(); ++i) {
    try {
      int slot = std::stoi(command[i].AsString());
      if (slot < 0 || slot >= 16384) {
        return CommandResult(false, "ERR Invalid slot number");
      }
      slots.push_back(static_cast<uint16_t>(slot));
    } catch (const std::exception& e) {
      return CommandResult(false, "ERR Invalid slot number");
    }
  }

  // Check for duplicate slots
  std::set<uint16_t> slot_set(slots.begin(), slots.end());
  if (slot_set.size() != slots.size()) {
    return CommandResult(false, "ERR Duplicate slots provided");
  }

  // Add slots using context
  if (context->ClusterAddSlots(slots)) {
    return CommandResult(true, "OK");
  } else {
    return CommandResult(false, "ERR Failed to add slots");
  }
}

// CLUSTER DELSLOTS - Remove slots from a node
CommandResult HandleClusterDelSlots(const protocol::Command& command,
                                    CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'cluster delslots' command");
  }

  // Parse slot numbers
  std::vector<uint16_t> slots;
  for (size_t i = 0; i < command.ArgCount(); ++i) {
    try {
      int slot = std::stoi(command[i].AsString());
      if (slot < 0 || slot >= 16384) {
        return CommandResult(false, "ERR Invalid slot number");
      }
      slots.push_back(static_cast<uint16_t>(slot));
    } catch (const std::exception& e) {
      return CommandResult(false, "ERR Invalid slot number");
    }
  }

  // Check for duplicate slots
  std::set<uint16_t> slot_set(slots.begin(), slots.end());
  if (slot_set.size() != slots.size()) {
    return CommandResult(false, "ERR Duplicate slots provided");
  }

  // Remove slots using context
  if (context->ClusterDelSlots(slots)) {
    return CommandResult(true, "OK");
  } else {
    return CommandResult(false, "ERR Failed to remove slots");
  }
}

// CLUSTER FLUSHSLOTS - Remove all slots from a node
CommandResult HandleClusterFlushSlots(const protocol::Command& command,
                                      CommandContext* context) {
  if (command.ArgCount() != 0) {
    return CommandResult(
        false,
        "ERR wrong number of arguments for 'cluster flushslots' command");
  }

  return CommandResult(false, "ERR This instance has cluster support disabled");
}

// CLUSTER SETSLOT - Assign a slot to a specific node
CommandResult HandleClusterSetSlot(const protocol::Command& command,
                                   CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'cluster setslot' command");
  }

  return CommandResult(false, "ERR This instance has cluster support disabled");
}

// CLUSTER GETKEYSINSLOT - Get keys in a specific slot
CommandResult HandleClusterGetKeysInSlot(const protocol::Command& command,
                                         CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(
        false,
        "ERR wrong number of arguments for 'cluster getkeysinslot' command");
  }

  protocol::RespValue resp;
  resp.SetArray({});
  return CommandResult(resp);
}

// CLUSTER COUNTKEYSINSLOT - Count keys in a specific slot
CommandResult HandleClusterCountKeysInSlot(const protocol::Command& command,
                                           CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(
        false,
        "ERR wrong number of arguments for 'cluster countkeysinslot' command");
  }

  protocol::RespValue resp;
  resp.SetInteger(0);
  return CommandResult(resp);
}

// CLUSTER KEYSLOT - Calculate the hash slot for a given key
CommandResult HandleClusterKeySlot(const protocol::Command& command,
                                   CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'cluster keyslot' command");
  }

  auto* server = static_cast<server::Server*>(context->GetServer());
  auto* shard_manager = server->GetShardManager();

  if (!shard_manager) {
    return CommandResult(false,
                         "ERR This instance has cluster support disabled");
  }

  const std::string& key = command[0].AsString();

  // Use ShardManager's HashSlotCalculator
  uint16_t slot = cluster::HashSlotCalculator::CalculateWithTag(key);

  protocol::RespValue resp;
  resp.SetInteger(slot);
  return CommandResult(resp);
}

// CLUSTER BUMPEPOCH - Increment the config epoch
CommandResult HandleClusterBumpEpoch(const protocol::Command& command,
                                     CommandContext* context) {
  if (command.ArgCount() != 0) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'cluster bumpepoch' command");
  }

  return CommandResult(false, "ERR This instance has cluster support disabled");
}

// CLUSTER RESET - Reset a node
CommandResult HandleClusterReset(const protocol::Command& command,
                                 CommandContext* context) {
  if (command.ArgCount() > 1) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'cluster reset' command");
  }

  return CommandResult(false, "ERR This instance has cluster support disabled");
}

// Register cluster commands (using subcommand registration)
// Note: CLUSTER is a container command, subcommands are handled separately
// This is a simplified approach for now

}  // namespace astra::commands
