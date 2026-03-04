// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include "admin_commands.hpp"
#include "command_auto_register.hpp"
#include "acl_commands.hpp"
#include "astra/base/logging.hpp"
#include "astra/cluster/gossip_manager.hpp"
#include "astra/cluster/shard_manager.hpp"
#include "astra/storage/key_metadata.hpp"
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
  oss << "version=1.0.0\r\n";
  oss << "os=Linux\r\n";
  oss << "arch=x86_64\r\n";
  oss << "\r\n";
  
  // Clients section
  oss << "# Clients\r\n";
  oss << "connected_clients=1\r\n";
  oss << "\r\n";
  
  // Memory section
  oss << "# Memory\r\n";
  oss << "used_memory_human=unknown\r\n";
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
  
  RespValue response;
  response.SetString(oss.str(), protocol::RespType::kBulkString);
  return CommandResult(response);
}

// COMMAND - Redis command introspection
CommandResult HandleCommand(const astra::protocol::Command& command, CommandContext* context) {
  // COMMAND can be called with no args (returns all commands) or with subcommand
  std::vector<RespValue> result;
  
  if (command.ArgCount() == 0) {
    // Return empty array for now - full implementation would list all commands
    return CommandResult(RespValue(std::move(result)));
  }
  
  const auto& subcommand = command[0].AsString();
  
  if (subcommand == "DOCS") {
    // COMMAND DOCS - return command documentation
    return CommandResult(RespValue(std::move(result)));
  } else if (subcommand == "COUNT") {
    // COMMAND COUNT - return number of commands
    RespValue count;
    count.SetInteger(static_cast<int64_t>(RuntimeCommandRegistry::Instance().GetCommandCount()));
    return CommandResult(count);
  } else if (subcommand == "GETKEYS") {
    // COMMAND GETKEYS - extract keys from a command
    return CommandResult(false, "ERR COMMAND GETKEYS not implemented");
  } else if (subcommand == "LIST") {
    // COMMAND LIST - return list of command names
    return CommandResult(RespValue(std::move(result)));
  }
  
  // Return empty array for unknown subcommands
  return CommandResult(RespValue(std::move(result)));
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
      port = std::stoi(command[2].AsString());
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
    return CommandResult(false, "ERR cluster forget not implemented yet");
  } else if (subcommand == "REPLICATE") {
    return CommandResult(false, "ERR cluster replicate not implemented yet");
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
      slot = static_cast<uint16_t>(std::stoi(command[1].AsString()));
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
    
    uint16_t slot = 0;
    int count = 0;
    try {
      slot = static_cast<uint16_t>(std::stoi(command[1].AsString()));
      count = std::stoi(command[2].AsString());
    } catch (...) {
      return CommandResult(false, "ERR invalid slot or count");
    }
    
    // For now, return empty array - actual implementation would scan keys
    std::vector<RespValue> result;
    return CommandResult(RespValue(std::move(result)));
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
    port = std::stoi(command[1].AsString());
  } catch (...) {
    return CommandResult(false, "ERR invalid port number");
  }
  const auto& key = command[2].AsString();  // Empty string if KEYS option used
  // int destination_db = std::stoi(command[3].AsString());
  // int timeout = std::stoi(command[4].AsString());
  
  // Parse options
  bool copy = false;
  bool replace = false;
  std::vector<std::string> keys;
  
  if (!key.empty()) {
    keys.push_back(key);
  }
  
  for (size_t i = 5; i < command.ArgCount(); ++i) {
    const auto& opt = command[i].AsString();
    if (opt == "COPY") {
      copy = true;
    } else if (opt == "REPLACE") {
      replace = true;
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
  if (command.ArgCount() != 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'keys' command");
  }

  Database* db = context->GetDatabase();
  if (!db) {
    return CommandResult(false, "ERR database not initialized");
  }

  const auto& pattern_arg = command[0];
  if (!pattern_arg.IsBulkString()) {
    return CommandResult(false, "ERR wrong type of pattern argument");
  }

  std::string pattern = pattern_arg.AsString();
  auto all_keys = db->GetAllKeys();
  
  std::vector<RespValue> result;
  for (const auto& key : all_keys) {
    // Simple pattern matching: only support * wildcard
    if (pattern == "*" || key.find(pattern.substr(1)) != std::string::npos) {
      RespValue key_val;
      key_val.SetString(key, protocol::RespType::kBulkString);
      result.push_back(key_val);
    }
  }
  
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
  // For now, just clear current database
  // TODO: Implement multi-database support
  Database* db = context->GetDatabase();
  if (db) {
    db->Clear();
  }
  
  RespValue response;
  response.SetString("OK", protocol::RespType::kSimpleString);
  return CommandResult(response);
}

// Auto-register all admin commands
ASTRADB_REGISTER_COMMAND(PING, 1, "fast", RoutingStrategy::kNone, HandlePing);
ASTRADB_REGISTER_COMMAND(INFO, 1, "readonly", RoutingStrategy::kNone, HandleInfo);
ASTRADB_REGISTER_COMMAND(COMMAND, 0, "readonly", RoutingStrategy::kNone, HandleCommand);
ASTRADB_REGISTER_COMMAND(DEBUG, -2, "admin", RoutingStrategy::kNone, HandleDebug);
ASTRADB_REGISTER_COMMAND(CLUSTER, -2, "readonly", RoutingStrategy::kNone, HandleCluster);
ASTRADB_REGISTER_COMMAND(MIGRATE, -6, "write", RoutingStrategy::kByFirstKey, HandleMigrate);
ASTRADB_REGISTER_COMMAND(ASKING, 1, "fast", RoutingStrategy::kNone, HandleAsking);
ASTRADB_REGISTER_COMMAND(BGSAVE, 1, "admin", RoutingStrategy::kNone, HandleBgSave);
ASTRADB_REGISTER_COMMAND(LASTSAVE, 1, "readonly", RoutingStrategy::kNone, HandleLastSave);
ASTRADB_REGISTER_COMMAND(SAVE, 1, "admin", RoutingStrategy::kNone, HandleSave);
ASTRADB_REGISTER_COMMAND(TYPE, 2, "readonly", RoutingStrategy::kByFirstKey, HandleType);
ASTRADB_REGISTER_COMMAND(KEYS, 2, "readonly", RoutingStrategy::kNone, HandleKeys);
ASTRADB_REGISTER_COMMAND(DBSIZE, 1, "readonly", RoutingStrategy::kNone, HandleDbSize);
ASTRADB_REGISTER_COMMAND(FLUSHDB, 1, "write", RoutingStrategy::kNone, HandleFlushDb);
ASTRADB_REGISTER_COMMAND(FLUSHALL, 1, "write", RoutingStrategy::kNone, HandleFlushAll);
ASTRADB_REGISTER_COMMAND(AUTH, -2, "no-auth", RoutingStrategy::kNone, HandleAuth);
ASTRADB_REGISTER_COMMAND(ACL, -2, "admin", RoutingStrategy::kNone, HandleAcl);

}  // namespace astra::commands