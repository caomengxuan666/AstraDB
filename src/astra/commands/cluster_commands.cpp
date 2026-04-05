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
#include "astra/commands/command_auto_register.hpp"
#include "core/gossip_core.hpp"  // For libgossip types

namespace astra::commands {

// Helper function to format node ID
static std::string FormatNodeId(const cluster::NodeId& node_id) {
  char buf[33];
  for (int i = 0; i < 16; ++i) {
    snprintf(buf + i * 2, 3, "%02x", node_id[i]);
  }
  buf[32] = '\0';
  return std::string(buf);
}

// CLUSTER - Main cluster command handler
CommandResult HandleCluster(const astra::protocol::Command& command,
                            CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'cluster' command");
  }

  const auto& subcommand = command[0].AsString();
  std::string upper_subcommand = absl::AsciiStrToUpper(subcommand);

  if (!context || !context->IsClusterEnabled()) {
    // Return error for cluster commands when cluster is not enabled
    if (upper_subcommand == "INFO") {
      std::string info =
          "cluster_state:down\r\n"
          "cluster_slots_assigned:0\r\n"
          "cluster_slots_ok:0\r\n"
          "cluster_slots_pfail:0\r\n"
          "cluster_slots_fail:0\r\n"
          "cluster_known_nodes:0\r\n"
          "cluster_size:0\r\n";

      RespValue response;
      response.SetString(info, protocol::RespType::kBulkString);
      return CommandResult(response);
    }
    return CommandResult(false,
                         "ERR This instance has cluster support disabled");
  }

  auto* gossip = context->GetGossipManagerMutable();

  if (upper_subcommand == "INFO") {
    // Return real cluster info
    auto stats =
        gossip ? gossip->GetStats() : cluster::GossipManager::GossipStats{};

    // Calculate actual slot assignment from ClusterState
    uint32_t slots_assigned = 0;
    if (auto* ctx = dynamic_cast<server::WorkerCommandContext*>(context)) {
      auto cluster_state = ctx->GetClusterState();
      if (cluster_state) {
        // Count assigned slots
        for (uint16_t slot = 0; slot < 16384; ++slot) {
          if (cluster_state->GetSlotOwner(slot).has_value()) {
            slots_assigned++;
          }
        }
      }
    }

    std::ostringstream oss;
    oss << "cluster_state:ok\r\n";
    oss << "cluster_slots_assigned:" << slots_assigned << "\r\n";
    oss << "cluster_slots_ok:" << slots_assigned << "\r\n";
    oss << "cluster_slots_pfail:0\r\n";
    oss << "cluster_slots_fail:0\r\n";
    oss << "cluster_known_nodes:" << stats.known_nodes << "\r\n";
    oss << "cluster_size:1\r\n";
    oss << "cluster_current_epoch:1\r\n";
    oss << "cluster_my_epoch:1\r\n";
    oss << "cluster_stats_messages_sent:" << stats.sent_messages << "\r\n";
    oss << "cluster_stats_messages_received:" << stats.received_messages
        << "\r\n";

    RespValue response;
    response.SetString(oss.str(), protocol::RespType::kBulkString);
    return CommandResult(response);

  } else if (upper_subcommand == "KEYSLOT") {
    // CLUSTER KEYSLOT <key>
    // Returns the hash slot for the specified key
    if (command.ArgCount() < 2) {
      return CommandResult(
          false, "ERR wrong number of arguments for 'cluster|keyslot' command");
    }

    const auto& key = command[1].AsString();
    uint16_t slot = cluster::HashSlotCalculator::CalculateWithTag(key);

    RespValue response;
    response.SetInteger(slot);
    return CommandResult(response);

  } else if (upper_subcommand == "NODES") {
    // Return real node info with slot assignments from ClusterState
    std::ostringstream oss;
    auto* shard_manager = context->GetClusterShardManager();

    // Get ClusterState for slot information
    std::shared_ptr<cluster::ClusterState> cluster_state;
    if (auto* ctx = dynamic_cast<server::WorkerCommandContext*>(context)) {
      cluster_state = ctx->GetClusterState();
    }

    if (gossip) {
      auto nodes = gossip->GetNodes();
      auto self = gossip->GetSelf();

      for (const auto& node : nodes) {
        // Format: <node_id> <ip:port@cport> <flags> <master> <ping_sent>
        // <pong_recv> <config_epoch> <link_state> <slots>
        std::string node_id = FormatNodeId(node.id);
        std::string flags;

        // Determine flags
        if (node.status == cluster::NodeStatus::online) {
          flags = "master";
        } else if (node.status == cluster::NodeStatus::suspect) {
          flags = "master?,fail?";
        } else if (node.status == cluster::NodeStatus::failed) {
          flags = "master,fail";
        } else {
          flags = "master";
        }

        // Mark self
        if (self.id == node.id) {
          if (!flags.empty()) flags += ",";
          flags += "myself";
        }

        oss << node_id << " ";
        oss << node.ip << ":" << node.port << "@" << (node.port + 1000) << " ";
        oss << flags << " ";
        oss << "- ";  // master (empty for masters)
        oss << "0 ";  // ping_sent
        oss << "0 ";  // pong_recv
        oss << node.config_epoch << " ";
        oss << "connected ";  // link_state

        // Add slots from ClusterState
        if (cluster_state) {
          std::string slots_str;
          uint16_t slot_start = 0;
          bool in_range = false;
          for (uint16_t slot = 0; slot < 16384; ++slot) {
            auto owner = cluster_state->GetSlotOwner(slot);
            if (owner.has_value() && owner.value() == node_id) {
              if (!in_range) {
                slot_start = slot;
                in_range = true;
              }
            } else {
              if (in_range) {
                if (slot_start == slot - 1) {
                  slots_str += std::to_string(slot_start);
                } else {
                  slots_str += std::to_string(slot_start) + "-" +
                               std::to_string(slot - 1);
                }
                slots_str += " ";
                in_range = false;
              }
            }
          }
          // Handle last range
          if (in_range) {
            if (slot_start == 16383) {
              slots_str += std::to_string(slot_start);
            } else {
              slots_str += std::to_string(slot_start) + "-16383";
            }
          }
          oss << slots_str;
        } else if (self.id == node.id && shard_manager) {
          // Fallback: show all slots for self if no ClusterState
          oss << "0-16383";
        }
        oss << "\r\n";
      }

      // If no nodes, add self
      if (nodes.empty()) {
        std::string node_id = FormatNodeId(self.id);
        oss << node_id << " ";
        oss << self.ip << ":" << self.port << "@" << (self.port + 1000) << " ";
        oss << "myself,master ";
        oss << "- 0 0 1 connected 0-16383\r\n";
      }
    }

    RespValue response;
    response.SetString(oss.str(), protocol::RespType::kBulkString);
    return CommandResult(response);

  } else if (upper_subcommand == "MEET") {
    // CLUSTER MEET <ip> <port>
    if (command.ArgCount() < 3) {
      return CommandResult(
          false, "ERR wrong number of arguments for 'cluster|meet' command");
    }

    const auto& ip = command[1].AsString();
    int port = 0;
    try {
      if (!absl::SimpleAtoi(command[2].AsString(), &port)) {
        return CommandResult(false, "ERR invalid port number");
      }
    } catch (...) {
      return CommandResult(false, "ERR invalid port number");
    }

    if (context->ClusterMeet(ip, port)) {
      RespValue response;
      response.SetString("OK", protocol::RespType::kSimpleString);
      return CommandResult(response);
    } else {
      return CommandResult(false, "ERR failed to meet node");
    }

  } else if (upper_subcommand == "SLOTS") {
    // Return slot distribution
    std::vector<RespValue> result;

    auto* shard_manager = context->GetClusterShardManager();
    if (gossip && shard_manager) {
      auto self = gossip->GetSelf();

      // Get all nodes and their slot assignments from gossip metadata
      auto nodes = gossip->GetNodes();
      
      // Add self to the list (GetNodes() only returns other nodes)
      std::vector<cluster::AstraNodeView> all_nodes;
      all_nodes.push_back(self);  // Add self first
      all_nodes.insert(all_nodes.end(), nodes.begin(), nodes.end());  // Add other nodes
      
      for (const auto& node : all_nodes) {
        std::string node_id_str = FormatNodeId(node.id);
        
        // Get slot assignments from node metadata
        std::string slots_str;
        if (node.metadata.contains("slots")) {
          slots_str = node.metadata.at("slots");
        }
        
        // Skip nodes without slots
        if (slots_str.empty()) {
          continue;
        }
        
        // Parse slot ranges from metadata (format: "0-1000 2000-3000")
        std::vector<std::pair<uint16_t, uint16_t>> slot_ranges;
        std::istringstream iss(slots_str);
        std::string range_str;
        while (iss >> range_str) {
          size_t dash_pos = range_str.find('-');
          if (dash_pos != std::string::npos) {
            // Range: start-end
            uint16_t start = std::stoul(range_str.substr(0, dash_pos));
            uint16_t end = std::stoul(range_str.substr(dash_pos + 1));
            slot_ranges.emplace_back(start, end);
          } else {
            // Single slot
            uint16_t slot = std::stoul(range_str);
            slot_ranges.emplace_back(slot, slot);
          }
        }
        
        // Create slot info for this node
        for (const auto& [start, end] : slot_ranges) {
          std::vector<RespValue> slot_info;

          RespValue start_slot, end_slot;
          start_slot.SetInteger(start);
          end_slot.SetInteger(end);
          slot_info.push_back(start_slot);
          slot_info.push_back(end_slot);

          // Add node info: ip, port, node_id
          // FIX: Convert gossip port to data port (data_port = gossip_port - 10000)
          RespValue ip_val, port_val, node_id_val;
          ip_val.SetString(node.ip, protocol::RespType::kBulkString);
          port_val.SetInteger(node.port - 10000);  // FIX: Convert to data port
          node_id_val.SetString(node_id_str, protocol::RespType::kBulkString);
          slot_info.push_back(ip_val);
          slot_info.push_back(port_val);
          slot_info.push_back(node_id_val);

          result.push_back(RespValue(std::move(slot_info)));
        }
      }
    }

    return CommandResult(RespValue(std::move(result)));

  } else if (upper_subcommand == "FORGET") {
    // CLUSTER FORGET <node_id>
    // Remove a node from the cluster
    if (command.ArgCount() < 2) {
      return CommandResult(
          false, "ERR wrong number of arguments for 'cluster|forget' command");
    }

    const auto& node_id_str = command[1].AsString();
    cluster::NodeId node_id;
    if (!cluster::GossipManager::ParseNodeId(node_id_str, node_id)) {
      return CommandResult(false, "ERR invalid node id");
    }

    auto* gossip = context->GetGossipManager();
    if (!gossip) {
      return CommandResult(false, "ERR cluster not enabled");
    }

    // Check if trying to forget self
    auto self = gossip->GetSelf();
    if (self.id == node_id) {
      return CommandResult(false,
                           "ERR I tried hard but I can't forget myself...");
    }

    // In production, we would use gossip_core_->remove_node(node_id)
    // For now, we'll just return OK
    // TODO: Implement actual node removal in GossipManager

    RespValue response;
    response.SetString("OK", protocol::RespType::kSimpleString);
    return CommandResult(response);

  } else if (upper_subcommand == "REPLICATE") {
    // CLUSTER REPLICATE <master_node_id>
    // Configure this node as a replica of the specified master node
    if (command.ArgCount() < 2) {
      return CommandResult(
          false,
          "ERR wrong number of arguments for 'cluster|replicate' command");
    }

    const auto& master_id_str = command[1].AsString();
    cluster::NodeId master_id;
    if (!cluster::GossipManager::ParseNodeId(master_id_str, master_id)) {
      return CommandResult(false, "ERR invalid node id");
    }

    auto* gossip = context->GetGossipManager();
    if (!gossip) {
      return CommandResult(false, "ERR cluster not enabled");
    }

    // Find the master node
    auto master_node = gossip->FindNode(master_id);
    if (!master_node) {
      return CommandResult(false, "ERR Unknown master node");
    }

    // Check if master is not a replica itself
    if (master_node->role == "replica") {
      return CommandResult(false, "ERR can't replicate a replica node");
    }

    // In production, we would:
    // 1. Update this node's role to replica
    // 2. Set the master_node_id
    // 3. Start replication from the master
    // 4. Notify other nodes via gossip

    // For now, we'll just return OK
    // TODO: Implement actual replication logic

    RespValue response;
    response.SetString("OK", protocol::RespType::kSimpleString);
    return CommandResult(response);

  } else if (upper_subcommand == "ADDSLOTS") {
    // CLUSTER ADDSLOTS <slot1> <slot2> ...
    if (command.ArgCount() < 2) {
      return CommandResult(
          false,
          "ERR wrong number of arguments for 'cluster|addslots' command");
    }

    // Parse slot numbers
    std::vector<uint16_t> slots;
    for (size_t i = 1; i < command.ArgCount(); ++i) {
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
    absl::flat_hash_set<uint16_t> slot_set(slots.begin(), slots.end());
    if (slot_set.size() != slots.size()) {
      return CommandResult(false, "ERR Duplicate slots provided");
    }

    // Add slots using context
    if (context->ClusterAddSlots(slots)) {
      RespValue response;
      response.SetString("OK", protocol::RespType::kSimpleString);
      return CommandResult(response);
    } else {
      return CommandResult(false, "ERR Failed to add slots");
    }

  } else if (upper_subcommand == "DELSLOTS") {
    // CLUSTER DELSLOTS <slot1> <slot2> ...
    if (command.ArgCount() < 2) {
      return CommandResult(
          false,
          "ERR wrong number of arguments for 'cluster|delslots' command");
    }

    // Parse slot numbers
    std::vector<uint16_t> slots;
    for (size_t i = 1; i < command.ArgCount(); ++i) {
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
    absl::flat_hash_set<uint16_t> slot_set(slots.begin(), slots.end());
    if (slot_set.size() != slots.size()) {
      return CommandResult(false, "ERR Duplicate slots provided");
    }

    // Remove slots using context
    if (context->ClusterDelSlots(slots)) {
      RespValue response;
      response.SetString("OK", protocol::RespType::kSimpleString);
      return CommandResult(response);
    } else {
      return CommandResult(false, "ERR Failed to remove slots");
    }
  } else if (upper_subcommand == "SETSLOT") {
    // CLUSTER SETSLOT <slot> IMPORTING|MIGRATING|STABLE|NODE <node_id>
    // Used during manual slot migration
    if (command.ArgCount() < 3) {
      return CommandResult(
          false, "ERR wrong number of arguments for 'cluster|setslot' command");
    }

    uint16_t slot = 0;
    try {
      int temp_slot;
      if (!absl::SimpleAtoi(command[1].AsString(), &temp_slot)) {
        return CommandResult(false, "ERR invalid slot number");
      }
      slot = static_cast<uint16_t>(temp_slot);
    } catch (...) {
      return CommandResult(false, "ERR invalid slot number");
    }

    const auto& action = command[2].AsString();
    auto* shard_manager = context->GetClusterShardManager();

    if (!shard_manager) {
      return CommandResult(false, "ERR cluster not enabled");
    }

    auto shard_id = shard_manager->GetShardForSlot(slot);

    if (action == "IMPORTING") {
      // Set this shard to importing state
      if (command.ArgCount() < 4) {
        return CommandResult(false,
                             "ERR wrong number of arguments for "
                             "'cluster|setslot importing' command");
      }
      cluster::NodeId source_node;
      if (!cluster::GossipManager::ParseNodeId(command[3].AsString(),
                                               source_node)) {
        return CommandResult(false, "ERR invalid node id");
      }
      shard_manager->StartImport(shard_id, source_node);
      RespValue response;
      response.SetString("OK", protocol::RespType::kSimpleString);
      return CommandResult(response);

    } else if (action == "MIGRATING") {
      // Set this shard to migrating state
      if (command.ArgCount() < 4) {
        return CommandResult(false,
                             "ERR wrong number of arguments for "
                             "'cluster|setslot migrating' command");
      }
      cluster::NodeId target_node;
      if (!cluster::GossipManager::ParseNodeId(command[3].AsString(),
                                               target_node)) {
        return CommandResult(false, "ERR invalid node id");
      }
      shard_manager->StartMigration(shard_id, target_node);
      RespValue response;
      response.SetString("OK", protocol::RespType::kSimpleString);
      return CommandResult(response);

    } else if (action == "STABLE") {
      // Mark slot as stable (migration complete)
      shard_manager->CompleteMigration(shard_id);
      RespValue response;
      response.SetString("OK", protocol::RespType::kSimpleString);
      return CommandResult(response);

    } else if (action == "NODE") {
      // Assign slot to a specific node (for cluster rebalancing)
      if (command.ArgCount() < 4) {
        return CommandResult(
            false,
            "ERR wrong number of arguments for 'cluster|setslot node' command");
      }
      // This would update the slot assignment directly
      // For now, just return OK
      RespValue response;
      response.SetString("OK", protocol::RespType::kSimpleString);
      return CommandResult(response);

    } else {
      return CommandResult(false, "ERR unknown setslot action");
    }

  } else if (upper_subcommand == "GETKEYSINSLOT") {
    // CLUSTER GETKEYSINSLOT <slot> <count>
    // Returns keys in the specified slot
    if (command.ArgCount() < 3) {
      return CommandResult(
          false,
          "ERR wrong number of arguments for 'cluster|getkeysinslot' command");
    }

    [[maybe_unused]] uint16_t slot = 0;
    [[maybe_unused]] int count = 0;
    try {
      int temp_slot;
      if (!absl::SimpleAtoi(command[1].AsString(), &temp_slot)) {
        return CommandResult(false, "ERR invalid slot number");
      }
      slot = static_cast<uint16_t>(temp_slot);
      int temp_count;
      if (!absl::SimpleAtoi(command[2].AsString(), &temp_count)) {
        return CommandResult(false, "ERR invalid count number");
      }
      count = temp_count;
      // slot and count are parsed for validation; reserved for future cluster
      // slot management
    } catch (...) {
      return CommandResult(false, "ERR invalid slot or count");
    }

    // For now, return empty array - actual implementation would scan keys
    std::vector<RespValue> result;
    return CommandResult(RespValue(result));
  }

  return CommandResult(false, "ERR unknown cluster subcommand");
}

// Register CLUSTER command
ASTRADB_REGISTER_COMMAND(CLUSTER, -2, "readonly", RoutingStrategy::kNone,
                         HandleCluster);

}  // namespace astra::commands
