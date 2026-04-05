// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include "replication_commands.hpp"

#include <asio.hpp>

#include "absl/strings/str_cat.h"
#include "astra/base/logging.hpp"
#include "astra/network/connection.hpp"
#include "astra/replication/replication_manager.hpp"
#include "command_auto_register.hpp"

namespace astra::commands {

// SYNC - Synchronize with master (slave only)
CommandResult HandleSync(const protocol::Command& command,
                         CommandContext* context) {
  if (command.ArgCount() > 0) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'sync' command");
  }

  auto* repl_manager = context->GetReplicationManager();
  if (!repl_manager) {
    return CommandResult(false, "ERR replication not configured");
  }

  if (repl_manager->GetRole() != replication::ReplicationRole::kMaster) {
    return CommandResult(false, "ERR SYNC only valid on master");
  }

  auto* socket = context->GetRawSocket();
  if (!socket) {
    return CommandResult(false, "ERR no connection");
  }

  // SYNC is a special command that requires direct socket access
  // We need to:
  // 1. Write the SYNC response header directly to the socket
  // 2. Spawn a coroutine to send the RDB snapshot
  // 3. Then continue sending command stream

  std::string response_header = repl_manager->HandleSync("?");

  ASTRADB_LOG_DEBUG("SYNC: socket pointer={}", static_cast<void*>(socket));

  // TODO: The current implementation uses a blocking write for the SYNC response header
  // This works for testing but should be made non-blocking in production

  ASTRADB_LOG_DEBUG("SYNC: Writing response header to socket");

  // Write response header directly to socket (blocking for now)
  asio::error_code ec;
  size_t bytes_sent = asio::write(
      *socket, asio::buffer(response_header),
      ec);

  if (ec) {
    ASTRADB_LOG_ERROR("Failed to write SYNC response: {}", ec.message());
    return CommandResult(false, "ERR failed to write SYNC response");
  }

  ASTRADB_LOG_DEBUG("SYNC response header sent ({} bytes)", bytes_sent);

  // Spawn coroutine to send RDB snapshot
  // We need to keep the socket alive during the async operation
  asio::co_spawn(
      socket->get_executor(),
      [repl_manager, socket]() -> asio::awaitable<void> {
        co_await repl_manager->SendRdbSnapshot(socket);
      },
      asio::detached);

  // Return empty response (data is sent directly to socket)
  protocol::RespValue resp;
  resp.SetString("", protocol::RespType::kBulkString);
  return CommandResult(resp);
}

// PSYNC - Partial synchronization
CommandResult HandlePsync(const protocol::Command& command,
                          CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'psync' command");
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

  if (repl_manager->GetRole() != replication::ReplicationRole::kMaster) {
    return CommandResult(false, "ERR PSYNC only valid on master");
  }

  std::string response = repl_manager->HandlePsync(replication_id, offset);

  protocol::RespValue resp;
  resp.SetString(response, protocol::RespType::kBulkString);
  return CommandResult(resp);
}

// REPLCONF - Configure replication
CommandResult HandleReplconf(const protocol::Command& command,
                             CommandContext* context) {
  if (command.ArgCount() < 2) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'replconf' command");
  }

  auto* repl_manager = context->GetReplicationManager();
  if (!repl_manager) {
    return CommandResult(false, "ERR replication not configured");
  }

  // Parse key-value pairs
  for (size_t i = 0; i < command.ArgCount(); ++i) {
    const std::string& key = command[i].AsString();

    // Handle known REPLCONF commands
    if (key == "listening-port") {
      if (i + 1 < command.ArgCount()) {
        const std::string& port = command[i + 1].AsString();
        ASTRADB_LOG_DEBUG("REPLCONF listening-port: {}", port);
        ++i;
      }
    } else if (key == "ip-address") {
      if (i + 1 < command.ArgCount()) {
        const std::string& ip = command[i + 1].AsString();
        ASTRADB_LOG_DEBUG("REPLCONF ip-address: {}", ip);
        ++i;
      }
    } else if (key == "capa") {
      if (i + 1 < command.ArgCount()) {
        const std::string& capability = command[i + 1].AsString();
        ASTRADB_LOG_DEBUG("REPLCONF capability: {}", capability);
        ++i;
      }
    } else if (key == "ack") {
      if (i + 1 < command.ArgCount()) {
        const std::string& offset_str = command[i + 1].AsString();
        try {
          uint64_t offset = std::stoull(offset_str);
          ASTRADB_LOG_DEBUG("REPLCONF ack: {}", offset);
          // Get slave ID from connection info (simplified - using 0 for now)
          uint64_t slave_id = 0;  // TODO: Get actual slave ID from connection
          repl_manager->HandleReplconfAck(slave_id, offset);
        } catch (...) {
          return CommandResult(false, "ERR invalid offset value");
        }
        ++i;
      }
    } else {
      ASTRADB_LOG_WARN("Unknown REPLCONF command: {}", key);
    }
  }

  protocol::RespValue resp;
  resp.SetString("OK", protocol::RespType::kSimpleString);
  return CommandResult(resp);
}

// REPLICAOF - Set up replication
CommandResult HandleReplicaof(const protocol::Command& command,
                              CommandContext* context) {
  if (command.ArgCount() != 2) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'replicaof' command");
  }

  const std::string& host = command[0].AsString();
  const std::string& port_str = command[1].AsString();

  auto* repl_manager = context->GetReplicationManager();
  if (!repl_manager) {
    return CommandResult(false, "ERR replication not configured");
  }

  // Check for NO ONE (stop replication)
  if (host == "NO" && port_str == "ONE") {
    if (repl_manager->GetRole() == replication::ReplicationRole::kSlave) {
      ASTRADB_LOG_INFO("Stopping replication, promoting to master");
      // Note: Actual promotion would require stopping the slave connection
      // and reinitializing as master
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

  if (repl_manager->GetRole() == replication::ReplicationRole::kMaster) {
    // Cannot change from master to slave while being a master
    return CommandResult(
        false, "ERR Cannot change role from master to slave");
  }

  ASTRADB_LOG_INFO("Setting up replication to {}:{}", host, port);

  // Note: Actual connection would be implemented here
  // This would initialize the ReplicationManager as slave and connect to master

  protocol::RespValue resp;
  resp.SetString("OK", protocol::RespType::kSimpleString);
  return CommandResult(resp);
}

// ROLE - Return role and replication state
CommandResult HandleRole(const protocol::Command& command,
                         CommandContext* context) {
  if (command.ArgCount() != 0) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'role' command");
  }

  auto* repl_manager = context->GetReplicationManager();
  if (!repl_manager) {
    // No replication configured, return master
    protocol::RespValue role_val;
    role_val.SetString("master", protocol::RespType::kBulkString);

    protocol::RespValue offset_val;
    offset_val.SetInteger(0);

    protocol::RespValue slave_count_val;
    slave_count_val.SetInteger(0);

    std::vector<protocol::RespValue> response_array;
    response_array.push_back(role_val);
    response_array.push_back(offset_val);
    response_array.push_back(slave_count_val);

    protocol::RespValue response;
    response.SetArray(response_array);
    return CommandResult(response);
  }

  auto role = repl_manager->GetRole();

  if (role == replication::ReplicationRole::kMaster) {
    // Master response: ["master", repl_offset, slave_count, [[slave_host,
    // slave_port, slave_offset, ...], ...]]
    protocol::RespValue role_val;
    role_val.SetString("master", protocol::RespType::kBulkString);

    protocol::RespValue offset_val;
    offset_val.SetInteger(static_cast<int64_t>(repl_manager->GetReplOffset()));

    protocol::RespValue slave_count_val;
    slave_count_val.SetInteger(
        static_cast<int64_t>(repl_manager->GetSlaveCount()));

    // Get slave info
    auto slave_info = repl_manager->GetSlaveInfo();
    std::vector<protocol::RespValue> slaves_array;

    for (const auto& [host, offset] : slave_info) {
      protocol::RespValue host_val;
      host_val.SetString(host, protocol::RespType::kBulkString);

      protocol::RespValue port_val;
      port_val.SetInteger(6379);  // Default port

      protocol::RespValue slave_offset_val;
      slave_offset_val.SetInteger(static_cast<int64_t>(offset));

      std::vector<protocol::RespValue> slave_entry;
      slave_entry.push_back(host_val);
      slave_entry.push_back(port_val);
      slave_entry.push_back(slave_offset_val);

      protocol::RespValue slave_entry_val;
      slave_entry_val.SetArray(slave_entry);
      slaves_array.push_back(slave_entry_val);
    }

    std::vector<protocol::RespValue> response_array;
    response_array.push_back(role_val);
    response_array.push_back(offset_val);
    response_array.push_back(slave_count_val);

    protocol::RespValue slaves_val;
    slaves_val.SetArray(slaves_array);
    response_array.push_back(slaves_val);

    protocol::RespValue response;
    response.SetArray(response_array);
    return CommandResult(response);
  } else if (role == replication::ReplicationRole::kSlave) {
    // Slave response: ["slave", master_host, master_port, repl_offset,
    // connected]
    protocol::RespValue role_val;
    role_val.SetString("slave", protocol::RespType::kBulkString);

    protocol::RespValue host_val;
    host_val.SetString(
        "127.0.0.1", protocol::RespType::kBulkString);  // TODO: Get from config

    protocol::RespValue port_val;
    port_val.SetInteger(6379);  // TODO: Get from config

    protocol::RespValue offset_val;
    offset_val.SetInteger(static_cast<int64_t>(repl_manager->GetReplOffset()));

    protocol::RespValue connected_val;
    connected_val.SetInteger(1);  // TODO: Check actual connection status

    std::vector<protocol::RespValue> response_array;
    response_array.push_back(role_val);
    response_array.push_back(host_val);
    response_array.push_back(port_val);
    response_array.push_back(offset_val);
    response_array.push_back(connected_val);

    protocol::RespValue response;
    response.SetArray(response_array);
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
ASTRADB_REGISTER_COMMAND(SYNC, 1, "readonly", RoutingStrategy::kNone,
                         HandleSync);
ASTRADB_REGISTER_COMMAND(PSYNC, 3, "readonly", RoutingStrategy::kNone,
                         HandlePsync);
ASTRADB_REGISTER_COMMAND(REPLCONF, -3, "readonly", RoutingStrategy::kNone,
                         HandleReplconf);
ASTRADB_REGISTER_COMMAND(REPLICAOF, 3, "write", RoutingStrategy::kNone,
                         HandleReplicaof);
ASTRADB_REGISTER_COMMAND(ROLE, 1, "readonly", RoutingStrategy::kNone,
                         HandleRole);

}  // namespace astra::commands
