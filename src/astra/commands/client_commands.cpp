// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include "client_commands.hpp"
#include "command_auto_register.hpp"
#include "astra/base/logging.hpp"
#include "astra/server/server.hpp"
#include "astra/network/connection.hpp"
#include <sstream>
#include <iomanip>

namespace astra::commands {

// CLIENT LIST - List all connected clients
CommandResult HandleClientList(const protocol::Command& command, CommandContext* context) {
  // CLIENT LIST takes no arguments beyond the subcommand
  // The subcommand is already handled by the router, so we accept 0 additional args

  // Get server from context
  auto server_ptr = context->GetServer();
  if (!server_ptr) {
    return CommandResult(false, "ERR server not available");
  }

  auto* server = static_cast<server::Server*>(server_ptr);

  // Get connection list
  auto connections = server->GetConnections();
  
  std::string result;
  for (const auto& [id, conn_weak] : connections) {
    auto conn = conn_weak.lock();
    if (conn && conn->IsConnected()) {
      std::string addr = conn->GetRemoteAddress();
      
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
      result += absl::StrCat("id=", id, " ");
      result += "addr=" + absl::StrCat(ip, ":", port) + " ";
      result += "name= ";  // Empty name for now
      result += "age=0 ";  // Age not tracked for now
      result += "idle=0 ";  // Idle time not tracked for now
      result += "\n";
    }
  }
  
  protocol::RespValue resp;
  resp.SetString(result, protocol::RespType::kBulkString);
  return CommandResult(resp);
}

// CLIENT - Main client command router
CommandResult HandleClient(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'client' command");
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
  } else {
    return CommandResult(false, "ERR unknown subcommand '" + sub + "'");
  }
}


// CLIENT INFO - Get information about the current client connection
CommandResult HandleClientInfo(const protocol::Command& command, CommandContext* context) {
  // CLIENT INFO takes no arguments beyond the subcommand
  // The subcommand is already handled by the router, so we accept 0 additional args

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
  info << "name=\n";
  info << "addr=" << ip << ":" << port << "\n";
  info << "laddr=127.0.0.1:6379\n";  // Local address
  info << "sock=-1\n";  // Socket fd
  info << "age=0\n";  // Age not tracked
  info << "idle=0\n";  // Idle time not tracked
  info << "db=0\n";  // Current database
  info << "sub=0\n";  // Number of subscriptions
  info << "psub=0\n";  // Number of pattern subscriptions
  info << "multi=-1\n";  // Multi flag
  info << "qbuf=0\n";  // Query buffer size
  info << "qbuf-free=0\n";  // Query buffer free space
  info << "obl=0\n";  // Output buffer size
  info << "oll=0\n";  // Output list length
  info << "omem=0\n";  // Output memory
  info << "events=r\n";  // Events
  info << "cmd=client\n";  // Last command
  info << "user=default\n";  // User name
  info << "redir=-1\n";  // Redirect
  info << "resp=2\n";  // RESP version
  
  protocol::RespValue resp;
  resp.SetString(info.str(), protocol::RespType::kBulkString);
  return CommandResult(resp);
}

// CLIENT KILL [ip:port] [ID client-id] [TYPE normal|master|slave|pubsub]
CommandResult HandleClientKill(const protocol::Command& command, CommandContext* context) {
  if (command.ArgCount() < 1) {
    return CommandResult(false, "ERR wrong number of arguments for 'client kill' command");
  }

  // Get server from context
  auto server_ptr = context->GetServer();
  if (!server_ptr) {
    return CommandResult(false, "ERR server not available");
  }

  auto* server = static_cast<server::Server*>(server_ptr);

  std::string target_type;
  std::string target_addr;
  uint64_t target_id = 0;
  bool has_id = false;
  
  size_t i = 0;
  while (i < command.ArgCount()) {
    const std::string& arg = command[i].AsString();
    
    if (arg == "ID") {
      if (i + 1 >= command.ArgCount()) {
        return CommandResult(false, "ERR ID option requires a client ID");
      }
      try {
        target_id = std::stoull(command[i + 1].AsString());
        has_id = true;
        i += 2;
      } catch (...) {
        return CommandResult(false, "ERR invalid client ID");
      }
    } else if (arg == "TYPE") {
      if (i + 1 >= command.ArgCount()) {
        return CommandResult(false, "ERR TYPE option requires a type");
      }
      target_type = command[i + 1].AsString();
      i += 2;
    } else {
      // Assume it's an address (ip:port)
      target_addr = arg;
      i++;
    }
  }
  
  // Get connections
  auto connections = server->GetConnections();
  uint64_t killed = 0;
  
  for (const auto& [id, conn_weak] : connections) {
    auto conn = conn_weak.lock();
    if (!conn || !conn->IsConnected()) {
      continue;
    }
    
    // Skip self (can't kill own connection)
    if (id == context->GetConnection()->GetId()) {
      continue;
    }
    
    bool match = true;
    
    // Check ID match
    if (has_id && id != target_id) {
      match = false;
    }
    
    // Check address match
    if (!target_addr.empty()) {
      std::string addr = conn->GetRemoteAddress();
      if (addr != target_addr) {
        match = false;
      }
    }
    
    // Check type match (simplified - all connections are "normal" for now)
    if (!target_type.empty() && target_type != "normal") {
      match = false;
    }
    
    // Kill matching connection
    if (match) {
      // Skip self (can't kill own connection)
      if (id == context->GetConnectionId()) {
        continue;
      }
      conn->Close();
      killed++;
    }
  }
  
  protocol::RespValue resp;
  resp.SetInteger(killed);
  return CommandResult(resp);
}

// Auto-register all client commands
ASTRADB_REGISTER_COMMAND(CLIENT, -2, "readonly", RoutingStrategy::kByFirstKey, HandleClient);
ASTRADB_REGISTER_COMMAND(CLIENT_LIST, 1, "readonly", RoutingStrategy::kByFirstKey, HandleClientList);
ASTRADB_REGISTER_COMMAND(CLIENT_INFO, 1, "readonly", RoutingStrategy::kByFirstKey, HandleClientInfo);
ASTRADB_REGISTER_COMMAND(CLIENT_KILL, -2, "write", RoutingStrategy::kByFirstKey, HandleClientKill);

}  // namespace astra::commands
