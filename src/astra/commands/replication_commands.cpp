// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include "replication_commands.hpp"
#include "command_auto_register.hpp"
#include "astra/base/logging.hpp"
#include "absl/strings/str_cat.h"
#include "astra/replication/replication_manager.hpp"

namespace astra::commands {

// SYNC - Synchronize with master (slave only)
CommandResult HandleSync(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() > 0) {
    return CommandResult(false, "ERR wrong number of arguments for 'sync' command");
  }

  auto* repl_manager = context->GetReplicationManager();
  if (!repl_manager) {
    return CommandResult(false, "ERR replication not configured");
  }

  std::string response = repl_manager->HandleSync("?");
  
  protocol::RespValue resp;
  resp.SetString(response, protocol::RespType::kBulkString);
  return CommandResult(resp);
}

// PSYNC - Partial synchronization
CommandResult HandlePsync(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'psync' command");
  }

  const std::string& replication_id = command[0].AsString();
  uint64_t offset = 0;
  
  try {
    offset = std::stoull(command[1].AsString());
  } catch (...) {
    return CommandResult(false, "ERR invalid offset value");
  }

  auto* repl_manager = context->GetReplicationManager();
  if (!repl_manager) {
    return CommandResult(false, "ERR replication not configured");
  }

  std::string response = repl_manager->HandlePsync(replication_id, offset);
  
  protocol::RespValue resp;
  resp.SetString(response, protocol::RespType::kBulkString);
  return CommandResult(resp);
}

// REPLCONF - Configure replication
CommandResult HandleReplconf(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 2 || command.ArgCount() % 2 != 0) {
    return CommandResult(false, "ERR wrong number of arguments for 'replconf' command");
  }

  // Parse key-value pairs
  for (size_t i = 0; i < command.ArgCount(); i += 2) {
    const std::string& key = command[i].AsString();
    const std::string& value = command[i + 1].AsString();
    
    // TODO: Handle replication configuration
    ASTRADB_LOG_DEBUG("REPLCONF: {}={}", key, value);
  }
  
  protocol::RespValue resp;
  resp.SetString("OK", protocol::RespType::kSimpleString);
  return CommandResult(resp);
}

// REPLICAOF - Set up replication
CommandResult HandleReplicaof(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(false, "ERR wrong number of arguments for 'replicaof' command");
  }

  const std::string& host = command[0].AsString();
  const std::string& port_str = command[1].AsString();

  // Check for NO ONE (stop replication)
  if (host == "NO" && port_str == "ONE") {
    auto* repl_manager = context->GetReplicationManager();
    if (repl_manager && repl_manager->GetRole() == replication::ReplicationRole::kSlave) {
      ASTRADB_LOG_INFO("Stopping replication, promoting to master");
      // TODO: Stop replication and promote to master
    }
    
    protocol::RespValue resp;
    resp.SetString("OK", protocol::RespType::kSimpleString);
    return CommandResult(resp);
  }

  // Set up replication to master
  uint16_t port = 0;
  try {
    port = static_cast<uint16_t>(std::stoul(port_str));
  } catch (...) {
    return CommandResult(false, "ERR invalid port number");
  }

  ASTRADB_LOG_INFO("Setting up replication to {}:{}", host, port);
  // TODO: Connect to master and start replication
  
  protocol::RespValue resp;
  resp.SetString("OK", protocol::RespType::kSimpleString);
  return CommandResult(resp);
}

// ROLE - Return role and replication state
CommandResult HandleRole(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() != 0) {
    return CommandResult(false, "ERR wrong number of arguments for 'role' command");
  }

  auto* repl_manager = context->GetReplicationManager();
  if (!repl_manager) {
    // No replication configured, return master
    protocol::RespValue master_val;
    master_val.SetString("master", protocol::RespType::kBulkString);
    protocol::RespValue response;
    response.SetArray({master_val});
    return CommandResult(response);
  }

  auto role = repl_manager->GetRole();
  
  if (role == replication::ReplicationRole::kMaster) {
    // Master response: ["master", repl_offset, slave_count, [[slave_host, slave_offset, ...], ...]]
    protocol::RespValue role_val;
    role_val.SetString("master", protocol::RespType::kBulkString);
    
    protocol::RespValue offset_val;
    offset_val.SetInteger(static_cast<int64_t>(repl_manager->GetReplOffset()));
    
    protocol::RespValue slave_count_val;
    slave_count_val.SetInteger(static_cast<int64_t>(repl_manager->GetSlaveCount()));
    
    protocol::RespValue response;
    response.SetArray({role_val, offset_val, slave_count_val});
    return CommandResult(response);
  } else if (role == replication::ReplicationRole::kSlave) {
    // Slave response: ["slave", master_host, master_port, repl_offset, connected]
    protocol::RespValue role_val;
    role_val.SetString("slave", protocol::RespType::kBulkString);
    
    protocol::RespValue host_val;
    host_val.SetString("127.0.0.1", protocol::RespType::kBulkString);  // TODO: Get from config
    
    protocol::RespValue port_val;
    port_val.SetInteger(6379);  // TODO: Get from config
    
    protocol::RespValue offset_val;
    offset_val.SetInteger(static_cast<int64_t>(repl_manager->GetReplOffset()));
    
    protocol::RespValue connected_val;
    connected_val.SetInteger(1);  // TODO: Check actual connection status
    
    protocol::RespValue response;
    response.SetArray({role_val, host_val, port_val, offset_val, connected_val});
    return CommandResult(response);
  }
  
  // No role
  protocol::RespValue role_val;
  role_val.SetString("none", protocol::RespType::kBulkString);
  protocol::RespValue response;
  response.SetArray({role_val});
  return CommandResult(response);
}

// Register replication commands
ASTRADB_REGISTER_COMMAND(SYNC, 1, "readonly", RoutingStrategy::kNone, HandleSync);
ASTRADB_REGISTER_COMMAND(PSYNC, 3, "readonly", RoutingStrategy::kNone, HandlePsync);
ASTRADB_REGISTER_COMMAND(REPLCONF, -3, "readonly", RoutingStrategy::kNone, HandleReplconf);
ASTRADB_REGISTER_COMMAND(REPLICAOF, 3, "write", RoutingStrategy::kNone, HandleReplicaof);
ASTRADB_REGISTER_COMMAND(ROLE, 1, "readonly", RoutingStrategy::kNone, HandleRole);

}  // namespace astra::commands