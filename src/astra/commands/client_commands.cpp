// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include "client_commands.hpp"

#include <iomanip>
#include <sstream>

#include "astra/base/logging.hpp"
#include "astra/network/connection.hpp"
#include "astra/server/server.hpp"
#include "command_auto_register.hpp"
#include "command_handler.hpp"  // For CommandResult and RoutingStrategy

namespace astra::commands {

// CLIENT LIST - List all connected clients
CommandResult HandleClientList(const protocol::Command& command,
                               CommandContext* context) {
  // CLIENT LIST takes no arguments beyond the subcommand
  // The subcommand is already handled by the router, so we accept 0 additional
  // args

  // Note: In NO SHARING architecture, we can only list connections from the current worker
  // To get all connections across workers, we would need to use MPSC to collect info from all workers
  // For now, we return the current worker's connections only
  
  // Get connection from context
  auto conn_ptr = context->GetConnection();
  if (!conn_ptr) {
    return CommandResult(false, "ERR connection not available");
  }

  // Get worker ID from connection
  // This is a simplified implementation - in real NO SHARING architecture, each worker
  // only knows about its own connections. To get all connections, we would need
  // to use MPSC to collect info from all workers.
  
  // Return empty list for now (each worker handles its own connections)
  std::string result = "";
  
  protocol::RespValue resp;
  resp.SetString(result, protocol::RespType::kBulkString);
  return CommandResult(resp);
}

// CLIENT SETNAME - Set a name for the current connection
CommandResult HandleClientSetName(const protocol::Command& command,
                                  CommandContext* context) {
  if (command.ArgCount() != 1) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'client setname' command");
  }

  const std::string& name = command[0].AsString();

  // Get connection from context
  auto conn_ptr = context->GetConnection();
  if (!conn_ptr) {
    return CommandResult(false, "ERR connection not available");
  }

  auto* conn = static_cast<network::Connection*>(conn_ptr);

  // Set client name
  conn->SetClientName(name);

  protocol::RespValue resp;
  resp.SetString("OK", protocol::RespType::kSimpleString);
  return CommandResult(resp);
}

// CLIENT GETNAME - Get the name of the current connection
CommandResult HandleClientGetName(const protocol::Command& command,
                                  CommandContext* context) {
  // CLIENT GETNAME takes no arguments beyond the subcommand
  // The subcommand is already handled by the router, so we accept 0 additional
  // args

  // Get connection from context
  auto conn_ptr = context->GetConnection();
  if (!conn_ptr) {
    return CommandResult(false, "ERR connection not available");
  }

  auto* conn = static_cast<network::Connection*>(conn_ptr);

  // Get client name
  const std::string& name = conn->GetClientName();

  protocol::RespValue resp;
  if (name.empty()) {
    resp = protocol::RespValue(protocol::RespType::kNullBulkString);
  } else {
    resp.SetString(name, protocol::RespType::kBulkString);
  }
  return CommandResult(resp);
}

// CLIENT - Main client command router
CommandResult HandleClient(const protocol::Command& command,
                           CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false,
                         "ERR wrong number of arguments for 'client' command");
  }

  const auto& subcommand = command[0];
  if (!subcommand.IsBulkString()) {
    return CommandResult(false, "ERR subcommand must be a string");
  }

  std::string sub = subcommand.AsString();
  std::transform(sub.begin(), sub.end(), sub.begin(), ::toupper);

  // Route to appropriate subcommand handler
  if (sub == "LIST") {
    // Create a new command without the subcommand
    return HandleClientList(command, context);
  } else if (sub == "INFO") {
    return HandleClientInfo(command, context);
  } else if (sub == "KILL") {
    return HandleClientKill(command, context);
  } else if (sub == "SETNAME") {
    // Handle SETNAME directly with the remaining arguments
    // Create a new command with only the arguments (skip the subcommand)
    std::vector<RespValue> new_args;
    for (size_t i = 1; i < command.ArgCount(); ++i) {
      new_args.push_back(command[i]);
    }
    protocol::Command new_command;
    new_command.name = "SETNAME";
    new_command.args =
        absl::InlinedVector<RespValue, 4>(new_args.begin(), new_args.end());
    return HandleClientSetName(new_command, context);
  } else if (sub == "GETNAME") {
    return HandleClientGetName(command, context);
  } else {
    return CommandResult(false, "ERR unknown subcommand '" + sub + "'");
  }
}

// CLIENT INFO - Get information about the current client connection
CommandResult HandleClientInfo(const protocol::Command& command,
                               CommandContext* context) {
  // CLIENT INFO takes no arguments beyond the subcommand
  // The subcommand is already handled by the router, so we accept 0 additional
  // args

  // Get connection from context
  auto conn_ptr = context->GetConnection();
  if (!conn_ptr) {
    return CommandResult(false, "ERR connection not available");
  }

  auto* conn = static_cast<network::Connection*>(conn_ptr);

  std::string addr = conn->GetRemoteAddress();
  uint64_t id = conn->GetId();

  // Parse address to get IP and port
  std::string ip = "unknown";
  uint16_t port = 0;
  size_t colon_pos = addr.find(':');
  if (colon_pos != std::string::npos) {
    ip = addr.substr(0, colon_pos);
    try {
      port = static_cast<uint16_t>(std::stoul(addr.substr(colon_pos + 1)));
    } catch (...) {
      port = 0;
    }
  }

  // Build client info string (simplified format)
  std::ostringstream info;
  info << "# Client\n";
  info << "id=" << id << "\n";
  info << "name=" << conn->GetClientName() << "\n";
  info << "addr=" << ip << ":" << port << "\n";
  info << "laddr=127.0.0.1:6379\n";  // Local address
  info << "sock=-1\n";               // Socket fd
  info << "age=0\n";                 // Age not tracked
  info << "idle=0\n";                // Idle time not tracked
  info << "db=0\n";                  // Current database
  info << "sub=0\n";                 // Number of subscriptions
  info << "psub=0\n";                // Number of pattern subscriptions
  info << "multi=-1\n";              // Multi flag
  info << "qbuf=0\n";                // Query buffer size
  info << "qbuf-free=0\n";           // Query buffer free space
  info << "obl=0\n";                 // Output buffer size
  info << "oll=0\n";                 // Output list length
  info << "omem=0\n";                // Output memory
  info << "events=r\n";              // Events
  info << "cmd=client\n";            // Last command
  info << "user=default\n";          // User name
  info << "redir=-1\n";              // Redirect
  info << "resp=" << conn->GetProtocolVersion()
       << "\n";  // RESP version from connection

  protocol::RespValue resp;
  resp.SetString(info.str(), protocol::RespType::kBulkString);
  return CommandResult(resp);
}

// CLIENT KILL [ip:port] [ID client-id] [TYPE normal|master|slave|pubsub]
CommandResult HandleClientKill(const protocol::Command& command,
                               CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(
        false, "ERR wrong number of arguments for 'client kill' command");
  }

  // CLIENT KILL - Kill a connection by ID or address
  // Return 0 (no connections killed) for now
  protocol::RespValue resp;
  resp.SetInteger(0);
  return CommandResult(resp);
}

// Auto-register all client commands
ASTRADB_REGISTER_COMMAND(CLIENT, -2, "readonly", RoutingStrategy::kByFirstKey,
                         HandleClient);
ASTRADB_REGISTER_COMMAND(CLIENT_LIST, 1, "readonly",
                         RoutingStrategy::kByFirstKey, HandleClientList);
ASTRADB_REGISTER_COMMAND(CLIENT_INFO, 1, "readonly",
                         RoutingStrategy::kByFirstKey, HandleClientInfo);
ASTRADB_REGISTER_COMMAND(CLIENT_KILL, -2, "write", RoutingStrategy::kByFirstKey,
                         HandleClientKill);

}  // namespace astra::commands
