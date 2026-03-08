// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include "admin_commands.hpp"
#include "command_auto_register.hpp"
#include "acl_commands.hpp"
#include "astra/base/logging.hpp"
#include "astra/cluster/gossip_manager.hpp"
#include "astra/cluster/shard_manager.hpp"
#include "astra/storage/key_metadata.hpp"
#include <absl/strings/ascii.h>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>

namespace astra::commands {

// PING
CommandResult HandlePing(const astra::protocol::Command& command, CommandContext* context) {
  RespValue pong;
  pong.SetString("PONG", protocol::RespType::kSimpleString);
  return CommandResult(pong);
}

// INFO
CommandResult HandleInfo(const astra::protocol::Command& command, CommandContext* context) {
  std::ostringstream oss;

  // Server section
  oss << "# Server\r\n";
  oss << "redis_version:7.0.0\r\n";
  oss << "os:Linux\r\n";
  oss << "arch_bits:64\r\n";
  oss << "\r\n";

  // Clients section
  oss << "# Clients\r\n";
  oss << "connected_clients:1\r\n";
  oss << "\r\n";

  // Memory section
  oss << "# Memory\r\n";
  oss << "used_memory_human:unknown\r\n";
  oss << "\r\n";

  // Persistence section
  oss << "# Persistence\r\n";
  if (context && context->IsPersistenceEnabled()) {
    oss << "enabled:yes\r\n";
    oss << "last_save:0\r\n";
  } else {
    oss << "enabled:no\r\n";
    oss << "last_save:0\r\n";
  }
  oss << "\r\n";

  // Cluster section
  oss << "# Cluster\r\n";
  if (context && context->IsClusterEnabled()) {
    oss << "cluster_enabled:1\r\n";
    auto* gossip = context->GetGossipManager();
    if (gossip) {
      oss << "cluster_known_nodes:" << gossip->GetNodeCount() << "\r\n";
    }
  } else {
    oss << "cluster_enabled:0\r\n";
  }
  oss << "\r\n";

  // Keyspace section - Redis Insight Browser depends on this
  oss << "# Keyspace\r\n";
  if (context && context->GetDatabaseManager()) {
    auto* db_manager = context->GetDatabaseManager();
    int db_count = static_cast<int>(db_manager->GetDatabaseCount());
    for (int i = 0; i < db_count; ++i) {
      auto* db = db_manager->GetDatabase(i);
      if (db) {
        size_t key_count = db->Size();
        if (key_count > 0) {
          oss << "db" << i << ":keys=" << key_count << ",expires=0,avg_ttl=0,subexpiry=0\r\n";
        }
      }
    }
  }
  oss << "\r\n";
  
  RespValue response;
  response.SetString(oss.str(), protocol::RespType::kBulkString);
  return CommandResult(response);
}

// Helper function to build command info array
void BuildCommandInfoArrayHelper(std::vector<RespValue>& cmd_array, auto info) {
  // Command name
  RespValue name_val;
  name_val.SetString(info->name, protocol::RespType::kBulkString);
  cmd_array.push_back(name_val);
  
  // Arity
  RespValue arity_val;
  arity_val.SetInteger(info->arity);
  cmd_array.push_back(arity_val);
  
  // Flags (array of strings) - flags is now std::vector<std::string>
  std::vector<RespValue> flags_array;
  for (const auto& flag : info->flags) {
    RespValue flag_val;
    flag_val.SetString(flag, protocol::RespType::kBulkString);
    flags_array.push_back(flag_val);
  }
  cmd_array.push_back(RespValue(std::move(flags_array)));
  
  // First key
  RespValue first_key_val;
  first_key_val.SetInteger(0);
  cmd_array.push_back(first_key_val);
  
  // Last key
  RespValue last_key_val;
  last_key_val.SetInteger(0);
  cmd_array.push_back(last_key_val);
  
  // Key step
  RespValue step_val;
  step_val.SetInteger(0);
  cmd_array.push_back(step_val);
  
  // Tips (empty string)
  RespValue tips_val;
  tips_val.SetString("", protocol::RespType::kBulkString);
  cmd_array.push_back(tips_val);
  
  // Microseconds (0)
  RespValue microseconds_val;
  microseconds_val.SetInteger(0);
  cmd_array.push_back(microseconds_val);
  
  // Category (array with one element - use "server" as default)
  std::vector<RespValue> category_array;
  RespValue category_val;
  category_val.SetString("server", protocol::RespType::kBulkString);
  category_array.push_back(category_val);
  cmd_array.push_back(RespValue(std::move(category_array)));
}

// COMMAND - Redis command introspection
CommandResult HandleCommand(const astra::protocol::Command& command, CommandContext* context) {
  ASTRADB_LOG_TRACE("HandleCommand: arg_count={}", command.ArgCount());
  
  if (command.ArgCount() == 0) {
    ASTRADB_LOG_TRACE("HandleCommand: returning full command info");
    // Return all commands in the format expected by redis-cli
    // Each command is an array: [name, arity, flags, first_key, last_key, step, "", 0, [category]]
    auto& registry = GetGlobalCommandRegistry();
    auto command_names = registry.GetCommandNames();
    
    ASTRADB_LOG_TRACE("HandleCommand: got {} command names", command_names.size());
    
    std::vector<RespValue> result;
    for (const auto& name : command_names) {
      const auto* info = registry.GetInfo(name);
      if (info) {
        std::vector<RespValue> cmd_array;
        BuildCommandInfoArrayHelper(cmd_array, info);
        result.push_back(RespValue(std::move(cmd_array)));
      }
    }
    
    return CommandResult(RespValue(std::move(result)));
  }
  
  const auto& subcommand = command[0].AsString();
  std::string upper_subcommand = absl::AsciiStrToUpper(subcommand);
  
  ASTRADB_LOG_TRACE("HandleCommand: subcommand='{}', upper_subcommand='{}', subcommand.length()={}", subcommand, upper_subcommand, subcommand.length());
  
  if (upper_subcommand == "DOCS") {
    ASTRADB_LOG_TRACE("HandleCommand: DOCS branch");
    
    if (command.ArgCount() < 2) {
      return CommandResult(false, "ERR wrong number of arguments for 'COMMAND|DOCS' command");
    }
    
    const std::string& cmd_name = command[1].AsString();
    ASTRADB_LOG_TRACE("HandleCommand: DOCS for command '{}'", cmd_name);
    
    auto& registry = GetGlobalCommandRegistry();
    const auto* info = registry.GetInfo(cmd_name);
    
    if (!info) {
      ASTRADB_LOG_TRACE("HandleCommand: DOCS - command '{}' not found", cmd_name);
      // Command not found, return null (RespType::kNullBulkString by default)
      RespValue null_result(RespType::kNullBulkString);
      return CommandResult(null_result);
    }
    
    // Build documentation array according to Redis COMMAND DOCS spec
    // Format: array with documentation information
    std::vector<RespValue> docs_array;
    
    // 1. Command name
    RespValue name_val;
    name_val.SetString(info->name, protocol::RespType::kBulkString);
    docs_array.push_back(name_val);
    
    // 2. Summary - build from available info
    std::ostringstream summary;
    summary << info->name << " - ";
    if (info->arity >= 0) {
      summary << "arity: " << info->arity << ", ";
    } else {
      summary << "arity: at least " << (-info->arity - 1) << ", ";
    }
    
    // Add flags to summary
    if (!info->flags.empty()) {
      summary << "flags: ";
      for (size_t i = 0; i < info->flags.size(); ++i) {
        if (i > 0) summary << ", ";
        summary << info->flags[i];
      }
    } else {
      summary << "no special flags";
    }
    
    RespValue summary_val;
    summary_val.SetString(summary.str(), protocol::RespType::kBulkString);
    docs_array.push_back(summary_val);
    
    // 3. Complexity
    RespValue complexity_val;
    complexity_val.SetString("O(1)", protocol::RespType::kBulkString);
    docs_array.push_back(complexity_val);
    
    // 4. Since (version since command was introduced)
    RespValue since_val;
    since_val.SetString("1.0.0", protocol::RespType::kBulkString);
    docs_array.push_back(since_val);
    
    // 5. Group (command group/category)
    RespValue group_val;
    group_val.SetString("generic", protocol::RespType::kBulkString);
    docs_array.push_back(group_val);
    
    // 6. Syntax (command syntax)
    std::ostringstream syntax;
    syntax << info->name;
    if (info->arity < 0) {
      syntax << " [arg ...]";
    } else {
      for (int i = 1; i < info->arity; ++i) {
        syntax << " <arg" << i << ">";
      }
    }
    
    RespValue syntax_val;
    syntax_val.SetString(syntax.str(), protocol::RespType::kBulkString);
    docs_array.push_back(syntax_val);
    
    // 7. Example
    std::ostringstream example;
    example << info->name;
    if (info->arity <= 1) {
      // No arguments or just command name
    } else {
      example << " mykey myvalue";
    }
    
    RespValue example_val;
    example_val.SetString(example.str(), protocol::RespType::kBulkString);
    docs_array.push_back(example_val);
    
    // 8. Arguments documentation (array of argument info)
    std::vector<RespValue> args_docs;
    if (info->arity < 0) {
      // Variable arguments
      RespValue arg_name;
      arg_name.SetString("args", protocol::RespType::kBulkString);
      args_docs.push_back(arg_name);
      
      RespValue arg_type;
      arg_type.SetString("string", protocol::RespType::kBulkString);
      args_docs.push_back(arg_type);
      
      RespValue arg_flags;
      arg_flags.SetString("variadic", protocol::RespType::kBulkString);
      args_docs.push_back(arg_flags);
      
      RespValue arg_since;
      arg_since.SetString("1.0.0", protocol::RespType::kBulkString);
      args_docs.push_back(arg_since);
      
      RespValue arg_summary;
      arg_summary.SetString("One or more string arguments", protocol::RespType::kBulkString);
      args_docs.push_back(arg_summary);
      
      docs_array.push_back(RespValue(args_docs));
    }
    
    return CommandResult(RespValue(std::move(docs_array)));
  } else if (subcommand == "COUNT") {
    ASTRADB_LOG_TRACE("HandleCommand: COUNT branch, returning count");
    // COMMAND COUNT - return number of commands
    RespValue count;
    count.SetInteger(static_cast<int64_t>(RuntimeCommandRegistry::Instance().GetCommandCount()));
    return CommandResult(count);
  } else if (subcommand == "GETKEYS") {
    ASTRADB_LOG_TRACE("HandleCommand: GETKEYS branch, returning error");
    // COMMAND GETKEYS - extract keys from a command
    return CommandResult(false, "ERR COMMAND GETKEYS not implemented");
  } else if (upper_subcommand == "LIST") {
    ASTRADB_LOG_TRACE("HandleCommand: LIST branch");
    // COMMAND LIST - return list of command names
    auto& registry = GetGlobalCommandRegistry();
    auto command_names = registry.GetCommandNames();
    
    ASTRADB_LOG_TRACE("HandleCommand: LIST branch, got {} command names", command_names.size());
    
    std::vector<RespValue> result;
    for (const auto& name : command_names) {
      RespValue name_val;
      name_val.SetString(name, protocol::RespType::kBulkString);
      result.push_back(name_val);
    }
    
    ASTRADB_LOG_TRACE("HandleCommand: LIST branch, returning array with {} elements", result.size());
    return CommandResult(RespValue(std::move(result)));
  } else if (upper_subcommand == "INFO") {
    ASTRADB_LOG_TRACE("HandleCommand: INFO branch");
    // COMMAND INFO - return information about specific commands or all commands
    auto& registry = GetGlobalCommandRegistry();
    
    std::vector<RespValue> result;
    
    if (command.ArgCount() == 1) {
      // No command names specified, return info for all commands
      auto command_names = registry.GetCommandNames();
      for (const auto& name : command_names) {
        const auto* info = registry.GetInfo(name);
        if (info) {
          std::vector<RespValue> cmd_array;
          BuildCommandInfoArrayHelper(cmd_array, info);
          result.push_back(RespValue(std::move(cmd_array)));
        }
      }
    } else {
      // Get info for specified commands
      for (size_t i = 1; i < command.ArgCount(); ++i) {
        const std::string& cmd_name = command[i].AsString();
        const auto* info = registry.GetInfo(cmd_name);
        if (info) {
          std::vector<RespValue> cmd_array;
          BuildCommandInfoArrayHelper(cmd_array, info);
          result.push_back(RespValue(std::move(cmd_array)));
        } else {
          // Command not found, return null
          result.push_back(RespValue(RespType::kNullBulkString));
        }
      }
    }
    
    return CommandResult(RespValue(std::move(result)));
  }
  
  // Return empty array for unknown subcommands
  ASTRADB_LOG_TRACE("HandleCommand: unknown subcommand, returning empty array");
  std::vector<RespValue> result;
  return CommandResult(RespValue(result));
}

// DEBUG - Debug commands
CommandResult HandleDebug(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'debug' command");
  }
  
  const auto& subcommand = command[0].AsString();
  
  if (subcommand == "SEGFAULT") {
    // Intentionally crash (for testing)
    ASTRADB_LOG_WARN("DEBUG SEGFAULT requested");
    return CommandResult(false, "ERR SEGFAULT not implemented for safety");
  } else if (subcommand == "SLEEP") {
    // Sleep for specified seconds
    return CommandResult(false, "ERR SLEEP not implemented for safety");
  }
  
  return CommandResult(false, "ERR unknown debug subcommand");
}

// Helper function to format node ID as hex string
static std::string FormatNodeId(const cluster::NodeId& id) {
  return cluster::GossipManager::NodeIdToString(id);
}

// CLUSTER - Cluster management commands
CommandResult HandleCluster(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'cluster' command");
  }
  
  const auto& subcommand = command[0].AsString();
  
  if (!context || !context->IsClusterEnabled()) {
    // Return error for cluster commands when cluster is not enabled
    if (subcommand == "INFO") {
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
    return CommandResult(false, "ERR This instance has cluster support disabled");
  }
  
  auto* gossip = context->GetGossipManagerMutable();
  
  if (subcommand == "INFO") {
    // Return real cluster info
    auto stats = gossip ? gossip->GetStats() : cluster::GossipManager::GossipStats{};
    
    std::ostringstream oss;
    oss << "cluster_state:ok\r\n";
    oss << "cluster_slots_assigned:16384\r\n";
    oss << "cluster_slots_ok:16384\r\n";
    oss << "cluster_slots_pfail:0\r\n";
    oss << "cluster_slots_fail:0\r\n";
    oss << "cluster_known_nodes:" << stats.known_nodes << "\r\n";
    oss << "cluster_size:1\r\n";
    oss << "cluster_current_epoch:1\r\n";
    oss << "cluster_my_epoch:1\r\n";
    oss << "cluster_stats_messages_sent:" << stats.sent_messages << "\r\n";
    oss << "cluster_stats_messages_received:" << stats.received_messages << "\r\n";
    
    RespValue response;
    response.SetString(oss.str(), protocol::RespType::kBulkString);
    return CommandResult(response);
    
  } else if (subcommand == "NODES") {
    // Return real node info
    std::ostringstream oss;
    auto* shard_manager = context->GetClusterShardManager();
    
    if (gossip) {
      auto nodes = gossip->GetNodes();
      auto self = gossip->GetSelf();
      
      for (const auto& node : nodes) {
        // Format: <node_id> <ip:port@cport> <flags> <master> <ping_sent> <pong_recv> <config_epoch> <link_state> <slots>
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
        
        // Add slots for self
        if (self.id == node.id && shard_manager) {
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
    
  } else if (subcommand == "MEET") {
    // CLUSTER MEET <ip> <port>
    if (command.ArgCount() < 3) {
      return CommandResult(false, "ERR wrong number of arguments for 'cluster|meet' command");
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
    
  } else if (subcommand == "SLOTS") {
    // Return slot distribution
    std::vector<RespValue> result;
    
    auto* shard_manager = context->GetClusterShardManager();
    if (gossip && shard_manager) {
      auto self = gossip->GetSelf();
      std::string node_id = FormatNodeId(self.id);
      
      // Single slot range for now (all slots on this node)
      std::vector<RespValue> slot_info;
      
      RespValue start_slot, end_slot;
      start_slot.SetInteger(0);
      end_slot.SetInteger(16383);
      slot_info.push_back(start_slot);
      slot_info.push_back(end_slot);
      
      // Add node info: ip, port, node_id
      RespValue ip_val, port_val, node_id_val;
      ip_val.SetString(self.ip, protocol::RespType::kBulkString);
      port_val.SetInteger(self.port);
      node_id_val.SetString(node_id, protocol::RespType::kBulkString);
      slot_info.push_back(ip_val);
      slot_info.push_back(port_val);
      slot_info.push_back(node_id_val);
      
      result.push_back(RespValue(std::move(slot_info)));
    }
    
    return CommandResult(RespValue(std::move(result)));
    
  } else if (subcommand == "FORGET") {
    // CLUSTER FORGET <node_id>
    // Remove a node from the cluster
    if (command.ArgCount() < 2) {
      return CommandResult(false, "ERR wrong number of arguments for 'cluster|forget' command");
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
      return CommandResult(false, "ERR I tried hard but I can't forget myself...");
    }
    
    // In production, we would use gossip_core_->remove_node(node_id)
    // For now, we'll just return OK
    // TODO: Implement actual node removal in GossipManager
    
    RespValue response;
    response.SetString("OK", protocol::RespType::kSimpleString);
    return CommandResult(response);
    
  } else if (subcommand == "REPLICATE") {
    // CLUSTER REPLICATE <master_node_id>
    // Configure this node as a replica of the specified master node
    if (command.ArgCount() < 2) {
      return CommandResult(false, "ERR wrong number of arguments for 'cluster|replicate' command");
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
    
  } else if (subcommand == "ADDSLOTS" || subcommand == "DELSLOTS") {
    return CommandResult(false, "ERR cluster slot management not implemented yet");
  } else if (subcommand == "SETSLOT") {
    // CLUSTER SETSLOT <slot> IMPORTING|MIGRATING|STABLE|NODE <node_id>
    // Used during manual slot migration
    if (command.ArgCount() < 3) {
      return CommandResult(false, "ERR wrong number of arguments for 'cluster|setslot' command");
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
        return CommandResult(false, "ERR wrong number of arguments for 'cluster|setslot importing' command");
      }
      cluster::NodeId source_node;
      if (!cluster::GossipManager::ParseNodeId(command[3].AsString(), source_node)) {
        return CommandResult(false, "ERR invalid node id");
      }
      shard_manager->StartImport(shard_id, source_node);
      RespValue response;
      response.SetString("OK", protocol::RespType::kSimpleString);
      return CommandResult(response);
      
    } else if (action == "MIGRATING") {
      // Set this shard to migrating state
      if (command.ArgCount() < 4) {
        return CommandResult(false, "ERR wrong number of arguments for 'cluster|setslot migrating' command");
      }
      cluster::NodeId target_node;
      if (!cluster::GossipManager::ParseNodeId(command[3].AsString(), target_node)) {
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
        return CommandResult(false, "ERR wrong number of arguments for 'cluster|setslot node' command");
      }
      // This would update the slot assignment directly
      // For now, just return OK
      RespValue response;
      response.SetString("OK", protocol::RespType::kSimpleString);
      return CommandResult(response);
      
    } else {
      return CommandResult(false, "ERR unknown setslot action");
    }
    
  } else if (subcommand == "GETKEYSINSLOT") {
    // CLUSTER GETKEYSINSLOT <slot> <count>
    // Returns keys in the specified slot
    if (command.ArgCount() < 3) {
      return CommandResult(false, "ERR wrong number of arguments for 'cluster|getkeysinslot' command");
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
      // slot and count are parsed for validation; reserved for future cluster slot management
    } catch (...) {
      return CommandResult(false, "ERR invalid slot or count");
    }
    
    // For now, return empty array - actual implementation would scan keys
    std::vector<RespValue> result;
    return CommandResult(RespValue(result));
  }
  
  return CommandResult(false, "ERR unknown cluster subcommand");
}

// BGSAVE - Background save (persistence)
CommandResult HandleBgSave(const astra::protocol::Command& command, CommandContext* context) {
  // TODO: Implement actual background save
  RespValue response;
  response.SetString("Background saving started", protocol::RespType::kSimpleString);
  return CommandResult(response);
}

// LASTSAVE - Last save timestamp
CommandResult HandleLastSave(const astra::protocol::Command& command, CommandContext* context) {
  // Return Unix timestamp of last save
  auto now = std::chrono::system_clock::now();
  auto epoch = now.time_since_epoch();
  auto seconds = std::chrono::duration_cast<std::chrono::seconds>(epoch).count();
  
  RespValue response;
  response.SetInteger(static_cast<int64_t>(seconds));
  return CommandResult(response);
}

// SAVE - Synchronous save (persistence)
CommandResult HandleSave(const astra::protocol::Command& command, CommandContext* context) {
  // TODO: Implement actual save
  RespValue response;
  response.SetString("OK", protocol::RespType::kSimpleString);
  return CommandResult(response);
}

// MIGRATE - Migrate a key to another Redis instance
// MIGRATE host port key|"" destination-db timeout [COPY] [REPLACE] [AUTH password] [AUTH2 username password] [KEYS key [key ...]]
CommandResult HandleMigrate(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 4) {
    return CommandResult(false, "ERR wrong number of arguments for 'migrate' command");
  }
  
  // Parse arguments
  const auto& host = command[0].AsString();
  int port = 0;
  try {
    if (!absl::SimpleAtoi(command[1].AsString(), &port)) {
      return CommandResult(false, "ERR invalid port number");
    }
  } catch (...) {
    return CommandResult(false, "ERR invalid port number");
  }
  const auto& key = command[2].AsString();  // Empty string if KEYS option used
  // int destination_db = std::stoi(command[3].AsString());
  // int timeout = std::stoi(command[4].AsString());
  
  // Parse options
  std::vector<std::string> keys;
  
  if (!key.empty()) {
    keys.push_back(key);
  }
  
  for (size_t i = 5; i < command.ArgCount(); ++i) {
    const auto& opt = command[i].AsString();
    if (opt == "COPY") {
      // TODO: Implement COPY option (don't delete key from source)
      (void)opt;  // Suppress unused variable warning
    } else if (opt == "REPLACE") {
      // TODO: Implement REPLACE option (overwrite existing key)
      (void)opt;  // Suppress unused variable warning
    } else if (opt == "KEYS") {
      // Remaining arguments are keys
      for (size_t j = i + 1; j < command.ArgCount(); ++j) {
        keys.push_back(command[j].AsString());
      }
      break;
    }
  }
  
  if (keys.empty()) {
    return CommandResult(false, "ERR no key to migrate");
  }
  
  // Check cluster mode
  if (!context || !context->IsClusterEnabled()) {
    return CommandResult(false, "ERR MIGRATE requires cluster mode");
  }
  
  // In a real implementation, this would:
  // 1. Connect to target node
  // 2. Send DUMP + RESTORE or direct data transfer
  // 3. Optionally delete the key from source (unless COPY)
  
  // For now, return success (actual migration would be async)
  ASTRADB_LOG_INFO("MIGRATE: migrating {} keys to {}:{}", keys.size(), host, port);
  
  RespValue response;
  response.SetString("OK", protocol::RespType::kSimpleString);
  return CommandResult(response);
}

// ASKING - Indicate client is asking for a key during migration
// This is sent by clients when they receive an ASK redirect
CommandResult HandleAsking(const astra::protocol::Command& command, CommandContext* context) {
  // Client is in asking mode - next command should be executed
  // even if the slot is in IMPORTING state
  RespValue response;
  response.SetString("OK", protocol::RespType::kSimpleString);
  return CommandResult(response);
}

// TYPE key - Get key type
CommandResult HandleType(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'type' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& key_arg = command[0];
  if (!key_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of key argument");
  }

  std::string key = key_arg.AsString();
  auto type = db->GetType(key);
  
  std::string type_str = "none";
  if (type.has_value()) {
    switch (*type) {
      case astra::storage::KeyType::kString: type_str = "string"; break;
      case astra::storage::KeyType::kHash: type_str = "hash"; break;
      case astra::storage::KeyType::kSet: type_str = "set"; break;
      case astra::storage::KeyType::kZSet: type_str = "zset"; break;
      case astra::storage::KeyType::kList: type_str = "list"; break;
      case astra::storage::KeyType::kStream: type_str = "stream"; break;
      default: type_str = "none"; break;
    }
  }
  
  RespValue response;
  response.SetString(type_str, protocol::RespType::kSimpleString);
  return CommandResult(response);
}

// KEYS pattern - Find all keys matching pattern
CommandResult HandleKeys(const astra::protocol::Command& command, CommandContext* context) {
  ASTRADB_LOG_TRACE("HandleKeys: arg_count={}", command.ArgCount());
  
  if (command.ArgCount() != 1) {
    ASTRADB_LOG_TRACE("HandleKeys: wrong number of arguments");
    return CommandResult(false, "ERR wrong number of arguments for 'keys' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    ASTRADB_LOG_TRACE("HandleKeys: database not initialized");
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& pattern_arg = command[0];
  if (!pattern_arg.IsBulkString()) {
    ASTRADB_LOG_TRACE("HandleKeys: wrong type of pattern argument");
    return CommandResult(false, "ERR wrong type of pattern argument");
  }

  std::string pattern = pattern_arg.AsString();
  ASTRADB_LOG_TRACE("HandleKeys: pattern='{}'", pattern);
  
  auto all_keys = db->GetAllKeys();
  ASTRADB_LOG_TRACE("HandleKeys: got {} keys", all_keys.size());
  
  std::vector<RespValue> result;
  for (const auto& key : all_keys) {
    // Simple pattern matching: only support * wildcard
    if (pattern == "*" || key.find(pattern.substr(1)) != std::string::npos) {
      RespValue key_val;
      key_val.SetString(key, protocol::RespType::kBulkString);
      result.push_back(key_val);
    }
  }
  
  ASTRADB_LOG_TRACE("HandleKeys: returning array with {} elements", result.size());
  return CommandResult(RespValue(std::move(result)));
}

// DBSIZE - Return number of keys
CommandResult HandleDbSize(const astra::protocol::Command& command, CommandContext* context) {
  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  size_t size = db->DbSize();
  return CommandResult(RespValue(static_cast<int64_t>(size)));
}

// FLUSHDB - Clear current database
CommandResult HandleFlushDb(const astra::protocol::Command& command, CommandContext* context) {
  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  db->Clear();
  
  RespValue response;
  response.SetString("OK", protocol::RespType::kSimpleString);
  return CommandResult(response);
}

// FLUSHALL - Clear all databases
CommandResult HandleFlushAll(const astra::protocol::Command& command, CommandContext* context) {
  // Get database manager and clear all databases
  DatabaseManager* db_manager = context->GetDatabaseManager();
  if (db_manager) {
    size_t db_count = db_manager->GetDatabaseCount();
    for (size_t i = 0; i < db_count; ++i) {
      Database* db = db_manager->GetDatabase(static_cast<int>(i));
      if (db) {
        db->Clear();
      }
    }
  } else {
    // Fallback: clear current database only
    Database* db = context->GetDatabase();
    if (db) {
      db->Clear();
    }
  }
  
  RespValue response;
  response.SetString("OK", protocol::RespType::kSimpleString);
  return CommandResult(response);
}

// SELECT - Select the database with the specified zero-based numeric index
CommandResult HandleSelect(const astra::protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'SELECT' command");
  }

  const auto& arg = command[0];
  if (arg.GetType() != RespType::kBulkString) {
    return CommandResult(false, "ERR invalid argument type for 'SELECT' command");
  }

  // Parse database index
  std::string index_str = arg.AsString();
  int db_index;
  try {
    db_index = std::stoi(index_str);
  } catch (...) {
    return CommandResult(false, "ERR invalid database index");
  }

  // Validate database index
  DatabaseManager* db_manager = context->GetDatabaseManager();
  if (db_manager) {
    if (db_index < 0 || static_cast<size_t>(db_index) >= db_manager->GetDatabaseCount()) {
      return CommandResult(false, "ERR DB index is out of range");
    }
  } else {
    // Fallback: only allow index 0
    if (db_index != 0) {
      return CommandResult(false, "ERR DB index is out of range");
    }
  }

  // Set the database index
  context->SetDBIndex(db_index);

  RespValue response;
  response.SetString("OK", protocol::RespType::kSimpleString);
  return CommandResult(response);
}

// CONFIG - Configuration management commands
CommandResult HandleConfig(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'config' command");
  }

  const auto& subcommand = command[0].AsString();
  std::string upper_subcommand = absl::AsciiStrToUpper(subcommand);

  if (upper_subcommand == "GET") {
    // CONFIG GET parameter [parameter ...]
    if (command.ArgCount() < 2) {
      return CommandResult(false, "ERR wrong number of arguments for 'config get' command");
    }

    std::vector<RespValue> result;
    
    // Support common configuration parameters
    for (size_t i = 1; i < command.ArgCount(); ++i) {
      const std::string& param = command[i].AsString();
      std::string upper_param = absl::AsciiStrToUpper(param);
      
      // Match parameter against supported configurations
      if (upper_param == "DATABASES" || upper_param == "*" || upper_param == "*DATABASES*") {
        RespValue key;
        key.SetString("databases", protocol::RespType::kBulkString);
        result.push_back(key);
        
        RespValue value;
        // Get database count from DatabaseManager
        DatabaseManager* db_manager = context->GetDatabaseManager();
        int db_count = db_manager ? static_cast<int>(db_manager->GetDatabaseCount()) : 16;
        value.SetString(absl::StrCat(db_count), protocol::RespType::kBulkString);
        result.push_back(value);
      } else if (upper_param == "IO-THREADS" || upper_param == "*" || upper_param == "*IO*THREADS*") {
        RespValue key;
        key.SetString("io-threads", protocol::RespType::kBulkString);
        result.push_back(key);
        
        RespValue value;
        value.SetString("1", protocol::RespType::kBulkString);  // AstraDB uses single thread
        result.push_back(value);
      } else if (upper_param == "MAXMEMORY" || upper_param == "*" || upper_param == "*MAXMEMORY*") {
        RespValue key;
        key.SetString("maxmemory", protocol::RespType::kBulkString);
        result.push_back(key);
        
        RespValue value;
        value.SetString("0", protocol::RespType::kBulkString);  // No limit
        result.push_back(value);
      } else if (upper_param == "PORT" || upper_param == "*" || upper_param == "*PORT*") {
        RespValue key;
        key.SetString("port", protocol::RespType::kBulkString);
        result.push_back(key);
        
        RespValue value;
        value.SetString("6379", protocol::RespType::kBulkString);
        result.push_back(value);
      }
      // Add more parameters as needed
    }
    
    return CommandResult(RespValue(std::move(result)));
  } else {
    return CommandResult(false, "ERR unknown subcommand '" + subcommand + "'");
  }
}

// MODULE - Module management commands
CommandResult HandleModule(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'module' command");
  }

  const auto& subcommand = command[0].AsString();
  std::string upper_subcommand = absl::AsciiStrToUpper(subcommand);

  if (upper_subcommand == "LIST") {
    // MODULE LIST - return list of loaded modules
    // AstraDB doesn't support modules yet, return empty array
    std::vector<RespValue> result;
    return CommandResult(RespValue(std::move(result)));
  } else {
    return CommandResult(false, "ERR unknown subcommand '" + subcommand + "'");
  }
}

// SCAN - Incrementally iterate the keyspace
CommandResult HandleScan(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'scan' command");
  }

  // Parse cursor
  uint64_t cursor = 0;
  try {
    if (!absl::SimpleAtoi(command[0].AsString(), &cursor)) {
      return CommandResult(false, "ERR invalid cursor");
    }
  } catch (...) {
    return CommandResult(false, "ERR invalid cursor");
  }

  // Parse options
  std::string match_pattern = "*";
  int count = 10;
  
  for (size_t i = 1; i < command.ArgCount(); ++i) {
    const std::string& arg = command[i].AsString();
    std::string upper_arg = absl::AsciiStrToUpper(arg);
    
    if (upper_arg == "MATCH" && i + 1 < command.ArgCount()) {
      match_pattern = command[++i].AsString();
    } else if (upper_arg == "COUNT" && i + 1 < command.ArgCount()) {
      try {
        count = std::stoi(command[++i].AsString());
        if (count < 1) count = 10;
      } catch (...) {
        return CommandResult(false, "ERR invalid count");
      }
    }
    // TYPE option is not implemented yet
  }

  // Get database
  auto db = context->GetDatabase();
  if (!db) {
    std::vector<RespValue> result;
    RespValue cursor_val;
    cursor_val.SetString("0", protocol::RespType::kBulkString);
    result.push_back(cursor_val);

    RespValue keys_val;
    keys_val.SetArray({});
    result.push_back(keys_val);

    return CommandResult(RespValue(std::move(result)));
  }

  // Get all keys (simplified implementation - should use real cursor-based iteration)
  auto keys = db->GetAllKeys();
  
  // Filter keys by pattern
  std::vector<std::string> filtered_keys;
  for (const auto& key : keys) {
    // Glob pattern matching
    bool matches = false;
    if (match_pattern == "*") {
      // Match all keys
      matches = true;
    } else if (match_pattern.find('*') == std::string::npos && match_pattern.find('?') == std::string::npos) {
      // No wildcards, exact match
      matches = (key == match_pattern);
    } else {
      // Simple glob pattern matching (supports * wildcards at start, end, or both)
      std::string pattern = match_pattern;
      std::string target = key;

      // Handle pattern like "prefix*" or "*suffix" or "*middle*" or "pre*fix"
      if (pattern[0] == '*' && pattern.back() == '*') {
        // *middle* - contains
        std::string middle = pattern.substr(1, pattern.size() - 2);
        matches = (target.find(middle) != std::string::npos);
      } else if (pattern[0] == '*') {
        // *suffix - ends with
        std::string suffix = pattern.substr(1);
        matches = (target.size() >= suffix.size() && target.substr(target.size() - suffix.size()) == suffix);
      } else if (pattern.back() == '*') {
        // prefix* - starts with
        std::string prefix = pattern.substr(0, pattern.size() - 1);
        matches = (target.size() >= prefix.size() && target.substr(0, prefix.size()) == prefix);
      } else {
        // pre*fix - more complex pattern, use simple substring match for now
        matches = (target.find(pattern) != std::string::npos);
      }
    }

    if (matches) {
      filtered_keys.push_back(key);
    }
  }

  // Simplified cursor logic (for now, return all keys at once)
  // In a real implementation, this should use actual cursor-based iteration
  std::vector<RespValue> result;

  // New cursor (0 indicates end of iteration)
  RespValue cursor_val;
  cursor_val.SetString("0", protocol::RespType::kBulkString);
  result.push_back(cursor_val);
  
  // Keys array
  std::vector<RespValue> keys_array;
  size_t start_idx = static_cast<size_t>(cursor);
  size_t end_idx = std::min(start_idx + static_cast<size_t>(count), filtered_keys.size());
  
  for (size_t i = start_idx; i < end_idx; ++i) {
    RespValue key_val;
    key_val.SetString(filtered_keys[i], protocol::RespType::kBulkString);
    keys_array.push_back(key_val);
  }
  
  RespValue keys_val;
  keys_val.SetArray(std::move(keys_array));
  result.push_back(keys_val);
  
  return CommandResult(RespValue(std::move(result)));
}

// MEMORY - Memory introspection commands
CommandResult HandleMemory(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'memory' command");
  }

  const auto& subcommand = command[0].AsString();
  std::string upper_subcommand = absl::AsciiStrToUpper(subcommand);

  if (upper_subcommand == "USAGE") {
    // MEMORY USAGE key [SAMPLES count]
    if (command.ArgCount() < 2) {
      return CommandResult(false, "ERR wrong number of arguments for 'memory|usage' command");
    }

    const std::string& key = command[1].AsString();
    auto db = context->GetDatabase();
    if (!db) {
      return CommandResult(protocol::RespValue(protocol::RespType::kNullBulkString));
    }

    auto key_type = db->GetType(key);
    if (!key_type.has_value()) {
      return CommandResult(protocol::RespValue(protocol::RespType::kNullBulkString));
    }

    // Estimate memory usage (simplified implementation)
    // This is a rough estimate - actual Redis does more sophisticated calculations
    size_t key_size = key.size();
    size_t value_size = 0;
    size_t overhead = 56;  // Base Redis object overhead

    // Get approximate size based on type
    switch (key_type.value()) {
      case storage::KeyType::kString: {
        auto value = db->Get(key);
        if (value.has_value()) {
          value_size = value.value().value.size();
        }
        break;
      }
      case storage::KeyType::kHash: {
        // Get hash size
        auto hash_size = db->HLen(key);
        // Estimate: each field/value pair ~50 bytes on average
        value_size = hash_size * 50;
        break;
      }
      case storage::KeyType::kList: {
        // Get list size
        auto list_size = db->LLen(key);
        // Estimate: each element ~30 bytes on average
        value_size = list_size * 30;
        break;
      }
      case storage::KeyType::kSet: {
        // Get set size
        auto set_size = db->SCard(key);
        // Estimate: each element ~30 bytes on average
        value_size = set_size * 30;
        break;
      }
      case storage::KeyType::kZSet: {
        // Get zset size
        auto zset_size = db->ZCard(key);
        // Estimate: each member+score pair ~50 bytes on average
        value_size = zset_size * 50;
        break;
      }
      case storage::KeyType::kStream:
        // Stream - rough estimate
        value_size = 1024;  // 1KB overhead
        break;
      default:
        break;
    }

    // Add Redis internal overhead
    size_t total_size = key_size + value_size + overhead;

    // Return as integer
    protocol::RespValue resp;
    resp.SetInteger(static_cast<int64_t>(total_size));
    return CommandResult(resp);

  } else if (upper_subcommand == "STATS") {
    // MEMORY STATS - Return memory usage statistics
    std::vector<RespValue> result;

    // Basic memory statistics (simplified implementation)
    // peak.allocated
    RespValue peak_allocated_key;
    peak_allocated_key.SetString("peak.allocated", protocol::RespType::kBulkString);
    result.push_back(peak_allocated_key);
    RespValue peak_allocated_val;
    peak_allocated_val.SetInteger(0);  // TODO: Implement peak memory tracking
    result.push_back(peak_allocated_val);

    // total.allocated
    RespValue total_allocated_key;
    total_allocated_key.SetString("total.allocated", protocol::RespType::kBulkString);
    result.push_back(total_allocated_key);
    RespValue total_allocated_val;
    total_allocated_val.SetInteger(0);  // TODO: Implement actual memory tracking
    result.push_back(total_allocated_val);

    // startup.allocated
    RespValue startup_allocated_key;
    startup_allocated_key.SetString("startup.allocated", protocol::RespType::kBulkString);
    result.push_back(startup_allocated_key);
    RespValue startup_allocated_val;
    startup_allocated_val.SetInteger(1024 * 1024);  // Approximate 1MB
    result.push_back(startup_allocated_val);

    // replication.backlog
    RespValue replication_backlog_key;
    replication_backlog_key.SetString("replication.backlog", protocol::RespType::kBulkString);
    result.push_back(replication_backlog_key);
    RespValue replication_backlog_val;
    replication_backlog_val.SetInteger(0);  // No replication backlog
    result.push_back(replication_backlog_val);

    // keys.count
    RespValue keys_count_key;
    keys_count_key.SetString("keys.count", protocol::RespType::kBulkString);
    result.push_back(keys_count_key);
    RespValue keys_count_val;
    auto db = context->GetDatabase();
    size_t key_count = db ? db->Size() : 0;
    keys_count_val.SetInteger(static_cast<int64_t>(key_count));
    result.push_back(keys_count_val);

    // dataset.bytes
    RespValue dataset_bytes_key;
    dataset_bytes_key.SetString("dataset.bytes", protocol::RespType::kBulkString);
    result.push_back(dataset_bytes_key);
    RespValue dataset_bytes_val;
    dataset_bytes_val.SetInteger(0);  // TODO: Implement actual dataset tracking
    result.push_back(dataset_bytes_val);

    // overhead.total
    RespValue overhead_total_key;
    overhead_total_key.SetString("overhead.total", protocol::RespType::kBulkString);
    result.push_back(overhead_total_key);
    RespValue overhead_total_val;
    overhead_total_val.SetInteger(1024 * 1024);  // Approximate overhead
    result.push_back(overhead_total_val);

    return CommandResult(RespValue(std::move(result)));

  } else if (upper_subcommand == "HELP") {
    // MEMORY HELP - Show help text
    std::string help_text =
      "MEMORY <subcommand> [<arg> [value] ...]. Subcommands are:\n"
      "USAGE     <key> [SAMPLES <count>] - Estimate memory usage of a key\n"
      "STATS                             - Show memory usage statistics\n"
      "HELP                              - Show this help text";
    protocol::RespValue resp;
    resp.SetString(help_text, protocol::RespType::kBulkString);
    return CommandResult(resp);

  } else {
    return CommandResult(false, "ERR unknown subcommand '" + subcommand + "'");
  }
}

// Auto-register all admin commands
ASTRADB_REGISTER_COMMAND(PING, 1, "readonly,fast", RoutingStrategy::kNone, HandlePing);
ASTRADB_REGISTER_COMMAND(INFO, 1, "readonly", RoutingStrategy::kNone, HandleInfo);
ASTRADB_REGISTER_COMMAND(COMMAND, -1, "readonly,admin", RoutingStrategy::kNone, HandleCommand);
ASTRADB_REGISTER_COMMAND(DEBUG, -2, "admin", RoutingStrategy::kNone, HandleDebug);
ASTRADB_REGISTER_COMMAND(CLUSTER, -2, "readonly", RoutingStrategy::kNone, HandleCluster);
ASTRADB_REGISTER_COMMAND(MIGRATE, -6, "write", RoutingStrategy::kByFirstKey, HandleMigrate);
ASTRADB_REGISTER_COMMAND(MODULE, -2, "admin", RoutingStrategy::kNone, HandleModule);
ASTRADB_REGISTER_COMMAND(CONFIG, -2, "admin,slow,dangerous", RoutingStrategy::kNone, HandleConfig);
ASTRADB_REGISTER_COMMAND(SCAN, -2, "readonly,slow", RoutingStrategy::kNone, HandleScan);
ASTRADB_REGISTER_COMMAND(MEMORY, -2, "readonly,slow", RoutingStrategy::kNone, HandleMemory);
ASTRADB_REGISTER_COMMAND(ASKING, 1, "fast", RoutingStrategy::kNone, HandleAsking);
ASTRADB_REGISTER_COMMAND(BGSAVE, 1, "admin", RoutingStrategy::kNone, HandleBgSave);
ASTRADB_REGISTER_COMMAND(LASTSAVE, 1, "readonly", RoutingStrategy::kNone, HandleLastSave);
ASTRADB_REGISTER_COMMAND(SAVE, 1, "admin", RoutingStrategy::kNone, HandleSave);
ASTRADB_REGISTER_COMMAND(TYPE, 2, "readonly", RoutingStrategy::kByFirstKey, HandleType);
ASTRADB_REGISTER_COMMAND(KEYS, 2, "readonly", RoutingStrategy::kNone, HandleKeys);
ASTRADB_REGISTER_COMMAND(DBSIZE, 1, "readonly,fast", RoutingStrategy::kNone, HandleDbSize);
ASTRADB_REGISTER_COMMAND(FLUSHDB, 1, "write", RoutingStrategy::kNone, HandleFlushDb);
ASTRADB_REGISTER_COMMAND(FLUSHALL, 1, "write", RoutingStrategy::kNone, HandleFlushAll);
ASTRADB_REGISTER_COMMAND(SELECT, 2, "fast", RoutingStrategy::kNone, HandleSelect);
ASTRADB_REGISTER_COMMAND(AUTH, -2, "no-auth", RoutingStrategy::kNone, HandleAuth);
ASTRADB_REGISTER_COMMAND(ACL, -2, "admin", RoutingStrategy::kNone, HandleAcl);

}  // namespace astra::commands