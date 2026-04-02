// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include "cluster_commands.hpp"

#include "astra/base/logging.hpp"
#include "astra/cluster/gossip_manager.hpp"
#include "astra/cluster/shard_manager.hpp"
#include "astra/server/server.hpp"
#include "core/gossip_core.hpp"  // For libgossip types

namespace astra::commands {

// Calculate CRC16 for cluster slot calculation
static uint16_t ClusterSlotCrc16(const std::string& key) noexcept {
  uint32_t crc = 0;
  for (char c : key) {
    crc ^= static_cast<uint32_t>(c) << 8;
    for (int i = 0; i < 8; ++i) {
      if (crc & 0x8000) {
        crc = (crc << 1) ^ 0x1021;
      } else {
        crc <<= 1;
      }
    }
  }
  return static_cast<uint16_t>(crc & 0x3FFF);
}

// Get cluster slot for a key
static uint16_t GetSlotForKey(const std::string& key) noexcept {
  // Extract hash tag if present
  size_t start = key.find('{');
  if (start != std::string::npos) {
    size_t end = key.find('}', start + 1);
    if (end != std::string::npos) {
      std::string hash_tag = key.substr(start + 1, end - start - 1);
      return ClusterSlotCrc16(hash_tag);
    }
  }
  return ClusterSlotCrc16(key);
}

// CLUSTER INFO - Get cluster information
CommandResult HandleClusterInfo(const protocol::Command& command,
                                CommandContext* context) {
  if (command.ArgCount() != 0) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'cluster info' command");
  }

  auto* server = static_cast<server::Server*>(context->GetServer());
  auto* cluster_manager = server->GetClusterManager();
  auto* gossip_manager = server->GetGossipManager();
  auto* shard_manager = server->GetShardManager();

  // Check if cluster is enabled
  if (!cluster_manager || !gossip_manager || !shard_manager) {
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
  if (shard_manager) {
    info += "cluster_slots_assigned:" + std::to_string(cluster::kHashSlotCount) + "\n";
    info += "cluster_slots_ok:" + std::to_string(cluster::kHashSlotCount) + "\n";
    info += "cluster_slots_pfail:0\n";
    info += "cluster_slots_fail:0\n";
  }
  
  // Nodes information
  if (gossip_manager) {
    size_t node_count = gossip_manager->GetNodeCount();
    info += "cluster_known_nodes:" + std::to_string(node_count) + "\n";
    info += "cluster_size:" + std::to_string(node_count) + "\n";
  } else {
    info += "cluster_known_nodes:1\n";
    info += "cluster_size:1\n";
  }
  
  // Epoch information (default values for now)
  info += "cluster_current_epoch:1\n";
  info += "cluster_my_epoch:1\n";
  
  // Gossip statistics
  if (gossip_manager) {
    auto stats = gossip_manager->GetStats();
    info += "cluster_stats_messages_sent:" + std::to_string(stats.sent_messages) + "\n";
    info += "cluster_stats_messages_received:" + std::to_string(stats.received_messages) + "\n";
  } else {
    info += "cluster_stats_messages_sent:0\n";
    info += "cluster_stats_messages_received:0\n";
  }

  protocol::RespValue resp;
  resp.SetString(info, protocol::RespType::kBulkString);
  return CommandResult(resp);
}

// CLUSTER NODES - List all nodes in the cluster
CommandResult HandleClusterNodes(const protocol::Command& command,
                                 CommandContext* context) {
  if (command.ArgCount() != 0) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'cluster nodes' command");
  }

  auto* server = static_cast<server::Server*>(context->GetServer());
  auto* gossip_manager = server->GetGossipManager();

  // Check if cluster is enabled
  if (!gossip_manager) {
    std::string result = "";
    protocol::RespValue resp;
    resp.SetString(result, protocol::RespType::kBulkString);
    return CommandResult(resp);
  }

  // Get all nodes from gossip manager
  auto nodes = gossip_manager->GetNodes();
  
  // Build nodes string in Redis cluster format
  std::string result;
  for (const auto& node : nodes) {
    // Format: <id> <ip>:<port@bus-port> <flags> <master> <ping-sent> <pong-recv> <config-epoch> <link-state> <slot> <slot> ... <slot>
    std::string node_str = cluster::GossipManager::NodeIdToString(node.id);
    node_str += " " + node.ip + ":" + std::to_string(node.port) + "@" + std::to_string(node.port + 10000);  // bus port = data port + 10000
    
    // Flags
    uint32_t flags = 0;
    if (node.status == libgossip::node_status::online) {
      flags |= 0x1000;  // master
    }
    node_str += " " + std::to_string(flags);
    
    // Master ID (self for master nodes)
    node_str += " " + std::to_string(-1);
    
    // Ping and pong timestamps
    node_str += " " + std::to_string(0) + " " + std::to_string(0);
    
    // Config epoch
    node_str += " " + std::to_string(node.config_epoch);
    
    // Link state
    node_str += " connected";
    
    // Slots (not assigned yet)
    // node_str += "\n";  // Redis cluster format has newlines
    
    if (!result.empty()) {
      result += "\n";
    }
    result += node_str;
  }

  protocol::RespValue resp;
  resp.SetString(result, protocol::RespType::kBulkString);
  return CommandResult(resp);
}

// CLUSTER MEET - Add a node to the cluster
CommandResult HandleClusterMeet(const protocol::Command& command,
                                CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'cluster meet' command");
  }

  auto* server = static_cast<server::Server*>(context->GetServer());
  auto* gossip_manager = server->GetGossipManager();

  if (!gossip_manager) {
    return CommandResult(false, "ERR This instance has cluster support disabled");
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

// CLUSTER SLOTS - Get cluster slots mapping
CommandResult HandleClusterSlots(const protocol::Command& command,
                                 CommandContext* context) {
  if (command.ArgCount() != 0) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'cluster slots' command");
  }

  protocol::RespValue resp;
  resp.SetArray({});

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

  return CommandResult(false, "ERR This instance has cluster support disabled");
}

// CLUSTER DELSLOTS - Remove slots from a node
CommandResult HandleClusterDelSlots(const protocol::Command& command,
                                    CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'cluster delslots' command");
  }

  return CommandResult(false, "ERR This instance has cluster support disabled");
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
    return CommandResult(false, "ERR This instance has cluster support disabled");
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
